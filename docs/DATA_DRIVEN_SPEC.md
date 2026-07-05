# Spec: Data-Driven Angerblast (TOML, runtime-loaded)

## Goal

Move the game's **content AND behaviour** out of C and into editable TOML files loaded at
runtime, so tuning/designing is editing text — no recompile. Delete the hardcoded data
tables and the big behaviour `switch`es, replacing them with a small, fixed **interpreter**
that runs data-defined patterns.

Parser: **tomlc99** (MIT, vendored at `src/vendor/toml.{c,h}`, compiles clean with our
flags). Files live in `assets/*.toml`, loaded at startup, hot-reloadable with a key.

## The honest design principle

C can't execute arbitrary text. "Behaviour in data" works by recognising that all our
behaviours are **parameterised variations of a small set of primitives**. Looking at the
real code, every enemy move/fire and weapon fire is a formula over a fixed set of inputs
(`pos, anchor, age t, phase, dirToPlayer, speed, stage, runTime`). So instead of a general
scripting VM, we define:

- a **closed vocabulary of primitive ops** (move kinds, fire kinds, weapon emitters), each
  implemented once in C and taking **numeric parameters from data**;
- data files that **compose** these ops with parameters.

This deletes the per-type `switch` bodies (the duplicated formulas) and replaces them with
*one* parameterised implementation per primitive + a data table of parameters. Net: fewer
lines, and new content needs zero C.

Where a behaviour is truly one-off, it stays a named primitive — we don't invent a Turing
language. The win is **all tuning + composition + new combinations live in TOML**.

---

## 1. File layout (`assets/`)

```
assets/
  balance.toml      # all the balance.h constants + xp/difficulty curves
  palette.toml      # the 64 colours (already a flat list)
  runes.toml        # 5 runes: name, colour, stat label, + the stat they grant
  effects.toml      # status effects: tint, duration, stack rule, dot/halt flags
  enemies.toml      # enemy roster: shape, move op+params, fire op+params, hp/spd/etc
  weapons.toml      # weapon roster: emitter op+params, base dmg/interval, colour
  trifectas.toml    # the 12 colour combos
  evolutions.toml   # weapon x trifecta matrix (name, desc, mods, special, onHit, onKill)
  formations.toml   # the spawn director's formation shapes
  bosses.toml       # boss phases + fire patterns
```

Each maps 1:1 to a current `data/*.c` table or a hardcoded `switch`. TOML's tables/arrays
express these naturally and allow comments.

---

## 2. The primitive vocabularies (the "mini-language")

These enums already exist in C; data references them **by name string**, the loader maps
name→enum, and the C primitive reads parameters from the loaded struct.

### 2a. Enemy movement ops (replaces `switch (T->move)`)

Each enemy type names a `move` op and supplies params. The C `enemies_move()` becomes one
function with a `switch(op)` of ~10 tiny branches (kept — they're the *primitives*), but
the **per-type variation** (the `*0.6f`, `*3.0f`, band heights, step sizes) moves to data:

```toml
[[enemy]]
name = "Weaver"
move = "weave"
move_params = { fall = 1.0, wave_amp = 2.7, wave_freq = 3.0 }   # was sp*0.9*3.0 hardcoded
```

Ops: `march, dive, weave, crab, snake, hunt, hover, orbit, drift, straight`. Each has a
documented param set (amplitudes, multipliers, band y, step size). A `MoveParams` struct
(≤6 floats) carries them; the C op formula reads them instead of literals.

### 2b. Enemy fire ops (replaces `switch (T->fire)` / danmaku)

The danmaku patterns are **emitters**: count, speed-scale, angle spread, rotation step,
colour. Generalise into ONE data-driven emitter:

```toml
fire = "radial"
fire_params = { count = 8, count_per_stage = 1, speed_scale = 0.8, spin = 0.4, color = "hotpink" }
# ring/flower/spiral all become "radial" with different count/spin/arc.
fire = "aimed"                                   # aimed-single
fire = "spread"; fire_params = { count = 3, arc = 0.44 }
fire = "down"
```

`ring`, `flower`, `spiral` collapse into **`radial`** with `{count, arc, spin}` — three
hardcoded cases become one parameterised op + three data rows. `aimed/down/spread` likewise.

### 2c. Weapon emitters (replaces `switch (wp->type)` + `emit_special`)

The 13 weapon firing bodies are emitters too. Decompose each into a **base shape** + params:

```toml
[[weapon]]
name = "Side Guns"
emit = "fixed_dirs"      # fire along fixed angle list
emit_params = { dirs = [0.0, 3.14159], speed_scale = 1.0, radius = 4 }

[[weapon]]
name = "Ion Cannon"
emit = "straight"
emit_params = { speed_scale = 1.4, radius = 11, pierce = 999 }

[[weapon]]
name = "Chain Bolt"
emit = "chain"           # nearest-target hop (lightning)
emit_params = { targets = 2, range = 420 }

[[weapon]]
name = "Damage Aura"
emit = "aura"
emit_params = { base_radius = 60, per_level = 14 }
```

Emitter ops: `straight, fixed_dirs, aimed, autoaim, radial, homing_volley, chain, aura,
orbital, cone(flame), lob(goo)`. ~10 ops cover all 13 weapons (some share). The
`emit_special` evolution patterns (beam/rail/shotgun/storm/swarm/void/prism/chaos) become
**emitter modifiers** (a `pattern` field + params) applied on top, also data.

### 2d. Why this shrinks code

Today: `weapons.c` has ~13 bespoke firing blocks **plus** `emit_special`'s ~12 pattern
blocks = ~250 lines of near-duplicate "loop k, compute angle, bullets_add". After: ~10
generic emitter functions (~120 lines) + a TOML table. The 23 enemy move/fire cases
(~130 lines) → ~10 generic ops (~70 lines) + data. **~250 lines of logic deleted**, and
the *combinations* (which weapon fires which pattern, which enemy moves how) are pure data.

---

## 3. Loader architecture

```
src/data/loader.{c,h}      # owns parsing: open assets/, toml_parse, fill the runtime tables
src/data/dtypes.h          # the runtime structs the loader fills (EnemyType, WeaponDef, ...)
```

- The existing `data/*.c` literal tables are **deleted**; their structs (in `*_types.h`)
  stay but become **non-const, loader-filled** (we already made them runtime-fillable for
  the palette, so this is the same pattern extended).
- `loader_load_all()` at startup parses every `assets/*.toml` into the global tables;
  returns false + prints the offending file/line on a parse error (fail loud).
- Name→enum maps: small static `{ "weave", MV_WEAVE }` tables in the loader translate the
  string ops to the C enums the interpreter switches on.
- **Hot-reload:** a debug key (e.g. F5) re-runs `loader_load_all()`. Since tables are plain
  arrays filled in place and the game reads them by index each frame, a reload just swaps
  the numbers — no pointers to dangle. (Counts can grow up to fixed caps.)
- **Colours by name:** `palette.toml` defines names; other files reference `color = "coral"`.
  The loader resolves names → `Color` at load (a tiny `palette_by_name()`).

---

## 4. Migration plan (game stays playable each step)

1. **Loader skeleton + balance.toml.** Vendor parser into the Makefile. Load `balance.toml`
   into the existing `balance.h` constants (turn the `#define`s into loaded globals). Verify
   identical play. *Smallest end-to-end slice proving the pipeline.*
2. **Flat data tables → TOML.** palette, runes, effects, trifectas, weapons_def (stats only,
   not behaviour yet), enemy stats (not behaviour yet), evolutions matrix. Delete those
   `data/*.c` literals. Game identical, now mostly data.
3. **Enemy behaviour ops.** Introduce `MoveParams`/`FireParams`, the generic
   `enemy_move()`/`enemy_fire()` reading params, and `enemies.toml` move/fire fields. Delete
   the two `switch` bodies. Collapse ring/flower/spiral→radial.
4. **Weapon emitters.** Generic emitter ops + `weapons.toml` emit fields; delete the
   `switch(wp->type)` firing bodies and fold `emit_special` patterns into data modifiers.
5. **Formations + bosses → TOML.** `spawn.c`'s formation `switch` and `boss.c`'s phases.
6. **Hot-reload key + a `assets/README`** documenting the schema for designers.

Each step: build clean, `make test`, and a screenshot/bench to confirm parity.

---

## 5. Risks / decisions

- **Single-binary / web build property:** runtime files break the "no asset files" jam
  trait. Decision: ship `assets/` next to the binary for desktop; keep a `make bake` later
  that embeds TOML→C if we ever need the web build. (Out of scope now; runtime-load now.)
- **Behaviour that resists params:** a few ops (march's row-step, orbit's dive handoff)
  carry small state machines. They stay as named primitives with params; we don't force
  them into a generic form. The goal is *data-composed*, not *data-implemented-from-scratch*.
- **Parse errors at runtime** instead of compile errors: the loader must fail loud (file +
  line + key) and refuse to launch on malformed data, so a typo isn't a silent 0.
- **Perf:** parsing happens once at startup (and on F5). Zero hot-path cost — the
  interpreter is the same `switch` we have now, just fed from loaded params.

## 6. What stays in C (correctly)

Engine + framework: the pools, spatial grid, collision, rendering, input, audio, the
GameWorld plumbing, and the **primitive op implementations** themselves. Data drives
*which* primitives run with *what* numbers and *how they combine* — it doesn't reimplement
vector math. That's the right line: data = design, C = engine.

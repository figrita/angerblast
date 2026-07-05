# Handoff: data-driving BEHAVIOUR (Steps 3-5)

This is the next-agent guide for finishing the data-driven migration. Read
`docs/DATA_DRIVEN_SPEC.md` (the original plan) and `docs/BIOME_SPEC.md` first — this doc is
the concrete, current-state continuation.

## Where the pipeline stands (DONE)

All **content/data tables** are runtime TOML, loaded by `src/data/loader.c` via the
vendored `src/vendor/toml.{c,h}` (MIT). The loader:
- parses `assets/*.toml` (global) + `assets/biomes/<dir>/{biome.toml, enemies/*.toml}`,
- fails loud (`DATA ERROR [file] key: msg`) and `main()` aborts on a bad load,
- interns strings into a static arena (`intern()`), resolves colours by name
  (`palette_by_name`), and maps op-name strings → C enums.
- **F5 hot-reloads** everything live (`loader_load_all("assets")` in the main loop).

Already data: balance, palette, runes, effects, trifectas, weapons (stats), evolutions
(156-cell matrix), the enemy roster, **enemy geometry** (a `shape` list of primitive
draw-ops → `Geometry`), and **biomes-as-folders** (theme + director + boss def + roster).

What's STILL hardcoded C (this handoff): the per-type **movement** formulas, **danmaku
fire** patterns, **weapon firing** emitters, **formation** shapes, and the **boss
danmaku**. These are *behaviour* — they stay as engine primitives, but their **parameters**
and **composition** should move to data.

## The guiding principle (don't build a VM)

Every behaviour is a **parameterised variation of a small primitive set**. The migration is
NOT "interpret arbitrary code from TOML" — it's:
1. keep ~10 primitive ops implemented once in C (the `switch` bodies become *one*
   parameterised function each),
2. move the per-type magic numbers (the `*0.6f`, `*3.0f`, counts, spins) into a small
   params struct loaded from TOML,
3. collapse near-duplicate cases (ring/flower/spiral → one `radial` op with different
   params).

Net effect: ~250 lines of duplicated formula deleted; new content/combinations = pure TOML.

## Conventions to follow (match the existing code)

- **Loader pattern** for an array-of-inline-tables: `toml_array_in(tab,"key")` →
  `toml_array_nelem` → `toml_table_at(arr,i)`; read fields with `rd_f/rd_i/rd_str/rd_name/
  rd_color`; optional fields with `opt_f/opt_i/rd_str_opt` (no error if absent). See
  `rd_shape()` in loader.c as the template for parsing a nested params block.
- **Op-name → enum**: a static `const char *T[]` + linear `strcmp` (see `enemy_move_id`,
  `geo_op_id`, `formation_id`, `spec_id`). Add new ones the same way.
- **Data tables are runtime-filled**: declare the array non-`const` in its `.h`, define it
  zeroed in its `.c`, fill it in the loader. (Gotcha: a `const` table is read-only memory →
  writing it segfaults. This already bit us once.)
- **TOML caveat**: no `;`-separated pairs on a line. Use either one key per line OR a single
  inline table `{a=1, b=2}`. Inline tables may contain arrays of inline tables (that's how
  `shape=[{...},{...}]` works).
- **Build/verify loop** (in the distrobox): `make` (expect zero warnings, `-Wall -Wextra`),
  `make test` (logic tests in `tests/test_runes.c` — they call `loader_load_all("assets")`
  first; add assertions there), and a screenshot harness (temporarily add an `AB_SHOT`
  block in `main()` that drives the world + `TakeScreenshot`, then DELETE it). Always remove
  harnesses before finishing.

---

## Step 3 — enemy MOVE + FIRE as data params

### What's hardcoded now
- **Movement**: `enemies_update()` in `src/systems/enemies.c`, `switch (T->move)` (lines
  ~264-303), 10 cases. Each is a formula over `(e->pos, e->anchor, e->t, e->phase, dirP,
  sp, dt)`. The per-type variation is baked literals (e.g. WEAVE: `e->pos.x += sinf(phase)
  * sp*0.9f*dt * 3.0f`).
- **Fire**: `enemy_fire()` in the same file, `switch (T->fire)` (lines ~199-241), 7 cases.
  `ring/flower/spiral` are near-identical radial bursts with different `count`, `arc`,
  `e->patternAng +=` spin step, and colour.

### Plan
1. **Collapse fire ops.** Replace `ring/flower/spiral` (and `down/aimed/spread`) with a
   single **`radial`** primitive parameterised by `{count, count_per_stage, speed_scale,
   arc, spin, aimed(bool), color}`. `down` = radial aimed-down count 1; `aimed` = aimed
   count 1; `spread` = aimed arc>0 count 3; `ring/flower/spiral` = arc=2π with different
   count/spin. Keep `none`.
2. **Add a params struct** to `EnemyType` (in `src/data/enemy_types.h`):
   ```c
   typedef struct { float p[8]; } MoveParams;   // op-specific slots (amp, freq, band, step...)
   typedef struct { int count, countPerStage; float speedScale, arc, spin; bool aimed; Color color; } FireParams;
   ```
   Add `MoveParams movep; FireParams firep;` to `EnemyType`. Document each move op's param
   meaning in a comment + in `assets/README.md`.
3. **Parameterise the C primitives.** Rewrite the `switch(T->move)` bodies to read
   `T->movep.p[...]` instead of literals (the formula stays; the magic numbers come from
   data). Rewrite `enemy_fire` so all radial-family cases call one
   `emit_radial(w, e, &T->firep)` helper; keep `e->patternAng += spin` for rotation.
4. **Loader.** In `enemy_from_table()` (loader.c), after `move`/`fire` op ids, read
   `move_params = {...}` and `fire_params = {...}` inline tables (reuse `opt_f`). Default
   sensible values so existing enemies that omit params still work.
5. **TOML.** Add `move_params`/`fire_params` to each enemy file in
   `assets/biomes/*/enemies/*.toml`. Translate the current literals exactly (so behaviour is
   unchanged), e.g. Weaver: `move_params={ fall=1.0, wave_amp=2.7, wave_freq=3.0 }`,
   Spinner: `fire="radial"`, `fire_params={ count=8, count_per_stage=1, arc=6.283, spin=0.4,
   speed_scale=0.8, color="hotpink" }`.
6. **Delete** the `MV_*`/`FR_*` cases that are now data (keep the enum *ids* as the op
   selector; the bodies collapse to the parameterised primitives).

### Verify
Screenshot the roster (each enemy moving/firing) and confirm identical to before. Collapse
should turn 7 fire cases → ~2 (`none` + `radial`) and shrink `enemy_fire` substantially.
Add a `make test` assertion that a known enemy loads its params.

---

## Step 4 — weapon firing patterns as data (emitters)

### What's hardcoded now
- `src/systems/weapons.c`: `fire_weapon()` `switch (wp->type)` (lines ~178-330) = 13
  bespoke firing bodies, PLUS `emit_special()` `switch(special)` (lines ~53-150) = 12
  evolution pattern bodies. ~250 lines of "loop k, compute angle, `bullets_add`".

### Plan
1. **Define emitter ops** (the closed set): `straight, fixed_dirs, aimed, autoaim, radial,
   homing_volley, chain, aura, orbital, cone(flame), lob(goo)`. ~10 cover all 13 weapons.
   Each is one C function reading params; the per-weapon variation becomes data.
2. **Add to `WeaponDef`** (in `src/data/weapons_def.h`): an `emit` op id + an `EmitParams`
   struct (`{ float speedScale, radius, spread; int count, pierce, dirs[…]; bool homing; }`).
   These replace the literals currently inside each `case WP_*`.
3. **Rewrite `fire_weapon`** to dispatch on `def->emit` (not `wp->type`) → the generic
   emitter, passing `EmitParams` + the folded `WeaponStats` (`weapon_stats(wp)` already gives
   damageMul/rateMul/multi/pierce/etc — keep using it). The evolution `special` stays as an
   **emitter modifier** layered on top: `emit_special` becomes a *parameterised reshape* of
   the base emit (beam = 1 long thin; rail = 1 fat slug; shotgun = wide cone; etc.) driven by
   `special` + a few data params, not 12 copy-pasted loops.
   - IMPORTANT: preserve the "evolution never reduces shot count" rule already in
     `emit_special` (every pattern fires ≥ `1 + multi`). There's a regression test instinct
     here — re-verify with a shot-count check (a prior bug).
4. **Boss targeting**: the recent fix routed lightning/auto-aim/homing through
   `target_nearest`/`target_damage` (in enemies.c) so they hit the boss. Keep that — the
   `chain`/`autoaim`/`homing_volley` emitters must use those helpers, not raw
   `enemies_nearest`.
5. **TOML.** Add `emit` + `emit_params` to `assets/weapons.toml`. Example mapping:
   - Side Guns → `emit="fixed_dirs"`, `emit_params={ dirs=[0.0,3.14159], speed_scale=1.0 }`
   - Ion → `emit="straight"`, `{ speed_scale=1.4, radius=11, pierce=999 }`
   - Chain Bolt → `emit="chain"`, `{ targets=2, range=420 }`
   - Aura → `emit="aura"`, `{ base_radius=60, per_level=14 }`
   - Orbital → `emit="orbital"`, Flame → `emit="cone"`, Goo → `emit="lob"` (note: goo fires
     the special wad-with-EFFECT_GOO behaviour — keep that intact).
6. **Delete** the per-weapon firing bodies once the emitter + params reproduce them.

### Verify
This is the riskiest step (it touches the firing *feel*). Screenshot each evolution firing
(beam/rail/shotgun/storm/swarm/void/prism/chaos) and compare to current. Confirm the
shot-count-never-decreases rule, boss targeting (lightning/autoaim/homing hit the boss), and
the lightning bolt visual (`fx_spawn_bolt`) still fire. `make test` + a manual bench.

---

## Step 5 — formations + boss danmaku to TOML

### What's hardcoded now
- **Formations**: `spawn_formation()` in `src/systems/spawn.c`, `switch (shape)` (lines
  ~37-90), 5 shapes (row/vee/stream/grid/swarm). Biomes already pick *which* formation ids
  via `biome.toml [director].formations`, but the **shape geometry** (counts, spacing, rows)
  is hardcoded + scaled by `w->stage`.
- **Boss danmaku**: `boss_update()` in `src/systems/boss.c` (lines ~31-46) fires a hardcoded
  aimed-spread + flower. The biome `[boss].phases` array IS loaded into
  `BossDef.phases[]` (`BossPhase {fireOp, count, plusFlower, hpAbove}` in `data/biome.h`),
  but `boss_update` **ignores it** and uses its own literals. Wiring that up is the main task.

### Plan
1. **Boss danmaku from data (do this first — the struct already exists).** In `boss_update`,
   pick the active `BossPhase` by `hp/hpMax >= phase.hpAbove`, then fire using
   `phase.fireOp` (an enemy fire op id) with `phase.count`, overlaying a flower if
   `phase.plusFlower`. Reuse the Step-3 `emit_radial` so boss + enemies share one danmaku
   primitive. Delete the hardcoded boss bullet loops. (`BIOMES[w->biome].boss` is already
   the source.)
2. **Formations as data.** Add a `Formation` op + params to the biome director, OR a global
   `formations.toml` of named formation shapes (`{op, count, rows, spacing, margin,
   count_per_stage}`). Rewrite `spawn_formation` to read params instead of literal
   `10 + stage`, `cols=8+stage/2`, etc. Keep the 5 op *shapes* as primitives; data drives
   their counts/spacing/scaling. The biome's `formations` list already chooses which run.
3. **TOML.** Extend `assets/biomes/*/biome.toml` with formation params (or a global file).
   Boss phases are already authored in `biome.toml [boss].phases` — just make the engine
   honour them.

### Verify
Screenshot a boss in each phase (its danmaku should change with the data), and each
formation. Confirm both biomes (`01_void`, `02_ember`) still play and their distinct boss
phase configs produce visibly different patterns.

---

## Files you'll touch (quick map)

| Concern | Engine file | Data file(s) |
|---|---|---|
| enemy move/fire | `src/systems/enemies.c`, `src/data/enemy_types.{h,c}` | `assets/biomes/*/enemies/*.toml` |
| weapon emitters | `src/systems/weapons.c`, `src/data/weapons_def.{h,c}` | `assets/weapons.toml` |
| formations | `src/systems/spawn.c` | `assets/biomes/*/biome.toml` (or new `formations.toml`) |
| boss danmaku | `src/systems/boss.c` | `assets/biomes/*/biome.toml [boss].phases` (already loaded) |
| loading any of the above | `src/data/loader.c` | — |

## Definition of done
- The `switch(T->move)`, `switch(T->fire)`, `switch(wp->type)`, `emit_special` `switch`,
  `spawn_formation` `switch`, and the boss's hardcoded danmaku are **gone or reduced to
  parameterised primitives**; their numbers/composition live in TOML.
- `make` clean (no warnings), `make test` green, both biomes play identically-or-better,
  F5 hot-reload still works, no leftover `AB_SHOT` harness.
- `assets/README.md` updated with the new `move_params`/`fire_params`/`emit_params`/
  formation/boss-phase schemas.

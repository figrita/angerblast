# Angerblast data files

All game **content and tuning** lives here as TOML, loaded at startup. Edit a file and
relaunch — or press **F5 in-game** to hot-reload without restarting. A malformed file
fails loud (prints the file + key) and refuses to launch.

> Engine behaviour (movement formulas, firing patterns, collision, rendering) is still in
> C; these files drive *which* primitives run with *what* numbers and *how they combine*.
> The op **names** below are recognised by the engine — using an unknown name errors.

## Files

### Global (cross-biome systems)
| File | What it defines |
|------|-----------------|
| `balance.toml`   | player / enemy-scaling / director / boss / xp tuning numbers |
| `palette.toml`   | the 64 colours + semantic names other files reference (`color = "coral"`) |
| `runes.toml`     | the 5 socketable runes (name, colour, stat label) |
| `effects.toml`   | status effects (tint, duration, stack rule, dot/halt flags) |
| `trifectas.toml` | the 12 rune combos (colour counts, sum to 3) |
| `weapons.toml`   | the weapon registry: stats + colour + **`emit` op & `emit_params`** (firing) |
| `evolutions.toml`| weapon×trifecta evolution matrix: shared `[mods.*]` presets + a name/desc grid |
| `formations.toml`| the spawn director's formation **shapes** (counts/spacing/scaling per op) |
| `emitters.toml`  | the **composable danmaku graph** — named bursts that can spawn child bursts |

### Biomes (`biomes/<name>/`)
A **biome is a folder**, loaded in **alphabetical order** (`01_void/`, `02_ember/`...).
Beating a biome's boss advances to the next; the last biome loops with rising difficulty.
Each biome owns:

- **`biome.toml`** — `name`, `[theme]` (background + 3 star colours), `[director]` (field
  caps, `waves_to_boss`, allowed `formations`), `[boss]` (name, hp, radius, `phases`).
- **`enemies/*.toml`** — one file per enemy, loaded alphabetically. Each is fully
  self-describing: stats + colour + `move`/`fire` ops (+ optional `move_params`/
  `fire_params`) + a data-defined **`shape`**. Optional: `emitter = "name"` routes its fire
  through a composable emitter (see below); `elite = true` makes it a **miniboss** (big,
  tanky, spawned as a mirrored pair, excluded from normal waves).

**Formations:** `row vee stream grid swarm` (geometry tuned in `formations.toml`)

### Boss phases (`[boss].phases` in `biome.toml`)

The boss reads its danmaku from a list of phases, picked by remaining HP (authored
high→low). Each phase reuses the shared enemy danmaku emitter:

```toml
[boss]
name = "Null Maw"
hp = 600
radius = 46
phases = [
  { hp_above = 0.66, fire = "aimed",  count = 5 },               # while hp/max >= 0.66
  { hp_above = 0.33, fire = "aimed",  count = 7,  plus = "flower" },
  { hp_above = 0.0,  fire = "aimed",  count = 10, plus = "flower" },
]
```

| key | meaning |
|-----|---------|
| `hp_above` | this phase is active while `hp/maxHp >= hp_above` (first match wins) |
| `fire` | an enemy `fire` op name (`aimed`/`spread`/`ring`/...); `count`>1 auto-fans an aimed pattern |
| `count` | projectiles in the main pattern |
| `plus = "flower"` | overlay a rotating flower burst |
| `fire_params` | (optional) override the main pattern's `FireParams` (same keys as enemy `fire_params`) |

### Multi-part bosses (`[[boss.part]]` in `biome.toml`)

A boss can be a **head + hands** instead of a single body. Add a `[[boss.part]]` list; when
present the `phases` above are ignored for combat (the parts carry the HP). The **head** is
shielded (unhittable) until every non-head part (hand) is destroyed; killing the head ends
the fight. Each part fires its own danmaku and can use a composable `emitter`.

```toml
[[boss.part]]
head = true
off_y = -10
hp = 700
radius = 46
emitter = "spiral_pods"   # the head sheds recursive ring-pods once exposed
interval = 1.1

[[boss.part]]
off_x = -130            # a hand to the left
off_y = 40
hp = 300
radius = 30
fire = "ring"
count = 10
interval = 1.4
```

| key | meaning |
|-----|---------|
| `head` | `true` for the head (shielded until hands die; its death = win). Default false (a hand). |
| `off_x` / `off_y` | spawn offset from the boss anchor |
| `hp` / `radius` | part health (scaled by stage) + hit radius |
| `fire` / `count` | a `fire` op + projectile count (this part's danmaku) |
| `emitter` | (optional) a composable emitter name — overrides `fire` for this part |
| `interval` | seconds between this part's bursts |
| `fire_params` | (optional) override the part's `FireParams` |

### Composable emitters (`emitters.toml`)

A named burst of bullets; a bullet it spawns can carry a **child** emitter (referenced by
name) that fires on a timer or on death — so split-shots / trailing spawners / bursting pods
are pure data. This is *not* a scripting VM: emitters reference each other only by name, the
engine walks the graph, and runaway is bounded by a depth cap (`EMITTER_MAX_DEPTH = 4`) + the
bullet pool. Every child reference is validated at load (fail loud). Enemies/boss-parts opt in
with `emitter = "name"`.

```toml
emitter = [
  { name="burst_ring", count=10, arc=6.28319, speed_scale=0.7, color="hotpink" },
  { name="splitter",   count=3, arc=0.5, aimed=true, color="coral",
    # ^ each dart, on death, bursts into burst_ring:
    child="burst_ring", child_mode="death", child_speed=90 },
]
```

| key | meaning |
|-----|---------|
| `name` | unique name (referenced by `emitter=` and by other emitters' `child`) |
| `count` / `count_per_stage` | bullets per burst (+per stage) |
| `speed_scale` | × the base speed the caller supplies |
| `arc` | fan span (rad); `6.28319` = full ring |
| `spin` | rad added to the rotating phase each burst |
| `aimed` | fan centred on the player |
| `radius` / `color` | bullet look |
| `child` | name of the emitter each spawned bullet carries |
| `child_mode` | `"timer"` (fire every `child_interval` s) or `"death"` (once on expiry) |
| `child_interval` / `child_speed` | timing + base speed for the child burst |

### Geometry (the `shape` field on each enemy)

An enemy's silhouette is a **list of primitive draw layers** (composed bottom-to-top).
Sizes are fractions of the enemy radius (1.0 = full radius). Colour tokens: `"body"` (the
enemy's own colour), `"white"`, or any palette name.

```toml
shape = [
  { op="poly", sides=8, r=1.0, color="body", spin=20 },      # spinning octagon body
  { op="ring", r=0.75, thick=0.18, color="gold" },           # a glowing ring
  { op="circle", r=0.3, color="white", alpha=0.6 },          # bright core
]
```

| op | params |
|----|--------|
| `poly`   | `sides, r, spin(deg/s), spin_offset, color, outline, alpha` |
| `circle` | `r, color, alpha` |
| `rect`   | `w, h, color, outline, alpha` |
| `ring`   | `r, thick, color, alpha` |
| `line`   | `x1, y1, x2, y2, thick, color` (endpoints relative to centre) |

### Enemy `move` + `move_params`

Each enemy names a `move` op; the formula is a C primitive, but its numbers come from an
optional `move_params` inline table. Omit it and the op's faithful defaults apply.

```toml
move = "weave"
move_params = { phase_rate = 3.0, fall_mul = 1.0, wave_amp = 2.7 }
```

| move op | `move_params` keys (defaults in parens) |
|---------|------------------------------------------|
| `march` | `step_mul(1.0) edge_margin(40) row_drop(26) settle_speed(6)` |
| `dive`  | `descend_mul(0.6) hold_time(1.2) dive_mul(1.6)` |
| `weave` | `phase_rate(3.0) fall_mul(1.0) wave_amp(2.7)` |
| `crab`  | `fall_mul(0.6) wave_freq(1.5) wave_amp(2.5)` |
| `snake` | `edge_margin(30) row_drop(30)` |
| `hunt`  | `steer(0.04)` |
| `hover` | `strafe_freq(0.8) strafe_amp(2.0)` |
| `orbit` | `orbit_time(2.5) radius(60) ang_rate(2.0) y_scale(0.5) descend_mul(0.3) dive_mul(1.4)` |
| `drift` / `straight` | (no params — slow / brisk straight descent) |
| `lunge` | `creep_mul(0.5) trigger_dist(220) lunge_mul(3.0) lunge_dur(0.45) recover_dur(1.0)` — creep down, then DASH-seek the player when within trigger_dist, recover, repeat |
| `swoop` | `fall_mul(0.8) arc_rate(2.4) arc_amp(2.8)` — arc/swoop dive curving across toward the player |
| `spiral` | `fall_mul(0.7) spin_rate(3.0) radius(70)` — corkscrew down its spawn axis |

### Melee-danger knobs (any enemy)

Give non-shooting (or any) enemies agency so they aren't free XP:

| key | meaning |
|-----|---------|
| `death_burst` | N bullets sprayed radially on death, blooming from a ~38px ring (so a point-blank kill leaves a dodge gap, not a corpse-shotgun) |
| `lunge_every` | seconds between periodic dash-at-player hops — a behaviour LAYER on top of any move op (creep-then-pounce). 0 = off |

### Enemy `fire` + `fire_params`

`down`/`aimed`/`spread`/`ring`/`flower`/`spiral` are all the **one** `radial` emitter with
different params (those names are kept as presets; `radial` is the canonical op). Each names
a `fire` op + an optional `fire_params` override:

```toml
fire = "ring"
fire_params = { count = 8, count_per_stage = 1, speed_scale = 0.8, arc = 6.28319, spin = 0.4, aimed = false, color = "hotpink" }
```

| key | meaning |
|-----|---------|
| `count` / `count_per_stage` | base projectiles (+per stage; was ring's `8 + stage`) |
| `speed_scale` | × the stage-scaled enemy bullet speed |
| `arc` | fan span (rad); `6.28319` = full ring, small = a tight aimed fan |
| `spin` | rad added to the rotating phase each shot (flower/spiral) |
| `aimed` | `true` = fan centred on the player; `false` = straight down / full ring |
| `color` | bullet colour (palette name) |

Preset defaults: `down`=1 straight-down · `aimed`=1 at player · `spread`=3 over 0.44 rad ·
`ring`=8(+1/stage) full ring spin 0.4 · `flower`=5 full ring spin 0.35 · `spiral`=2 full ring spin 0.28.

### Weapon `emit` + `emit_params` (in `weapons.toml`)

Every weapon's firing is one of a closed set of **emitter ops**; the per-weapon numbers live
in `emit_params`. Firing dispatches on `emit`, not the weapon's identity — a new weapon is a
new row. (The evolution `special` still reshapes the projectile emitters on top.)

```toml
{ name="Ion Cannon", base_interval=1.60, base_damage=60, color="cyan",
  emit="straight", emit_params={ speed_scale=1.4, radius=11, pierce=999 } }
```

| emit op | used by | key `emit_params` |
|---------|---------|--------------------|
| `straight` | Main, Tail, Ion | `speed_scale, radius, spread(total fan rad), pierce` |
| `fixed_dirs` | Side Guns | `speed_scale, radius, dirs=[…]` (rad; 0=right, π=left) |
| `aimed` | Aim Cannon | `radius, spread` (per-extra-projectile step) |
| `autoaim` | Auto-Aim Gun | `speed_scale, radius, spread, range` |
| `homing_volley` | Homing, Missiles | `speed_scale`(ABS speed)`, radius, spread`(scatter)`, evo_bonus` |
| `chain` | Chain Bolt | `targets`(hops/chain)`, range`. Multi-rune (green) = +1 **separate** chain; pierce-rune (purple) = +1 **hop** per chain. Hits the boss. |
| `aura` | Damage Aura | `base_radius, per_level, per_multi, evo_radius` |
| `orbital` | Orbital | `targets`(balls)`, base_radius, per_level, evo_radius` |
| `cone` | Flamethrower | `base_radius, per_level, evo_radius, count, spread` |
| `lob` | Flammable Goo | `speed_scale, radius` (wads that coat EFFECT_GOO) |

### Formation shapes (`formations.toml`)

The 5 formation **shapes** stay C primitives; their counts/spacing/per-stage scaling/caps
come from data. Order is fixed: index = op id (`row vee stream grid swarm`). A biome's
`[director].formations` list chooses which of these it spawns.

| shape | keys |
|-------|------|
| `row`    | `count, count_per_stage, count_max, rows, margin, row_gap` |
| `vee`    | `per, per_per_stage, per_max, dx_step, y_top_step, y_anchor_step` |
| `stream` | `lanes, lanes_div`(stage/div)`, lanes_max, count, count_max, y_step` |
| `grid`   | `cols, cols_div`(stage/div)`, cols_max, rows, margin, row_gap` |
| `swarm`  | `count, count_per_stage, count_max` |

## Op vocabularies (names the engine knows)

**Enemy `shape`:** `square triangle diamond hex circle cross penta star`
**Enemy `move`:** `march dive weave crab snake hunt hover orbit drift straight lunge swoop spiral`
**Enemy `fire`:** `none down aimed spread ring flower spiral radial`
**Weapon `emit`:** `straight fixed_dirs aimed autoaim homing_volley chain aura orbital cone lob`
**Formation op:** `row vee stream grid swarm`
**Evolution `special`:** `none explode beam swarm rapid pierceall burn shotgun rail storm voidpull prism chaos`
**Evolution `on_kill`:** `none magnet chain shield bounty frag reignite`
**`on_hit_effect` / `aura_effect`:** `none slow burn poison freeze shock weaken mark goo`
**Effect `stack`:** `refresh max_duration add`
**Child mode (`emitters.toml`):** `timer death`
**Colours:** any name from `palette.toml`'s `names` list (unknown → white).

## Engine features (not yet data-authored, but tuned via `balance.toml`)

These systems are C primitives the player interacts with; only their numbers are data:

- **Barrel-roll dodge** (Shift / pad R1): an i-frame dash. Tuned in `balance.toml [player]`:
  `dodge_speed`, `dodge_time`, `dodge_iframe`, `dodge_cooldown` (all optional, sane defaults).
- **Pickups** (magnet / board-wipe / instant-level / frenzy): drop from kills + bosses; their
  kinds are a fixed engine set (no TOML yet).
- **Drones** (follow / auto / sweep): companion that carries one socketable weapon; offered as
  level-up cards. Its gun reuses `weapons.toml` + the rune/trifecta system.
- **Aegis** passive: your shots shoot down enemy bullets.
- **Codex**: the in-game encyclopedia (open from Pause with `C` / pad Square) reads every data
  table above — adding an enemy/weapon/effect automatically lists it.

## Notes

- **Order matters** for `runes`, `effects`, `weapons`, `trifectas`, and `formations`: the
  array index *is* the in-engine id, so don't reorder rows (you can edit values freely).
- `enemies` can be reordered/extended freely (up to 64 types).
- `evolutions.toml`: each `[[row]]` is one weapon (in `weapons.toml` order); its `evo`
  array is the 12 trifectas in `trifectas.toml` order. The stat/behaviour `mods` come from
  the `[mods.<trifecta>]` preset, so all weapons share a column's mechanics — only the
  name/desc differ per cell.
- Inline tables (`{a=1, b=2}`) keep rows on one line; TOML does **not** allow `;`-separated
  pairs on a line, so multi-field blocks otherwise need one key per line.

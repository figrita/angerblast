# Spec: Biomes-as-folders + Geometry-as-data

Extends the TOML pipeline so that **a biome is a folder**: its theme (palette/background),
its enemy roster (a folder of enemy files), its boss, and its director tuning. Beating a
biome's boss advances to the next biome (alphabetical folder order). Enemy **geometry**
becomes data too — drawn from a list of primitive ops, not a `switch(shape)`.

## 1. Folder layout

```
assets/
  balance.toml palette.toml runes.toml effects.toml trifectas.toml weapons.toml evolutions.toml   # GLOBAL
  biomes/
    01_void/
      biome.toml
      enemies/
        grunt.toml
        diver.toml
        ...
    02_ember/
      biome.toml
      enemies/ ...
```

- **Order = alphabetical folder name** (`01_`, `02_` prefixes). The loader scans
  `assets/biomes/`, sorts dir names, loads each in order. No index file.
- **Global files stay global** (runes/weapons/evolutions/effects/trifectas are
  cross-biome systems). The palette is global as a *base*; a biome may override entries.

## 2. Geometry as data (layered primitives)

An enemy's silhouette is a `shape` = an **array of draw-op layers**, each an inline table.
The engine implements ~6 primitive ops; geometry is composing them with params. Replaces
`draw_enemy_shape`'s `switch`.

```toml
# a hexagon body with a white core (was SH_HEX)
shape = [
  { op="poly", sides=6, r=1.0, color="body", outline="white" },
  { op="circle", r=0.35, color="white", alpha=0.5 },
]

# a spinning 5-point star with a faint inner star (was SH_STAR)
shape = [
  { op="poly", sides=5, r=1.0, color="body", spin=30 },
  { op="poly", sides=5, r=0.6, color="body", spin=30, spin_offset=36, alpha=0.6 },
]

# a plus/cross (was SH_CROSS)
shape = [
  { op="rect", w=2.0, h=0.8, color="body" },
  { op="rect", w=0.8, h=2.0, color="body" },
  { op="circle", r=0.35, color="white", alpha=0.5 },
]
```

### Primitive ops (the closed vocabulary)
| op | params | draws |
|----|--------|-------|
| `poly`   | `sides, r, color, spin(deg/s), spin_offset, outline, alpha` | regular polygon (sides=3..n); spin animates |
| `circle` | `r, color, outline, alpha` | filled circle |
| `rect`   | `w, h, color, outline, alpha` | centred rectangle (w/h in radius units) |
| `ring`   | `r, thick, color, alpha` | circle outline |
| `line`   | `x1,y1,x2,y2, thick, color` | a segment (unit space) |

**Coordinate space:** all `r/w/h/x/y` are **fractions of the enemy's radius** (1.0 = full
radius), so one shape definition scales to any enemy size. `color` is a palette name OR the
special token `"body"` (the enemy's own colour) / `"white"`. `alpha` fades the layer.
`spin` (deg/sec) animates off `runTime`. Status-effect tint + rings stay engine-applied on
top (they're universal, not per-enemy art).

A `ShapeLayer` C struct (≤8 fields) + a `Geometry { ShapeLayer layers[MAX_LAYERS]; int n; }`
on the enemy type. `draw_geometry(p, radius, baseColor, runTime, &geo)` walks the layers.
~6 primitive branches replace the 8 hardcoded shape cases, and **any** silhouette is now
data — far more than the original 8.

## 3. The enemy file (one per enemy, in a biome's `enemies/`)

Self-contained: geometry + colour + behaviour + stats. The old `enemies.toml` row, split
into a file, with `shape` now a primitive list.

```toml
# assets/biomes/01_void/enemies/spinner.toml
name = "Spinner"
color = "orchid"            # base colour ("body" in shape layers resolves to this)
hp = 16
speed = 28
radius = 14
xp = 3
min_stage = 1               # earliest wave-index within the biome it appears

move = "hover"              # op name (engine movement primitive)
fire = "ring"               # op name (engine fire primitive)
fire_interval = 2.2

shape = [
  { op="poly", sides=5, r=1.0, color="body", spin=40 },
  { op="circle", r=0.35, color="white", alpha=0.5 },
]
```

The move/fire op names stay (engine primitives, as today). `min_stage` becomes "earliest
**wave index within this biome**" so a biome ramps its own roster.

## 4. biome.toml (the biome metadata)

```toml
name = "The Void"

[theme]
background = "black"        # ClearBackground colour (palette name)
star_colors = ["navy", "indigo", "periwinkle"]   # the 3 parallax layers
# optional palette overrides: remap named colours just for this biome
[theme.palette_override]
coral = "ff4060"           # hex; rebinds "coral" while in this biome

[director]
field_cap_base = 60
field_cap_per_stage = 20
field_cap_max = 400
wave_interval_base = 1.4
waves_to_boss = 5          # formations before the boss
# which formation shapes this biome uses (names; engine has the shape primitives)
formations = ["row", "vee", "stream", "grid", "swarm"]

[boss]
name = "Null Maw"
hp = 600
radius = 46
# boss fire pattern references the same danmaku ops enemies use
phases = [
  { hp_above = 0.66, fire = "aimed_spread", count = 5 },
  { hp_above = 0.33, fire = "aimed_spread", count = 7, plus = "flower" },
  { hp_above = 0.0,  fire = "aimed_spread", count = 10, plus = "flower" },
]
```

## 5. Engine integration

- **`Biome` runtime struct** (in a new `data/biome.{h,c}` or extend dtypes): name, theme
  colours, a `EnemyType roster[MAX_ROSTER]` + count, director params, boss def. An array
  `BIOMES[MAX_BIOMES]` + `NUM_BIOMES`, filled by the loader.
- **`world.biome`** = current biome index. `ETYPES`/`NUM_ETYPES` are **replaced** by
  "the current biome's roster" — `enemies_pick_type`, `enemies_spawn`, `draw_enemy_shape`
  read `BIOMES[w->biome].roster[...]`. (Enemy `type` index is now within-biome.)
- **Progression:** boss death (boss.c) increments `w->biome` (was `w->stage`); on the last
  biome, loop or win. `director_reset` pulls tuning from `BIOMES[w->biome].director`.
- **Theme:** `render_arena` reads the current biome's background + star colours instead of
  the global `PAL_*`. Palette overrides applied on biome enter (re-resolve named colours).
- **`w->stage`** stays as the *within-biome wave counter* (drives roster `min_stage` + the
  difficulty ramp); `w->biome` is the new outer progression.

## 6. Loader changes

- `loader_load_all` still loads the **global** files, then `load_biomes("assets/biomes")`:
  `opendir`, collect + `qsort` the subdir names, for each: parse `biome.toml`, then
  `opendir(biome/enemies)`, parse each `*.toml` into the biome's roster (sorted for
  determinism).
- New parse helpers: `rd_shape()` (array of layer inline-tables → `Geometry`),
  `rd_color_or_body()` (resolves "body"/"white"/palette name → a tagged colour), a string
  array reader (star_colors, formations).
- Directory scan needs `<dirent.h>` (POSIX — fine on Linux/distrobox; the web build would
  need a manifest, out of scope now).
- Hot-reload (F5) re-scans biomes too.

## 7. Migration plan (stays playable each step)

1. **Geometry primitives in C** + `draw_geometry`; add a `shape` array to the *current*
   `enemies.toml` (still flat) and render from it. Delete `draw_enemy_shape`'s switch +
   the `SH_*` enum. Verify enemies look identical. *(Geometry-as-data, no biomes yet.)*
2. **Biome struct + loader scan.** Introduce `BIOMES[]`; wrap the existing roster as a
   single biome `01_default/`. Engine reads `BIOMES[0].roster`. Verify identical play.
3. **Per-biome theme** (background/stars + palette override) in `render_arena` + on enter.
4. **Per-biome director + boss** from `biome.toml`; boss death → next biome.
5. **Author a 2nd biome** (new palette, new enemies with new geometry) to prove the folder
   workflow end to end.
6. Update `assets/README.md` with the biome + geometry schema.

## 8. Risks / decisions

- **`dirent` portability:** desktop-only for now (matches "runtime files" choice). A future
  `make bake` could generate a manifest for web.
- **Enemy `type` index is now biome-local** — anything that stored a global enemy id must
  use (biome, localIndex). Today only spawn/draw touch it; contained.
- **Geometry perf:** a few primitive draws per enemy × hundreds of enemies. Keep layers
  small (≤4 typical); it's the same draw-call order of magnitude as now.
- **Palette overrides + global colour names:** overrides are applied by re-resolving on
  biome enter; the base palette is restored when leaving (store the base, re-apply).

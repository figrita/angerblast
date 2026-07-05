# Boss Editor — Feature Specification

**Tool:** `tools/geoditor.c` extended with a second mode, or a separate `tools/bosseditor.c`  
**Build target:** `make bosseditor` (same Makefile pattern as geoditor)  
**Status:** Pre-implementation spec. Sections marked ⚠️ UNKNOWN contain open design questions.

---

## 1. What This Tool Does

The boss editor lets you build and preview multi-segment bosses visually:

- Compose a boss from up to `MAX_BOSS_PARTS` (currently 5) named parts, each with its own `Geometry`, position offset, scale, color, HP, fire pattern, and emitter.
- Define animation sequences: keyframed motion paths that parts follow during the fight, with tween types and looping behavior.
- Preview the full boss live in the tool — spinning, firing (visual only), animating — at any scrub position.
- Serialize everything to a `[boss]` TOML block that drops directly into a `biome.toml`.

This is a superset of what the current hand-authored TOML achieves, plus animation data that does not yet exist in the runtime.

---

## 2. Current Data Model (Source of Truth)

### `BossPartDef` (biome.h)
```c
typedef struct {
    float offX, offY;    // spawn offset from anchor (pixels)
    float hp;
    float radius;
    bool  isHead;
    int   fireOp;        // -1 = none
    int   count;
    char  emitter[32];
    float interval;
    FireParams fp;
    Color color;
} BossPartDef;
```

### `BossDef` (biome.h)
```c
typedef struct {
    char         name[48];
    float        hp;          // single-body only
    float        radius;      // single-body only
    BossPhase    phases[MAX_BOSS_PHASES];   // single-body only
    int          phaseCount;
    BossPartDef  parts[MAX_BOSS_PARTS];
    int          partCount;
} BossDef;
```

### Live TOML examples (multi-part)
```toml
[boss]
name = "Cinder Tyrant"
[[boss.parts]]
isHead=true  hp=700  radius=46  offX=0  offY=-10
color="crimson"  fireOp="aimed"  count=5  interval=1.2
emitter="spiral_pods"

[[boss.parts]]
hp=300  radius=30  offX=-130  offY=40
color="maroon"  fireOp="ring"  count=10  interval=1.4

[[boss.parts]]
hp=300  radius=30  offX=130  offY=40
color="maroon"  fireOp="spread"  count=7  interval=1.0
```

### What the runtime does NOT currently have
- Any animation keyframe structs
- Part-specific Geometry shapes (all multi-part bosses use the radius circle implicitly — no custom shape per part)
- Phase transitions on multi-part bosses (only single-body has `BossPhase` / `hpAbove` thresholds)
- Per-part movement independent of the shared anchor sweep

---

## 3. New Data Needed in the Runtime

These structs must be added to `biome.h` and the loader before the tool can write files the game will read.

### 3a. Per-Part Geometry
```c
// Add to BossPartDef:
Geometry geo;          // shape definition; if count==0, falls back to circle
bool     hasGeo;       // false = draw as plain circle (backward compat)
```

### 3b. Animation Keyframes
```c
typedef enum {
    TWEEN_LINEAR,
    TWEEN_EASE_IN,
    TWEEN_EASE_OUT,
    TWEEN_EASE_INOUT,
    TWEEN_BOUNCE,      // ⚠️ UNKNOWN: needed? keep for now, skip if complex
    TWEEN_COUNT
} TweenType;

typedef struct {
    float     t;           // time in seconds from sequence start
    float     offX, offY;  // position relative to anchor at this keyframe
    float     rotation;    // degrees; 0 = upright
    float     scale;       // multiplier on base radius; 1.0 = normal
    TweenType tween;       // interpolation to NEXT keyframe
} BossKeyframe;

#define MAX_BOSS_KEYFRAMES 16

typedef struct {
    BossKeyframe frames[MAX_BOSS_KEYFRAMES];
    int          frameCount;
    bool         loops;        // true = wrap back to frame 0 at sequence end
    float        duration;     // total sequence length (seconds); 0 = auto from last frame t
} BossAnimSeq;

// Add to BossPartDef:
BossAnimSeq anim;
```

### 3c. ⚠️ UNKNOWN — Phase-gated animation
Should animation sequences change at HP thresholds (like single-body phases)?  
**Option A:** One sequence per part, plays forever looping — simple.  
**Option B:** Multiple sequences per part, each with an `hpAbove` gate, runtime picks the active one.  
**Option C:** The sequence itself contains "phase markers" — the runtime skips to a marker when HP crosses a threshold.  
**Recommendation:** Start with Option A (one sequence, looping). Phase-switching is a separate feature.

### 3d. ⚠️ UNKNOWN — Multi-part boss phases
Currently `BossPhase` (hpAbove fire-pattern switching) only exists on single-body bosses. Multi-part bosses never switch fire patterns as HP drops. Should the boss editor expose this?  
**Leave open.** This spec does not design it. The editor should not write `phases[]` for multi-part bosses.

---

## 4. TOML Serialization Format (Target)

The tool outputs a `[boss]` TOML block. For parts with geometry and animation:

```toml
[boss]
name = "Storm Sovereign"

[[boss.parts]]
isHead  = true
hp      = 1100
radius  = 48
offX    = 0
offY    = -10
color   = "gold"
fireOp  = "none"
emitter = "spiral_pods"
interval = 1.0
shape = [{op="poly",sides=6,r=1.0,color="body"},{op="ring",r=0.6,thick=0.1,color="white"}]

[[boss.parts.anim.frames]]
t=0.0  offX=0    offY=-10  rotation=0   scale=1.0  tween="ease_inout"
[[boss.parts.anim.frames]]
t=1.5  offX=40   offY=-40  rotation=15  scale=1.0  tween="ease_inout"
[[boss.parts.anim.frames]]
t=3.0  offX=0    offY=-10  rotation=0   scale=1.0  tween="ease_inout"
anim_loops = true

[[boss.parts]]
hp     = 460
radius = 32
offX   = -140
offY   = 44
color  = "coral"
fireOp = "ring"
count  = 12
interval = 1.2
shape = [{op="poly",sides=3,r=1.0,color="body",spin=1.0}]

[[boss.parts.anim.frames]]
t=0.0  offX=-140  offY=44   rotation=0    scale=1.0  tween="linear"
[[boss.parts.anim.frames]]
t=2.0  offX=-80   offY=80   rotation=-20  scale=0.9  tween="linear"
[[boss.parts.anim.frames]]
t=4.0  offX=-140  offY=44   rotation=0    scale=1.0  tween="linear"
anim_loops = true
```

⚠️ UNKNOWN: The loader (`src/data/loader.c`) currently parses `BossPartDef` from TOML using the toml.h library. Adding `shape=` and `anim` sub-tables requires either:
- Extending the existing loader with more `toml_` calls (likely messy but safe)
- A dedicated `boss_load_part_anim()` helper function

The loader already handles `shape=` for `EnemyType` (in `loader.c`). That exact pattern can be reused for `BossPartDef.geo`.

---

## 5. Editor Layout

```
┌────────────────────────────────────────────────────────────────────┐
│ [BROWSER 210]  [───────── PREVIEW (flex) ─────────]  [PANEL 320]  │
│                                                                     │
│  Biomes        ┌───────────────────────────────┐   ┌────────────┐ │
│  > Void        │  TOOLBAR: zoom dark/lt body▸  │   │ PARTS      │ │
│    Ember       │                               │   │ ─────────  │ │
│    Frost  ───▶ │      CANVAS                   │   │ ▶ Head     │ │
│    Tempest     │   (full boss preview,         │   │   Hand L   │ │
│    Abyss       │    animating in real time)    │   │   Hand R   │ │
│    Dino        │                               │   │ [+] [del]  │ │
│                └───────────────────────────────┘   ├────────────┤ │
│                                                    │ PROPERTIES │ │
│                ┌─ TIMELINE ────────────────────┐   │ (selected  │ │
│                │ Head  ●────●────●  [loop]      │   │  part)     │ │
│                │ HandL ●──────●──●  [loop]      │   │            │ │
│                │ HandR ●──────●──●  [loop]      │   │ [SHAPE]    │ │
│                │ ──────────────────── 4.0s      │   │ (embedded  │ │
│                │ [scrub slider]      [▶ play]   │   │  geoditor  │ │
│                └───────────────────────────────-┘   │  panel)    │ │
│                                                    └────────────┘ │
├────────────────────────────────────────────────────────────────────┤
│ [TOML output ───────────────────────────────────────────────] copy │
└────────────────────────────────────────────────────────────────────┘
```

**Three panels stacked in the right column:**
1. Part list (add/remove/reorder, select active part)
2. Part properties (stats, fire params, color, emitter)
3. Shape sub-panel (embedded geoditor layer editor for this part's `Geometry`)

**Below the preview canvas:** Timeline strip (not a full DAW — just keyframe dots per part).

---

## 6. Modes / Tabs

The tool has two top-level modes toggled with **M** or tab buttons in the toolbar:

| Mode | Description |
|------|-------------|
| **LAYOUT** | Position parts by dragging them on the canvas. Scrub through animation. This is the default mode. |
| **SHAPE** | Edit the selected part's geometry layers (same UI as geoditor). |

⚠️ UNKNOWN: Should LAYOUT and SHAPE be separate screen modes (like pages), or should the shape editor always be visible in a sub-panel while layout is visible in the canvas? The spec above shows them combined (shape panel in the right column). This is the preferred approach if the right panel is tall enough (≥550px). Below that height, switching modes may be necessary.

---

## 7. Canvas Behavior

### Layout mode
- All parts drawn at their current animation positions (driven by scrub time).
- Selected part highlighted with a colored border ring.
- **Click + drag** a part to move it — updates the nearest keyframe's offX/offY if a keyframe is selected, otherwise updates the part's base offX/offY.
- **Dashed circle** shows each part's `radius`.
- **Crosshair** at anchor (0, 0) center.
- **Reference ring** shows the expected boss arena size (~180px on the canvas at M zoom).
- **`G` key** = grab selected part (enter drag mode with keyboard, move with arrow keys, confirm with Enter).

### Shape mode
- Selected part only, same behavior as current geoditor preview.
- Other parts shown dimly in background for spatial context.

### Playback controls
- **Space** = play/pause animation
- **Left/Right arrow** = step one frame (1/60s) when paused  
- **Home** = rewind to t=0
- Timeline scrub bar: click to seek, drag to scrub

---

## 8. Part List Panel

```
PARTS
─────────────────
▶ [●] Head         (gold swatch, isHead marker)
  [●] Hand L
  [●] Hand R
─────────────────
[+Add]  [Dup]  [Del]
```

- Click a row to select that part. The canvas highlights it, the property panel and shape panel update.
- Color swatch (clickable — opens color cycle, same as geoditor layer swatch).
- `isHead` marker shown as a small crown icon or "H" badge.
- Max 5 parts (`MAX_BOSS_PARTS`). Add button disabled at cap.
- Reorder with `[` / `]` keys or drag (drag is a stretch goal — ⚠️ UNKNOWN whether raylib drag-reorder is worth implementing).

---

## 9. Part Properties Panel

Fields for the selected part, displayed as labeled rows with `+/-` or `< >` cycle buttons (same UX as geoditor):

| Field | Type | Notes |
|-------|------|-------|
| `isHead` | bool | Only one part should be isHead — editor enforces this |
| `offX`, `offY` | float | Base spawn offset; shown as a pair |
| `hp` | float | Step 50 |
| `radius` | float | Step 1 |
| `color` | palette | Same cycle UI as geoditor |
| `fireOp` | enum | none / aimed / ring / spread / spiral / radial |
| `count` | int | Projectile count |
| `interval` | float | Seconds between fires |
| `emitter` | string | ⚠️ UNKNOWN — how to enumerate valid emitter IDs for a dropdown? Options: hardcode list, read from assets/, or free-text field. Recommend: read emitter names from loaded data. |
| `arc` | float | FireParams.arc |
| `spin` | float | FireParams.spin |
| `aimed` | bool | FireParams.aimed |
| `bullet_color` | palette | FireParams.color |

⚠️ UNKNOWN: `FireParams` has more fields than what most bosses use (`count`, `countPerStage`, `speedScale`, `arc`, `spin`, `aimed`, `color`). Should the editor expose all of them, or just the commonly used subset? **Recommend exposing all** with rarely-used ones hidden below a fold/separator.

---

## 10. Animation / Keyframe Panel

### Timeline strip (below preview)

- Horizontal axis = time (0 to sequence duration).
- One row per part.
- Each keyframe shown as a draggable dot `●`.
- Selected keyframe highlighted.
- Click empty space on a row = add keyframe at that time.
- Right-click keyframe = delete.
- Drag keyframe dot = move in time.

### Keyframe properties (shown when a keyframe is selected)

| Field | Type | Notes |
|-------|------|-------|
| `t` | float | Time in seconds |
| `offX`, `offY` | float | Position at this keyframe |
| `rotation` | float | Degrees |
| `scale` | float | Radius multiplier |
| `tween` | enum | linear / ease_in / ease_out / ease_inout / bounce |

Editing `offX`/`offY` in properties OR dragging the part on the canvas at that scrub position both update the keyframe.

⚠️ UNKNOWN: Should `rotation` animate the whole part (all layers spin together) or only affect a specific layer's `spin` offset? Recommend: rotate the entire part transform. Per-layer spin is already handled by `ShapeLayer.spin` in the geometry system.

⚠️ UNKNOWN: Should `scale` be per-keyframe or just a static per-part multiplier? Per-keyframe enables "squish on impact" effects. Per-static is simpler. **Start with per-keyframe.**

⚠️ UNKNOWN: What is the right default sequence for a newly added part? Options:
- Two keyframes: t=0 at base offset, t=2.0 at base offset + wobble. Feels "alive."
- One keyframe at t=0 (no movement). Safest.
- No keyframes (static). Backward compatible.  
**Recommend: no keyframes by default** — the TOML omits the `anim` block and the loader/runtime treats missing anim as static.

### Sequence controls

- `[loop]` checkbox per part — sets `anim.loops`
- `[duration]` field per part — sets `anim.duration` (0 = auto from last frame t)
- Global `[▶ Play]` / `[⏸ Pause]` / `[⏮ Reset]` buttons

---

## 11. Shape Sub-Panel (Embedded Geoditor)

When a part is selected, the right panel's lower half shows the geoditor layer editor for that part's `Geometry`. This is the same code as `draw_layer_list()` and `draw_props()` from geoditor, but scoped to `selectedPart->geo` instead of the global `g_geo`.

- Body color for preview is the part's own `color` field (not the global boss body color).
- The embedded preview (solo inset) shows the part's shape alone.
- The full canvas preview still shows the composed boss.

⚠️ UNKNOWN: If we share the geoditor draw functions, we need to either:
1. Pass the target `Geometry*` and `Color*` as parameters to `draw_layer_list` / `draw_props` (refactor geoditor.c).
2. Extract the geoditor UI into a shared `tools/geoui.c` that both `geoditor.c` and `bosseditor.c` include.
3. Merge both tools into one binary with a mode switch.

**Recommend option 3** — one binary, mode switch (`make bosseditor` just builds `tools/bosseditor.c` which `#include`s `tools/geoui.h`). This avoids build complexity.

---

## 12. Keyboard Shortcuts

| Key | Action |
|-----|--------|
| `M` | Toggle Layout / Shape mode |
| `Space` | Play / pause animation |
| `Home` | Rewind to t=0 |
| `Left` / `Right` | Step one frame (when paused) |
| `G` | Grab — enter drag mode for selected part |
| `Enter` | Confirm drag |
| `Esc` | Cancel drag / deselect keyframe |
| `K` | Add keyframe at current scrub time for selected part |
| `Delete` | Delete selected keyframe (or selected layer in Shape mode) |
| `Tab` | Cycle selected part |
| `[` / `]` | Reorder selected part |
| `A` | Add layer (Shape mode) |
| `B` / `C` / `O` | Color / pal color / outline (Shape mode) |
| `S` | Copy TOML to clipboard |
| `1`–`5` | Jump to part by index |

---

## 13. TOML Output Strategy

The `S` key (and a persistent copy button) serializes the current boss to a `[boss]` TOML block.

**What gets omitted when default:**
- `shape=` omitted if `hasGeo == false` (backward compatible with current plain-circle parts)
- `[[boss.parts.anim.frames]]` block omitted if `anim.frameCount == 0`
- `anim_loops` omitted if false
- `arc`, `spin`, `aimed` omitted if at fire-op defaults (0, 0, false)

**What the output looks like in the TOML bar:**
The bottom strip is too narrow for a full multi-part boss (potentially 80+ lines). Instead:
- The strip shows a one-line summary: `3 parts | 9 keyframes | [copy full TOML]`
- Copying puts the full block on the clipboard

⚠️ UNKNOWN: Should the tool also be able to **write directly to a `biome.toml` file** (replacing the `[boss]` section in-place)? This would require parsing the existing file, finding the `[boss]` block, replacing it, and writing back. Considerably more complex than clipboard. **Defer to v2.**

---

## 14. File Loading

On startup, the tool loads the full asset tree the same way geoditor does (`loader_load_all(assets)`). The browser panel on the left lets you pick a biome, then shows the current `[boss]` data for that biome loaded into the editor state.

⚠️ UNKNOWN: If the loader parses biome.toml into the `BossDef` struct (which it does), and we add `BossAnimSeq` and `Geometry geo` to `BossPartDef`, will the loader correctly handle old TOML files that don't have those fields? It must degrade gracefully: missing `shape=` → `hasGeo=false`, missing `anim` → `frameCount=0`. The loader already does this for optional fields via `toml_table_t` null checks, so this should be safe — but must be verified when implementing.

---

## 15. Runtime Changes Required

The boss editor is a standalone tool but the animation data it produces must be consumed by the game. Required runtime changes:

### biome.h
- Add `BossKeyframe`, `BossAnimSeq`, `TweenType` structs (section 3b above)
- Add `BossAnimSeq anim` and `Geometry geo; bool hasGeo` to `BossPartDef`

### loader.c
- Parse `shape=` in `[[boss.parts]]` the same way enemy shape= is parsed
- Parse `[[boss.parts.anim.frames]]` sub-table array into `BossAnimSeq`
- Parse `anim_loops`, `anim_duration` per part

### boss.c
- In `boss_update_parts()`: if `part.anim.frameCount > 0`, evaluate the keyframe sequence at current time and apply offset/rotation/scale as a transform on top of the base offX/offY
- In `boss.c` draw call (or render.c wherever boss parts are drawn): if `part.hasGeo`, call `geometry_draw(&part.geo, ...)` instead of `DrawCircleV`

⚠️ UNKNOWN: Where are boss parts currently drawn? The survey did not include render.c boss drawing code. If boss parts are drawn in `render.c` rather than `boss.c`, the `geometry_draw` call goes there instead.

⚠️ UNKNOWN: The tween evaluation function (lerp with curve) needs to be written somewhere. Options: `src/data/geometry.c` (wrong home), `src/systems/boss.c` (local to boss), `src/data/anim.c` (new file). **Recommend `src/data/anim.c`** — small file, shared by boss and potentially future systems.

---

## 16. Implementation Order

1. **Data structs** — add `BossKeyframe`, `BossAnimSeq`, `TweenType` to `biome.h`; add `anim`/`geo`/`hasGeo` to `BossPartDef`. No behavior change — old bosses still work.
2. **Loader** — parse new fields from TOML; silent no-op on old files.
3. **Runtime anim eval** — `src/data/anim.c` with `anim_eval(BossAnimSeq*, float t, float *offX, float *offY, float *rot, float *scale)`.
4. **boss.c** — apply anim eval in `boss_update_parts()`, call `geometry_draw` when `hasGeo`.
5. **Boss editor tool** — `tools/bosseditor.c`, can use existing geoditor as a reference; embed geoui functions directly (option 3 from section 11).
6. **Iterate on TOML round-trip** — load existing biome, edit, copy TOML, paste into file, verify game renders correctly.

---

## 17. Open Questions Summary

| # | Question | Recommended default |
|---|----------|---------------------|
| 1 | Phase-gated animation (HP thresholds switch sequences)? | Defer — one looping sequence per part only |
| 2 | Multi-part boss fire-pattern phases (like single-body BossPhase)? | Defer — not in editor v1 |
| 3 | `rotation` animates whole part transform or per-layer spin? | Whole-part transform |
| 4 | `scale` per-keyframe or static? | Per-keyframe |
| 5 | Default keyframes for new parts? | None (static, no anim block in TOML) |
| 6 | Emitter dropdown: hardcode list or read from assets? | Read from loaded data |
| 7 | Shared geoui: separate file or merge into one binary? | One binary with mode switch |
| 8 | Write directly back to biome.toml? | Defer to v2, clipboard only |
| 9 | Drag-reorder parts in the part list? | Defer, use [ ] keys |
| 10 | `BOUNCE` tween type — implement or drop? | Drop for v1 (4 tweens: linear/ease_in/ease_out/ease_inout) |
| 11 | Where are boss parts currently drawn (boss.c or render.c)? | Needs code check before implementing |
| 12 | Tween eval function home? | New `src/data/anim.c` |

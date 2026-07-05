# Spec: Enemy Status Effects + Evolution Effects

Two intertwined systems for Angerblast:

1. A **status-effect system** on enemies — a generic, data-driven affliction layer
   (Slow, Burn at minimum; Poison/Freeze/Shock/Weaken/Mark drop in as data).
2. A set of **evolution effects** — distinctive on-kill / on-hit / on-fire / aura / economy
   behaviours that hook into the existing weapon **evolution** layer, including the
   user-requested *"attract all gems from enemies killed by this gun"*.

Everything below references the code that exists **today**: the single `GameWorld *w`
threaded through systems (no globals), the fixed-size pools with a rolling `cursor`, the
`noun_verb` module API, the data tables in `src/data/`, and `weapon_stats()` folding
level + runes + evolution. Build is `make` in the distrobox; logic tests are
`tests/test_runes.c` via `make test`.

---

## 0. How this connects to what exists

The codebase already has a **proto-status layer** that we are generalising:

- `Puddle` (`core/common.h`) — a ground zone that ticks DoT against enemies inside it
  (`puddles_update`). This is *ground* burn, not an *on-enemy* burn.
- `HIT_BURN` / `HIT_EXPLODE` (`core/common.h`) — a bullet `onHit` tag, stamped via the
  `w->bulletOnHit` / `w->bulletStyle` pattern (`bullets_add`), triggered in `bullet_onhit`
  (`bullets.c`). `HIT_BURN` today just spawns a small `Puddle`.
- `SPEC_BURN` (`evolutions.h`) — the NAPALM trifecta column; `emit_special` maps it to
  `HIT_BURN`, so a NAPALM weapon already "leaves fire".

The new system adds a **third thing**: an effect that lives **on the enemy itself** with a
timer/magnitude, so Burn can tick on a moving target (not just on the ground), Slow can
reduce that enemy's speed and fire rate, and future effects (Freeze/Shock/Weaken/Mark)
slot in by data. We **keep** puddles and `HIT_BURN`; we re-point them at the new layer
(see §5).

---

## 1. Status-effect data model

### 1.1 The representation decision (per-enemy)

The enemy pool is `MAX_ENEMIES = 2048` (`core/common.h`), `enemies_update` already loops
all 2048 every frame. Two candidate representations:

**Option A — fixed array of active effects per enemy**
```c
typedef struct { unsigned char id; float timer; float mag; unsigned char stacks; } ActiveEffect;
// Enemy gains: ActiveEffect effects[MAX_ENEMY_EFFECTS];  (e.g. 4)
```
Pros: arbitrary effect mix, clean iteration. Cons: 4 × 8 B = 32 B × 2048 = 64 KB of extra
enemy memory, an inner loop per enemy per frame, and per-effect "find existing / find free
slot" scans on every apply.

**Option B — flat per-effect fields on `Enemy` (RECOMMENDED)**
Because the full effect roster is small and known (≤8), store each as a dedicated
`float timer` (+ `float mag` / `unsigned char stacks` where the effect needs it). An effect
is "active" iff its timer `> 0`.
```c
// added to Enemy:
float slowTimer,  slowMag;     // Slow:  mag in [0,1] = fractional speed/rate reduction
float burnTimer,  burnDps;     // Burn:  dps applied while burnTimer > 0
float burnTick;                // Burn:  sub-second accumulator (apply DoT ~10x/s)
// room for the data-driven roster:
float effTimer[EFFECT_COUNT];  // one timer per EffectId (Slow/Burn reuse the above OR live here)
float effMag[EFFECT_COUNT];    // magnitude per effect
```

**Recommendation: Option B, with a single small parallel pair of arrays keyed by
`EffectId`** — `float effTimer[EFFECT_COUNT]` and `float effMag[EFFECT_COUNT]` (plus one
`float burnTick` accumulator shared by all DoT effects). This is the best fit for a 2048
pool: it is **branch-cheap** (a per-effect `if (timer <= 0) continue;` with most effects
inactive most of the time), needs **no slot search** on apply (index by id), and the whole
status block is a contiguous `2 * EFFECT_COUNT` floats. With `EFFECT_COUNT == 8` that is
~68 B/enemy — comparable to Option A but with O(1) apply and no nested struct.

Concretely, add to `Enemy` in `core/common.h`:
```c
typedef struct {
    Vector2 pos, vel;
    float radius, hp, maxHp;
    int type;
    float fireTimer, phase;
    float t;
    Vector2 anchor;
    int stepDir;
    float patternAng;
    Color color;
    bool active;
    // ---- status effects (NEW) ----
    float effTimer[EFFECT_COUNT];   // seconds remaining per EffectId (0 = inactive)
    float effMag[EFFECT_COUNT];     // magnitude per EffectId (meaning is per-effect)
    unsigned char effStacks[EFFECT_COUNT]; // for stack-rule effects (Poison, Mark)
    float dotTick;                  // shared sub-second accumulator for DoT effects
} Enemy;
```
> Note: `EFFECT_COUNT` must be visible in `common.h`. Define the `EffectId` enum **in
> `core/common.h`** (next to `HIT_*` / `BSTYLE_*`), so `Enemy` can size the arrays without a
> dependency on `data/`. The richer `EffectDef` table lives in `data/effects.{h,c}`.

### 1.2 `EffectId` enum (in `core/common.h`)

```c
// status effects that can be active on an enemy
typedef enum {
    EFFECT_SLOW,    // movement + fire-rate reduction (mag = fraction, 0.4 = 40% slower)
    EFFECT_BURN,    // fire DoT (mag = dps)
    EFFECT_POISON,  // stacking DoT that ramps with stacks (mag = dps per stack)
    EFFECT_FREEZE,  // hard stop: movement AND firing fully halted (mag unused)
    EFFECT_SHOCK,   // stun: movement halted, brief (mag unused); pairs with chain dmg
    EFFECT_WEAKEN,  // vulnerability: incoming damage * (1 + mag)
    EFFECT_MARK,    // tags target; consumed by detonate / focus-fire effects (mag unused)
    EFFECT_COUNT
} EffectId;
```

### 1.3 `EffectDef` data table (in `data/effects.{h,c}`)

Mirrors the `RUNES[]` / `EVOLUTIONS[]` table style (a flat `const` array indexed by id):
```c
typedef enum { STACK_REFRESH, STACK_MAX_DURATION, STACK_ADD } StackRule;

typedef struct {
    const char *name;       // "Slow", "Burn", ...
    Color       tint;       // colour to blend into the enemy while afflicted
    float       defDuration;// default duration if an applier passes 0
    StackRule   stackRule;  // how re-application combines
    unsigned char maxStacks;// cap for STACK_ADD effects (1 for non-stacking)
    bool        isDot;      // true → ticks damage via dotTick
    bool        haltsMove;  // Freeze/Shock
    bool        haltsFire;  // Freeze
} EffectDef;

extern const EffectDef EFFECTS[EFFECT_COUNT];
```
```c
// data/effects.c
const EffectDef EFFECTS[EFFECT_COUNT] = {
  [EFFECT_SLOW]   = { "Slow",   (Color){120,180,255,255}, 2.5f, STACK_MAX_DURATION, 1, false,false,false },
  [EFFECT_BURN]   = { "Burn",   (Color){255,120,40,255},  3.0f, STACK_REFRESH,      1, true, false,false },
  [EFFECT_POISON] = { "Poison", (Color){120,230,90,255},  4.0f, STACK_ADD,          5, true, false,false },
  [EFFECT_FREEZE] = { "Freeze", (Color){160,220,255,255}, 1.2f, STACK_REFRESH,      1, false,true, true  },
  [EFFECT_SHOCK]  = { "Shock",  (Color){255,240,120,255}, 0.6f, STACK_REFRESH,      1, false,true, false },
  [EFFECT_WEAKEN] = { "Weaken", (Color){230,120,230,255}, 4.0f, STACK_MAX_DURATION, 1, false,false,false },
  [EFFECT_MARK]   = { "Mark",   (Color){255,255,255,255}, 5.0f, STACK_REFRESH,      1, false,false,false },
};
```

**Stack rules** (applied in `enemies_apply_effect`):
- `STACK_REFRESH` — set timer to `max(existing, duration)` is fine, but "refresh" means
  *reset to new duration* (Burn keeps burning as long as you keep hitting). Use
  `timer = duration; mag = magnitude;`.
- `STACK_MAX_DURATION` — keep the **stronger / longer**: `timer = max(timer, duration)`,
  `mag = max(mag, magnitude)` (Slow shouldn't be cancellable by a weaker source).
- `STACK_ADD` — `stacks = min(maxStacks, stacks+1); timer = duration;` and effective DoT is
  `mag * stacks` (Poison ramps).

---

## 2. Applying effects

### 2.1 The public API (in `systems/enemies.{h,c}`)

```c
// enemies.h
void enemies_apply_effect(GameWorld *w, Enemy *e, EffectId id, float duration, float magnitude);
```
```c
// enemies.c
void enemies_apply_effect(GameWorld *w, Enemy *e, EffectId id, float duration, float magnitude) {
    if (!e->active) return;
    const EffectDef *d = &EFFECTS[id];
    if (duration <= 0) duration = d->defDuration;
    switch (d->stackRule) {
        case STACK_REFRESH:      e->effTimer[id] = duration; e->effMag[id] = magnitude; break;
        case STACK_MAX_DURATION: if (duration > e->effTimer[id]) e->effTimer[id] = duration;
                                 if (magnitude > e->effMag[id])  e->effMag[id]  = magnitude; break;
        case STACK_ADD:          if (e->effStacks[id] < d->maxStacks) e->effStacks[id]++;
                                 e->effTimer[id] = duration; e->effMag[id] = magnitude; break;
    }
    // tiny apply-fx so the player sees it land
    fx_spawn_particles(w, e->pos, 2, d->tint, 60);
}
```
This is the single choke point. Bullets, auras, on-kill code, and puddles all call it.

### 2.2 Where applies happen

| Source | Location | How |
|--------|----------|-----|
| **Projectile on-hit** | `bullet_onhit()` in `bullets.c`, and the per-enemy hit in `bullets_update()` collision loop | extend the `w->bulletOnHit` stamping (§2.3) so the bullet carries an `EffectId` to apply to whatever it strikes |
| **Aura / beam weapons** | `enemies_damage_near()` in `enemies.c`, and the lightning/orbital/aura paths in `weapons.c` | these already iterate the struck enemies; add an `effect` parameter (see §2.3) |
| **On-kill** | `enemies_kill()` in `enemies.c` | evolution on-kill hooks fire here (e.g. chain-explode, gem-magnet) — but on-kill applies to *neighbours*, via `enemies_damage_near` / `enemies_apply_effect` on a near-scan |
| **Puddles** | `puddles_update()` | re-point `HIT_BURN` puddles to apply `EFFECT_BURN` on the enemies inside, instead of (or in addition to) raw `hp -=` |

### 2.3 Extending the `w->bulletOnHit` stamping pattern (RECOMMENDED)

Today `GameWorld` carries two "next-bullet" stamps that `bullets_add` copies onto each
friendly bullet:
```c
int bulletOnHit;   // HIT_*  stamped onto bullets created by the next bullets_add calls
int bulletStyle;   // BSTYLE_* stamped onto the next friendly bullets
```
Add a **third stamp** for status effects, exactly parallel:
```c
// world.h GameWorld:
int   bulletEffect;     // EffectId to apply on hit, or -1 for none
float bulletEffectMag;  // magnitude carried to enemies_apply_effect (0 → EffectDef default)
```
And the matching fields on `Bullet` (`core/common.h`), copied in `bullets_add` just like
`onHit`/`style`:
```c
// Bullet:
int   effect;      // EffectId to apply on hit, or -1
float effectMag;
```
```c
// bullets_add() — alongside the existing onHit/style copy:
int   effect    = friendly ? w->bulletEffect    : -1;
float effectMag = friendly ? w->bulletEffectMag : 0;
// ... .effect = effect, .effectMag = effectMag ...
```
Then in the `bullets_update()` collision loop, after `e->hp -= b->damage;`:
```c
if (b->effect >= 0) enemies_apply_effect(w, e, b->effect, 0, b->effectMag);
```
`emit_special()` and `fire_weapon()` set `w->bulletEffect` the same way they set
`w->bulletOnHit` today, and reset both to default (`-1` / `0`) at the end of the fire call.

For **aura/lightning/orbital/flame** (which call `enemies_damage_near` or hit enemies
directly, not via bullets), add an overload that carries the effect:
```c
void enemies_damage_near_fx(GameWorld *w, Vector2 c, float range, float dmg,
                            EffectId effect, float dur, float mag);
// existing enemies_damage_near becomes a thin wrapper passing effect = -1.
```
This keeps the common path allocation-free and lets evolved auras "slow everything they
tick" (§6 Cryostatic Field) without touching the hot bullet loop.

---

## 3. Ticking effects (per frame, cheap)

All ticking happens in **one new helper** called from the top of the existing
`enemies_update()` per-enemy body (which already loops 2048 and has `e`, `T`, `dt` in hand).

```c
// enemies.c — called once per active enemy from enemies_update, before movement
static void enemy_tick_effects(GameWorld *w, Enemy *e, float dt,
                               float *outSpeedMul, float *outFireMul, bool *outFrozen) {
    float speedMul = 1.0f, fireMul = 1.0f; bool frozen = false;
    bool anyDot = false; float dotDps = 0.0f;

    for (int id = 0; id < EFFECT_COUNT; id++) {
        if (e->effTimer[id] <= 0) continue;          // cheap early-out, usually true
        e->effTimer[id] -= dt;
        if (e->effTimer[id] <= 0) { e->effStacks[id] = 0; e->effMag[id] = 0; continue; }
        const EffectDef *d = &EFFECTS[id];
        if (d->haltsMove) frozen = true;
        if (id == EFFECT_SLOW)   { speedMul *= (1.0f - e->effMag[id]); fireMul *= (1.0f - e->effMag[id]); }
        if (id == EFFECT_FREEZE) { speedMul = 0; fireMul = 0; }
        if (id == EFFECT_SHOCK)  { speedMul = 0; }   // stunned movement; can still finish a wind-up
        if (d->isDot) { anyDot = true;
            float st = (d->stackRule == STACK_ADD) ? (float)e->effStacks[id] : 1.0f;
            dotDps += e->effMag[id] * st; }
    }

    if (anyDot) {                                    // apply DoT ~10x/sec, not every frame
        e->dotTick += dt;
        if (e->dotTick >= 0.1f) {
            float dmg = dotDps * e->dotTick;
            e->dotTick = 0;
            e->hp -= dmg;
            fx_spawn_damagenum(w, e->pos, (int)(dmg+0.5f), false);
            if (e->hp <= 0) { enemies_kill(w, e); return; }   // dead-by-DoT
        }
    }
    *outSpeedMul = speedMul; *outFireMul = fireMul; *outFrozen = frozen;
}
```

### 3.1 Hooking Slow / Freeze into movement + fire

`enemies_update()` already computes `sp` (effective speed) once per enemy. Multiply it by
`speedMul`:
```c
float sp = T->speed * (1.0f + w->stage*ENEMY_SPEED_PER_STAGE) * speedMul;
```
If `frozen`, skip the whole `switch (T->move)` movement block (the enemy holds position).
For fire rate, `enemy_fire()` decrements `e->fireTimer -= dt`. Pass `fireMul` so a slowed
enemy charges its danmaku slower:
```c
e->fireTimer -= dt * fireMul;   // fireMul < 1 → fires less often; 0 when frozen
```
(Threaded by setting `e->fireTimer` decrement, or by passing `fireMul` into `enemy_fire`.)

### 3.2 Weaken hook (damage taken)

`EFFECT_WEAKEN` multiplies **incoming** damage. The cleanest single point is wherever
`e->hp -=` happens. Add a tiny inline used by `bullets_update`, `enemies_damage_near`, the
lightning path, and DoT:
```c
static inline float enemy_incoming(const Enemy *e, float dmg) {
    return (e->effTimer[EFFECT_WEAKEN] > 0) ? dmg * (1.0f + e->effMag[EFFECT_WEAKEN]) : dmg;
}
// usage: e->hp -= enemy_incoming(e, b->damage);
```

### 3.3 Per-frame cost at scale

- Outer loop is the existing 2048-enemy `enemies_update` walk — **no new loop**.
- The `for (id < EFFECT_COUNT)` inner loop is 8 iterations of `if (timer<=0) continue;`.
  For the common case (no effects) that is 8 compares and a branch — a handful of ns.
- DoT damage numbers are throttled to 10 Hz via `dotTick`, so a screen full of burning
  enemies doesn't flood the `MAX_DMGNUM = 512` pool (and FX cursor wrap handles overflow).
- No heap, no per-frame allocation, struct stays POD (memcpy-able, matches the codebase).

This is the reason for **Option B**: the hot path is "8 timer compares, almost all skipped".

---

## 4. Visual feedback

Afflicted enemies must read at a glance. `draw_enemy_shape()` (`platform/render.c:59`)
draws the enemy with `Color c = e->color`. Insert a **tint blend** computed from active
effects right before drawing:

```c
// in draw_enemy_shape, before the switch:
Color c = e->color;
for (int id = 0; id < EFFECT_COUNT; id++) {
    if (e->effTimer[id] <= 0) continue;
    Color t = EFFECTS[id].tint;
    c.r = (unsigned char)((c.r + t.r) / 2);
    c.g = (unsigned char)((c.g + t.g) / 2);
    c.b = (unsigned char)((c.b + t.b) / 2);
}
```
Plus per-effect flourishes (cheap, reuse `fx_spawn_particles` and primitive draws):
- **Burn**: occasional upward ember particle in `enemy_tick_effects` on the DoT tick —
  `fx_spawn_particles(w, e->pos, 1, EFFECTS[EFFECT_BURN].tint, 80);` (already wired since the
  DoT branch runs there).
- **Slow / Freeze**: a faint cyan ring — in `draw_enemy_shape`,
  `if (e->effTimer[EFFECT_FREEZE]>0) DrawCircleLines(p.x, p.y, r*1.3f, Fade(EFFECTS[EFFECT_FREEZE].tint,0.6f));`
  Frozen enemies can also draw a static white glint instead of the rotating one.
- **Poison**: small green bubbles via the same DoT-tick particle path (different tint).
- **Shock**: a 1-frame white flash / tiny arc lines — draw 2–3 short `DrawLineV` jitters.
- **Mark**: a small reticle (`DrawPoly(p, 4, r*1.4f, rt*90, Fade(white,0.7f))`) so the
  player can see detonation targets.

The existing `Puddle` ground draw in `render_world` (the orange/green discs) stays as-is —
it is the *ground* visual; on-enemy burn is the new tint + embers.

---

## 5. Wiring effects to evolutions

### 5.1 Reconcile `HIT_BURN` / `SPEC_BURN` with the new Burn

Today the NAPALM column (`SPEC_BURN`) → `HIT_BURN` → `puddles_spawn`. Two clean choices:

- **(Recommended) Keep the puddle AND apply on-enemy Burn.** `bullet_onhit()` already
  spawns a puddle for `HIT_BURN`; *also* set `w->bulletEffect = EFFECT_BURN` for NAPALM
  shots so the **directly struck** enemy starts burning immediately, and the puddle ignites
  enemies that walk through afterward. To make the puddle apply Burn instead of raw damage,
  change `puddles_update` to call `enemies_apply_effect(w, e, EFFECT_BURN, 1.0f, q->dps)`
  for enemies inside it (short refreshing duration so leaving the puddle stops the burn
  shortly after).

This gives NAPALM a coherent identity: a burning *zone* plus a burning *enemy*, both routed
through one `EffectDef`.

### 5.2 Carry an `EffectId` on an evolution (data)

Add an optional on-hit effect to `EvoMods` (`data/evolutions.h`) so any evolution cell can
inflict a status without a new `EvoSpecial`:
```c
typedef struct {
    float damageMul, rateMul, sizeMul;
    int   multiAdd, pierceAdd;
    EvoSpecial special;
    int   onHitEffect;   // EffectId or -1   (NEW)
    float onHitEffectMag;// magnitude         (NEW)
} EvoMods;
```
Most presets get `-1, 0`. NAPALM gets `EFFECT_BURN`. A new "frost" lean could be a mod-only
change with `EFFECT_SLOW` and no new `EvoSpecial`:
```c
#define M_NAPALM { 1.7f,0.95f,1.2f, 0,0, SPEC_BURN, EFFECT_BURN, 14.0f }
// example reflavoured cell, no new special needed:
#define M_FROST  { 1.4f,0.95f,1.1f, 0,1, SPEC_NONE, EFFECT_SLOW, 0.45f }
```
`weapon_stats()` already folds `EvoMods` into `WeaponStats`; add the two fields:
```c
// WeaponStats (weapons.h):  int onHitEffect; float onHitEffectMag;
// weapon_stats():           s.onHitEffect = m->onHitEffect; s.onHitEffectMag = m->onHitEffectMag;
```
And `fire_weapon()` stamps them right next to the existing `w->bulletOnHit` line:
```c
w->bulletEffect    = st.onHitEffect;
w->bulletEffectMag = st.onHitEffectMag;
```
This means **any** of the 13×12 evolution cells can carry a status by editing one table —
no code path per cell, matching the existing "evolution is a data row" philosophy.

### 5.3 When you DO want a dedicated `EvoSpecial`

For effects that aren't "apply a status on hit" (the on-kill / aura / economy effects in
§6), add a new `EvoSpecial` value and dispatch it where that trigger lives — e.g. a
`SPEC_PROSPECTOR` that `enemies_kill` reads to magnetise gems (§6.1). The §6 table marks
which effects are `EvoMods.onHitEffect` (data only) vs. a new `EvoSpecial` (code path).

---

## 6. The evolution-effects brainstorm

Each effect lists **trigger**, **what it does**, **suited weapons/trifectas**, and an
**implementation sketch** in terms of real functions/pools. "Mechanism" notes whether it is
pure data (`EvoMods.onHitEffect`) or needs a new `EvoSpecial` + code.

### 6.1 Prospector — "attract all gems from enemies killed by this gun" ⭐ (user request)
- **Trigger:** on-kill. **Suits:** AUTOAIM, AIMED, MAINGUN; trifectas SWARM/TEMPEST (high
  kill volume). **Mechanism:** new `EvoSpecial SPEC_PROSPECTOR`.
- **The ownership problem:** gems are a **global pool** (`GemPool`) with **no owner** —
  `gems_drop` doesn't know which weapon caused the kill, and `enemies_kill` doesn't know the
  killing weapon either. Three options:
  - **(a) Tag gems dropped by this weapon's kills as auto-magnetized.** Requires plumbing
    "current killing weapon has Prospector" into `enemies_kill` → `gems_drop`, and setting
    `Gem.magnetized = true` at drop. Clean but needs a `w->killCredit` flag (who is firing).
  - **(b) On-kill burst-pull nearby gems.** On a Prospector kill, scan the `GemPool` near the
    death point and set `g->magnetized = true` for gems within radius R. No per-gem owner
    needed; the magnet logic in `gems_update` already homes magnetized gems to the player.
  - **(c) Global pickup-radius buff while this weapon is owned.** Weakest/least distinctive.
- **RECOMMENDATION: (b), with a touch of (a).** Implement a `w->killBurstMagnet` flag set by
  `weapons_update`/`fire_weapon` for the frame a Prospector weapon's bullets are live is
  brittle; simplest robust version: stamp the credit on the **bullet** (it already carries
  per-weapon info implicitly via `weaponKind`). Add `bool magnetKill` to `Bullet` (stamped
  like `effect`); when such a bullet's hit kills (`e->hp<=0` in `bullets_update`), call a new
  `gems_magnetize_near(w, e->pos, 220)` **after** `enemies_kill` (so the just-dropped gem is
  included). For aura/lightning weapons set a transient `w->magnetThisKill` flag around their
  damage calls. New tiny API in `gems.{h,c}`:
  ```c
  void gems_magnetize_near(GameWorld *w, Vector2 c, float radius) {
      for (int i = 0; i < MAX_GEMS; i++) { Gem *g = &w->gems.items[i];
          if (g->active && Vector2DistanceSqr(g->pos, c) <= radius*radius) g->magnetized = true; }
  }
  ```
  Option (b) needs **zero** new ownership bookkeeping in the common case and reuses the
  existing `magnetized` field + `gems_update` homing. Name the evolution e.g. **"Prospector"
  / "Gem Magnet"** in the AUTOAIM/AIMED rows.

### 6.2 Chain-Detonate — kill explodes into neighbours
- **Trigger:** on-kill. **Suits:** INFERNO, RAILGUN (heavy hits); MAINGUN/ION.
  **Mechanism:** `SPEC_*` flag read in `enemies_kill`.
- **Sketch:** in `enemies_kill`, if the credited weapon has the flag, call
  `enemies_damage_near(w, e->pos, 50, e->maxHp * 0.5f)` and a particle burst. Reuses the
  exact AoE primitive `bullet_onhit`'s `HIT_EXPLODE` uses; cap recursion by only chaining off
  bullet/weapon kills, never off chain damage (pass a "noChain" via a static guard or by
  using the non-effect `enemies_damage_near`).

### 6.3 Overkill Splash — surplus damage spills to neighbours
- **Trigger:** on-hit (lethal). **Suits:** RAILGUN, VOID, INFERNO. **Mechanism:**
  `SPEC_OVERKILL` or compute inline in `bullets_update`.
- **Sketch:** in the collision loop, before clamping, compute `over = b->damage - e->hp_pre`;
  if `over > 0` after the kill, `enemies_damage_near(w, e->pos, 40, over)`. Rewards big slugs
  against trash; naturally scales with damage stacking.

### 6.4 Cryo Rounds — slow on hit (data-only)
- **Trigger:** on-hit. **Suits:** any STREAM/RAILGUN (single strong shots); a "frost" lean.
  **Mechanism:** `EvoMods.onHitEffect = EFFECT_SLOW`, mag 0.4–0.6. No code path.
- **Sketch:** purely §5.2. The stamped `b->effect = EFFECT_SLOW` does it.

### 6.5 Freeze-on-Crit — crits freeze
- **Trigger:** on-hit + crit. **Suits:** AIMED/AUTOAIM (precise); LANCE/RAILGUN.
  **Mechanism:** needs crit support (see 6.13) + apply `EFFECT_FREEZE`.
- **Sketch:** where a hit rolls a crit, also `enemies_apply_effect(w, e, EFFECT_FREEZE, 1.0f, 0)`.
  Visual: cyan ring from §4.

### 6.6 Vampiric Burn — burn that heals the player
- **Trigger:** passive (rides Burn). **Suits:** NAPALM, FLAME, GOO. **Mechanism:**
  `SPEC_VAMPBURN` checked in `enemy_tick_effects` DoT branch.
- **Sketch:** when the credited build has Vampiric Burn and Burn DoT ticks, heal
  `w->player.shield` (or hp) by a small fraction of the DoT (`p->shield = min(shieldMax, +k*dmg)`).
  Tracking "credited build" can be coarse — a `w->vampBurn` bool set when any owned weapon has
  the evolution, since Burn is the carrier.

### 6.7 Shield-on-Kill — kills grant overshield
- **Trigger:** on-kill. **Suits:** ORBITAL, AURA (constant kills); BLITZ.
  **Mechanism:** `SPEC_*` in `enemies_kill`.
- **Sketch:** `p->shield = fminf(p->shieldMax, p->shield + 1.5f)` per kill (small, capped).
  Uses the existing `shield/shieldMax` fields on `Player`.

### 6.8 Bloom of Greed — bonus XP/score on kill
- **Trigger:** on-kill. **Suits:** any "farming" build; SWARM/TEMPEST. **Mechanism:** drop
  extra gem value in `enemies_kill`.
- **Sketch:** `gems_drop(w, e->pos, ETYPES[e->type].xp * 0.5f, e->color);` an extra time, or
  bump `w->score`. Reuses `gems_drop` + the pool-full merge it already handles.

### 6.9 Frag Seekers — spawn homing sub-munitions on kill
- **Trigger:** on-kill. **Suits:** HOMING, MISSILE; SCATTER. **Mechanism:** `SPEC_FRAGKILL`
  in `enemies_kill`.
- **Sketch:** on kill, fire 2–3 `bullets_add(... homing=true)` from `e->pos` with small dmg.
  Reuses the homing flag handled in `bullets_update`. Stamp `weaponKind = WK_HOMING`.

### 6.10 Cryostatic Field — slow-aura while firing
- **Trigger:** on-fire / aura tick. **Suits:** AURA, ORBITAL, FLAME, GOO.
  **Mechanism:** route the weapon's tick through `enemies_damage_near_fx` (§2.3) with
  `EFFECT_SLOW`.
- **Sketch:** in the `WP_AURA` / `WP_ORBITAL` fire path, replace `enemies_damage_near` with
  the `_fx` variant carrying `EFFECT_SLOW, 0.6f, 0.35f`. Every enemy the aura touches gets a
  refreshing slow → an actual control zone. Cheap (no extra loop).

### 6.11 Mark & Detonate — tag, then blow up on the next hit
- **Trigger:** on-hit (apply Mark) + on-hit (consume). **Suits:** AIMED, LIGHTNING, ION.
  **Mechanism:** `EvoMods.onHitEffect = EFFECT_MARK` + a `SPEC_DETONATE` consume.
- **Sketch:** first hit applies `EFFECT_MARK`. In `bullets_update`, when a `SPEC_DETONATE`
  bullet strikes an enemy whose `effTimer[EFFECT_MARK] > 0`, clear the mark and
  `enemies_damage_near(w, e->pos, 60, b->damage * 2.0f)`. Lets a "tagger + popper" pair-build
  emerge. Mark reticle visual from §4.

### 6.12 Ricochet — bullets bounce to a new target after a hit
- **Trigger:** on-hit. **Suits:** STREAM, SCATTER, TEMPEST; AUTOAIM. **Mechanism:**
  `SPEC_RICOCHET` + a small `Bullet.bounceLeft`.
- **Sketch:** add `unsigned char bounceLeft` to `Bullet`. On a non-killing or post-kill hit
  with `bounceLeft>0`, redirect `b->vel` toward `enemies_nearest(w, b->pos, 300, currentIdx)`
  (keep magnitude), decrement `bounceLeft`, and **don't** deactivate. Reuses
  `enemies_nearest`.

### 6.13 Marksman — every Nth shot is a guaranteed crit
- **Trigger:** on-fire. **Suits:** AIMED, RAILGUN, MAINGUN. **Mechanism:** `SPEC_*` + a
  `Weapon.shotCount` counter + a `Bullet.crit` flag (crit already exists on `DamageNum`).
- **Sketch:** add `int shotCount` to `Weapon`; in `fire_weapon`, `if (++wp->shotCount % N == 0)`
  set `w->bulletCrit = true` (new stamp) → `bullets_add` copies to `Bullet.crit`; in the
  collision loop crit bullets deal `* critMul` and call `fx_spawn_damagenum(..., crit=true)`
  (the `crit` bool already drives bigger/gold numbers). This also unlocks 6.5.

### 6.14 Formation Bounty — clearing a full formation drops a chest/pickup
- **Trigger:** on-kill (last of a group). **Suits:** any; thematic with the Director.
  **Mechanism:** track formation membership. **Sketch:** `Director`/spawn already groups a
  formation; tag spawned enemies with a `formationId` (reuse `anchor` or add a field), and in
  `enemies_kill` decrement a per-formation live count. At zero, `gems_drop` a fat XP gem (or,
  once the WISHLIST chest exists, drop a chest) at `e->pos`. Pure economy reward; ties into
  WISHLIST §1.5 formation events.

### 6.15 Static Discharge — chain lightning on hit
- **Trigger:** on-hit. **Suits:** LIGHTNING, ION; TEMPEST. **Mechanism:** `SPEC_*` reusing
  the `WP_LIGHTNING` chaining code. **Sketch:** on hit, run the existing nearest-target
  hop loop (`enemies_nearest` + small dmg) for `1 + multi` hops from `e->pos`. Optionally
  apply `EFFECT_SHOCK` to each hop (stun) for a control build.

### 6.16 Toxic Bloom — poison on hit that spreads on death
- **Trigger:** on-hit (apply Poison) + on-kill (spread). **Suits:** GOO, FLAME; SWARM.
  **Mechanism:** `EvoMods.onHitEffect = EFFECT_POISON` + a `SPEC_SPREADPOISON`.
- **Sketch:** hits stack Poison (STACK_ADD ramps). On a poisoned enemy's death,
  `enemies_damage_near` + apply Poison to neighbours via `enemies_damage_near_fx` — a
  plague build. Poison stacks naturally with the §3 DoT path.

### 6.17 Executioner — bonus damage to low-HP / marked enemies
- **Trigger:** on-hit (passive modifier). **Suits:** RAILGUN, VOID, AIMED. **Mechanism:**
  inline in the hit math. **Sketch:** in the collision loop, if `e->hp < e->maxHp * 0.25f`,
  `dmg *= 1.5f` before subtracting. Pairs with Weaken/Mark. Pure arithmetic, no new pool.

### 6.18 Gravity Well — VOID kills pull nearby enemies inward
- **Trigger:** on-kill. **Suits:** VOID (knockback theme), ION. **Mechanism:** `SPEC_*` in
  `enemies_kill`. **Sketch:** scan grid near `e->pos`, nudge each `en->pos` toward the death
  point (`Vector2Lerp` a few px) and apply brief `EFFECT_SLOW`. Sets up the next AoE; reuses
  the grid scan pattern from `enemies_damage_near`.

### 6.19 Cinder Rebirth — burning enemies that die re-ignite the area
- **Trigger:** on-kill (if burning). **Suits:** NAPALM, FLAME. **Mechanism:** `enemies_kill`
  reads `e->effTimer[EFFECT_BURN]`. **Sketch:** if the dying enemy is burning,
  `puddles_spawn(w, e->pos, 28, 2.0f, dps)` — death feeds the fire. Reuses the existing
  puddle pool; closes the Burn loop.

### 6.20 Magnetic Overflow — periodically vacuum all gems while this weapon fires
- **Trigger:** on-fire (cadence). **Suits:** AURA, ORBITAL (continuous). **Mechanism:**
  `SPEC_*` + a timer. **Sketch:** every few seconds of firing, call `gems_magnetize_near`
  (from 6.1) with a screen-sized radius — a softer, periodic Prospector for control builds.

> **Triggers covered:** on-hit (6.4, 6.11, 6.12, 6.17), on-kill (6.1, 6.2, 6.3, 6.7, 6.8,
> 6.9, 6.14, 6.18, 6.19), on-fire/aura (6.10, 6.13, 6.15, 6.20), passive (6.6). That's 20
> distinct effects spanning combat, control, economy, and QoL.

---

## 7. Phased implementation plan (each step builds & runs)

**Step 1 — core status layer (Slow + Burn).**
- `core/common.h`: add `EffectId` enum + `EFFECT_COUNT`; add the status fields to `Enemy`
  (`effTimer/effMag/effStacks/dotTick`).
- `data/effects.{h,c}`: `EffectDef EFFECTS[EFFECT_COUNT]`.
- `enemies.{h,c}`: `enemies_apply_effect`, `enemy_tick_effects`, wire `speedMul`/`fireMul`/
  `frozen` into `enemies_update` + `enemy_fire`; `enemy_incoming` weaken inline.
- `render.c`: tint blend + Burn embers + Freeze/Slow ring in `draw_enemy_shape`.
- **Verify:** temporarily call `enemies_apply_effect(w, e, EFFECT_SLOW, ...)` on spawn or a
  debug key; confirm slowed movement, fire-rate drop, burn DoT + tint. *Builds and plays.*

**Step 2 — wire to evolutions (on-hit data path).**
- `world.h`: add `bulletEffect` / `bulletEffectMag` stamps. `common.h` `Bullet`: add
  `effect` / `effectMag`. `bullets.c`: copy in `bullets_add`, apply in the collision loop.
- `evolutions.h`: add `onHitEffect` / `onHitEffectMag` to `EvoMods`; set NAPALM →
  `EFFECT_BURN`, add a frost-lean cell → `EFFECT_SLOW`. `weapons.{h,c}`: fold into
  `WeaponStats`, stamp in `fire_weapon`. Re-point `HIT_BURN` puddles to apply `EFFECT_BURN`
  (§5.1). **Verify:** force a NAPALM/frost evolution; struck enemies burn/slow. *Builds.*

**Step 3 — the Prospector gem-magnet effect (the requested one).**
- `gems.{h,c}`: `gems_magnetize_near`. `common.h` `Bullet`: `magnetKill` stamp (+ world
  stamp). New `SPEC_PROSPECTOR`; in `bullets_update` call `gems_magnetize_near` after a
  Prospector bullet's kill. Name it in the AUTOAIM/AIMED evolution rows. **Verify:** kills
  by that gun auto-pull their gems to the player. *Builds.*

**Step 4 — the rest of §6.**
- Add the `EvoSpecial` values + `enemies_damage_near_fx`, the crit stamp (6.13), bounce/mark
  fields, and the on-kill hooks in `enemies_kill`. Roll out effects a few at a time, each
  behind its own `EvoSpecial`/`EvoMods` data so the build stays green between additions.

**Tests (`tests/test_runes.c`, run via `make test`).** It already links the game objects
minus `game.o` and tests pure logic. Add:
- `enemies_apply_effect` + `enemy_tick_effects` math (build a stack `Enemy`, apply Slow,
  assert `speedMul`/`fireMul`; apply Burn, tick `dt=0.1` repeatedly, assert hp drops by
  `dps*time`; assert timer expiry clears the effect and resets stacks).
- Stack rules: Poison `STACK_ADD` caps at `maxStacks` and ramps DoT; Slow `STACK_MAX_DURATION`
  keeps the stronger.
- `weapon_stats` carries `onHitEffect`/`onHitEffectMag` from a NAPALM evolution.
- `gems_magnetize_near` flips `magnetized` only for in-radius gems.

Keep render/audio out of the tested helpers (or guard fx calls) so the test TU links without
raylib draw state, matching how the current tests avoid `game.o`.

---

## 8. Open questions (for you to decide / tune)

1. **Stack caps & magnitudes:** Slow cap (one source vs. additive)? Poison `maxStacks` (5?)
   and per-stack dps? Freeze/Shock durations (1.2s / 0.6s) — and should bosses be immune or
   resistant (a `bool effectImmune` / resistance multiplier on `EnemyType` / `Boss`)?
2. **Durations:** defaults in `EffectDef` (Burn 3s, Slow 2.5s) — final values?
3. **DoT tick rate:** 10 Hz proposed. Faster = smoother numbers but more `DamageNum` churn.
4. **Weaken stacking with crit/Executioner:** do these multiply together or is there a cap?
5. **HUD display:** show active-effect icons on/under afflicted enemies, or only the tint +
   particles? A boss effect bar?
6. **Kill credit for on-kill effects:** is the "stamp on the bullet / transient world flag"
   approach (§6.1) acceptable, or do we want a proper `w->killCredit` weapon-index threaded
   through `enemies_kill` (cleaner, slightly more plumbing)?
7. **Prospector radius** and whether it also drops a bigger gem (combine with §6.8 Bloom).
8. **Balance knobs surfaced to data:** put effect magnitudes in `data/balance.h` (like the
   enemy-scaling constants) so tuning doesn't touch `effects.c`?
9. **Player-facing effects:** do enemy bullets / bosses ever apply effects to the *player*
   (e.g. a slow field), or is the system enemy-only for now?

---

### Summary

This spec adds a generic, data-driven **enemy status-effect layer** (Slow + Burn now;
Poison/Freeze/Shock/Weaken/Mark as data) stored as flat per-`EffectId` timer/magnitude
arrays on `Enemy` (chosen over a per-enemy effect list for the 2048-pool hot path: ~8 timer
compares, almost all skipped, no slot search, no allocation). Effects are applied through a
single `enemies_apply_effect` choke point, fed by extending the existing
`w->bulletOnHit`/`bulletStyle` stamping with a parallel `bulletEffect`, and ticked once
inside the existing `enemies_update` loop (Slow scales `sp`/`fireTimer`, Burn DoT throttled
to 10 Hz, Weaken multiplies incoming damage). Visuals reuse `draw_enemy_shape` (tint blend +
rings) and `fx_spawn_particles`. Evolutions carry effects either as pure data
(`EvoMods.onHitEffect`, e.g. NAPALM→Burn) or via new `EvoSpecial` code paths. The fun layer
is **20 named evolution effects** across on-hit/on-kill/on-fire/aura/economy — led by the
requested **Prospector** gem-magnet, implemented via a new `gems_magnetize_near` burst-pull
on kill (recommended over per-gem ownership) reusing the existing `Gem.magnetized` homing.
A four-step always-building rollout (core → evo-wiring → Prospector → the rest) plus
`make test` additions closes it out.

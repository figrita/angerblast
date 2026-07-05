// weapons_def.h - the weapon registry. Every weapon behaviour is a WP_* id; the table
// holds display name, base fire interval, and a base-damage figure. Firing behaviour is
// dispatched on the id in systems/weapons.c. A player holds up to MAX_WEAPONS at once,
// each with SOCKETS rune sockets.
#ifndef ANGERBLAST_WEAPONS_DEF_H
#define ANGERBLAST_WEAPONS_DEF_H

#include "raylib.h"

#define MAX_WEAPONS 6
#define SOCKETS     3

typedef enum {
    WP_MAINGUN,                                  // innate straight-up cannon (always owned)
    WP_SIDEGUN, WP_TAILGUN, WP_HOMING, WP_LIGHTNING, WP_AURA, WP_ORBITAL,
    WP_AIMED, WP_AUTOAIM, WP_MISSILE, WP_FLAME, WP_GOO, WP_ION,
    WP_COUNT
} WeaponId;

// Emitter ops — the closed vocabulary every weapon's firing decomposes into. The per-weapon
// numbers live in EmitParams (data); the C primitive in weapons.c reads them. Dispatch is on
// `def->emit`, not the WeaponId, so new weapons = new data rows, not new C.
typedef enum {
    EMIT_STRAIGHT,      // a fan of `count(1+multi)` shots along `baseDir`, spread `spread`
    EMIT_FIXED_DIRS,    // one shot (×multi width-jitter) along each angle in dirs[]
    EMIT_AIMED,         // fan along the input/auto aim direction
    EMIT_AUTOAIM,       // fan toward the nearest target (incl. boss)
    EMIT_HOMING_VOLLEY, // `count(1+multi+evoBonus)` homing shots, scattered
    EMIT_CHAIN,         // lightning: hop nearest targets, fx bolts
    EMIT_AURA,          // pulse AoE damage in a growing radius
    EMIT_ORBITAL,       // continuous rotating contact balls (no timer)
    EMIT_CONE,          // flamethrower: scattered short-range AoE points
    EMIT_LOB,           // goo: slow wads that coat (EFFECT_GOO) on hit
    EMIT_COUNT
} EmitOp;

// EmitParams — per-weapon firing numbers (loaded from `emit_params` in weapons.toml).
// Fields are interpreted per-op; unused ones stay 0. See weapons.c for which op reads what.
typedef struct {
    float speedScale;   // multiplies player bullet speed (straight/aimed/autoaim/lob)
    float radius;       // projectile radius
    float spread;       // fan arc (radians) across the owned shots
    int   count;        // base projectile count (before +multi), 0 => "1 + multi"
    int   pierce;       // forced pierce (e.g. ION 999); <0 => use stat pierce
    int   evoBonus;     // extra projectiles when evolved (HOMING +2)
    bool  homing;       // shots home
    float dirs[4];      // fixed directions (EMIT_FIXED_DIRS); dirCount entries
    int   dirCount;
    // AoE-style ops (aura/orbital/cone/chain) — radii/reach/range/targets scaling
    float baseRadius;   // aura base / orbital orbit base / cone base reach
    float perLevel;     // + per weapon level
    float perMulti;     // + per extra projectile (aura)
    float evoRadius;    // + when evolved
    int   targets;      // chain hop count base (chain), or balls base (orbital)
    float range;        // chain/autoaim search range
} EmitParams;

typedef struct {
    const char *name;
    const char *sprite;    // codex/UI icon path relative to assets/ (NULL = swatch only)
    float baseInterval;   // seconds between fires at level 1, before rate mods
    float baseDamage;     // damage at level 1, before damage mods
    Color color;
    int       emit;       // EmitOp — how this weapon fires
    EmitParams ep;        // the firing numbers
} WeaponDef;

extern WeaponDef WEAPONS_DEF[WP_COUNT];

// emit op name -> EmitOp (loader); -1 if unknown.
int weapon_emit_id(const char *name);

#endif // ANGERBLAST_WEAPONS_DEF_H

// weapons.h - the socketed auto-weapon loadout. Each held weapon fires on its own timer,
// reading effective stats from its level + socketed runes + evolution.
#ifndef ANGERBLAST_WEAPONS_H
#define ANGERBLAST_WEAPONS_H

#include "world.h"

#include "data/evolutions.h"   // EvoSpecial

// Effective per-weapon stats, folded from level + runes + evolution.
typedef struct {
    float damageMul, rateMul, velMul, sizeMul;
    int   pierce, multi;
    bool  evolved;
    int   special;        // EvoSpecial behaviour when evolved, else SPEC_NONE
    int   onHitEffect;    // EffectId applied on hit, or -1
    float onHitEffectMag;
    int   onKill;         // OnKill behaviour
    int   auraEffect;     // EffectId applied by aura/orbital ticks, or -1
} WeaponStats;

WeaponStats weapon_stats(const Weapon *wp);
void        weapons_update(GameWorld *w, float dt);
// fire one weapon from an arbitrary origin (drones use this from their own position).
void        weapons_fire_one(GameWorld *w, Weapon *wp, Vector2 origin, float dt);

#endif // ANGERBLAST_WEAPONS_H

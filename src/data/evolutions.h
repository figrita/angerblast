// evolutions.h - the WP_COUNT x TRI_COUNT evolution matrix. Every weapon has a UNIQUE
// named evolution for every trifecta. A cell carries a name, a one-line description of
// what's special, and the stat/behaviour mods applied to the weapon while evolved.
#ifndef ANGERBLAST_EVOLUTIONS_H
#define ANGERBLAST_EVOLUTIONS_H

#include "core/common.h"      // EffectId (EFFECT_*)
#include "data/weapons_def.h"
#include "data/trifectas.h"

// Special evolved behaviours layered on top of the base weapon's firing. The trifecta
// theme picks the family; the weapon flavours the delivery.
typedef enum {
    SPEC_NONE,
    SPEC_EXPLODE,     // INFERNO: shots detonate in an AoE on hit
    SPEC_BEAM,        // STREAM:  long fast piercing shots / continuous reach
    SPEC_SWARM,       // SWARM:   many extra projectiles
    SPEC_RAPID,       // BLITZ:   very high fire rate
    SPEC_PIERCEALL,   // LANCE:   shots pierce everything, longer
    SPEC_BURN,        // NAPALM:  leaves burning ground / DoT
    SPEC_SHOTGUN,     // SCATTER: wide burst of strong pellets
    SPEC_RAIL,        // RAILGUN: single huge piercing slug
    SPEC_STORM,       // TEMPEST: dense fast spray, slight spread
    SPEC_VOIDPULL,    // VOID:    heavy piercing + knockback
    SPEC_PRISM,       // PRISM:   balanced multi-shot rainbow
    SPEC_CHAOS,       // CHAOS:   erratic homing scatter
} EvoSpecial;

// on-kill behaviours (read in enemies_kill via the killing bullet/weapon). 0 = none.
typedef enum {
    ONKILL_NONE,
    ONKILL_MAGNET,     // Prospector: pull nearby gems to the player
    ONKILL_CHAIN,      // Chain-Detonate: AoE burst into neighbours
    ONKILL_SHIELD,     // Shield-on-Kill: gain a little overshield
    ONKILL_BOUNTY,     // Bloom of Greed: drop extra XP
    ONKILL_FRAG,       // Frag Seekers: spawn homing sub-munitions
    ONKILL_REIGNITE,   // Cinder Rebirth: burning deaths leave fire
} OnKill;

typedef struct {
    float damageMul, rateMul, sizeMul;
    int   multiAdd, pierceAdd;
    EvoSpecial special;
    int   onHitEffect;     // EffectId applied on hit, or -1
    float onHitEffectMag;  // magnitude
    int   onKill;          // OnKill behaviour
    int   auraEffect;      // EffectId applied by aura/orbital ticks, or -1 (control fields)
} EvoMods;

typedef struct {
    const char *name;     // e.g. "Inferno Lance"
    const char *desc;     // e.g. "shots detonate on impact"
    EvoMods mods;
} Evolution;

// EVOLUTIONS[weaponId][trifectaId]
extern Evolution EVOLUTIONS[WP_COUNT][TRI_COUNT];

#endif // ANGERBLAST_EVOLUTIONS_H

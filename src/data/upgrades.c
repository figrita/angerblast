// upgrades.c - passive (global) upgrade catalogue. These buff the main gun / survivability;
// per-weapon power now comes from runes, not from these.
#include "data/upgrades.h"

#define PASV(nm, ds, spr) { .name=(nm), .desc=(ds), .sprite=(spr) }

const Passive PASSIVES[PASV_COUNT] = {
    [PASV_RATE]        = PASV("Rapid Fire",       "Main gun fires 15% faster",                     "sprites/common/pasv_rapidfire.png"),
    [PASV_DAMAGE]      = PASV("Heavy Rounds",     "Main gun damage +25%",                          "sprites/common/pasv_heavyrounds.png"),
    [PASV_SPREAD]      = PASV("Spread Shot",      "Main gun +1 projectile",                        "sprites/common/pasv_spreadshot.png"),
    [PASV_BULLETSPEED] = PASV("Velocity Coils",   "Bullet speed +20%",                             "sprites/common/pasv_velocitycoils.png"),
    [PASV_MOVESPEED]   = PASV("Thrusters",        "Move speed +12%",                               "sprites/common/pasv_thrusters.png"),
    [PASV_MAXSHIELD]   = PASV("Shield Capacitor", "+25 max shield, refill",                        "sprites/common/pasv_shieldcap.png"),
    [PASV_MAXHP]       = PASV("Reinforced Hull",  "+1 max HP, heal 1",                              "sprites/common/pasv_reinforcedhull.png"),
    [PASV_REGEN]       = PASV("Fast Recharge",    "Shield regens sooner & faster",                  "sprites/common/pasv_fastrecharge.png"),
    [PASV_MAGNET]      = PASV("Magnet Coil",      "Pickup radius +40%",                             "sprites/common/pasv_magnetcoil.png"),
    [PASV_GREED]       = PASV("Greed",            "XP gained +15%",                                 "sprites/common/pasv_greed.png"),
    [PASV_AEGIS]       = PASV("Aegis Rounds",     "35% of shots destroy an enemy bullet on contact", "sprites/common/pasv_aegisrounds.png"),
    [PASV_ARMOR]       = PASV("Armor Plating",    "~17% dmg cut per stack (diminishing returns)",   "sprites/common/pasv_armorplating.png"),
};
#undef PASV

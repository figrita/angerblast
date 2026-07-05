// upgrades.h - the *passive* upgrade catalogue (global player buffs that are NOT weapons
// or runes). Weapons come from weapons_def.h; runes from runes.h. Effects live in
// systems/upgrades_sys.c.
#ifndef ANGERBLAST_UPGRADES_H
#define ANGERBLAST_UPGRADES_H

typedef enum {
    PASV_RATE, PASV_DAMAGE, PASV_SPREAD, PASV_BULLETSPEED,
    PASV_MOVESPEED, PASV_MAXSHIELD, PASV_MAXHP, PASV_REGEN, PASV_MAGNET, PASV_GREED,
    PASV_AEGIS,
    PASV_ARMOR,
    PASV_COUNT
} PassiveId;

typedef struct { const char *name, *desc; const char *sprite; } Passive;

extern const Passive PASSIVES[PASV_COUNT];

#endif // ANGERBLAST_UPGRADES_H

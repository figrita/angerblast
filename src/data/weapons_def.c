// weapons_def.c - the weapon registry is loaded from assets/weapons.toml by the loader.
#include "data/weapons_def.h"
#include <string.h>

WeaponDef WEAPONS_DEF[WP_COUNT];   // filled by the loader

int weapon_emit_id(const char *name) {
    static const char *T[EMIT_COUNT] = {
        "straight","fixed_dirs","aimed","autoaim","homing_volley",
        "chain","aura","orbital","cone","lob"
    };
    for (int i = 0; i < EMIT_COUNT; i++) if (strcmp(name, T[i]) == 0) return i;
    return -1;
}

// runes.h - the five socketable "gems" (called Runes in code to avoid clashing with the
// XP-pickup `Gem` type). Each rune buffs the weapon it's socketed into; three runes in a
// weapon may form a Trifecta that evolves it (see trifectas.h).
#ifndef ANGERBLAST_RUNES_H
#define ANGERBLAST_RUNES_H

#include "raylib.h"

typedef enum {
    RUNE_NONE = -1,
    RUNE_RED,      // Damage
    RUNE_BLUE,     // Velocity (bullet speed / reach)
    RUNE_GREEN,    // Multi (+1 projectile / +1 target)
    RUNE_YELLOW,   // Rate (fire faster)
    RUNE_PURPLE,   // Pierce
    RUNE_COUNT
} RuneId;

typedef struct {
    const char *name;
    const char *sprite;   // codex/UI icon path relative to assets/ (NULL = swatch only)
    Color color;
    const char *stat;     // short label for the buff
} RuneDef;

extern RuneDef RUNES[RUNE_COUNT];

#endif // ANGERBLAST_RUNES_H

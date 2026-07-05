// effects.h - the status-effect catalogue. EffectId itself lives in core/common.h (so the
// Enemy struct can size its arrays); this is the richer per-effect data table.
#ifndef ANGERBLAST_EFFECTS_H
#define ANGERBLAST_EFFECTS_H

#include "core/common.h"

typedef enum { STACK_REFRESH, STACK_MAX_DURATION, STACK_ADD } StackRule;

typedef struct {
    const char *name;
    const char *sprite;      // codex/UI icon path relative to assets/ (NULL = swatch only)
    Color       tint;        // blended into the enemy while afflicted
    float       defDuration; // used when an applier passes duration <= 0
    StackRule   stackRule;
    unsigned char maxStacks; // cap for STACK_ADD (1 for non-stacking)
    bool        isDot;       // ticks damage via dotTick
    bool        haltsMove;   // Freeze / Shock
    bool        haltsFire;   // Freeze
} EffectDef;

extern EffectDef EFFECTS[EFFECT_COUNT];

#endif // ANGERBLAST_EFFECTS_H

// emit.h - runs the composable emitter graph (data/emitter.h) at runtime.
//
// emitter_fire() spawns one emitter's burst of bullets from `origin`. Each spawned bullet is
// stamped with the emitter's CHILD emitter (if any), so when its timer fires or it dies, the
// bullet runs that child — recursively, up to EMITTER_MAX_DEPTH. bullets_update() drives the
// per-bullet child ticking via emitter_bullet_tick().
#ifndef ANGERBLAST_EMIT_H
#define ANGERBLAST_EMIT_H

#include "world.h"

// Fire emitter `id` from `origin`. `baseAng` orients a non-aimed fan; `baseSpeed` is the
// reference speed the emitter's speedScale multiplies. `friendly`/`weaponKind` tag the
// bullets. `*patternAng` carries the rotating phase across calls. `depth` is the recursion
// level (callers pass 0; children pass parent depth + 1). No-op past EMITTER_MAX_DEPTH.
void emitter_fire(GameWorld *w, int id, Vector2 origin, float baseAng, float baseSpeed,
                  bool friendly, int weaponKind, int depth, float *patternAng);

// Advance one bullet's child emitter (called per active bullet each frame). Fires CHILD_ON_TIMER
// bursts as the timer elapses. Returns nothing; bullets_update handles CHILD_ON_DEATH on expiry.
void emitter_bullet_tick(GameWorld *w, Bullet *b, float dt);

// Fire a bullet's CHILD_ON_DEATH emitter at its current position (called when it expires).
void emitter_bullet_death(GameWorld *w, Bullet *b);

#endif // ANGERBLAST_EMIT_H

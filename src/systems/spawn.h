// spawn.h - the wave director: spawns Galaga/Raiden-style formations from the top,
// keeps the field populated, and gates the boss (waves pause until it's killed).
#ifndef ANGERBLAST_SPAWN_H
#define ANGERBLAST_SPAWN_H

#include "world.h"

void director_reset(GameWorld *w);
void biome_enter(GameWorld *w, int idx);        // set roster + director from biome[idx]
void director_update(GameWorld *w, float dt);   // may transition to STATE_BOSS_INTRO

#endif // ANGERBLAST_SPAWN_H

// puddles.h - flammable-goo puddles: persistent ground zones that burn enemies inside.
#ifndef ANGERBLAST_PUDDLES_H
#define ANGERBLAST_PUDDLES_H

#include "world.h"

void puddles_spawn(GameWorld *w, Vector2 pos, float radius, float life, float dps);
void puddles_update(GameWorld *w, float dt);   // ticks DoT against nearby enemies

#endif // ANGERBLAST_PUDDLES_H

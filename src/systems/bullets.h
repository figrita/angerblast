// bullets.h - unified bullet pool (friendly + enemy). Collision is grid-accelerated.
#ifndef ANGERBLAST_BULLETS_H
#define ANGERBLAST_BULLETS_H

#include "world.h"

int  bullets_add(GameWorld *w, Vector2 pos, Vector2 vel, float radius, float dmg,
                 Color color, bool friendly, int weaponKind, int pierce, bool homing);
void bullets_update(GameWorld *w, float dt);

#endif // ANGERBLAST_BULLETS_H

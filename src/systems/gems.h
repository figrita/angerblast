// gems.h - XP gem pool. Gems fall slowly, magnet to the player within pickup radius,
// despawn off the bottom, and grant XP on contact.
#ifndef ANGERBLAST_GEMS_H
#define ANGERBLAST_GEMS_H

#include "world.h"

void gems_drop(GameWorld *w, Vector2 pos, double value, Color c);
void gems_update(GameWorld *w, float dt);
void gems_magnetize_near(GameWorld *w, Vector2 c, float radius);  // Prospector: pull nearby gems

#endif // ANGERBLAST_GEMS_H

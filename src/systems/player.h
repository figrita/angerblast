// player.h - the player ship: reset, movement + shield regen, and the shield/HP damage
// model. The main gun is now a socketed weapon in the loadout (fired by weapons_update).
#ifndef ANGERBLAST_PLAYER_H
#define ANGERBLAST_PLAYER_H

#include "world.h"

void player_reset(GameWorld *w);                  // baseline stats (uses balance.h)
void player_update_movement(GameWorld *w, float dt);  // move, clamp, regen
// shield-then-HP, sets death state. `source` tags the cause-of-death for analytics
// (e.g. an enemy name, "boss", "bullet").
void player_damage(GameWorld *w, float amount, const char *source);

#endif // ANGERBLAST_PLAYER_H

// pickups.h - power-up pickups (magnet / board-wipe / instant-levelup / frenzy). Rarer than
// gems; they fall, magnetise to the player, and fire their effect on contact.
#ifndef ANGERBLAST_PICKUPS_H
#define ANGERBLAST_PICKUPS_H

#include "world.h"

void pickups_drop(GameWorld *w, Vector2 pos, int kind);   // spawn a pickup of `kind`
void pickups_maybe_drop(GameWorld *w, Vector2 pos);       // small random chance to drop one
void pickups_update(GameWorld *w, float dt);              // move, collect, fire effects

const char *pickup_name(int kind);   // display label (cards / encyclopedia)
const char *pickup_desc(int kind);   // one-line description
Color       pickup_color(int kind);  // accent colour
const char *pickup_sprite(int kind); // codex/UI icon path relative to assets/ (NULL = swatch only)

#endif // ANGERBLAST_PICKUPS_H

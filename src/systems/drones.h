// drones.h - companion drones. Each drone carries one socketed Weapon (its own rune sockets)
// and a movement AI (follow / auto / sweep). The drone fires its weapon from its own position.
#ifndef ANGERBLAST_DRONES_H
#define ANGERBLAST_DRONES_H

#include "world.h"

// add a drone with movement AI `ai`, carrying weapon `weaponType` (its own empty sockets).
void drone_grant(GameWorld *w, int ai, int weaponType);

// move every drone by its AI and fire its weapon.
void drones_update(GameWorld *w, float dt);

// the default weapon a given drone AI ships with (so the card knows what it grants).
int  drone_default_weapon(int ai);

// display name for a drone AI ("Follow Drone" etc), for cards + encyclopedia.
const char *drone_ai_name(int ai);

// codex/UI icon path relative to assets/ for a drone AI (NULL = swatch only).
const char *drone_ai_sprite(int ai);

#endif // ANGERBLAST_DRONES_H

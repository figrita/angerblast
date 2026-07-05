// boss.h - the stage boss: summon, multi-phase danmaku, death (advances the stage).
#ifndef ANGERBLAST_BOSS_H
#define ANGERBLAST_BOSS_H

#include "world.h"

void boss_spawn(GameWorld *w);
void boss_update(GameWorld *w, float dt);
// Damage a multi-part boss part (bullets.c). Head ignores damage while hands live.
// Returns true if the hit landed.
bool boss_damage_part(GameWorld *w, int partIdx, float dmg);

#endif // ANGERBLAST_BOSS_H

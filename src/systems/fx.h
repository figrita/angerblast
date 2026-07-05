// fx.h - cosmetic pools: particles and floating damage numbers.
#ifndef ANGERBLAST_FX_H
#define ANGERBLAST_FX_H

#include "world.h"

void fx_spawn_particles(GameWorld *w, Vector2 pos, int count, Color base, float speed);
void fx_spawn_damagenum(GameWorld *w, Vector2 pos, int amount, bool crit);
void fx_spawn_bolt(GameWorld *w, Vector2 a, Vector2 b, Color col);  // lightning arc
void fx_spawn_ring(GameWorld *w, Vector2 pos, float r0, float r1, float life, float thick, Color col);
void fx_kill_pop(GameWorld *w, Vector2 pos, float radius, Color col);  // the standard death punch
void fx_hitstop(GameWorld *w, float amount);   // add near-freeze frames (capped)
void fx_update(GameWorld *w, float dt);   // advances particles + damage numbers + bolts + rings

#endif // ANGERBLAST_FX_H

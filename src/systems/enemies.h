// enemies.h - enemy pool: spawning, per-type movement, danmaku fire, and the shared
// damage helpers (kill, area damage, nearest) that weapons/bullets/puddles call into.
#ifndef ANGERBLAST_ENEMIES_H
#define ANGERBLAST_ENEMIES_H

#include "world.h"
#include "data/enemy_types.h"   // FireParams

void enemies_spawn(GameWorld *w, int type, Vector2 pos, Vector2 anchor);
int  enemies_pick_elite(GameWorld *w);          // a stage-valid elite type, or -1
void enemies_spawn_elite_pair(GameWorld *w);    // mirrored elite pair (miniboss moment)

// Shared danmaku primitive: fan `fp->count` (+countPerStage*stage) enemy bullets from
// `origin` over `fp->arc` radians. If `fp->aimed`, the fan is centred on the player; else
// on `baseAng` (use PI/2 = straight down). `*patternAng` carries the rotating phase across
// calls (advanced by `fp->spin`). Used by both enemy fire and the boss.
void emit_radial(GameWorld *w, Vector2 origin, const FireParams *fp,
                 float baseAng, float *patternAng);
int  enemies_pick_type(GameWorld *w);     // a stage-valid type
int  enemies_count_active(GameWorld *w);
void enemies_update(GameWorld *w, float dt);

// kill-source tags (analytics: which damage type got the kill)
enum { KILL_DIRECT, KILL_AOE, KILL_DOT };
// shared damage helpers
void enemies_kill(GameWorld *w, Enemy *e);                                  // death fx + gem + score (direct)
void enemies_kill_src(GameWorld *w, Enemy *e, int src);                     // same, tagging the kill source
void enemies_damage_near(GameWorld *w, Vector2 center, float range, float dmg);
void enemies_damage_near_fx(GameWorld *w, Vector2 center, float range, float dmg,
                            int effect, float dur, float mag);              // AoE + optional status
int  enemies_nearest(GameWorld *w, Vector2 from, float maxDist, int skip);  // index or -1

// Nearest valid TARGET including the boss. Fills outPos with the target position; sets
// *outIsBoss; for enemy targets returns the index (also via *outEnemyIdx), -1 if boss/none.
// `skipIdx` skips an enemy index; `skipBoss` excludes the boss (for chain hops).
bool target_nearest(GameWorld *w, Vector2 from, float maxDist, int skipIdx, bool skipBoss,
                    Vector2 *outPos, bool *outIsBoss, int *outEnemyIdx);
// apply damage to a target (enemy or boss) chosen by target_nearest; handles kill + weaken.
void target_damage(GameWorld *w, bool isBoss, int enemyIdx, float dmg, int effect, float effMag);

// status effects
void  enemies_apply_effect(GameWorld *w, Enemy *e, int id, float duration, float magnitude);
float enemy_incoming(const Enemy *e, float dmg);   // Weaken-modified incoming damage

#endif // ANGERBLAST_ENEMIES_H

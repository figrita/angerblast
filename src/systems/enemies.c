// enemies.c - enemy pool: spawn, movement patterns, danmaku fire, damage helpers.
#include "systems/enemies.h"
#include "data/palette.h"
#include "systems/grid.h"
#include "systems/fx.h"
#include "systems/gems.h"
#include "systems/pickups.h"        // power-up drops on kill
#include "systems/bullets.h"
#include "systems/emit.h"          // composable emitter graph
#include "systems/boss.h"          // boss_damage_part (multi-part targeting)
#include "systems/player.h"        // player_damage
#include "data/enemy_types.h"
#include "data/effects.h"
#include "data/balance.h"
#include "platform/audio.h"
#include <math.h>

// ---- status effects ----
void enemies_apply_effect(GameWorld *w, Enemy *e, int id, float duration, float magnitude) {
    if (!e->active || id < 0 || id >= EFFECT_COUNT) return;
    const EffectDef *d = &EFFECTS[id];
    if (duration <= 0) duration = d->defDuration;
    w->analytics.effectApplied[id]++;
    // Flammable goo: igniting a gooed enemy doubles the burn's dps.
    if (id == EFFECT_BURN && e->effTimer[EFFECT_GOO] > 0) {
        magnitude *= 2.0f;
        w->analytics.gooIgnites++;
        fx_spawn_particles(w, e->pos, 8, PALETTE[PC_PEACH], 160);  // whoosh
    }
    switch (d->stackRule) {
        case STACK_REFRESH:
            e->effTimer[id] = duration; e->effMag[id] = magnitude; break;
        case STACK_MAX_DURATION:
            if (duration > e->effTimer[id]) e->effTimer[id] = duration;
            if (magnitude > e->effMag[id]) e->effMag[id] = magnitude;
            break;
        case STACK_ADD:
            if (e->effStacks[id] < d->maxStacks) e->effStacks[id]++;
            e->effTimer[id] = duration; e->effMag[id] = magnitude; break;
    }
    fx_spawn_particles(w, e->pos, 2, d->tint, 60);
}

// incoming-damage modifier. Weaken (and goo's weaken component) amplify damage taken.
float enemy_incoming(const Enemy *e, float dmg) {
    float mul = 1.0f;
    if (e->effTimer[EFFECT_WEAKEN] > 0) mul += e->effMag[EFFECT_WEAKEN];
    if (e->effTimer[EFFECT_GOO]    > 0) mul += 0.25f;   // gooed enemies take +25% from everything
    return dmg * mul;
}

// advance an enemy's effects; returns movement/fire multipliers and frozen flag via outs.
// applies DoT (~10 Hz). May kill the enemy (returns true if it died).
static bool enemy_tick_effects(GameWorld *w, Enemy *e, float dt,
                               float *outSpeedMul, float *outFireMul, bool *outFrozen) {
    float speedMul = 1.0f, fireMul = 1.0f; bool frozen = false, anyDot = false; float dotDps = 0;
    for (int id = 0; id < EFFECT_COUNT; id++) {
        if (e->effTimer[id] <= 0) continue;
        e->effTimer[id] -= dt;
        if (e->effTimer[id] <= 0) { e->effStacks[id] = 0; e->effMag[id] = 0; continue; }
        const EffectDef *d = &EFFECTS[id];
        if (d->haltsMove) frozen = true;
        if (id == EFFECT_SLOW)   { speedMul *= (1.0f - e->effMag[id]); fireMul *= (1.0f - e->effMag[id]); }
        if (id == EFFECT_GOO)    { speedMul *= 0.65f; }   // sticky goo slows movement ~35%
        if (id == EFFECT_FREEZE) { speedMul = 0; fireMul = 0; }
        if (id == EFFECT_SHOCK)  { speedMul = 0; }
        if (d->isDot) { anyDot = true;
            float st = (d->stackRule == STACK_ADD) ? (float)e->effStacks[id] : 1.0f;
            dotDps += e->effMag[id] * st; }
    }
    if (anyDot) {
        e->dotTick += dt;
        if (e->dotTick >= 0.1f) {
            float dmg = enemy_incoming(e, dotDps * e->dotTick);
            e->dotTick = 0; e->hp -= dmg;
            fx_spawn_damagenum(w, e->pos, (int)(dmg+0.5f), false);
            fx_spawn_particles(w, e->pos, 1, EFFECTS[EFFECT_BURN].tint, 80);
            if (e->hp <= 0) { enemies_kill_src(w, e, KILL_DOT); return true; }
        }
    }
    *outSpeedMul = speedMul; *outFireMul = fireMul; *outFrozen = frozen;
    return false;
}

// ---- shared damage helpers ----
void enemies_kill_src(GameWorld *w, Enemy *e, int src) {
    e->active = false;
    w->lastKillPos = e->pos;        // remember where the wave's last kill landed (gem-burst origin)
    fx_kill_pop(w, e->pos, e->radius, e->color);          // shockwave + sparks + hitstop beat
    float xp = ETYPES[e->type].xp;
    if (e->eliteTier > 0) {                                // elite deaths are an EVENT
        xp *= 1.0f + (e->eliteTier - 1) * ELITE_XP_PER_LOOP;
        Color ring = e->eliteTier > 1 ? PALETTE[PC_PEACH] : PALETTE[PC_GOLD];  // looped elites ring hotter
        fx_spawn_ring(w, e->pos, e->radius, e->radius*4.5f, 0.45f, 5, ring);
        if (e->eliteTier > 1) fx_spawn_ring(w, e->pos, e->radius*1.3f, e->radius*6.0f, 0.6f, 5, PALETTE[PC_GOLD]);
        fx_hitstop(w, 0.10f);
        w->shakeAmount = fmaxf(w->shakeAmount, 14 + (e->eliteTier - 1) * 3.0f);
    }
    gems_drop(w, e->pos, xp, e->color);
    pickups_maybe_drop(w, e->pos);     // small chance of a power-up
    w->kills++; w->score += 10 + w->stage;
    audio_play_kill(&w->audio);
    // ---- analytics ----
    Analytics *a = &w->analytics;
    a->killed++;
    int tid = ETYPES[e->type].threatId;   // stable per-NAME analytics slot
    if (tid >= 0 && tid < MAX_ENEMY_TYPES) { a->typeKills[tid]++; a->typeLife[tid] += w->runTime - e->spawnTime; }
    a->enemyLifeSum += w->runTime - e->spawnTime;
    if (e->hp < 0) a->overkillSum += -e->hp;     // how far past 0 the killing blow went
    { int band = (int)(e->pos.y / (SCREEN/(float)DEATH_Y_BANDS));   // where on the screen it died
      if (band < 0) band = 0;
      if (band >= DEATH_Y_BANDS) band = DEATH_Y_BANDS-1;
      a->deathYBand[band]++; }
    if (src == KILL_DOT)      a->killsByDot++;
    else if (src == KILL_AOE) a->killsByAoe++;
    else                      a->killsByDirect++;
    if (ETYPES[e->type].elite) { a->eliteKills++; a->eliteDeaths++; a->eliteLifeSum += w->runTime - e->spawnTime; }
    // ---- death burst: a melee enemy sprays a radial ring of bullets when it dies, so killing
    // it point-blank is dangerous (not free XP). The ring spawns from a RADIUS (not the exact
    // death point) so a point-blank kill puts the bullets AROUND you with a gap to slip out,
    // rather than an unavoidable corpse-shotgun on top of you. Speed scales with stage.
    int nb = ETYPES[e->type].deathBurst;
    if (nb > 0) {
        float bspeed = (ENEMY_BULLET_BASE + w->stage*ENEMY_BULLET_PER_STAGE) * 0.85f;
        const float spawnR = 38.0f;     // the reaction-gap radius the ring blooms from
        float a0 = frand(w)*2*PI;       // random ring orientation so bursts never look stamped
        w->bulletSrcType = (short)tid;  // blame the bursting enemy
        for (int k = 0; k < nb; k++) {
            float a = a0 + (float)k / nb * 2*PI;
            Vector2 dir = { cosf(a), sinf(a) };
            Vector2 o = { e->pos.x + dir.x*spawnR, e->pos.y + dir.y*spawnR };
            // never materialise a burst bullet inside the player's reaction bubble —
            // point-blank kills stay dangerous but never UNDODGEABLE
            if (Vector2DistanceSqr(o, w->player.pos) < 42.0f*42.0f) continue;
            bullets_add(w, o, (Vector2){dir.x*bspeed, dir.y*bspeed}, 5, 1, e->color, false, WK_ENEMY, 0, false);
        }
        w->bulletSrcType = -1;
    }
}
void enemies_kill(GameWorld *w, Enemy *e) { enemies_kill_src(w, e, KILL_DIRECT); }

// AoE damage, optionally applying a status effect to each struck enemy (id<0 = none).
void enemies_damage_near_fx(GameWorld *w, Vector2 center, float range, float dmg,
                            int effect, float dur, float mag) {
    Grid *g = &w->grid;
    int cx = (int)(center.x/CELL_SIZE), cy = (int)(center.y/CELL_SIZE);
    for (int gy = cy-1; gy <= cy+1; gy++) { if (gy<0||gy>=GRID_CELLS) continue;
        for (int gx = cx-1; gx <= cx+1; gx++) { if (gx<0||gx>=GRID_CELLS) continue;
            int c = gy*GRID_CELLS + gx;
            for (int k = 0; k < g->n[c]; k++) {
                Enemy *e = &w->enemies.items[g->cell[c][k]]; if (!e->active) continue;
                float rr = range + e->radius;
                if (Vector2DistanceSqr(center, e->pos) <= rr*rr) {
                    float applied = enemy_incoming(e, dmg);
                    e->hp -= applied; w->analytics.damageDealt += applied;
                    fx_spawn_damagenum(w, e->pos, (int)(dmg+0.5f), false);
                    if (effect >= 0) enemies_apply_effect(w, e, effect, dur, mag);
                    if (e->hp <= 0) enemies_kill_src(w, e, KILL_AOE);
                }
            }
        }
    }
}

void enemies_damage_near(GameWorld *w, Vector2 center, float range, float dmg) {
    enemies_damage_near_fx(w, center, range, dmg, -1, 0, 0);
}

int enemies_nearest(GameWorld *w, Vector2 from, float maxDist, int skip) {
    int best = -1; float bestD = maxDist*maxDist;
    for (int i = 0; i < MAX_ENEMIES; i++) {
        if (!w->enemies.items[i].active || i == skip) continue;
        float d = Vector2DistanceSqr(from, w->enemies.items[i].pos);
        if (d < bestD) { bestD = d; best = i; }
    }
    return best;
}

int enemies_count_active(GameWorld *w) {
    int n = 0; for (int i = 0; i < MAX_ENEMIES; i++) if (w->enemies.items[i].active) n++; return n;
}

// Nearest boss aim point (and which part). For a multi-part boss the head is skipped while
// hands live (you can't hit it anyway). Returns false if no targetable boss point in range.
// On success: *outPart = part index (>=0) for multi-part, or -1 for a single-body boss.
static bool boss_target_point(GameWorld *w, Vector2 from, Vector2 *outPos, int *outPart) {
    Boss *b = &w->boss;
    if (!b->active) return false;
    if (b->partCount == 0) { *outPos = b->pos; *outPart = -1; return true; }
    bool handsLive = false;
    for (int i=0;i<b->partCount;i++) if (b->parts[i].alive && !b->parts[i].isHead) handsLive = true;
    int best=-1; float bestD=1e18f;
    for (int i=0;i<b->partCount;i++){ BossPartState *ps=&b->parts[i]; if(!ps->alive)continue;
        if (ps->isHead && handsLive) continue;                   // head unhittable while hands live
        float d=Vector2DistanceSqr(from, ps->pos); if(d<bestD){bestD=d;best=i;} }
    if (best<0) return false;
    *outPos = b->parts[best].pos; *outPart = best; return true;
}

// nearest target including the boss
bool target_nearest(GameWorld *w, Vector2 from, float maxDist, int skipIdx, bool skipBoss,
                    Vector2 *outPos, bool *outIsBoss, int *outEnemyIdx) {
    int e = enemies_nearest(w, from, maxDist, skipIdx);
    float ed = (e >= 0) ? Vector2DistanceSqr(from, w->enemies.items[e].pos) : 1e18f;
    Vector2 bpos; int bpart; bool bossOk = (!skipBoss && boss_target_point(w, from, &bpos, &bpart));
    float bd = bossOk ? Vector2DistanceSqr(from, bpos) : 1e18f;
    if (e < 0 && !bossOk) return false;
    if (bossOk && bd <= ed) {
        if (bd > maxDist*maxDist) { if (e < 0) return false; }   // boss out of range, fall to enemy
        else { *outPos = bpos; *outIsBoss = true; *outEnemyIdx = bpart; return true; }  // part idx or -1
    }
    if (e < 0) return false;
    *outPos = w->enemies.items[e].pos; *outIsBoss = false; *outEnemyIdx = e; return true;
}

void target_damage(GameWorld *w, bool isBoss, int enemyIdx, float dmg, int effect, float effMag) {
    if (isBoss) {
        if (!w->boss.active) return;
        if (enemyIdx >= 0) {                              // multi-part: damage a specific part
            if (boss_damage_part(w, enemyIdx, dmg)) fx_spawn_damagenum(w, w->boss.parts[enemyIdx].pos, (int)dmg, true);
        } else {
            w->boss.hp -= dmg;
            fx_spawn_damagenum(w, w->boss.pos, (int)dmg, true);
        }
    } else {
        if (enemyIdx < 0) return;
        Enemy *en = &w->enemies.items[enemyIdx]; if (!en->active) return;
        float applied = enemy_incoming(en, dmg);
        en->hp -= applied; w->analytics.damageDealt += applied;
        fx_spawn_damagenum(w, en->pos, (int)dmg, true);
        if (effect >= 0) enemies_apply_effect(w, en, effect, 0, effMag);
        if (en->hp <= 0) enemies_kill(w, en);
    }
}

// ---- spawn ----
void enemies_spawn(GameWorld *w, int type, Vector2 pos, Vector2 anchor) {
    EnemyPool *ep = &w->enemies;
    int i = -1;
    for (int k = 0; k < MAX_ENEMIES; k++) {
        int idx = (ep->cursor + k) % MAX_ENEMIES;
        if (!ep->items[idx].active) { i = idx; ep->cursor = (idx + 1) % MAX_ENEMIES; break; }
    }
    if (i < 0) return;
    const EnemyType *T = &ETYPES[type];
    Enemy *e = &ep->items[i];
    e->pos = pos; e->vel = (Vector2){0,0}; e->radius = T->radius;
    float hpScale = 1.0f + w->stage*ENEMY_HP_LINEAR + w->stage*w->stage*ENEMY_HP_QUADRATIC;
    // elites get tougher every full biome-loop clear, on top of the normal stage curve --
    // eliteTier is baked in at spawn (0 = not elite; else 1 + loopCount) so kill-time xp/fx
    // can read back how tough THIS enemy was without re-deriving it from the current loop.
    e->eliteTier = T->elite ? (unsigned char)(1 + w->loopCount) : 0;
    if (e->eliteTier > 0) hpScale *= 1.0f + (e->eliteTier - 1) * ELITE_HP_PER_LOOP;
    e->hp = e->maxHp = T->hp * hpScale;
    e->type = type; e->color = T->color;
    e->fireTimer = frandr(w, 0.4f, T->fireInterval > 0 ? T->fireInterval : 1.5f);
    e->phase = frand(w)*2*PI; e->t = 0; e->anchor = anchor;
    e->stepDir = (frand(w)>0.5f)?1:-1; e->patternAng = frand(w)*2*PI;
    e->spawnTime = w->runTime;
    e->hitFlash = 0; e->spawnScale = 0;   // pop-in from nothing (cosmetic)
    e->active = true;
    // analytics: spawn count + incoming-HP accumulator (the DPS-vs-HP denominator)
    w->analytics.spawned++;
    w->analytics.incomingHp += e->maxHp;
}

int enemies_pick_type(GameWorld *w) {
    int valid[64], n = 0;
    // normal waves never pick elites — those spawn only via the dedicated elite-pair path.
    for (int i = 0; i < NUM_ETYPES; i++) if (ETYPES[i].minStage <= w->stage && !ETYPES[i].elite) valid[n++] = i;
    if (n == 0) return 0;
    return valid[irand(w, 0, n-1)];
}

// pick a stage-valid ELITE type, or -1 if this biome has none for the current stage.
int enemies_pick_elite(GameWorld *w) {
    int valid[64], n = 0;
    for (int i = 0; i < NUM_ETYPES; i++) if (ETYPES[i].minStage <= w->stage && ETYPES[i].elite) valid[n++] = i;
    if (n == 0) return -1;
    return valid[irand(w, 0, n-1)];
}

// Spawn a symmetric PAIR of elites (mirrored across the screen centre) so they fire
// radially-symmetric patterns — the "two dudes" miniboss moment.
void enemies_spawn_elite_pair(GameWorld *w) {
    int type = enemies_pick_elite(w);
    if (type < 0) return;
    float dx = SCREEN * 0.22f;
    float ax = SCREEN/2.0f;
    enemies_spawn(w, type, (Vector2){ ax - dx, -40 }, (Vector2){ ax - dx, 130 });
    enemies_spawn(w, type, (Vector2){ ax + dx, -40 }, (Vector2){ ax + dx, 130 });
    w->analytics.elitePairs++;
}

// ---- danmaku fire ----
// One parameterised emitter that subsumes the old down/aimed/spread/ring/flower/spiral.
// `baseAng` orients the fan when not aimed (PI/2 = straight down for enemies). The
// rotating phase lives in `*patternAng` so successive calls spin the pattern (flower/spiral).
void emit_radial(GameWorld *w, Vector2 origin, const FireParams *fp,
                 float baseAng, float *patternAng) {
    int n = fp->count + fp->countPerStage * w->stage;
    if (n < 1) n = 1;
    float bspeed = (ENEMY_BULLET_BASE + w->stage * ENEMY_BULLET_PER_STAGE) * fp->speedScale;

    // centre of the fan: aimed -> toward player; else the supplied base angle.
    float centre = baseAng;
    if (fp->aimed) {
        Vector2 toP = Vector2Subtract(w->player.pos, origin);
        centre = atan2f(toP.y, toP.x);
    }

    bool fullRing = fp->arc >= 2*PI - 0.001f;
    for (int k = 0; k < n; k++) {
        float a;
        if (fullRing) {
            // even spacing around the circle, started from the rotating phase (no dup endpoint)
            a = *patternAng + (float)k / n * 2*PI;
        } else if (n > 1) {
            // finite fan: span `arc`, centred on `centre` (k spans -arc/2 .. +arc/2)
            a = centre - fp->arc*0.5f + fp->arc * ((float)k / (n - 1));
        } else {
            a = centre;
        }
        bullets_add(w, origin, (Vector2){cosf(a)*bspeed, sinf(a)*bspeed}, 5, 1,
                    fp->color, false, WK_ENEMY, 0, false);
    }
    w->analytics.enemyBulletsFired += n;
    *patternAng += fp->spin;
}

static void enemy_fire(GameWorld *w, Enemy *e, float dt) {
    const EnemyType *T = &ETYPES[e->type];
    if ((T->fire == FR_NONE && T->emitter < 0) || T->fireInterval <= 0) return;
    if (e->pos.y < 0 || e->pos.y > SCREEN) return;     // only fire on-screen
    e->fireTimer -= dt;
    if (e->fireTimer > 0) return;
    e->fireTimer = T->fireInterval;
    // muzzle cue: a quick ring pulse at the moment of firing (the pre-fire glow is
    // drawn by render from fireTimer, so the whole windup->release reads clearly)
    fx_spawn_ring(w, e->pos, e->radius*0.4f, e->radius*1.3f, 0.12f, 2, PALETTE[PC_WHITE]);
    w->bulletSrcType = (short)T->threatId;  // tag bullets with the stable per-NAME analytics slot
    if (T->emitter >= 0) {     // composable emitter graph (may spawn sub-emitting bullets)
        float bspeed = ENEMY_BULLET_BASE + w->stage * ENEMY_BULLET_PER_STAGE;
        emitter_fire(w, T->emitter, e->pos, PI/2.0f, bspeed, false, WK_ENEMY, 0, &e->patternAng);
    } else {
        emit_radial(w, e->pos, &T->firep, PI/2.0f, &e->patternAng);   // PI/2 = straight down
    }
    w->bulletSrcType = -1;                 // reset so boss/other emits aren't mis-blamed
}

// ---- movement ----
void enemies_update(GameWorld *w, float dt) {
    Player *pl = &w->player;
    for (int i = 0; i < MAX_ENEMIES; i++) {
        Enemy *e = &w->enemies.items[i]; if (!e->active) continue;
        const EnemyType *T = &ETYPES[e->type];

        // game-feel timers (cosmetic; render reads them)
        if (e->hitFlash > 0) e->hitFlash -= dt;
        if (e->spawnScale < 1) { e->spawnScale += dt*5; if (e->spawnScale > 1) e->spawnScale = 1; }

        // status effects: tick first (may kill via DoT), then apply slow/freeze
        float speedMul = 1, fireMul = 1; bool frozen = false;
        if (enemy_tick_effects(w, e, dt, &speedMul, &fireMul, &frozen)) continue;  // died

        e->t += dt;
        Vector2 prevPos = e->pos;   // to derive actual velocity for the bot's threat prediction
        float sp = T->speed * ENEMY_SPEED_MUL * (1.0f + w->stage*ENEMY_SPEED_PER_STAGE) * speedMul;
        Vector2 toP = Vector2Subtract(pl->pos, e->pos);
        float dist = Vector2Length(toP);
        Vector2 dirP = (dist>1) ? Vector2Scale(toP, 1.0f/dist) : (Vector2){0,1};

        if (frozen) { enemy_fire(w, e, dt*fireMul); goto contact; }  // hold position, no/slow fire

        // Each case reads its magic numbers from T->movep.p[...] (loaded from `move_params`)
        // instead of baked literals. The formulas are unchanged; only the constants are data.
        const float *mp = T->movep.p;
        switch (T->move) {
            case MV_MARCH:   // p: 0=step_mul 1=edge_margin 2=row_drop 3=settle_speed
                if (e->pos.y < e->anchor.y) e->pos.y += sp*dt;
                else { e->pos.x += e->stepDir * sp*mp[0]*dt;
                    if (e->pos.x < mp[1] || e->pos.x > SCREEN-mp[1]) { e->stepDir *= -1; e->anchor.y += mp[2]; }
                    e->pos.y += mp[3]*dt; }
                break;
            case MV_DIVE:    // p: 0=descend_mul 1=hold_time 2=dive_mul
                if (e->pos.y < e->anchor.y && e->t < mp[1]) e->pos.y += sp*mp[0]*dt;
                else {
                    // COMMITTED plunge: aim once at dive start, then barely steer —
                    // a tracking dive at speed is undodgeable; a committed one is a read
                    if (e->phase < 900.0f) { e->phase = 999.0f; e->vel = Vector2Scale(dirP, sp*mp[2]); }
                    e->vel = Vector2Lerp(e->vel, Vector2Scale(dirP, sp*mp[2]), 0.015f);
                    e->pos = Vector2Add(e->pos, Vector2Scale(e->vel, dt));
                }
                break;
            case MV_WEAVE:   // p: 0=phase_rate 1=fall_mul 2=wave_amp
                e->phase += dt*mp[0]; e->pos.y += sp*mp[1]*dt;
                e->pos.x += sinf(e->phase) * sp*mp[2]*dt;
                break;
            case MV_CRAB:    // p: 0=fall_mul 1=wave_freq 2=wave_amp
                e->pos.y += sp*mp[0]*dt;
                e->pos.x += sinf(e->t*mp[1] + e->phase) * sp*mp[2]*dt;
                break;
            case MV_SNAKE:   // p: 0=edge_margin 1=row_drop
                e->pos.x += e->stepDir * sp*dt;
                if (e->pos.x < mp[0] || e->pos.x > SCREEN-mp[0]) { e->stepDir *= -1; e->pos.y += mp[1]; }
                break;
            case MV_HUNT:    // p: 0=steer
                e->vel = Vector2Lerp(e->vel, Vector2Scale(dirP, sp), mp[0]);
                e->pos = Vector2Add(e->pos, Vector2Scale(e->vel, dt));
                break;
            case MV_HOVER:   // p: 0=strafe_freq 1=strafe_amp
                if (e->pos.y < e->anchor.y) e->pos.y += sp*dt;
                else e->pos.x += sinf(e->t*mp[0] + e->phase) * sp*mp[1]*dt;
                break;
            case MV_ORBIT:   // p: 0=orbit_time 1=radius 2=ang_rate 3=y_scale 4=descend_mul 5=dive_mul
                if (e->t < mp[0]) {
                    float r = mp[1], a = e->t*mp[2] + e->phase;
                    e->pos.x = e->anchor.x + cosf(a)*r;
                    e->pos.y = e->anchor.y + sinf(a)*r*mp[3] + e->t*sp*mp[4];
                } else e->pos = Vector2Add(e->pos, Vector2Scale(dirP, sp*mp[5]*dt));
                break;
            case MV_DRIFT:    e->pos.y += sp*dt; break;   // (no params)
            case MV_STRAIGHT: e->pos.y += sp*dt; break;   // (no params)
            case MV_LUNGE: {  // creep down; when close, DASH-seek the player, recover, repeat
                // p: 0=creep_mul 1=trigger_dist 2=lunge_mul 3=lunge_dur 4=recover_dur
                float creepMul=mp[0], trig=mp[1], lungeMul=mp[2], lungeDur=mp[3], recDur=mp[4];
                if (e->phase < 0.5f) {                     // state 0: creeping downward
                    e->pos.y += sp*creepMul*dt;
                    if (dist < trig) { e->phase = 1; e->lungeTimer = lungeDur; e->vel = Vector2Scale(dirP, sp*lungeMul); }
                } else if (e->phase < 1.5f) {              // state 1: lunging (locked dash at the player)
                    e->pos = Vector2Add(e->pos, Vector2Scale(e->vel, dt));
                    e->lungeTimer -= dt;
                    if (e->lungeTimer <= 0) { e->phase = 2; e->lungeTimer = recDur; }
                } else {                                    // state 2: recover (drift, then creep again)
                    e->pos.y += sp*creepMul*0.5f*dt;
                    e->lungeTimer -= dt;
                    if (e->lungeTimer <= 0) e->phase = 0;
                }
                break;
            }
            case MV_SWOOP:    // p: 0=fall_mul 1=arc_rate 2=arc_amp — descend while curving across
                e->phase += dt*mp[1];
                e->pos.y += sp*mp[0]*dt;
                e->pos.x += cosf(e->phase) * sp*mp[2]*dt + (dirP.x>0?1:-1)*sp*0.3f*dt;  // bias toward player
                break;
            case MV_SPIRAL:   // p: 0=fall_mul 1=spin_rate 2=radius — corkscrew toward the bottom
                e->phase += dt*mp[1];
                if (e->anchor.x == 0) e->anchor.x = e->pos.x;   // lock the spiral axis once
                e->pos.y += sp*mp[0]*dt;
                e->pos.x = e->anchor.x + cosf(e->phase)*mp[2];
                break;
        }

        // periodic-lunge LAYER: on a timer, dash a short hop toward the player on top of the
        // base move (creep-then-pounce feel). Skips MV_LUNGE (which owns its own lunge state).
        if (T->lungeEvery > 0 && T->move != MV_LUNGE) {
            e->lungeTimer -= dt;
            if (e->lungeTimer <= 0) {
                e->lungeTimer = T->lungeEvery;
                e->pos = Vector2Add(e->pos, Vector2Scale(dirP, sp*1.2f));  // a quick pounce hop
            }
        }

        // actual per-frame velocity (so the bot's danger_at can predict ANY move op's path,
        // not just the ops that happen to maintain e->vel). dt>0 always here.
        e->vel = Vector2Scale(Vector2Subtract(e->pos, prevPos), 1.0f/dt);

        enemy_fire(w, e, dt*fireMul);

    contact:
        if (e->pos.y > SCREEN+60 || e->pos.x < -90 || e->pos.x > SCREEN+90) {
            // off-bottom = a leak (escaped unpunished); side exits aren't really leaks but rare
            if (e->pos.y > SCREEN+60) { w->analytics.leaked++; int lt=T->threatId; if (lt>=0 && lt<MAX_ENEMY_TYPES) w->analytics.typeLeaks[lt]++; }
            e->active = false; continue;
        }

        if (dist < e->radius + pl->radius) {
            w->analytics.contactEvents++;
            if (T->threatId >= 0 && T->threatId < MAX_ENEMY_TYPES) w->analytics.dmgToPlayerByType[T->threatId]++;
            player_damage(w, 1, T->name ? T->name : "enemy (contact)");
            e->pos = Vector2Subtract(e->pos, Vector2Scale(dirP, 16));
        }
    }
}

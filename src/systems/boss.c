// boss.c - stage boss. Supports single-body bosses (HP on the Boss) and multi-part bosses
// (head + hands, each a BossPartState with its own HP/danmaku). On death it advances the
// stage and re-arms the director.
#include "systems/boss.h"
#include "platform/audio.h"
#include "data/palette.h"
#include "systems/bullets.h"
#include "systems/gems.h"
#include "systems/fx.h"
#include "systems/player.h"
#include "platform/input.h"
#include "data/balance.h"
#include "data/biome.h"
#include "systems/spawn.h"
#include "systems/enemies.h"     // emit_radial (shared danmaku primitive)
#include "systems/emit.h"        // emitter_fire (composable parts)
#include "systems/pickups.h"     // boss pickup rewards
#include "systems/analytics.h"   // stage-clear logging
#include "data/anim.h"           // part keyframe sequences
#include <math.h>
#include <stdio.h>

void boss_spawn(GameWorld *w) {
    Boss *b = &w->boss;
    BossDef *def = &BIOMES[w->biome].boss;
    b->active = true;
    b->pos = (Vector2){ SCREEN/2.0f, -60 };
    b->vel = (Vector2){ 0, 40 };
    b->radius = def->radius > 0 ? def->radius : BOSS_RADIUS;
    float baseHp = def->hp > 0 ? def->hp : (BOSS_HP_BASE + w->stage*BOSS_HP_PER_STAGE);
    b->hpMax = b->hp = baseHp;
    b->phase = 0; b->fireTimer = 1.0f; b->patternTimer = 0;
    b->patternAng = 0; b->flowerAng = 0; b->animTime = 0;
    w->analytics.bossSpawnTime = w->runTime;   // TTK clock starts

    // multi-part layout: instantiate each part's runtime state, scaling HP with stage.
    b->partCount = def->partCount;
    float hpScale = 1.0f + w->stage*0.15f;
    for (int i = 0; i < def->partCount && i < MAX_BOSS_PARTS_RT; i++) {
        const BossPartDef *pd = &def->parts[i];
        BossPartState *ps = &b->parts[i];
        ps->hpMax = ps->hp = pd->hp * hpScale;
        ps->radius = pd->radius;
        ps->isHead = pd->isHead;
        ps->alive = true;
        ps->fireTimer = frandr(w, 0.3f, pd->interval);
        ps->patternAng = frand(w)*2*PI;
        ps->pos = (Vector2){ b->pos.x + pd->offX, b->pos.y + pd->offY };
        ps->animRot = 0;
    }
}

// true while at least one non-head part (a hand) is still alive — the head is shielded then.
static bool hands_alive(const Boss *b) {
    for (int i = 0; i < b->partCount; i++)
        if (b->parts[i].alive && !b->parts[i].isHead) return true;
    return false;
}

// the boss is defeated when: single-body hp<=0, OR (multi-part) the head is dead.
static bool boss_defeated(const Boss *b) {
    if (b->partCount == 0) return b->hp <= 0;
    for (int i = 0; i < b->partCount; i++)
        if (b->parts[i].isHead && !b->parts[i].alive) return true;
    // headless multi-part: defeated when all parts dead
    bool headExists = false;
    for (int i = 0; i < b->partCount; i++) if (b->parts[i].isHead) headExists = true;
    if (!headExists) { for (int i=0;i<b->partCount;i++) if (b->parts[i].alive) return false; return true; }
    return false;
}

// Apply damage to a boss part (bullets.c calls this). Head ignores damage while hands live.
// Returns true if the hit landed (so the bullet can spawn a damage number).
bool boss_damage_part(GameWorld *w, int partIdx, float dmg) {
    Boss *b = &w->boss;
    if (partIdx < 0 || partIdx >= b->partCount) return false;
    BossPartState *ps = &b->parts[partIdx];
    if (!ps->alive) return false;
    if (ps->isHead && hands_alive(b)) {                  // head shielded by living hands
        fx_spawn_particles(w, ps->pos, 1, PALETTE[PC_LILAC], 60);   // "clink" — no damage
        w->analytics.bossClinks++;
        return false;
    }
    ps->hp -= dmg;
    if (ps->hp <= 0) {
        ps->alive = false;
        fx_spawn_particles(w, ps->pos, 40, PALETTE[PC_PEACH], 260);
        fx_spawn_ring(w, ps->pos, ps->radius*0.5f, ps->radius*3.2f, 0.5f, 6, PALETTE[PC_GOLD]);
        fx_spawn_ring(w, ps->pos, ps->radius*0.3f, ps->radius*2.0f, 0.3f, 3, PALETTE[PC_WHITE]);
        fx_hitstop(w, 0.12f);
        for (int k=0;k<4;k++) gems_drop(w, (Vector2){ps->pos.x+frandr(w, -20,20),ps->pos.y+frandr(w, -20,20)}, 4+w->stage, PALETTE[PC_GOLD]);
        pickups_drop(w, ps->pos, irand(w, 0, PICKUP_KIND_COUNT-1));   // a hand kill rewards a pickup
        w->shakeAmount = fmaxf(w->shakeAmount, 12);
    }
    return true;
}

static void boss_die(GameWorld *w) {
    Boss *b = &w->boss;
    b->active = false;
    w->analytics.bossFights++;
    w->analytics.bossTTKSum += (float)(w->runTime - w->analytics.bossSpawnTime);
    fx_spawn_particles(w, b->pos, 80, PALETTE[PC_PEACH], 320);
    fx_spawn_ring(w, b->pos, b->radius*0.4f, SCREEN*0.9f, 0.8f, 8, PALETTE[PC_GOLD]);
    fx_spawn_ring(w, b->pos, b->radius*0.3f, SCREEN*0.6f, 0.55f, 4, PALETTE[PC_WHITE]);

    // ---- BOARD WIPE: the kill detonates everything left on the field ----
    // every enemy bullet turns into a gem (bullet-cancel jackpot, classic shmup)
    for (int i = 0; i < MAX_BULLETS; i++) {
        Bullet *eb = &w->bullets.items[i];
        if (!eb->active || eb->friendly) continue;
        eb->active = false;
        gems_drop(w, eb->pos, 1, PALETTE[PC_GOLD]);
    }
    // every straggler enemy pops (their kill fx cascade on their own)
    for (int i = 0; i < MAX_ENEMIES; i++) {
        Enemy *e = &w->enemies.items[i];
        if (e->active) enemies_kill_src(w, e, KILL_AOE);
    }
    // ...and any death-burst rings those kills just sprayed get wiped too
    for (int i = 0; i < MAX_BULLETS; i++) {
        Bullet *eb = &w->bullets.items[i];
        if (eb->active && !eb->friendly) eb->active = false;
    }

    // a modest gem reward (not a whole stage of XP) + a frenzy. No free instant-level: the
    // boss is a milestone, not a level handout.
    for (int k=0;k<12;k++)
        gems_drop(w, (Vector2){b->pos.x+frandr(w, -40,40),b->pos.y+frandr(w, -40,40)}, 6+w->stage, PALETTE[PC_GOLD]);
    pickups_drop(w, (Vector2){b->pos.x+30,b->pos.y}, PICK_FRENZY);
    audio_play(&w->audio, SFX_BOSS_DIE); input_rumble(1.0f,1.0f,0.5f); w->shakeAmount=24;
    fx_hitstop(w, 0.12f);            // one hard beat...
    w->clearFlash = 1.0f;            // ...a white flash...
    {                                // ...and the HELL YEAH slam banner
        static const char *YELLS[] = { "HELL YEAH!", "OBLITERATED!", "BIOME DOWN!", "GET BLASTED!" };
        w->stageBannerText = YELLS[irand(w, 0, 3)];
        analytics_stage_cleared(w);   // log the per-stage row before advancing
        w->stage++;
        int next = w->biome + 1;
        if (next >= NUM_BIOMES) { next = 0; w->loopCount++; }   // full clear: loop back around, tougher
        biome_enter(w, next);
        if (w->loopCount > 0) {
            snprintf(w->stageBannerSubBuf, sizeof(w->stageBannerSubBuf),
                     "LOOP %d - %s", w->loopCount + 1, BIOMES[w->biome].name);
            w->stageBannerSub = w->stageBannerSubBuf;
        } else {
            w->stageBannerSub = BIOMES[w->biome].name;   // "NEXT: <biome>"
        }
        w->stageBannerTimer = 2.4f;
    }
    w->director.waveTimer = 3.2f;    // a breath to vacuum the gem jackpot before the next wave
}

// --- multi-part update: position parts on the swept anchor, fire each, check defeat ---
static void boss_update_parts(GameWorld *w, float dt) {
    Boss *b = &w->boss;
    BossDef *def = &BIOMES[w->biome].boss;
    Player *pl = &w->player;
    b->animTime += dt;
    for (int i = 0; i < b->partCount; i++) {
        const BossPartDef *pd = &def->parts[i];
        BossPartState *ps = &b->parts[i];
        // keyframed motion layers over the base offset (static when no frames)
        float ox = pd->offX, oy = pd->offY, rot = 0, sc = 1.0f;
        anim_eval(&pd->anim, b->animTime, &ox, &oy, &rot, &sc);
        ps->pos = (Vector2){ b->pos.x + ox, b->pos.y + oy };
        ps->animRot = rot;
        ps->radius = pd->radius * (sc > 0.05f ? sc : 0.05f);   // hitbox follows the squash
        if (!ps->alive) continue;
        // contact damage
        if (Vector2Distance(pl->pos, ps->pos) < ps->radius + pl->radius) player_damage(w, 1, "boss (contact)");
        if (b->pos.y < 0) continue;   // don't fire until on-screen
        ps->fireTimer -= dt;
        if (ps->fireTimer <= 0) {
            ps->fireTimer = pd->interval > 0 ? pd->interval : 1.0f;
            float bspeed = ENEMY_BULLET_BASE + w->stage * ENEMY_BULLET_PER_STAGE;
            if (pd->emitter >= 0)
                emitter_fire(w, pd->emitter, ps->pos, PI/2.0f, bspeed, false, WK_ENEMY, 0, &ps->patternAng);
            else if (pd->fireOp >= 0)
                emit_radial(w, ps->pos, &pd->fp, PI/2.0f, &ps->patternAng);
        }
    }
    if (boss_defeated(b)) boss_die(w);
}

void boss_update(GameWorld *w, float dt) {
    Boss *b = &w->boss;
    if (!b->active) return;
    Player *pl = &w->player;
    BossDef *def = &BIOMES[w->biome].boss;

    // anchor sweep (shared by single-body + multi-part)
    if (b->pos.y < 130) b->pos.y += 40*dt;
    else { b->patternTimer += dt; b->pos.x = SCREEN/2.0f + sinf(b->patternTimer*0.8f)*200; }

    if (b->partCount > 0) { boss_update_parts(w, dt); return; }   // multi-part path

    // ---- single-body boss (data-driven phases) ----
    int ph = 0;
    if (def->phaseCount > 0) {
        ph = def->phaseCount - 1;
        for (int i = 0; i < def->phaseCount; i++)
            if (b->hp >= b->hpMax * def->phases[i].hpAbove) { ph = i; break; }
    }
    b->phase = ph;

    b->fireTimer -= dt;
    if (b->fireTimer <= 0 && b->pos.y > 0 && def->phaseCount > 0) {
        b->fireTimer = 0.7f - b->phase*0.12f;
        const BossPhase *bp = &def->phases[ph];
        emit_radial(w, b->pos, &bp->mainFp, PI/2.0f, &b->patternAng);
        if (bp->plusFlower) emit_radial(w, b->pos, &bp->flowerFp, PI/2.0f, &b->flowerAng);
    }
    if (Vector2Distance(pl->pos,b->pos) < b->radius+pl->radius) player_damage(w, 1, "boss (contact)");

    if (b->hp <= 0) boss_die(w);
}

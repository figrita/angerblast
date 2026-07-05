// player.c - player ship state, movement, main gun, and damage model.
#include "systems/player.h"
#include "platform/audio.h"
#include "data/palette.h"
#include "systems/bullets.h"
#include "systems/fx.h"
#include "systems/upgrades_sys.h"   // xp_for_level
#include "platform/input.h"
#include "systems/pilot.h"
#include "systems/analytics.h"
#include "data/balance.h"
#include <math.h>
#include <string.h>
#include <stdio.h>   // snprintf (cause-of-death tag)

void player_reset(GameWorld *w) {
    Player *p = &w->player;
    memset(p, 0, sizeof(*p));
    p->pos = (Vector2){ SCREEN/2.0f, SCREEN*0.8f };
    p->radius = PLAYER_RADIUS;
    p->shieldMax = p->shield = PLAYER_SHIELD_MAX;
    p->shieldRegenDelay = PLAYER_SHIELD_DELAY;
    p->shieldRegenRate = PLAYER_SHIELD_RATE;
    p->hp = p->hpMax = PLAYER_HP;
    p->moveSpeed = PLAYER_MOVE_SPEED;
    p->pickupRadius = PLAYER_PICKUP_RADIUS;
    p->fireInterval = PLAYER_FIRE_INTERVAL;
    p->bulletDamage = PLAYER_BULLET_DAMAGE;
    p->bulletSpeed = PLAYER_BULLET_SPEED;
    p->globalDamageMul = 1; p->globalRateMul = 1; p->xpMul = 1;
    p->armor = 0;
    p->level = 1; p->xp = 0; p->xpToNext = xp_for_level(1);
    p->bank = 0;
    p->autofire = true;   // charge shot: hold fire to charge; T toggles the stream
    p->chargeShotIdx = -1;

    // start with the Main Cannon — a real socketed weapon (fires straight up).
    w->loadout.count = 0;
    weapon_grant(w, WP_MAINGUN);
}

void player_update_movement(GameWorld *w, float dt) {
    Player *p = &w->player;
    Vector2 mv = pilot_move(w);

    // ---- barrel-roll dodge: a quick i-frame dash in the movement (or last-faced) direction
    if (p->rollCooldown > 0) p->rollCooldown -= dt;
    if (pilot_dodge(w)) {
        if (p->rollTimer <= 0 && p->rollCooldown <= 0) {
            Vector2 dir = mv;
            if (Vector2LengthSqr(dir) < 0.01f) dir = (Vector2){ 0, -1 };  // stationary -> dash up
            p->rollDir = Vector2Normalize(dir);
            p->rollTimer = DODGE_TIME;
            p->rollCooldown = DODGE_COOLDOWN;
            p->iframe = fmaxf(p->iframe, DODGE_IFRAME);
            fx_spawn_particles(w, p->pos, 14, PALETTE[PC_PALECYAN], 200);
            input_rumble(0.2f, 0.4f, 0.12f);
            audio_play(&w->audio, SFX_DODGE);
            w->analytics.dodges++;
        } else {
            w->analytics.dodgesOnCooldown++;   // press rejected (cooldown frustration)
        }
    }
    if (p->rollTimer > 0) {
        p->rollTimer -= dt;
        // dash at full dodge speed, ignoring normal accel/friction for the roll window
        p->vel = Vector2Scale(p->rollDir, DODGE_SPEED);
        Vector2 step = Vector2Scale(p->vel, dt);
        w->analytics.dodgeDistance += Vector2Length(step);
        p->pos = Vector2Add(p->pos, step);
    } else {
        p->vel = Vector2Add(p->vel, Vector2Scale(mv, PLAYER_ACCEL*dt));
        p->vel = Vector2Scale(p->vel, PLAYER_FRICTION);
        if (Vector2Length(p->vel) > p->moveSpeed)
            p->vel = Vector2Scale(Vector2Normalize(p->vel), p->moveSpeed);
        p->pos = Vector2Add(p->pos, Vector2Scale(p->vel, dt));
    }
    // smoothed horizontal lean for the sprite's pixel-shear bank (not gameplay)
    float bankTarget = p->moveSpeed > 0 ? p->vel.x / p->moveSpeed : 0;
    if (bankTarget > 1) bankTarget = 1; else if (bankTarget < -1) bankTarget = -1;
    p->bank += (bankTarget - p->bank) * fminf(1.0f, dt * 10.0f);
    float m = p->radius;
    if (p->pos.x < m) p->pos.x = m;
    if (p->pos.x > SCREEN-m) p->pos.x = SCREEN-m;
    if (p->pos.y < m) p->pos.y = m;
    if (p->pos.y > SCREEN-m) p->pos.y = SCREEN-m;

    if (p->iframe > 0) {
        p->iframe -= dt;
        if (p->iframe <= 0) {
            p->iframe = 0;
            fx_spawn_particles(w, p->pos, 6, PAL_HITFLASH, 80);
        }
    }
    if (p->shieldRegenTimer > 0) p->shieldRegenTimer -= dt;
    else if (p->shield < p->shieldMax) {
        float before = p->shield;
        p->shield += p->shieldRegenRate*dt;
        if (p->shield > p->shieldMax) p->shield = p->shieldMax;
        w->analytics.shieldRegened += p->shield - before;
    }
    // the main gun is now a weapon in the loadout; weapons_update fires it.
}

void player_damage(GameWorld *w, float amount, const char *source) {
    Player *p = &w->player;
    Analytics *a = &w->analytics;
    bool duringBoss = w->boss.active;
    // Barrel-roll dodge = TRUE invincibility: ignore the hit entirely (no shield drain, no hull
    // loss). Checked before shield-soak so a dodge through fire costs nothing.
    if (p->rollTimer > 0) {
        a->wastedHits++;
        a->dodgeAvoidedHits++;
        return;
    }
    // Armor: diminishing-returns damage reduction on the shield drain. reduction =
    // armor/(armor+K), so stacks never reach 100% and each point is worth a little less.
    float armorDR = p->armor / (p->armor + PLAYER_ARMOR_K);
    float drain = amount * PLAYER_SHIELD_HIT * (1.0f - armorDR);   // each hit bites the shield
    if (p->shield > 0) {
        float soak = fminf(drain, p->shield);
        p->shield -= drain;
        bool broke = (p->shield <= 0);
        if (p->shield < 0) p->shield = 0;
        p->shieldRegenTimer = p->shieldRegenDelay;
        w->shakeAmount = fmaxf(w->shakeAmount, 6);
        input_rumble(0.3f, 0.5f, 0.15f);
        a->shieldAbsorbed += soak;
        fx_spawn_ring(w, p->pos, p->radius, p->radius*2.2f, 0.18f, 2, PALETTE[PC_SKY]);
        if (broke) {                       // SHIELD DOWN: unmistakable
            a->shieldBreaks++;
            fx_spawn_ring(w, p->pos, p->radius, p->radius*3.6f, 0.4f, 4, PALETTE[PC_CORAL]);
            fx_hitstop(w, 0.06f);
            w->shakeAmount = fmaxf(w->shakeAmount, 10);
        }
        if (duringBoss) a->dmgDuringBoss += soak;
        return;
    }
    if (p->iframe > 0) {                 // post-hull-hit i-frames (dodge handled above)
        a->wastedHits++;
        return;
    }
    // hull hit: lose a heart — but the shield instantly refills to full (a fresh buffer to
    // fight back behind), so each HP loss is a hard reset, not a death spiral.
    p->hp--; p->iframe = PLAYER_IFRAME;
    p->shield = p->shieldMax; p->shieldRegenTimer = 0;
    w->shakeAmount = 18; w->hitFlash = 1.0f;
    a->hullLost++;
    if (duringBoss) a->dmgDuringBoss += 1;
    fx_spawn_particles(w, p->pos, 24, PALETTE[PC_CORAL], 220);
    audio_play(&w->audio, SFX_HIT);
    input_rumble(0.7f, 1.0f, 0.3f);
    if (p->hp <= 0) {
        audio_play(&w->audio, SFX_EXPLODE);
        if (w->score > w->bestScore) w->bestScore = w->score;
        a->deathStage = w->stage;
        snprintf(a->causeOfDeath, sizeof(a->causeOfDeath), "%s", source ? source : "?");
        analytics_classify_death(w);   // fair (on-the-ropes) vs unfair (sudden spike)?
        // open the post-run survey first; on submit it writes the report, then -> DEAD.
        a->surveyRow = 0;
        for (int i = 0; i < 4; i++) a->ratings[i] = 3;   // default mid
        w->state = STATE_SURVEY;
    }
}

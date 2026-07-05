// pickups.c - power-up pickups. See pickups.h.
#include "systems/pickups.h"
#include "systems/gems.h"          // gems_magnetize_near
#include "systems/enemies.h"       // enemies_damage_near (board-wipe)
#include "systems/fx.h"
#include "systems/upgrades_sys.h"  // xp_gain (instant level-up)
#include "data/palette.h"
#include "platform/input.h"
#include "platform/audio.h"        // audio_play_pickup
#include <math.h>
#include <stddef.h>

const char *pickup_name(int kind) {
    switch (kind) {
        case PICK_MAGNET:  return "Magnet";
        case PICK_WIPE:    return "Board Wipe";
        case PICK_LEVELUP: return "Instant Level";
        case PICK_FRENZY:  return "Frenzy";
        case PICK_HEAL:    return "Repair";
        default:           return "Pickup";
    }
}
const char *pickup_desc(int kind) {
    switch (kind) {
        case PICK_MAGNET:  return "Pulls every gem on screen to you";
        case PICK_WIPE:    return "Clears enemy bullets, damages all foes";
        case PICK_LEVELUP: return "Grants an immediate level-up";
        case PICK_FRENZY:  return "8s of +rate & +damage overdrive";
        case PICK_HEAL:    return "Restores 1 hull HP (falls fast — grab it!)";
        default:           return "";
    }
}
Color pickup_color(int kind) {
    switch (kind) {
        case PICK_MAGNET:  return PALETTE[PC_AQUABRT];
        case PICK_WIPE:    return PALETTE[PC_HOTPINK];
        case PICK_LEVELUP: return PALETTE[PC_GOLD];
        case PICK_FRENZY:  return PALETTE[PC_CORAL];
        case PICK_HEAL:    return PALETTE[PC_CRIMSON];   // a red repair heart
        default:           return (Color){255,255,255,255};
    }
}
const char *pickup_sprite(int kind) {
    switch (kind) {
        case PICK_MAGNET:  return "sprites/common/pickup_magnet.png";
        case PICK_WIPE:    return "sprites/common/pickup_wipe.png";
        case PICK_LEVELUP: return "sprites/common/pickup_levelup.png";
        case PICK_HEAL:    return "sprites/common/pickup_heal.png";
        case PICK_FRENZY:  return "sprites/common/pickup_freeze.png";  // shared badge (see header note)
        default:           return NULL;
    }
}

void pickups_drop(GameWorld *w, Vector2 pos, int kind) {
    PickupPool *pp = &w->pickups;
    int slot = -1;
    for (int i = 0; i < MAX_PICKUPS; i++) {
        int idx = (pp->cursor + i) % MAX_PICKUPS;
        if (!pp->items[idx].active) { slot = idx; pp->cursor = (idx + 1) % MAX_PICKUPS; break; }
    }
    if (slot < 0) return;
    if (kind >= 0 && kind < PICKUP_KIND_COUNT) w->analytics.pickupsDropped[kind]++;
    // HEAL plummets fast (you must dive for it); others drift gently.
    float vy = (kind == PICK_HEAL) ? frandr(w, 200, 260) : frandr(w, 30, 60);
    pp->items[slot] = (Pickup){ .pos=pos, .vel={ frandr(w, -20,20), vy },
        .kind=kind, .bob=frand(w)*2*PI, .active=true, .magnetized=false };
}

// Small chance, on an enemy death, to drop a random pickup (board-wipe is rarer).
void pickups_maybe_drop(GameWorld *w, Vector2 pos) {
    if (irand(w, 0, 99) >= 2) return;   // ~2% per kill
    int kind = irand(w, 0, PICKUP_KIND_COUNT-1);
    if (kind == PICK_WIPE && irand(w, 0,1)) kind = PICK_FRENZY;  // halve wipe frequency
    pickups_drop(w, pos, kind);
}

static void pickup_collect(GameWorld *w, int kind) {
    Player *pl = &w->player;
    fx_spawn_particles(w, pl->pos, 24, pickup_color(kind), 240);
    audio_play_pickup(&w->audio);
    input_rumble(0.3f, 0.5f, 0.18f);
    if (kind >= 0 && kind < PICKUP_KIND_COUNT) w->analytics.pickupsCollected[kind]++;
    switch (kind) {
        case PICK_MAGNET:
            gems_magnetize_near(w, pl->pos, 9999);   // whole screen
            break;
        case PICK_WIPE: {
            // clear every enemy bullet on screen, then blast all enemies
            for (int i = 0; i < MAX_BULLETS; i++) { Bullet *b = &w->bullets.items[i];
                if (b->active && !b->friendly) { fx_spawn_particles(w, b->pos, 1, b->color, 60); b->active = false; } }
            int before = w->analytics.killed;
            enemies_damage_near(w, (Vector2){SCREEN/2.0f, SCREEN/2.0f}, SCREEN*1.5f, 40 + w->stage*8);
            w->analytics.boardWipeKills += w->analytics.killed - before;
            w->shakeAmount = fmaxf(w->shakeAmount, 16); w->hitFlash = 0.5f;
            break;
        }
        case PICK_LEVELUP:
            xp_gain(w, pl->xpToNext - pl->xp + 1);   // push past the next threshold
            break;
        case PICK_FRENZY:
            pl->frenzyTimer = 8.0f;
            break;
        case PICK_HEAL:
            if (pl->hp < pl->hpMax) pl->hp++;        // restore one hull pip
            pl->shield = pl->shieldMax;              // and top off the shield
            break;
        default: break;
    }
}

void pickups_update(GameWorld *w, float dt) {
    Player *pl = &w->player;
    if (pl->frenzyTimer > 0) pl->frenzyTimer -= dt;
    for (int i = 0; i < MAX_PICKUPS; i++) {
        Pickup *p = &w->pickups.items[i]; if (!p->active) continue;
        p->bob += dt * 4;
        float d = Vector2Distance(p->pos, pl->pos);
        if (p->kind == PICK_HEAL) {
            // HEAL doesn't magnet — it plummets, so you must dive to catch it before it leaves.
            p->vel.x *= 0.98f;            // keep its fast downward velocity (no slow-down lerp)
        } else {
            if (d < pl->pickupRadius * 1.4f) p->magnetized = true;   // bigger lure than gems
            if (p->magnetized) {
                Vector2 dir = Vector2Normalize(Vector2Subtract(pl->pos, p->pos));
                p->vel = Vector2Lerp(p->vel, Vector2Scale(dir, 420), 0.2f);
            } else {
                p->vel.x *= 0.95f;
                p->vel.y = Lerp(p->vel.y, 50.0f, 0.04f);
            }
        }
        p->pos = Vector2Add(p->pos, Vector2Scale(p->vel, dt));
        if (p->pos.y > SCREEN + 24) { p->active = false; continue; }
        if (d < pl->radius + 12) { pickup_collect(w, p->kind); p->active = false; }
    }
}

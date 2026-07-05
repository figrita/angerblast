// drones.c - companion drones: movement AIs + firing. See drones.h.
#include "systems/drones.h"
#include "systems/weapons.h"
#include "systems/enemies.h"   // enemies_nearest / target_nearest
#include "data/weapons_def.h"
#include "data/runes.h"
#include <math.h>
#include <stddef.h>

const char *drone_ai_name(int ai) {
    switch (ai) {
        case DRONE_FOLLOW: return "Follow Drone";
        case DRONE_AUTO:   return "Auto Drone";
        case DRONE_SWEEP:  return "Sweep Drone";
        default:           return "Drone";
    }
}

const char *drone_ai_sprite(int ai) {
    switch (ai) {
        case DRONE_FOLLOW: return "sprites/common/drone_follow.png";
        case DRONE_AUTO:   return "sprites/common/drone_auto.png";
        case DRONE_SWEEP:  return "sprites/common/drone_sweep.png";
        default:           return NULL;
    }
}

// Each AI ships a fitting gun: follow = autoaim turret, auto = homing swarm, sweep = side guns.
int drone_default_weapon(int ai) {
    switch (ai) {
        case DRONE_FOLLOW: return WP_AUTOAIM;
        case DRONE_AUTO:   return WP_HOMING;
        case DRONE_SWEEP:  return WP_SIDEGUN;
        default:           return WP_AUTOAIM;
    }
}

void drone_grant(GameWorld *w, int ai, int weaponType) {
    if (w->drones.count >= MAX_DRONES) return;
    Drone *d = &w->drones.items[w->drones.count];
    *d = (Drone){ 0 };
    d->ai = ai;
    d->active = true;
    d->sweepDir = (frand(w) > 0.5f) ? 1 : -1;
    d->orbitSlot = w->drones.count;          // spreads followers around the player
    d->pos = w->player.pos;
    Weapon *wp = &d->weapon;
    wp->type = weaponType; wp->level = 1; wp->evolution = -1; wp->timer = frandr(w, 0, 0.3f);
    for (int i = 0; i < SOCKETS; i++) wp->runes[i] = RUNE_NONE;
    w->drones.count++;
}

// --- per-AI movement toward a target point, with smoothing ---
static void drone_move_to(Drone *d, Vector2 target, float lerpAmt, float dt) {
    Vector2 want = Vector2Subtract(target, d->pos);
    d->vel = Vector2Lerp(d->vel, Vector2Scale(want, 6.0f), lerpAmt);
    d->pos = Vector2Add(d->pos, Vector2Scale(d->vel, dt));
}

void drones_update(GameWorld *w, float dt) {
    Player *pl = &w->player;
    int nFollow = 0;
    for (int i = 0; i < w->drones.count; i++) {
        Drone *d = &w->drones.items[i];
        if (!d->active) continue;
        d->t += dt;

        switch (d->ai) {
            case DRONE_FOLLOW: {
                // trail the player in a ring slot just behind/around them
                float a = (float)w->runTime * 1.4f + (float)nFollow * (2*PI/3.0f);
                Vector2 slot = { pl->pos.x + cosf(a)*54, pl->pos.y + 40 + sinf(a)*22 };
                drone_move_to(d, slot, 0.10f, dt);
                nFollow++;
                break;
            }
            case DRONE_AUTO: {
                // roam toward the nearest enemy cluster, hovering at a standoff distance
                int e = enemies_nearest(w, d->pos, 600, -1);
                Vector2 target;
                if (e >= 0) {
                    Vector2 ep = w->enemies.items[e].pos;
                    Vector2 dir = Vector2Normalize(Vector2Subtract(d->pos, ep));
                    target = Vector2Add(ep, Vector2Scale(dir, 120));   // stand off 120px
                } else {
                    target = (Vector2){ pl->pos.x, pl->pos.y - 120 };  // idle above the player
                }
                drone_move_to(d, target, 0.06f, dt);
                break;
            }
            case DRONE_SWEEP: {
                // sweep left-right across a band above the player
                float bandY = fmaxf(60, pl->pos.y - 160);
                d->pos.y = Lerp(d->pos.y, bandY, 0.05f);
                d->pos.x += d->sweepDir * 220 * dt;
                if (d->pos.x < 40)        { d->pos.x = 40;        d->sweepDir = 1; }
                if (d->pos.x > SCREEN-40) { d->pos.x = SCREEN-40; d->sweepDir = -1; }
                break;
            }
            default: break;
        }
        // clamp to arena
        if (d->pos.x < 12) d->pos.x = 12;
        if (d->pos.x > SCREEN-12) d->pos.x = SCREEN-12;
        if (d->pos.y < 12) d->pos.y = 12;
        if (d->pos.y > SCREEN-12) d->pos.y = SCREEN-12;

        weapons_fire_one(w, &d->weapon, d->pos, dt);
    }
}

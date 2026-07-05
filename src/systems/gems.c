// gems.c - XP gems.
#include "systems/gems.h"
#include "systems/fx.h"
#include "systems/upgrades_sys.h"   // xp_gain
#include "platform/audio.h"

void gems_drop(GameWorld *w, Vector2 pos, double value, Color c) {
    GemPool *gp = &w->gems;
    int slot = -1;
    for (int i = 0; i < MAX_GEMS; i++) {
        int idx = (gp->cursor + i) % MAX_GEMS;
        if (!gp->items[idx].active) { slot = idx; gp->cursor = (idx + 1) % MAX_GEMS; break; }
    }
    if (slot < 0) {  // pool full: merge value into any active gem so XP isn't lost
        for (int j = 0; j < MAX_GEMS; j++) if (gp->items[j].active) { gp->items[j].value += value; w->analytics.gemMerges++; return; }
        return;
    }
    w->analytics.gemsDropped++;
    gp->items[slot] = (Gem){ .pos=pos, .vel={ frandr(w, -15,15), frandr(w, 40,80) }, .value=value,
        .active=true, .magnetized=false, .color=c };
}

// Prospector: flag every gem within `radius` of `c` as magnetized so it homes to the player.
void gems_magnetize_near(GameWorld *w, Vector2 c, float radius) {
    float r2 = radius*radius;
    for (int i = 0; i < MAX_GEMS; i++) { Gem *g = &w->gems.items[i];
        if (g->active && Vector2DistanceSqr(g->pos, c) <= r2) g->magnetized = true; }
}

void gems_update(GameWorld *w, float dt) {
    Player *pl = &w->player;
    for (int i = 0; i < MAX_GEMS; i++) {
        Gem *g = &w->gems.items[i]; if (!g->active) continue;
        float d = Vector2Distance(g->pos, pl->pos);
        if (d < pl->pickupRadius) g->magnetized = true;
        if (g->magnetized) {
            // home toward the player at a brisk, distance-scaled speed (faster when close so it
            // snaps in). Speed is always positive — the old `380 + (pickupRadius-d)*2` went
            // NEGATIVE for far gems (e.g. the whole-screen Magnet pickup), flinging them offscreen.
            Vector2 dir = (d > 0.01f) ? Vector2Scale(Vector2Subtract(pl->pos, g->pos), 1.0f/d)
                                      : (Vector2){0,0};
            float speed = 360.0f + 240.0f * (1.0f - fminf(d, 400.0f)/400.0f);  // ~360..600, capped
            g->vel = Vector2Lerp(g->vel, Vector2Scale(dir, speed), 0.25f);
        } else {  // slow downward fall, gentle horizontal damping
            g->vel.x *= 0.94f;
            g->vel.y = Lerp(g->vel.y, 60.0f, 0.04f);
        }
        g->pos = Vector2Add(g->pos, Vector2Scale(g->vel, dt));
        if (g->pos.y > SCREEN + 20) { g->active = false; w->analytics.gemsLeaked++; continue; }   // despawn off the bottom
        if (d < pl->radius + 8) {
            xp_gain(w, g->value);
            g->active = false;
            w->analytics.gemsCollected++;
            audio_play_pickup(&w->audio);
            fx_spawn_particles(w, g->pos, 1, g->color, 60);
        }
    }
}

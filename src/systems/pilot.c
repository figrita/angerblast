// pilot.c - HUMAN passthrough + a heuristic BOT pilot (potential-field dodging, gem greed,
#include <stdio.h>
#include <stdlib.h>
// greedy card/socket picks). The bot exists so the game can playtest itself headless.
#include "systems/pilot.h"
#include "platform/input.h"
#include "systems/upgrades_sys.h"   // g_cards, rune_would_evolve, rune_first_open_socket
#include "systems/enemies.h"        // enemies_nearest
#include "data/runes.h"
#include "data/trifectas.h"
#include "data/weapons_def.h"
#include <math.h>

// ===== heuristic bot =====
// Predicted DANGER at a candidate point `at`, looking `ahead` seconds forward: enemy bullets
// are advanced along their velocity, so the bot avoids where bullets WILL be, not just where
// they are — the key to threading danmaku. Adds standing contact danger from enemies/boss/walls.
// Lower = safer. This is the function the bot minimises when choosing where to move.
static float danger_at(GameWorld *w, Vector2 at, float ahead) {
    Player *p = &w->player;
    float dng = 0;
    const float hitR = p->radius + 6;
    for (int i = 0; i < MAX_BULLETS; i++) {
        Bullet *b = &w->bullets.items[i];
        if (!b->active || b->friendly) continue;
        // closest approach of the bullet's future path to `at` over [0, ahead]
        Vector2 rel = Vector2Subtract(b->pos, at);
        if (Vector2LengthSqr(rel) > 260*260) continue;             // far: skip (cheap cull)
        // sample a few points along the bullet's path
        for (float t = 0; t <= ahead; t += ahead/4.0f) {
            Vector2 bp = { b->pos.x + b->vel.x*t, b->pos.y + b->vel.y*t };
            float d = Vector2Distance(bp, at);
            if (d < hitR + 10) dng += (1.0f - d/(hitR+10)) * 6.0f * (1.0f - t/(ahead+0.001f));
        }
    }
    for (int i = 0; i < MAX_ENEMIES; i++) {
        Enemy *e = &w->enemies.items[i]; if (!e->active) continue;
        // Project the enemy forward like bullets, so the bot flees a lunging/swooping/diving
        // charge BEFORE it lands (it was blind to fast enemy movement before — only saw the
        // current position). Sample its path over the lookahead; nearest approach = danger.
        float reach = e->radius + p->radius + 30;
        float best = 1e9f;
        for (float t = 0; t <= ahead; t += ahead/4.0f) {
            Vector2 ep = { e->pos.x + e->vel.x*t, e->pos.y + e->vel.y*t };
            float d = Vector2Distance(ep, at);
            if (d < best) best = d;
        }
        if (best < reach) dng += (1.0f - best/reach) * 4.5f;
    }
    if (w->boss.active) {
        for (int pi = 0; pi < (w->boss.partCount?w->boss.partCount:1); pi++) {
            Vector2 bp; float br;
            if (w->boss.partCount==0) { bp=w->boss.pos; br=w->boss.radius; }
            else { if(!w->boss.parts[pi].alive)continue; bp=w->boss.parts[pi].pos; br=w->boss.parts[pi].radius; }
            float d=Vector2Distance(bp,at), reach=br+p->radius+30;
            if (d<reach) dng += (1.0f - d/reach)*5.0f;
        }
    }
    // edge penalty (corners trap you)
    float m = 46;
    if (at.x < m) dng += (m-at.x)/m * 2.0f;
    if (at.x > SCREEN-m) dng += (at.x-(SCREEN-m))/m * 2.0f;
    if (at.y < m) dng += (m-at.y)/m * 2.0f;
    if (at.y > SCREEN-m) dng += (at.y-(SCREEN-m))/m * 2.0f;
    return dng;
}

// SAFETY-SAMPLING movement: sample candidate destinations (8 dirs at two ranges + stay put),
// score each by predicted danger; go toward the safest. When everything's safe, drift toward
// the nearest gem (greed) / lower-centre home. This threads bullets far better than a raw field.
static Vector2 bot_move(GameWorld *w) {
    Player *p = &w->player;
    float ahead = 0.35f;                 // look ~0.35s forward
    float here = danger_at(w, p->pos, ahead);

    Vector2 best = (Vector2){0,0}; float bestDng = here; bool moved = false;
    const float reach1 = 34, reach2 = 70;
    for (int k = 0; k < 8; k++) {
        float a = k * (2*PI/8);
        Vector2 dir = { cosf(a), sinf(a) };
        for (int r = 0; r < 2; r++) {
            float rr = r ? reach2 : reach1;
            Vector2 cand = { p->pos.x + dir.x*rr, p->pos.y + dir.y*rr };
            if (cand.x<8||cand.x>SCREEN-8||cand.y<8||cand.y>SCREEN-8) continue;
            float dng = danger_at(w, cand, ahead);
            if (dng < bestDng - 0.05f) { bestDng = dng; best = dir; moved = true; }
        }
    }

    if (moved) return Vector2Normalize(best);   // flee toward the safest sampled spot

    // safe enough → pursue value: nearest gem, else hold a good lower-centre position
    float bestD = 1e18f; Vector2 gp = {0,0}; bool found=false;
    for (int i = 0; i < MAX_GEMS; i++) { Gem *g=&w->gems.items[i]; if(!g->active)continue;
        float d=Vector2DistanceSqr(p->pos,g->pos); if(d<bestD){bestD=d;gp=g->pos;found=true;} }
    Vector2 target = found ? gp : (Vector2){ SCREEN/2.0f, SCREEN*0.72f };
    Vector2 to = Vector2Subtract(target, p->pos);
    if (Vector2LengthSqr(to) < 64) return (Vector2){0,0};
    return Vector2Normalize(to);
}

// Dodge as a last resort: only when the current spot is genuinely dangerous AND no short
// step escapes it (so the i-frames are the real out, not a wasted roll). Uses danger_at so it
// agrees with the movement model.
static bool bot_dodge(GameWorld *w) {
    Player *p = &w->player;
    if (p->rollCooldown > 0 || p->rollTimer > 0) return false;
    float here = danger_at(w, p->pos, 0.18f);
    if (here < 3.0f) return false;                  // not in real danger — save the dodge
    // can we just step to safety? sample a ring; if any spot is much safer, walk instead.
    for (int k = 0; k < 8; k++) {
        float a = k*(2*PI/8); Vector2 c = { p->pos.x+cosf(a)*40, p->pos.y+sinf(a)*40 };
        if (c.x<8||c.x>SCREEN-8||c.y<8||c.y>SCREEN-8) continue;
        if (danger_at(w, c, 0.18f) < here*0.4f) return false;   // an escape exists — take it by moving
    }
    return true;   // boxed in → roll for the i-frames
}

// ---- HUMAN passthrough / BOT dispatch ----
Vector2 pilot_move(GameWorld *w) {
    return (w->pilot == PILOT_HUMAN) ? input_move() : bot_move(w);
}
bool pilot_dodge(GameWorld *w) {
    return (w->pilot == PILOT_HUMAN) ? input_dodge_pressed() : bot_dodge(w);
}
// the bot keeps auto-fire on and never charges, so balance sims match the old DPS profile
bool pilot_fire_down(GameWorld *w) {
    return (w->pilot == PILOT_HUMAN) && input_fire_down();
}
bool pilot_autofire_toggle(GameWorld *w) {
    return (w->pilot == PILOT_HUMAN) && input_autofire_toggle_pressed();
}
Vector2 pilot_aim(GameWorld *w, Vector2 from) {
    if (w->pilot == PILOT_HUMAN) return input_aim_dir(from);
    int e = enemies_nearest(w, from, 1e9f, -1);
    if (e >= 0) return Vector2Normalize(Vector2Subtract(w->enemies.items[e].pos, from));
    return (Vector2){ 0, -1 };
}

// ---- menu decisions ----
// Card priority: complete a trifecta > new weapon > damage/rate passive > rune for an open
// socket > anything. Returns a card index 0-2, or -1 for HUMAN (wait for their input).

// ---- trifecta build planning (bot) ----
// counts[] of the runes on a weapon IF `rune` were added (rune<0 = as-is)
static void rune_counts_with(const Weapon *wp, int rune, int out[RUNE_COUNT]) {
    for (int i = 0; i < RUNE_COUNT; i++) out[i] = 0;
    for (int i = 0; i < SOCKETS; i++) if (wp->runes[i] != RUNE_NONE) out[wp->runes[i]]++;
    if (rune >= 0) out[rune]++;
}
// is this partial still completable into SOME trifecta? (subset of its counts)
static bool completable(const int c[RUNE_COUNT]) {
    for (int t = 0; t < TRI_COUNT; t++) {
        bool ok = true;
        for (int r = 0; r < RUNE_COUNT; r++) if (c[r] > TRIFECTAS[t].counts[r]) { ok = false; break; }
        if (ok) return true;
    }
    return false;
}
// how many runes are already socketed on this weapon
static int socketed(const Weapon *wp) {
    int n = 0; for (int i = 0; i < SOCKETS; i++) if (wp->runes[i] != RUNE_NONE) n++; return n;
}
int pilot_pick_card(GameWorld *w) {
    if (w->pilot == PILOT_HUMAN) return -1;
    int best = 0, bestScore = -1;
    for (int i = 0; i < 3; i++) {
        Card *c = &g_cards[i]; int sc = 1;
        switch (c->kind) {
            case CARD_RUNE: {
                // rune value scales with trifecta PROGRESS: starting a socket line is meh,
                // continuing one is good, completing one is the best pick in the game
                int prog = 0;
                for (int i = 0; i < w->loadout.count; i++) {
                    int rc = 0;
                    for (int s = 0; s < SOCKETS; s++) if (w->loadout.items[i].runes[s] != RUNE_NONE) rc++;
                    if (rc < SOCKETS && rc > prog) prog = rc;
                }
                for (int d = 0; d < w->drones.count; d++) {
                    int rc = 0;
                    for (int s = 0; s < SOCKETS; s++) if (w->drones.items[d].weapon.runes[s] != RUNE_NONE) rc++;
                    if (rc < SOCKETS && rc > prog) prog = rc;
                }
                // base value rises once the arsenal exists: weapons first, then BUILD
                int base = (w->loadout.count >= 3) ? 7 : 4;
                sc = base;
                for (int wp = 0; wp < placement_count(w); wp++) {
                    int s = rune_first_open_socket(w, wp);
                    if (s < 0) continue;
                    if (rune_would_evolve(w, wp, s, c->value) >= 0) { sc = 14; break; }
                    Weapon *W2 = placement_weapon(w, wp);
                    int cc[RUNE_COUNT]; rune_counts_with(W2, c->value, cc);
                    int n = socketed(W2);
                    if (n > 0 && completable(cc)) { int v = base + (n+1)*3; if (v > sc) sc = v; }
                }
                (void)prog;
                break;
            }
            case CARD_NEW_WEAPON:   sc = 6; break;   // more weapons = more DPS coverage
            case CARD_DRONE:        sc = 6; break;
            case CARD_LEVEL_WEAPON: sc = 5; break;
            case CARD_PASSIVE:      sc = 3; break;
        }
        if (sc > bestScore) { bestScore = sc; best = i; }
        if (getenv("AB_DEBUG_CARDS")) fprintf(stderr, "card %d kind=%d val=%d sc=%d\n", i, c->kind, c->value, sc);
    }
    if (getenv("AB_DEBUG_CARDS")) fprintf(stderr, "-> pick %d\n", best);
    return best;
}

// Socket target: the weapon/drone the placed rune would EVOLVE, else the first with an open
// socket. Returns a placement index, or -1 for HUMAN.
int pilot_pick_socket(GameWorld *w, int placeCount) {
    if (w->pilot == PILOT_HUMAN) return -1;
    int rune = w->cardsel.placingRune;
    int firstOpen = -1, bestExtend = -1, bestExtendN = -1, freshLine = -1;
    for (int i = 0; i < placeCount; i++) {
        int s = rune_first_open_socket(w, i);
        if (s < 0) continue;
        if (firstOpen < 0) firstOpen = i;
        if (rune_would_evolve(w, i, s, rune) >= 0) return i;   // completes a trifecta: take it
        Weapon *wp = placement_weapon(w, i);
        int c[RUNE_COUNT]; rune_counts_with(wp, rune, c);
        int n = socketed(wp);
        if (completable(c)) {                                   // keeps a build alive
            if (n > 0 && n > bestExtendN) { bestExtend = i; bestExtendN = n; }
            else if (n == 0 && freshLine < 0) freshLine = i;
        }
    }
    if (bestExtend >= 0) return bestExtend;    // extend the most-advanced live build
    if (freshLine >= 0) return freshLine;      // else start a clean line
    return firstOpen >= 0 ? firstOpen : 0;     // else dump it (no live line exists)
}

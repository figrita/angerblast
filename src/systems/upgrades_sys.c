// upgrades_sys.c - XP curve, level-up card generation, the rune loadout, and trifectas.
#include "systems/upgrades_sys.h"
#include "platform/audio.h"
#include "platform/input.h"
#include "data/balance.h"
#include "data/runes.h"
#include "data/weapons_def.h"
#include "data/trifectas.h"
#include "data/evolutions.h"
#include "systems/drones.h"
#include <math.h>
#include <stddef.h>   // NULL

Card g_cards[3];

double xp_for_level(int level) { return XP_BASE * pow((double)level, XP_EXPONENT); }

// ---- loadout queries ----
static int weapon_slot(GameWorld *w, int type) {        // index of owned weapon `type`, or -1
    for (int i = 0; i < w->loadout.count; i++) if (w->loadout.items[i].type == type) return i;
    return -1;
}
static bool loadout_has_room(GameWorld *w) { return w->loadout.count < MAX_WEAPONS; }
static int  weapon_open_socket(const Weapon *wp) {       // first empty socket, or -1
    for (int i = 0; i < SOCKETS; i++) if (wp->runes[i] == RUNE_NONE) return i;
    return -1;
}
static bool loadout_has_open_socket(GameWorld *w) {
    for (int i = 0; i < w->loadout.count; i++) if (weapon_open_socket(&w->loadout.items[i]) >= 0) return true;
    for (int i = 0; i < w->drones.count; i++) if (weapon_open_socket(&w->drones.items[i].weapon) >= 0) return true;
    return false;
}

// ---- granting ----
void weapon_grant(GameWorld *w, int type) {
    if (!loadout_has_room(w)) return;
    Weapon *wp = &w->loadout.items[w->loadout.count++];
    wp->type = type; wp->level = 1; wp->evolution = -1; wp->timer = frandr(w, 0, 0.3f);
    for (int i = 0; i < SOCKETS; i++) wp->runes[i] = RUNE_NONE;
}

// ---- card generation ----
void cards_roll(GameWorld *w) {
    // Build a weighted bag of candidate cards, then pick 3 distinct-ish.
    Card pool[64]; int n = 0;

    // runes: always offerable if any weapon has an open socket. The pull gets STRONGER
    // as a weapon nears its trifecta (2 runes socketed = 1 away from evolving), so
    // evolutions actually happen in a run instead of being a theoretical endgame.
    if (loadout_has_open_socket(w)) {
        int best = 0;
        for (int i = 0; i < w->loadout.count; i++) {
            int rc = 0;
            for (int s = 0; s < SOCKETS; s++) if (w->loadout.items[i].runes[s] != RUNE_NONE) rc++;
            if (rc > best && rc < SOCKETS) best = rc;
        }
        int copies = 1 + best;   // 0 runes -> 1x, 1 -> 2x, 2 (one away!) -> 3x
        for (int k = 0; k < copies && n + RUNE_COUNT < 60; k++)
            for (int r = 0; r < RUNE_COUNT; r++) { pool[n].kind = CARD_RUNE; pool[n].value = r; n++; }
    }

    // new weapons: any not-yet-owned weapon, if there's room
    if (loadout_has_room(w))
        for (int t = 0; t < WP_COUNT; t++) if (weapon_slot(w, t) < 0) { pool[n].kind = CARD_NEW_WEAPON; pool[n].value = t; n++; }

    // weapon level-ups: owned weapons
    for (int i = 0; i < w->loadout.count; i++) { pool[n].kind = CARD_LEVEL_WEAPON; pool[n].value = w->loadout.items[i].type; n++; }

    // passives
    for (int pid = 0; pid < PASV_COUNT; pid++) { pool[n].kind = CARD_PASSIVE; pool[n].value = pid; n++; }

    // drones: offer each AI type if the drone bay has room (one per AI is plenty of variety)
    if (w->drones.count < MAX_DRONES)
        for (int ai = 0; ai < DRONE_AI_COUNT; ai++) { pool[n].kind = CARD_DRONE; pool[n].value = ai; n++; }

    // pick 3 distinct entries from the pool
    bool used[64] = { 0 };
    for (int c = 0; c < 3; c++) {
        int pick = -1, guard = 0;
        if (n > 0) do { pick = irand(w, 0, n-1); guard++; } while (used[pick] && guard < 128);
        if (pick < 0) { g_cards[c] = (Card){ CARD_PASSIVE, irand(w, 0, PASV_COUNT-1) }; continue; }
        used[pick] = true;
        g_cards[c] = pool[pick];
        w->cardsel.cards[c] = c;   // index handle (g_cards is the real data)
    }
    w->cardsel.hover = 0;
    w->cardsel.phase = PICK_CARD;
    w->cardsel.placingRune = RUNE_NONE;
}

void xp_gain(GameWorld *w, double v) {
    Player *p = &w->player;
    w->analytics.xpEarned += v * p->xpMul;
    p->xp += v * p->xpMul;
    while (p->xp >= p->xpToNext) {
        p->xp -= p->xpToNext; p->level++; w->analytics.levelUps++;
        p->xpToNext = xp_for_level(p->level); w->cardsel.pending++;
    }
    if (w->cardsel.pending > 0 && w->state == STATE_PLAY) {
        cards_roll(w); audio_play(&w->audio, SFX_LEVELUP); input_rumble(0.2f,0.3f,0.2f);
        w->state = STATE_LEVELUP;
    }
}

// ---- applying a passive ----
static void passive_apply(GameWorld *w, PassiveId id) {
    Player *p = &w->player;
    switch (id) {
        case PASV_RATE: p->globalRateMul *= 0.85f; break;
        case PASV_DAMAGE: p->globalDamageMul *= 1.25f; break;
        case PASV_SPREAD: p->spreadLevel++; break;
        case PASV_BULLETSPEED: p->bulletSpeed *= 1.2f; break;
        case PASV_MOVESPEED: p->moveSpeed *= 1.12f; break;
        case PASV_MAXSHIELD: p->shieldMax += 25; p->shield = p->shieldMax; break;
        case PASV_MAXHP: p->hpMax++; p->hp++; break;
        case PASV_REGEN: p->shieldRegenDelay *= 0.7f; p->shieldRegenRate *= 1.4f; break;
        case PASV_MAGNET: p->pickupRadius *= 1.4f; break;
        case PASV_GREED: p->xpMul *= 1.15f; break;
        case PASV_AEGIS: p->aegis = true; break;
        case PASV_ARMOR: p->armor += 20; break;   // DR = armor/(armor+armor_k), diminishing
        default: break;
    }
}

bool card_choose(GameWorld *w, int cardIndex) {
    Card *c = &g_cards[cardIndex];
    switch (c->kind) {
        case CARD_RUNE:
            // enter the placement sub-step
            w->cardsel.phase = PICK_SOCKET;
            w->cardsel.placingRune = c->value;
            w->cardsel.hover = 0;          // weapon cursor
            return true;
        case CARD_NEW_WEAPON:   weapon_grant(w, c->value); return false;
        case CARD_LEVEL_WEAPON: { int s = weapon_slot(w, c->value); if (s >= 0) w->loadout.items[s].level++; return false; }
        case CARD_PASSIVE:      passive_apply(w, c->value); return false;
        case CARD_DRONE:        drone_grant(w, c->value, drone_default_weapon(c->value)); return false;
    }
    return false;
}

// ---- trifecta detection helper ----
static void weapon_check_trifecta(Weapon *wp, GameWorld *w) {
    int evo = trifecta_match(wp->runes);
    if (evo >= 0 && wp->evolution != evo) {
        wp->evolution = evo;
        w->analytics.evolutionsFormed++;
        const Evolution *ev = &EVOLUTIONS[wp->type][evo];
        // fanfare + on-screen announcement banner
        audio_play(&w->audio, SFX_BOSS_DIE);
        input_rumble(0.6f, 0.8f, 0.4f);
        w->shakeAmount = fmaxf(w->shakeAmount, 14);
        w->evoBannerTimer = 3.5f;
        w->evoBannerName = ev->name;
        w->evoBannerDesc = ev->desc;
    }
}

// Rune placement addresses a COMBINED list: loadout weapons first, then drone weapons. This
// lets a rune socket into a drone's own gun via the same place-rune flow.
int placement_count(GameWorld *w) { return w->loadout.count + w->drones.count; }

Weapon *placement_weapon(GameWorld *w, int idx) {
    if (idx < 0) return NULL;
    if (idx < w->loadout.count) return &w->loadout.items[idx];
    int di = idx - w->loadout.count;
    if (di < w->drones.count) return &w->drones.items[di].weapon;
    return NULL;
}

int rune_first_open_socket(GameWorld *w, int weaponIndex) {
    Weapon *wp = placement_weapon(w, weaponIndex);
    return wp ? weapon_open_socket(wp) : -1;
}

void rune_place(GameWorld *w, int weaponIndex, int socketIndex) {
    Weapon *wp = placement_weapon(w, weaponIndex);
    if (!wp) return;
    if (socketIndex < 0 || socketIndex >= SOCKETS) return;
    if (wp->runes[socketIndex] != RUNE_NONE) return;   // final — no overwrite
    wp->runes[socketIndex] = w->cardsel.placingRune;
    if (w->cardsel.placingRune >= 0 && w->cardsel.placingRune < RUNE_COUNT)
        w->analytics.runesPlaced[w->cardsel.placingRune]++;
    weapon_check_trifecta(wp, w);
}

// Would placing `rune` into this socket complete a trifecta? returns TrifectaId or -1.
int rune_would_evolve(GameWorld *w, int weaponIndex, int socketIndex, int rune) {
    Weapon *wp = placement_weapon(w, weaponIndex);
    if (!wp) return -1;
    if (socketIndex < 0 || socketIndex >= SOCKETS || wp->runes[socketIndex] != RUNE_NONE) return -1;
    int test[SOCKETS];
    for (int i = 0; i < SOCKETS; i++) test[i] = wp->runes[i];
    test[socketIndex] = rune;
    return trifecta_match(test);
}

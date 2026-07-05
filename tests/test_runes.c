// Standalone logic tests for the rune/trifecta/weapon-stats systems. Links against the
// game's object files (minus game.o). Run via `make test` (see Makefile).
#include "world.h"
#include "systems/upgrades_sys.h"
#include "systems/weapons.h"
#include "systems/enemies.h"
#include "systems/gems.h"
#include "systems/bullets.h"
#include "systems/drones.h"
#include "systems/boss.h"
#include "systems/spawn.h"
#include "data/trifectas.h"
#include "data/runes.h"
#include "data/weapons_def.h"
#include "data/biome.h"
#include "data/emitter.h"
#include "data/loader.h"
#include <stdio.h>
#include <string.h>
#include <stdlib.h>

static int fails = 0;
#define CHECK(cond, msg) do { if (!(cond)) { printf("FAIL: %s\n", msg); fails++; } else printf("ok: %s\n", msg); } while (0)

int main(void) {
    // data must be loaded before anything reads BAL / the runtime tables
    if (!loader_load_all("assets")) { printf("FAIL: could not load assets/\n"); return 1; }

    // --- composable emitter graph loads + resolves child refs by name ---
    {
        extern Emitter EMITTERS[]; extern int NUM_EMITTERS;
        CHECK(NUM_EMITTERS >= 6, "emitters.toml loaded the emitter table");
        int splitter = emitter_id("splitter"), ring = emitter_id("burst_ring");
        CHECK(splitter >= 0 && ring >= 0, "named emitters resolve");
        CHECK(splitter >= 0 && EMITTERS[splitter].child == ring,
              "splitter's child ref resolved to burst_ring");
        CHECK(splitter >= 0 && EMITTERS[splitter].childMode == CHILD_ON_DEATH,
              "splitter child fires on death");
    }

    // --- elite (miniboss) enemies load with the flag + a composable emitter ---
    {
        extern Biome BIOMES[]; extern int NUM_BIOMES;
        const EnemyType *warden = NULL;
        for (int b = 0; b < NUM_BIOMES; b++)
            for (int i = 0; i < BIOMES[b].rosterCount; i++)
                if (BIOMES[b].roster[i].name && strcmp(BIOMES[b].roster[i].name, "Rift Warden") == 0)
                    warden = &BIOMES[b].roster[i];
        CHECK(warden != NULL, "Rift Warden elite loaded");
        if (warden) { CHECK(warden->elite, "Rift Warden flagged elite");
                      CHECK(warden->emitter >= 0, "Rift Warden uses a composable emitter"); }
    }

    // --- data-driven enemy move/fire params load correctly (Step 3) ---
    // The roster view points at biome 0's enemies after director_reset; but the test
    // doesn't enter a biome, so reach into BIOMES directly. Find the Spinner (ring fire).
    {
        extern Biome BIOMES[]; extern int NUM_BIOMES;
        const EnemyType *spinner = NULL, *weaver = NULL;
        for (int b = 0; b < NUM_BIOMES; b++)
            for (int i = 0; i < BIOMES[b].rosterCount; i++) {
                const EnemyType *et = &BIOMES[b].roster[i];
                if (et->name && strcmp(et->name, "Whirling Idol") == 0) spinner = et;
                if (et->name && strcmp(et->name, "Threadwraith")  == 0) weaver  = et;
            }
        CHECK(spinner != NULL, "Whirling Idol (spinner) enemy loaded from data");
        if (spinner) {
            CHECK(spinner->fire == FR_RADIAL, "Whirling Idol fire op is radial (ring collapsed)");
            CHECK(spinner->firep.count == 8 && spinner->firep.countPerStage == 0,
                  "Whirling Idol ring params: count 8 +0/stage");
            CHECK(spinner->firep.spin > 0.39f && spinner->firep.spin < 0.41f,
                  "Whirling Idol ring spin 0.4");
        }
        if (weaver) CHECK(weaver->movep.p[2] > 2.69f && weaver->movep.p[2] < 2.71f,
                          "Threadwraith weave wave_amp folds to 2.7");
    }

    // --- weapon emitters load from data (Step 4) ---
    CHECK(WEAPONS_DEF[WP_MAINGUN].emit == EMIT_STRAIGHT, "Main Cannon emit=straight");
    CHECK(WEAPONS_DEF[WP_ION].ep.pierce == 999, "Ion forced pierce 999 from data");
    CHECK(WEAPONS_DEF[WP_SIDEGUN].emit == EMIT_FIXED_DIRS &&
          WEAPONS_DEF[WP_SIDEGUN].ep.dirCount == 2, "Side Guns fixed_dirs with 2 dirs");
    CHECK(WEAPONS_DEF[WP_LIGHTNING].emit == EMIT_CHAIN &&
          WEAPONS_DEF[WP_LIGHTNING].ep.targets == 2, "Chain Bolt chain, targets 2");
    CHECK(WEAPONS_DEF[WP_GOO].emit == EMIT_LOB, "Flammable Goo emit=lob");

    // --- formations load from data (Step 5) ---
    {
        extern FormationDef FORMATIONS[];
        CHECK(FORMATIONS[0].p[0] == 10 && FORMATIONS[0].p[2] == 18, "row formation: count 10, cap 18");
        CHECK(FORMATIONS[4].p[1] == 3, "swarm formation: +3 per stage");
    }

    // --- trifecta matching (order independence) ---
    int rrr[3] = { RUNE_RED, RUNE_RED, RUNE_RED };
    CHECK(trifecta_match(rrr) == TRI_INFERNO, "RRR -> Inferno");
    int rgb[3] = { RUNE_BLUE, RUNE_RED, RUNE_GREEN };       // shuffled
    CHECK(trifecta_match(rgb) == TRI_PRISM, "BRG (any order) -> Prism");
    int rry[3] = { RUNE_YELLOW, RUNE_RED, RUNE_RED };
    CHECK(trifecta_match(rry) == TRI_NAPALM, "YRR -> Napalm");
    int partial[3] = { RUNE_RED, RUNE_RED, RUNE_NONE };
    CHECK(trifecta_match(partial) == -1, "incomplete -> no match");
    int nogood[3] = { RUNE_RED, RUNE_BLUE, RUNE_YELLOW };   // not a named trifecta
    CHECK(trifecta_match(nogood) == -1, "RBY -> no trifecta");

    // --- weapon_stats folds runes ---
    Weapon wp; memset(&wp, 0, sizeof(wp));
    wp.type = WP_SIDEGUN; wp.level = 1; wp.evolution = -1;
    wp.runes[0] = RUNE_RED; wp.runes[1] = RUNE_RED; wp.runes[2] = RUNE_NONE;
    WeaponStats s = weapon_stats(&wp);
    CHECK(s.damageMul > 1.49f && s.damageMul < 1.51f, "two reds -> +50% damage");
    wp.runes[2] = RUNE_PURPLE;   // RRP = Void trifecta? no: Void is PPR. RRP is not named.
    CHECK(trifecta_match(wp.runes) == -1, "RRP not a trifecta");
    s = weapon_stats(&wp);
    CHECK(s.pierce == 1, "one purple -> +1 pierce");

    // --- rune placement detects evolution through the public API ---
    GameWorld *w = calloc(1, sizeof(GameWorld));
    w->loadout.count = 1;
    Weapon *lw = &w->loadout.items[0];
    lw->type = WP_ION; lw->level = 1; lw->evolution = -1;
    for (int i = 0; i < SOCKETS; i++) lw->runes[i] = RUNE_NONE;

    // would-evolve hint before placing the completing rune
    lw->runes[0] = RUNE_RED; lw->runes[1] = RUNE_RED;
    int hint = rune_would_evolve(w, 0, 2, RUNE_RED);
    CHECK(hint == TRI_INFERNO, "glow hint: third red completes Inferno");

    w->cardsel.placingRune = RUNE_RED;
    rune_place(w, 0, 2);
    CHECK(lw->runes[2] == RUNE_RED, "rune placed into socket 2");
    CHECK(lw->evolution == TRI_INFERNO, "Ion + RRR evolved to Inferno");

    // placement is final: cannot overwrite a filled socket
    w->cardsel.placingRune = RUNE_BLUE;
    rune_place(w, 0, 2);
    CHECK(lw->runes[2] == RUNE_RED, "filled socket is final (no overwrite)");

    // evolved weapon has the evolution multipliers
    s = weapon_stats(lw);
    CHECK(s.evolved, "evolved flag set");

    // --- status effects ---
    Enemy te; memset(&te, 0, sizeof(te)); te.active = true; te.hp = te.maxHp = 100;
    // Slow: STACK_MAX_DURATION keeps the stronger magnitude
    enemies_apply_effect(w, &te, EFFECT_SLOW, 2.0f, 0.4f);
    enemies_apply_effect(w, &te, EFFECT_SLOW, 1.0f, 0.6f);
    CHECK(te.effMag[EFFECT_SLOW] > 0.59f, "Slow keeps stronger magnitude (0.6)");
    CHECK(te.effTimer[EFFECT_SLOW] > 1.9f, "Slow keeps longer duration (2.0)");
    // Weaken amplifies incoming damage
    enemies_apply_effect(w, &te, EFFECT_WEAKEN, 3.0f, 0.5f);
    CHECK(enemy_incoming(&te, 10.0f) > 14.9f, "Weaken: 10 dmg -> 15");
    // Poison stacks (STACK_ADD) and caps at maxStacks
    Enemy pe; memset(&pe, 0, sizeof(pe)); pe.active = true; pe.hp = pe.maxHp = 100;
    for (int i = 0; i < 10; i++) enemies_apply_effect(w, &pe, EFFECT_POISON, 4.0f, 2.0f);
    CHECK(pe.effStacks[EFFECT_POISON] == 5, "Poison caps at 5 stacks");
    // Burn DoT reduces hp over time (apply, then a fresh tick is internal; check timer set)
    enemies_apply_effect(w, &te, EFFECT_BURN, 3.0f, 20.0f);
    CHECK(te.effTimer[EFFECT_BURN] > 2.9f && te.effMag[EFFECT_BURN] == 20.0f, "Burn applied (dps=20)");
    // gems_magnetize_near flips only in-radius gems
    w->gems.items[0] = (Gem){ .pos={100,100}, .active=true };
    w->gems.items[1] = (Gem){ .pos={600,600}, .active=true };
    gems_magnetize_near(w, (Vector2){100,100}, 80);
    CHECK(w->gems.items[0].magnetized && !w->gems.items[1].magnetized, "magnetize_near: only nearby gem flagged");
    // a FAR magnetized gem must home TOWARD the player, never fling away (the offscreen bug).
    // Player far from the gem; magnetize a distant gem (as the whole-screen Magnet pickup does).
    for (int gi=0; gi<MAX_GEMS; gi++) w->gems.items[gi].active = false;
    w->player.pos = (Vector2){ 360, 360 }; w->player.pickupRadius = 90; w->player.radius = 11;
    w->gems.items[0] = (Gem){ .pos={ 60, 60 }, .active=true, .magnetized=true };  // ~424px away
    double d0 = Vector2Distance(w->gems.items[0].pos, w->player.pos);
    for (int s=0;s<5;s++) gems_update(w, 1.0f/60.0f);   // a few frames of homing
    double d1 = Vector2Distance(w->gems.items[0].pos, w->player.pos);
    CHECK(d1 < d0, "far magnetized gem moves TOWARD player (not flung offscreen)");
    // NAPALM evolution carries Burn on-hit (data path)
    Weapon nw; memset(&nw, 0, sizeof(nw)); nw.type = WP_FLAME; nw.level = 1; nw.evolution = TRI_NAPALM;
    WeaponStats ns = weapon_stats(&nw);
    CHECK(ns.onHitEffect == EFFECT_BURN, "NAPALM evolution stamps Burn on-hit");

    // --- flammable goo: burn on a gooed enemy is doubled ---
    Enemy ge; memset(&ge, 0, sizeof(ge)); ge.active = true; ge.hp = ge.maxHp = 100;
    enemies_apply_effect(w, &ge, EFFECT_BURN, 3.0f, 10.0f);   // not gooed -> dps stays 10
    CHECK(ge.effMag[EFFECT_BURN] == 10.0f, "burn on clean enemy: dps 10");
    Enemy ge2; memset(&ge2, 0, sizeof(ge2)); ge2.active = true; ge2.hp = ge2.maxHp = 100;
    enemies_apply_effect(w, &ge2, EFFECT_GOO, 5.0f, 0);       // coat it first
    enemies_apply_effect(w, &ge2, EFFECT_BURN, 3.0f, 10.0f);  // ignite -> 2x
    CHECK(ge2.effMag[EFFECT_BURN] == 20.0f, "burn on GOOED enemy: dps doubled to 20");
    // gooed enemies take +25% from everything
    CHECK(enemy_incoming(&ge2, 100.0f) > 124.9f, "gooed enemy takes +25% incoming");

    // --- Aegis: a friendly bullet with destroysBullets shoots down an overlapping enemy bullet ---
    {
        GameWorld *aw = calloc(1, sizeof(GameWorld));
        aw->player.pos = (Vector2){10000,10000};   // keep the player far from the test bullets
        aw->player.radius = 11;
        // enemy bullet at (300,300)
        bullets_add(aw, (Vector2){300,300}, (Vector2){0,10}, 5, 1, (Color){255,0,0,255}, false, WK_ENEMY, 0, false);
        // friendly aegis bullet overlapping it
        aw->bulletDestroysBullets = true;
        bullets_add(aw, (Vector2){300,300}, (Vector2){0,-10}, 5, 5, (Color){0,255,255,255}, true, WK_MAIN, 0, false);
        aw->bulletDestroysBullets = false;
        int enemyBullets = 0;
        for (int i=0;i<MAX_BULLETS;i++) if (aw->bullets.items[i].active && !aw->bullets.items[i].friendly) enemyBullets++;
        CHECK(enemyBullets == 1, "enemy bullet present before Aegis tick");
        bullets_update(aw, 1.0f/60.0f);
        enemyBullets = 0;
        for (int i=0;i<MAX_BULLETS;i++) if (aw->bullets.items[i].active && !aw->bullets.items[i].friendly) enemyBullets++;
        CHECK(enemyBullets == 0, "Aegis shot destroyed the enemy bullet");
        free(aw);
    }

    // --- drone grant: a drone carries its own socketable weapon ---
    {
        GameWorld *dw = calloc(1, sizeof(GameWorld));
        drone_grant(dw, DRONE_FOLLOW, drone_default_weapon(DRONE_FOLLOW));
        CHECK(dw->drones.count == 1, "drone granted");
        CHECK(dw->drones.items[0].weapon.type == WP_AUTOAIM, "follow drone defaults to autoaim gun");
        // placement list includes the drone's weapon after the loadout
        dw->loadout.count = 1; dw->loadout.items[0].type = WP_MAINGUN;
        CHECK(placement_count(dw) == 2, "placement list = 1 weapon + 1 drone");
        CHECK(placement_weapon(dw, 1) == &dw->drones.items[0].weapon, "index 1 addresses the drone weapon");
        free(dw);
    }

    // --- biome loop: beating the last biome wraps back to 0 and bumps loopCount, instead
    // of clamping forever on the last biome ---
    {
        extern int NUM_BIOMES;
        GameWorld *lw = calloc(1, sizeof(GameWorld));
        lw->stage = 1;
        director_reset(lw);   // biome_enter(lw, 0)
        CHECK(lw->biome == 0 && lw->loopCount == 0, "fresh run starts at biome 0, loop 0");
        for (int i = 0; i < NUM_BIOMES; i++) {
            boss_spawn(lw);
            lw->boss.hp = 0;              // force an instant kill: single-body path
            for (int pi = 0; pi < lw->boss.partCount; pi++)
                if (lw->boss.parts[pi].isHead) lw->boss.parts[pi].alive = false;  // ...and multi-part path
            boss_update(lw, 1.0f/60.0f);  // boss_die() fires, advances biome
        }
        CHECK(lw->biome == 0, "after NUM_BIOMES clears, biome wraps back to 0");
        CHECK(lw->loopCount == 1, "loopCount incremented on wraparound");
        CHECK(lw->stage == 1 + NUM_BIOMES, "stage keeps climbing across the loop (no reset)");
        // one more full lap should bump loopCount again
        for (int i = 0; i < NUM_BIOMES; i++) {
            boss_spawn(lw);
            lw->boss.hp = 0;
            for (int pi = 0; pi < lw->boss.partCount; pi++)
                if (lw->boss.parts[pi].isHead) lw->boss.parts[pi].alive = false;
            boss_update(lw, 1.0f/60.0f);
        }
        CHECK(lw->loopCount == 2, "loopCount increments again on a second full lap");
        free(lw);
    }

    // --- elite tier: enemies spawned as elites after a loop wraparound carry a higher
    // eliteTier than pre-loop elites, and non-elites always stay tier 0 ---
    {
        extern Biome BIOMES[];
        GameWorld *ew = calloc(1, sizeof(GameWorld));
        ew->stage = 1;
        director_reset(ew);
        int eliteType = -1, plainType = -1;
        for (int i = 0; i < BIOMES[0].rosterCount; i++) {
            if (BIOMES[0].roster[i].elite && eliteType < 0) eliteType = i;
            if (!BIOMES[0].roster[i].elite && plainType < 0) plainType = i;
        }
        CHECK(eliteType >= 0, "biome 0 has an elite type to spawn for the tier test");
        if (eliteType >= 0) {
            enemies_spawn(ew, eliteType, (Vector2){100,100}, (Vector2){100,100});
            CHECK(ew->enemies.items[0].eliteTier == 1, "pre-loop elite gets tier 1");
        }
        if (plainType >= 0) {
            enemies_spawn(ew, plainType, (Vector2){200,200}, (Vector2){200,200});
            bool foundPlainZeroTier = false;
            for (int i = 0; i < MAX_ENEMIES; i++)
                if (ew->enemies.items[i].active && ew->enemies.items[i].type == plainType
                    && ew->enemies.items[i].eliteTier == 0) foundPlainZeroTier = true;
            CHECK(foundPlainZeroTier, "non-elite always spawns at tier 0");
        }
        ew->loopCount = 2;   // pretend we're on the third pass through the biomes
        if (eliteType >= 0) {
            enemies_spawn(ew, eliteType, (Vector2){300,300}, (Vector2){300,300});
            bool foundTier3 = false;
            for (int i = 0; i < MAX_ENEMIES; i++)
                if (ew->enemies.items[i].active && ew->enemies.items[i].type == eliteType
                    && ew->enemies.items[i].pos.x == 300 && ew->enemies.items[i].eliteTier == 3)
                    foundTier3 = true;
            CHECK(foundTier3, "elite spawned after 2 loop wraps gets tier 1+loopCount = 3");
        }
        free(ew);
    }

    free(w);
    printf(fails ? "\n%d FAILURES\n" : "\nALL PASS\n", fails);
    return fails ? 1 : 0;
}

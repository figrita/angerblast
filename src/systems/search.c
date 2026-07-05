// search.c - headless batch runs (parallel via fork) + the build-tree fork search.
#include "systems/search.h"
#include "systems/player.h"
#include "systems/spawn.h"        // director_reset, biome_enter
#include "systems/boss.h"
#include "systems/weapons.h"
#include "systems/drones.h"
#include "systems/bullets.h"
#include "systems/gems.h"
#include "systems/pickups.h"
#include "systems/puddles.h"
#include "systems/enemies.h"
#include "systems/fx.h"
#include "systems/grid.h"
#include "systems/upgrades_sys.h"
#include "systems/analytics.h"
#include "systems/pilot.h"
#include "data/loader.h"
#include "data/weapons_def.h"
#include "data/biome.h"
#include "data/runes.h"
#include "data/upgrades.h"
#include "data/evolutions.h"
#include "data/trifectas.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <math.h>
#include <time.h>
#include <sys/stat.h>
#include <sys/wait.h>
#include <unistd.h>

// ---- one simulation step of the bot-driven game (no window/audio). Mirrors update_play +
//      the headless state handling, but lives here so the search can drive it directly. ----
static void sim_step(GameWorld *w, float dt) {
    if (w->state == STATE_PLAY || w->state == STATE_BOSS_INTRO) {
        w->runTime += dt;
        analytics_sample(w, dt);
        player_update_movement(w, dt);
        grid_rebuild(w);
        weapons_update(w, dt);
        drones_update(w, dt);
        puddles_update(w, dt);
        director_update(w, dt);
        enemies_update(w, dt);
        boss_update(w, dt);
        bullets_update(w, dt);
        gems_update(w, dt);
        pickups_update(w, dt);
        fx_update(w, dt);
        w->hitFlash *= 0.9f;
        if (w->evoBannerTimer > 0) w->evoBannerTimer -= dt;
        if (w->state == STATE_BOSS_INTRO) { w->bossWarnTimer -= dt;
            if (w->bossWarnTimer <= 0) { boss_spawn(w); w->state = STATE_PLAY; } }
    } else if (w->state == STATE_LEVELUP) {
        Cards *cs = &w->cardsel;
        if (cs->phase == PICK_CARD) {
            bool placing = card_choose(w, pilot_pick_card(w));
            if (!placing) { cs->pending--; if (cs->pending > 0) cards_roll(w); else w->state = STATE_PLAY; }
        } else {
            int pick = pilot_pick_socket(w, placement_count(w));
            int s = rune_first_open_socket(w, pick);
            if (s >= 0) { rune_place(w, pick, s); cs->pending--;
                if (cs->pending > 0) cards_roll(w); else w->state = STATE_PLAY; }
            else w->state = STATE_PLAY;
        }
    }
}

// reset a world for a fresh bot run at `seed` (headless: no audio bank to preserve).
static void sim_reset(GameWorld *w, unsigned long long seed) {
    memset(w, 0, sizeof(*w));
    rng_seed(&w->rng, seed ? seed : 1);
    w->pilot = PILOT_BOT;
    player_reset(w);
    director_reset(w);
    analytics_reset(w);
    w->state = STATE_PLAY;
    w->stage = 1;
}

static void fill_result(GameWorld *w, unsigned long long seed, RunResult *out) {
    Analytics *a = &w->analytics;
    memset(out, 0, sizeof(*out));
    out->seed = seed;
    out->endStage = w->stage; out->endLevel = w->player.level;
    out->furthestBiome = a->furthestBiome; out->stagesCleared = a->stagesCleared;
    out->runSeconds = (float)(w->runTime - a->startRunTime);
    out->survived = (w->state == STATE_PLAY || w->state == STATE_BOSS_INTRO) ? 1 : 0;
    out->deathUnfair = out->survived ? -1 : (a->deathWasUnfair ? 1 : 0);
    out->dpsVsHp = a->incomingHp > 0 ? (float)(a->damageDealt / a->incomingHp) : 0;
    out->threatAvg = a->threatSamples ? (float)(a->threatSum / a->threatSamples) : 0;
    snprintf(out->cause, sizeof(out->cause), "%s", a->causeOfDeath);
    for (int t=0;t<MAX_ENEMY_TYPES;t++){ out->dmgToPlayerByType[t]=a->dmgToPlayerByType[t]; out->bulletsByType[t]=a->bulletsByType[t]; }
}

void sim_run(GameWorld *w, unsigned long long seed, int maxFrames, RunResult *out) {
    sim_reset(w, seed);
    const float dt = 1.0f/60.0f;
    for (int f = 0; f < maxFrames; f++) {
        if (w->state == STATE_DEAD || w->state == STATE_SURVEY) break;
        sim_step(w, dt);
    }
    fill_result(w, seed, out);
}

// ---- aggregation: write a batch report from an array of results ----
static int cmp_int(const void *a, const void *b) { return *(const int*)a - *(const int*)b; }

static void write_batch_report(RunResult *r, int n, int maxFrames) {
    mkdir("reports", 0755);
    time_t now = time(NULL); struct tm *lt = localtime(&now);
    char stamp[32]; strftime(stamp, sizeof(stamp), "%Y%m%d-%H%M%S", lt);
    char path[256]; snprintf(path, sizeof(path), "reports/batch-%s.md", stamp);
    FILE *f = fopen(path, "w"); if (!f) { fprintf(stderr, "could not write %s\n", path); return; }

    int survived=0, unfair=0, earned=0;
    double sumStage=0, sumLevel=0, sumDps=0, sumThreat=0, sumSecs=0;
    int deathStageHist[16] = {0}, reachHist[16] = {0};
    int stages[4096]; int ns=0;
    for (int i=0;i<n;i++){ RunResult *x=&r[i];
        survived += x->survived;
        if (x->deathUnfair==1) unfair++; else if (x->deathUnfair==0) earned++;
        sumStage+=x->endStage; sumLevel+=x->endLevel; sumDps+=x->dpsVsHp; sumThreat+=x->threatAvg; sumSecs+=x->runSeconds;
        if (x->furthestBiome<16) reachHist[x->furthestBiome]++;
        if (!x->survived && x->endStage<16) deathStageHist[x->endStage]++;
        if (ns<4096) stages[ns++]=x->endStage;
    }
    qsort(stages, ns, sizeof(int), cmp_int);
    int median = ns ? stages[ns/2] : 0;
    int deaths = n - survived;

    fprintf(f, "# Angerblast Batch Report — %s\n\n", stamp);
    fprintf(f, "_%d bot games, %d-frame cap (~%.0f sim-min each). Hand to an LLM/Claude: "
               "\"tune any of the assets/*.toml data files to fix the balance issues below.\"_\n\n",
            n, maxFrames, maxFrames/3600.0);
    fprintf(f, "## Outcome distribution\n\n");
    fprintf(f, "- Survived to cap: **%d / %d (%.0f%%)**   |   died: %d\n", survived, n, 100.0*survived/n, deaths);
    fprintf(f, "- Of deaths: **%d unfair (sudden spike)**, %d earned (on the ropes)%s\n",
            unfair, earned, deaths? (unfair*100/ (deaths?deaths:1) > 30 ? "  ⚠ high unfair rate" : "") : "");
    fprintf(f, "- Avg end stage %.1f (median %d) | avg level %.1f | avg run %.0fs | avg DPS-vs-HP %.2fx | avg threat %.2f\n\n",
            sumStage/n, median, sumLevel/n, sumSecs/n, sumDps/n, sumThreat/n);

    fprintf(f, "## Furthest biome reached (run count)\n\n| biome | runs |\n|---|---|\n");
    for (int b=0;b<16;b++) if (reachHist[b]) fprintf(f, "| %d | %d |\n", b, reachHist[b]);
    fprintf(f, "\n## Death-stage histogram (the difficulty wall)\n\n| stage | deaths |\n|---|---|\n");
    for (int s=0;s<16;s++) if (deathStageHist[s]) fprintf(f, "| %d | %d |\n", s, deathStageHist[s]);
    fprintf(f, "\n");

    // ---- per-enemy THREAT (summed across all runs) — the "even deaths per enemy" view ----
    // Keyed by roster index; named from biome 0 (where most encounters happen). The GOAL is an
    // even `% dmg` column: no enemy should dominate the damage it lands on the player.
    long sumBlt[MAX_ENEMY_TYPES]={0}; long sumDmg[MAX_ENEMY_TYPES]={0}; long totDmg=0;
    for (int i=0;i<n;i++) for (int t=0;t<MAX_ENEMY_TYPES;t++){ sumBlt[t]+=r[i].bulletsByType[t]; sumDmg[t]+=r[i].dmgToPlayerByType[t]; }
    for (int t=0;t<MAX_ENEMY_TYPES;t++) totDmg+=sumDmg[t];
    fprintf(f, "## Per-enemy threat to the player (summed across runs)\n\n");
    fprintf(f, "_Goal: an EVEN `%% dmg` spread. A type hogging the share is too hard (cut fire rate / "
               "bullet count / speed); a type at ~0%% with many bullets never threatens (filler)._\n\n");
    fprintf(f, "| enemy | bullets | dmg->player | %% dmg | dmg/100shots |\n|---|---|---|---|---|\n");
    for (int t=0;t<MAX_ENEMY_TYPES;t++){
        if (sumBlt[t]==0 && sumDmg[t]==0) continue;
        const char *nm = enemy_threat_name(t);   // stable name slot (correct across biomes)
        fprintf(f, "| %s | %ld | %ld | %.0f%% | %.2f |\n", nm ? nm : "?", sumBlt[t], sumDmg[t],
                totDmg ? 100.0*sumDmg[t]/totDmg : 0, sumBlt[t] ? 100.0*sumDmg[t]/sumBlt[t] : 0);
    }
    fprintf(f, "\n");

    // reading guidance
    fprintf(f, "## Reading this\n\n");
    fprintf(f, "- **Survival ~100%%** → too easy (the skilled bot never dies); tighten enemy/boss/scaling TOML.\n");
    fprintf(f, "- **Survival ~0%% / early death wall** → too hard; ease scaling or boss HP, or check for unfair spikes.\n");
    fprintf(f, "- **High *unfair* death share** → a sudden-spike problem (telegraph bursts, soften elite/boss alpha) rather than a flat-damage one.\n");
    fprintf(f, "- **DPS-vs-HP ≫1** with high survival → weapons/evolutions outscale the roster; nerf evolution mods or rune magnitudes.\n");
    fprintf(f, "- Per-run detail is in the worst/representative single-run reports; this is the population view.\n\n");
    fclose(f);
    printf("batch: %d runs, %d survived (%.0f%%), %d unfair deaths. report: %s\n",
           n, survived, 100.0*survived/n, unfair, path);
}

// ===== build-tree fork search =====
// The world is a flat pointer-free struct, so forking a decision = memcpy. We advance to the
// next level-up, then for each offered card: copy the world, lock in that choice, and either
// recurse (fork the next level-up too, up to `depth`) or play the branch to the end. Each leaf
// records the card PATH it took and where it ended — a map of which builds win/die/are unfair.

#define FORK_MAX_PATH 8
typedef struct { char label[28]; } CardLabel;
typedef struct {
    int   nChoices;                 // how many forks deep this leaf actually went
    char  path[FORK_MAX_PATH][28];  // the card chosen at each fork
    RunResult res;
} ForkLeaf;

static ForkLeaf g_leaves[2048];
static int g_nLeaves;
static int g_forkFrames;

// human-readable label for an offered card (so the tree report names real choices)
static void card_label(const Card *c, char *out, int cap);  // fwd

// advance the world (bot-driven) until the next level-up card menu, death, or frame cap.
// returns true if it stopped AT a fresh PICK_CARD menu (a decision point), false if the run ended.
static bool advance_to_levelup(GameWorld *w, int *frameBudget) {
    const float dt = 1.0f/60.0f;
    while (*frameBudget > 0) {
        if (w->state == STATE_DEAD || w->state == STATE_SURVEY) return false;
        if (w->state == STATE_LEVELUP && w->cardsel.phase == PICK_CARD) return true;
        sim_step(w, dt);
        (*frameBudget)--;
    }
    return false;   // hit the cap (survived)
}

static void play_to_end(GameWorld *w, int frameBudget) {
    const float dt = 1.0f/60.0f;
    while (frameBudget-- > 0) {
        if (w->state == STATE_DEAD || w->state == STATE_SURVEY) break;
        sim_step(w, dt);
    }
}

static void explore(GameWorld *w, int depth, int frameBudget,
                    char path[][28], int pathLen, unsigned long long seed) {
    if (!advance_to_levelup(w, &frameBudget) || depth == 0 || pathLen >= FORK_MAX_PATH) {
        // no more forking here: play this branch out and record the leaf
        play_to_end(w, frameBudget);
        if (g_nLeaves < (int)(sizeof(g_leaves)/sizeof(g_leaves[0]))) {
            ForkLeaf *lf = &g_leaves[g_nLeaves++];
            lf->nChoices = pathLen;
            for (int i=0;i<pathLen;i++) snprintf(lf->path[i], 28, "%s", path[i]);
            fill_result(w, seed, &lf->res);
        }
        return;
    }
    // at a decision point: fork the world for each offered card
    for (int c = 0; c < 3; c++) {
        GameWorld *fork = malloc(sizeof(GameWorld));
        memcpy(fork, w, sizeof(GameWorld));          // cheap, pointer-free
        char lbl[28]; card_label(&g_cards[c], lbl, sizeof(lbl));
        // apply choice c in the fork (mirror the levelup resolution)
        Cards *cs = &fork->cardsel;
        bool placing = card_choose(fork, c);
        if (placing) {  // a rune: let the bot place it, resolving the socket sub-step
            int pick = pilot_pick_socket(fork, placement_count(fork));
            int s = rune_first_open_socket(fork, pick);
            if (s >= 0) rune_place(fork, pick, s);
        }
        cs->pending--;
        if (cs->pending > 0) cards_roll(fork); else fork->state = STATE_PLAY;
        snprintf(path[pathLen], 28, "%s", lbl);
        explore(fork, depth-1, frameBudget, path, pathLen+1, seed);
        free(fork);
    }
}

// (defined after the helpers it needs)
static void card_label(const Card *c, char *out, int cap) {
    switch (c->kind) {
        case CARD_RUNE:         snprintf(out, cap, "%s", RUNES[c->value].name); break;
        case CARD_NEW_WEAPON:   snprintf(out, cap, "+%s", WEAPONS_DEF[c->value].name); break;
        case CARD_LEVEL_WEAPON: snprintf(out, cap, "lvl %s", WEAPONS_DEF[c->value].name); break;
        case CARD_PASSIVE:      snprintf(out, cap, "%s", PASSIVES[c->value].name); break;
        case CARD_DRONE:        snprintf(out, cap, "%s", drone_ai_name(c->value)); break;
        default:                snprintf(out, cap, "?"); break;
    }
}

// ===== evolution matrix test =====
// Force-build EVERY weapon x trifecta cell and measure it against the same weapon
// unevolved: same seed, same frames, single-weapon loadout, mid-power level. Writes
// one row per cell: damage dealt, kill count, survival — so broken or non-improving
// evolutions are caught empirically, not by eyeballing 156 combos.
static void evo_run(GameWorld *w, unsigned long long seed, int wp, int tri, int frames, RunResult *out) {
    sim_reset(w, seed);
    Weapon *W = &w->loadout.items[0];
    W->type = wp; W->level = 4; W->evolution = -1;
    for (int i = 0; i < SOCKETS; i++) W->runes[i] = RUNE_NONE;
    if (tri >= 0) {
        int idx = 0;
        for (int r = 0; r < RUNE_COUNT; r++)
            for (int k = 0; k < TRIFECTAS[tri].counts[r] && idx < SOCKETS; k++) W->runes[idx++] = r;
        W->evolution = trifecta_match(W->runes);
    }
    const float dt = 1.0f/60.0f;
    for (int f = 0; f < frames; f++) {
        if (w->state == STATE_DEAD || w->state == STATE_SURVEY) break;
        sim_step(w, dt);
    }
    fill_result(w, seed, out);
}

int evo_matrix(unsigned long long seed, int maxFrames) {
    GameWorld *w = malloc(sizeof(GameWorld));
    mkdir("reports", 0755);
    char rp[256]; snprintf(rp, sizeof(rp), "reports/evomatrix-seed%llu.md", seed);
    FILE *f = fopen(rp, "w"); if (!f) { free(w); return 1; }
    fprintf(f, "# Evolution Matrix — seed %llu, %d frames per run\n\n", seed, maxFrames);
    fprintf(f, "| weapon | trifecta | evolution | dmg | dmg-vs-base | kills | stage | result |\n|---|---|---|---|---|---|---|---|\n");
    for (int wp = 0; wp < WP_COUNT; wp++) {
        RunResult base;
        evo_run(w, seed, wp, -1, maxFrames, &base);
        double baseDmg = w->analytics.damageDealt;
        int baseKills = w->kills;
        double baseSec = base.runSeconds > 1 ? base.runSeconds : 1;
        fprintf(f, "| %s | (none) | — | %.1f | 1.00x | %d | %d | %s |\n",
                WEAPONS_DEF[wp].name, baseDmg/baseSec, baseKills, base.endStage,
                base.survived ? "alive" : base.cause);
        for (int tri = 0; tri < TRI_COUNT; tri++) {
            RunResult rr;
            evo_run(w, seed, wp, tri, maxFrames, &rr);
            double dmg = w->analytics.damageDealt;
            double dps = dmg / (rr.runSeconds > 1 ? rr.runSeconds : 1);
            double baseDps = baseDmg / baseSec;
            fprintf(f, "| %s | %s | %s | %.1f | %.2fx | %d | %d | %s |\n",
                    WEAPONS_DEF[wp].name, TRIFECTAS[tri].name, EVOLUTIONS[wp][tri].name,
                    dps, baseDps > 0.1 ? dps/baseDps : 0, w->kills, rr.endStage,
                    rr.survived ? "alive" : rr.cause);
        }
        fflush(f);
    }
    fclose(f);
    printf("evo matrix: seed=%llu -> %d cells + %d baselines. report: %s\n",
           seed, WP_COUNT*TRI_COUNT, WP_COUNT, rp);
    free(w);
    return 0;
}

int fork_search(unsigned long long seed, int maxFrames, int depth) {
    g_nLeaves = 0; g_forkFrames = maxFrames;
    GameWorld *w = malloc(sizeof(GameWorld));
    sim_reset(w, seed);
    char path[FORK_MAX_PATH][28];
    explore(w, depth, maxFrames, path, 0, seed);
    free(w);

    // ---- write the build-tree report ----
    mkdir("reports", 0755);
    time_t now = time(NULL); struct tm *lt = localtime(&now);
    char stamp[32]; strftime(stamp, sizeof(stamp), "%Y%m%d-%H%M%S", lt);
    char rp[256]; snprintf(rp, sizeof(rp), "reports/forksearch-%s.md", stamp);
    FILE *f = fopen(rp, "w"); if (!f) return 1;
    fprintf(f, "# Angerblast Build-Tree Search — seed %llu, depth %d\n\n", seed, depth);
    fprintf(f, "_Forked the world at each of the first %d level-ups (3 cards each) and played every "
               "branch to the end. %d leaf builds. Shows which card PATHS are strong/weak/unfair._\n\n",
            depth, g_nLeaves);
    fprintf(f, "| build path (cards taken) | end stage | level | result | DPS-vs-HP |\n|---|---|---|---|---|\n");
    // sort leaves by end stage desc so the strongest builds surface first
    for (int i=0;i<g_nLeaves;i++) for (int j=i+1;j<g_nLeaves;j++)
        if (g_leaves[j].res.endStage > g_leaves[i].res.endStage) { ForkLeaf t=g_leaves[i]; g_leaves[i]=g_leaves[j]; g_leaves[j]=t; }
    for (int i=0;i<g_nLeaves;i++) { ForkLeaf *lf=&g_leaves[i];
        char pathStr[256]=""; for (int k=0;k<lf->nChoices;k++){ strncat(pathStr, lf->path[k], 200); if(k<lf->nChoices-1) strncat(pathStr, " > ", 200); }
        const char *result = lf->res.survived ? "survived" : (lf->res.deathUnfair? "UNFAIR death":"died");
        fprintf(f, "| %s | %d | %d | %s | %.2fx |\n", pathStr[0]?pathStr:"(none)",
                lf->res.endStage, lf->res.endLevel, result, lf->res.dpsVsHp); }
    fprintf(f, "\n## Reading this\n\n");
    fprintf(f, "- Paths clustered at high end-stage = dominant builds (candidates to nerf).\n");
    fprintf(f, "- Paths that consistently die early = weak/dead choices (buff, or they're traps).\n");
    fprintf(f, "- A healthy game has a SPREAD of viable paths and few UNFAIR deaths.\n");
    fclose(f);
    printf("fork search: seed=%llu depth=%d -> %d leaf builds. report: %s\n", seed, depth, g_nLeaves, rp);
    return 0;
}

// ---- parallel batch via fork(): each child runs a seed slice, writes results to a temp file ----
int batch_run(unsigned long long seed0, int count, int jobs, int maxFrames) {
    if (jobs < 1) jobs = 1;
    if (jobs > count) jobs = count;
    char tmpl[64]; snprintf(tmpl, sizeof(tmpl), "/tmp/ab_batch_%d", (int)getpid());

    if (jobs == 1) {
        RunResult *res = malloc(sizeof(RunResult)*count);
        GameWorld *w = malloc(sizeof(GameWorld));
        for (int i=0;i<count;i++) sim_run(w, seed0+i, maxFrames, &res[i]);
        write_batch_report(res, count, maxFrames);
        free(w); free(res); return 0;
    }

    // split [0,count) into `jobs` contiguous slices
    for (int j=0;j<jobs;j++) {
        pid_t pid = fork();
        if (pid == 0) {   // child: run its slice, write results, exit
            int lo = (long long)count*j/jobs, hi = (long long)count*(j+1)/jobs;
            GameWorld *w = malloc(sizeof(GameWorld));
            char fn[80]; snprintf(fn, sizeof(fn), "%s_%d.dat", tmpl, j);
            FILE *out = fopen(fn, "wb");
            for (int i=lo;i<hi;i++) { RunResult r; sim_run(w, seed0+i, maxFrames, &r); if(out) fwrite(&r,sizeof(r),1,out); }
            if (out) fclose(out);
            free(w);
            _exit(0);
        }
    }
    for (int j=0;j<jobs;j++) wait(NULL);   // reap children

    // parent: gather all results
    RunResult *res = malloc(sizeof(RunResult)*count); int n=0;
    for (int j=0;j<jobs;j++) {
        char fn[80]; snprintf(fn, sizeof(fn), "%s_%d.dat", tmpl, j);
        FILE *in = fopen(fn, "rb"); if (!in) continue;
        RunResult r; while (n<count && fread(&r,sizeof(r),1,in)==1) res[n++]=r;
        fclose(in); remove(fn);
    }
    write_batch_report(res, n, maxFrames);
    free(res);
    return 0;
}

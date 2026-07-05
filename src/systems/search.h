// search.h - headless batch runs + the build-tree fork search. The bot plays many seeded
// games (parallel across cores via fork()), results aggregate into one balance report. The
// fork search forks the world at each level-up to map which card paths are strong/weak/unfair.
#ifndef ANGERBLAST_SEARCH_H
#define ANGERBLAST_SEARCH_H

#include "world.h"

// Compact outcome of one bot game (cheap to pass between processes).
typedef struct {
    unsigned long long seed;
    int   endStage, endLevel, furthestBiome;
    int   stagesCleared;
    float runSeconds;
    int   survived;          // 1 = hit the frame cap alive
    int   deathUnfair;       // 1 = sudden-spike death, 0 = earned, -1 = survived
    float dpsVsHp;           // run damage / incoming HP
    float threatAvg;         // run-wide mean threat (pressure baseline)
    char  cause[48];
    // per-enemy-type threat (for the "even deaths per enemy" balance pass), summed by the batch
    int   dmgToPlayerByType[MAX_ENEMY_TYPES];
    long  bulletsByType[MAX_ENEMY_TYPES];
} RunResult;

// Play one full headless bot game from `seed` (fixed step, up to maxFrames). Resets + drives
// `w` to completion; fills `out`. Pure sim, no window. Deterministic from the seed.
void sim_run(GameWorld *w, unsigned long long seed, int maxFrames, RunResult *out);
int evo_matrix(unsigned long long seed, int maxFrames);   // test ALL weapon x trifecta cells

// Run `count` seeded bot games starting at `seed0`, fanned across `jobs` processes, and write
// an aggregate balance report to reports/. `jobs<=1` runs in-process. Returns 0 on success.
int  batch_run(unsigned long long seed0, int count, int jobs, int maxFrames);

// Fork-search: from a fresh run at `seed`, fork the world at each level-up, play every offered
// card branch to completion, and write a build-tree report (which paths win / die / are unfair).
int  fork_search(unsigned long long seed, int maxFrames, int depth);

#endif // ANGERBLAST_SEARCH_H

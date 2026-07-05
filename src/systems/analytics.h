// analytics.h - run instrumentation + the Markdown balance report. Counters live in
// GameWorld.analytics (data/analytics.h); systems bump them at their call sites. This module
// owns lifecycle (reset / stage boundary), the per-frame sampler, and report generation.
#ifndef ANGERBLAST_ANALYTICS_SYS_H
#define ANGERBLAST_ANALYTICS_SYS_H

#include "world.h"

void analytics_reset(GameWorld *w);          // new run: zero everything, seed minima
void analytics_sample(GameWorld *w, float dt);  // per-frame samplers (call in update_play)
void analytics_stage_cleared(GameWorld *w);  // append a per-stage row (call in boss_die, pre-stage++)
void analytics_classify_death(GameWorld *w); // read threat profile -> fair/unfair (call on death)

// Write the full Markdown report to reports/. `cause` tags the run end. Returns the path.
void analytics_write_report(GameWorld *w, const char *cause, char *outPath, int cap);

// High-score table (top 10, persisted to reports/scores.dat).
typedef struct { double score; float runTime; int stage, level; char cause[48]; } ScoreEntry;
int analytics_scores_load(ScoreEntry *out);   // returns count 0..10
int analytics_scores_submit(GameWorld *w);    // returns 1-based rank or 0 if not top-10

#endif // ANGERBLAST_ANALYTICS_SYS_H

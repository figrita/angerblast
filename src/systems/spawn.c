// spawn.c - formations + the wave director.
#include "systems/spawn.h"
#include "systems/enemies.h"
#include "systems/gems.h"        // gems_drop (formation-clear burst)
#include "data/balance.h"
#include "data/biome.h"
#include "data/palette.h"
#include <math.h>

// Enter a biome: point the enemy roster at it and arm the director from its tuning.
// Callers wrap idx into [0, NUM_BIOMES) themselves (boss_die loops back to 0 past the
// last biome, bumping w->loopCount) -- this clamp is just a defensive fallback.
void biome_enter(GameWorld *w, int idx) {
    if (idx < 0) idx = 0;
    if (idx >= NUM_BIOMES) idx = NUM_BIOMES - 1;
    w->biome = idx;
    Biome *bm = &BIOMES[idx];
    enemy_set_roster(bm->roster, bm->rosterCount);
    Director *d = &w->director;
    d->wavesThisStage = 0;
    d->wavesPerStage = bm->director.wavesToBoss > 0 ? bm->director.wavesToBoss : WAVES_PER_STAGE_BASE;
    d->waveTimer = 1.0f;
    d->bossPending = false;
    d->waveLive = false;
}

void director_reset(GameWorld *w) {
    biome_enter(w, 0);   // start in the first biome
}

// spawn one formation (a "wave") - several shapes, Galaga/Raiden style. The shape geometry
// stays a C primitive; its counts/spacing/scaling come from FORMATIONS[shape] (formations.toml).
static void spawn_formation(GameWorld *w) {
    // pick from the biome's allowed formations (fall back to all 5)
    BiomeDirector *bd = &BIOMES[w->biome].director;
    int shape;
    if (bd->formationCount > 0) shape = bd->formations[irand(w, 0, bd->formationCount-1)];
    else shape = irand(w, 0, NUM_FORMATION_OPS-1);
    int type = enemies_pick_type(w);
    const float topY = -30;
    int stage = w->stage;
    const float *fp = FORMATIONS[shape].p;
    // Push formation SETTLE anchors deeper into the arena so the hold-and-fight zone is mid-
    // screen, not the top edge — otherwise the player camps the top and kills everything on
    // entry (death-Y histogram piled up top). Anchors clamp so they never sit on the player.
    const float aPush = 175.0f;

    switch (shape) {
        case 0: { // row: p0=count p1=count/stage p2=count_max p3=rows p4=margin p5=row_gap
            int count = (int)fp[0] + (int)(fp[1]*stage); if (count > (int)fp[2]) count = (int)fp[2];
            int rows = (int)fp[3];
            float margin = fp[4], span = SCREEN - margin*2;
            for (int r=0;r<rows;r++) for (int k=0;k<count;k++){
                float x = margin + span * (count>1 ? (float)k/(count-1) : 0.5f);
                Vector2 a = { x, 80 + aPush + r*fp[5] + frandr(w, 0,20) };
                enemies_spawn(w, type, (Vector2){ x, topY - r*fp[5] - k*3 }, a);
            }
            break;
        }
        case 1: { // vee: p0=per p1=per/stage p2=per_max p3=dx_step p4=y_top_step p5=y_anchor_step
            int per = (int)fp[0] + (int)(fp[1]*stage); if (per > (int)fp[2]) per = (int)fp[2];
            for (int k=0;k<per;k++){
                float dx = (k+1) * fp[3];
                enemies_spawn(w, type, (Vector2){ SCREEN/2 - dx, topY - k*fp[4] }, (Vector2){ SCREEN/2 - dx, 80+aPush+k*fp[5] });
                enemies_spawn(w, type, (Vector2){ SCREEN/2 + dx, topY - k*fp[4] }, (Vector2){ SCREEN/2 + dx, 80+aPush+k*fp[5] });
            }
            enemies_spawn(w, type, (Vector2){ SCREEN/2, topY }, (Vector2){ SCREEN/2, 70+aPush });
            break;
        }
        case 2: { // stream: p0=lanes p1=lanes_div p2=lanes_max p3=count p4=count_max p5=y_step
            int lanes = (int)fp[0] + (fp[1]>0 ? (int)(stage/fp[1]) : 0); if (lanes>(int)fp[2]) lanes=(int)fp[2];
            int count = (int)fp[3] + stage; if (count>(int)fp[4]) count=(int)fp[4];
            for (int l=0;l<lanes;l++){ float x = frandr(w, 70, SCREEN-70);
                for (int k=0;k<count;k++)
                    enemies_spawn(w, type, (Vector2){ x + frandr(w, -12,12), topY - k*fp[5] - l*8 }, (Vector2){ x, 100+aPush }); }
            break;
        }
        case 3: { // grid: p0=cols p1=cols_div p2=cols_max p3=rows p4=margin p5=row_gap
            int cols = (int)fp[0] + (fp[1]>0 ? (int)(stage/fp[1]) : 0); if (cols>(int)fp[2]) cols=(int)fp[2];
            int rows = (int)fp[3];
            float margin = fp[4], span = SCREEN - margin*2;
            for (int r=0;r<rows;r++) for (int c=0;c<cols;c++){
                float x = margin + span*(cols>1?(float)c/(cols-1):0.5f);
                Vector2 a = { x, 60 + aPush + r*fp[5] };
                enemies_spawn(w, type, (Vector2){ x, topY - r*fp[5] }, a);
            }
            break;
        }
        default: { // swarm: p0=count p1=count/stage p2=count_max
            int count = (int)fp[0] + (int)(fp[1]*stage); if (count>(int)fp[2]) count=(int)fp[2];
            for (int k=0;k<count;k++){
                float x = frandr(w, 30, SCREEN-30);
                enemies_spawn(w, type, (Vector2){ x, topY - frandr(w, 0,260) }, (Vector2){ x, frandr(w, 60,220) });
            }
            break;
        }
    }
}

void director_update(GameWorld *w, float dt) {
    Director *d = &w->director;
    if (w->boss.active) return;                 // waves PAUSE while the boss is alive

    if (d->bossPending) {
        // wait for the field to mostly clear, then summon the boss. A grace timer
        // guarantees the boss appears even if a few stragglers linger.
        d->waveTimer -= dt;
        if (w->bossWarnTimer <= 0 && w->state == STATE_PLAY &&
            (enemies_count_active(w) <= 3 || d->waveTimer <= -BOSS_GRACE_SECONDS)) {
            if (d->waveTimer <= -BOSS_GRACE_SECONDS) w->analytics.bossGraceTimeouts++;  // field never cleared
            w->bossWarnTimer = 2.0f; w->state = STATE_BOSS_INTRO;
        }
        return;
    }

    d->waveTimer -= dt;
    BiomeDirector *bd = &BIOMES[w->biome].director;
    int capBase = bd->fieldCapBase ? bd->fieldCapBase : FIELD_CAP_BASE;
    int capPer  = bd->fieldCapPerStage ? bd->fieldCapPerStage : FIELD_CAP_PER_STAGE;
    int capMax  = bd->fieldCapMax ? bd->fieldCapMax : FIELD_CAP_MAX;
    int fieldCap = capBase + w->stage*capPer;
    if (fieldCap > capMax) fieldCap = capMax;

    // The wave interval is the REAL gap between waves: per-biome `wave_interval_base` wins,
    // else the global. A small per-stage shave keeps late game a touch tighter (clamped).
    float interval = (bd->waveIntervalBase > 0 ? bd->waveIntervalBase : WAVE_INTERVAL_BASE)
                     - fminf(WAVE_INTERVAL_MIN_SUB, w->stage*0.06f);

    // FORMATION CLEAR: a live wave whose field is now empty. Reward a gem burst at its last
    // location and grant a short LULL (calm before the next wave) so combat has a rhythm
    // instead of a relentless stream. The lull shrinks at higher stages (less mercy late).
    if (d->waveLive && enemies_count_active(w) <= 0) {
        d->waveLive = false;
        Vector2 c = w->lastKillPos;
        // Formation-clear reward: a noticeable ring-burst of gems so it READS as a bonus.
        // Scatter them in a ring pattern (not just a clump) so they're easy to see + collect.
        int burst = 8 + w->stage * 2;
        float gemVal = 2.0f + w->stage * 0.8f;
        for (int k = 0; k < burst; k++) {
            float ang = (float)k / burst * 6.28318f;
            float r = 30.0f + frandr(w, 0, 20);
            Vector2 p = { c.x + cosf(ang)*r, c.y + sinf(ang)*r };
            gems_drop(w, p, gemVal, PALETTE[PC_GOLD]);   // gold colour = "bonus" not just XP
        }
        w->shakeAmount = fmaxf(w->shakeAmount, 4);   // tiny screen-bump to punctuate the clear
        float lull = fmaxf(1.5f, 3.5f - w->stage*0.15f);
        if (d->waveTimer < lull) d->waveTimer = lull;
        return;
    }

    if (d->waveTimer <= 0) {
        // If the field is still full, don't bank negative time (which would dump the next wave
        // the instant the cap clears). Hold a short retry delay so the gap stays meaningful.
        if (enemies_count_active(w) >= fieldCap) { d->waveTimer = 1.0f; return; }

        // Roughly every 3rd wave (after the first), send a symmetric elite PAIR instead of a
        // normal formation — a recurring miniboss moment. Falls back to a formation if the
        // biome has no stage-valid elite.
        bool eliteWave = (d->wavesThisStage > 0) && (d->wavesThisStage % 3 == 0)
                         && enemies_pick_elite(w) >= 0;
        if (eliteWave) enemies_spawn_elite_pair(w);
        else           spawn_formation(w);
        d->waveLive = true;
        d->wavesThisStage++;
        d->waveTimer = interval;
        if (d->wavesThisStage >= d->wavesPerStage) { d->bossPending = true; d->waveTimer = 0; }
    }
}

// analytics.h - the metric structs. `Analytics` accumulates the whole run (most metrics are
// most useful as run totals/rates for an LLM report). A small per-stage array keeps the
// high-signal progression (clear time, level reached, DPS-vs-HP) so the report shows both the
// run roll-up and how each stage went. Almost every field is a counter/accumulator bumped at
// one call site — see docs/ANALYTICS.md for the site of each.
#ifndef ANGERBLAST_ANALYTICS_H
#define ANGERBLAST_ANALYTICS_H

#include "data/enemy_types.h"   // MAX_ENEMY_TYPES
#include "core/common.h"        // EFFECT_COUNT, PICKUP_KIND_COUNT
#include "data/weapons_def.h"   // WP_COUNT
#include "data/runes.h"         // RUNE_COUNT

#define MAX_STAGE_ROWS 32       // per-stage progression rows kept for the report

// One lightweight per-stage row (the progression table in the report).
typedef struct {
    int    stage;
    float  clearTime;           // seconds from stage start to boss kill
    int    levelAtClear;        // player level when the stage ended
    double damageDealt;         // friendly damage this stage
    double incomingHp;          // total enemy+boss HP that existed this stage
    int    hullLost;            // hull HP lost this stage
    float  bossTTK;             // boss time-to-kill (0 if none)
} StageRow;

// The whole-run analytics. Reset in game_reset(); per-stage row appended at each boss kill.
typedef struct {
    // ---- run framing ----
    double startRunTime;        // runTime at run start
    int    stagesCleared;       // bosses killed
    int    furthestBiome;
    int    endLevel;            // player level at run end
    int    deathStage;          // stage the run ended on (-1 = quit alive in menu)
    char   causeOfDeath[48];    // enemy type / "boss" / "bullet" / "quit" / "alive"

    // ---- per-stage tracking (for clear time + progression) ----
    double curStageStart;       // runTime when the current stage began
    double curStageDamageStart; // damageDealt snapshot at stage start
    double curStageIncomingStart;
    int    curStageHullStart;
    StageRow stages[MAX_STAGE_ROWS];
    int    stageRowCount;

    // ---- threat profile (for the "fair vs unfair death" verdict) ----
    // threat is a per-frame 0..1 danger estimate (nearby enemy bullets + foes + low shield).
    // The ring buffer holds the last ~2s so we can read the danger profile JUST BEFORE death.
    #define THREAT_RING 120          // ~2s at 60fps
    float  threatRing[THREAT_RING];
    int    threatHead;
    double threatSum; long threatSamples;   // run-wide avg threat (pressure baseline)
    float  timeInDanger;        // dt with threat above a "real danger" cutoff (on-the-ropes time)
    // filled at death:
    float  threatBeforeDeath;   // mean threat over the last ~1.5s before the killing blow
    float  peakThreatBeforeDeath;
    bool   deathWasUnfair;      // true = sudden spike out of calm; false = earned/on-the-ropes
    bool   deathClassified;

    // ---- A. survivability & defense ----
    int    hullLost;
    float  shieldAbsorbed;
    int    shieldBreaks;
    float  timeAtZeroShield;
    double shieldPctSum; long shieldSamples;
    float  shieldFullTime;
    float  shieldRegened;
    int    wastedHits;          // hits ignored due to iframe (non-dodge)
    int    minHp;               // lowest hp reached (init high)
    float  minShieldPct;        // lowest shield fraction (init 1)
    double proximitySum; long proximitySamples;

    // ---- B. dodge ----
    int    dodges, dodgesOnCooldown;
    float  dodgeCooldownTime;
    int    dodgeAvoidedHits;
    float  dodgeDistance;

    // ---- C. enemy population & lethality ----
    int    spawned, killed, leaked;
    double enemyLifeSum;
    int    killsByDirect, killsByAoe, killsByDot;
    double overkillSum;
    int    enemyBulletsFired, enemyBulletHits;
    double onScreenSum; long onScreenSamples;
    long   fieldCapFrames, totalFrames;
    int    contactEvents;
    int    typeKills[MAX_ENEMY_TYPES];
    double typeLife[MAX_ENEMY_TYPES];
    int    typeLeaks[MAX_ENEMY_TYPES];
    // per-enemy-type THREAT TO THE PLAYER: how many of its bullets exist, and how much damage
    // (shield+hull hits) it lands on you. The goal is an EVEN spread — no enemy that dominates.
    long   bulletsByType[MAX_ENEMY_TYPES];
    int    dmgToPlayerByType[MAX_ENEMY_TYPES];   // player-hits attributed to this type's bullets/contact
    // where on the screen enemies die (Y banded top->bottom). If kills pile up in the top
    // bands, the player is camping the top / out-ranging the field — wasted arena.
    #define DEATH_Y_BANDS 8
    int    deathYBand[DEATH_Y_BANDS];

    // ---- D. elites ----
    int    elitePairs, eliteKills, eliteDeaths;
    double eliteLifeSum;

    // ---- E. boss ----
    int    bossFights;
    double bossSpawnTime;       // runTime when the current boss spawned (for TTK)
    float  bossTTKSum, dmgDuringBoss;
    int    bossClinks, bossGraceTimeouts;

    // ---- F. offense, weapons & runes ----
    int    bulletsFired, bulletHits;
    double damageDealt;
    double dmgByWeaponKind[8];  // WK_* (see common.h)
    int    critHits; double critDamage;
    int    effectApplied[EFFECT_COUNT];
    int    gooIgnites;
    int    evolutionsFormed;
    int    runesPlaced[RUNE_COUNT];

    // ---- G. economy ----
    int    gemsDropped, gemsCollected, gemsLeaked, gemMerges;
    double xpEarned;
    int    levelUps;
    int    pickupsDropped[PICKUP_KIND_COUNT], pickupsCollected[PICKUP_KIND_COUNT];
    float  frenzyTime;
    int    boardWipeKills;

    // ---- H. pace, flow & meta ----
    float  idleTime, shakeTime, hitFlashTime;
    float  timeInLevelup, timeInPause, timeInCodex;
    double incomingHp;          // run total enemy+boss HP (for DPS-vs-HP)

    // ---- survey (filled on the post-run survey screen) ----
    bool   surveyDone;
    int    surveyRow;           // active form row (0-3 ratings, 4 notes, 5 submit)
    int    ratings[4];          // difficulty, fun, pacing, power — each 1-5
    char   notes[256];          // free-text
    int    notesLen;            // current length while typing
} Analytics;

#endif // ANGERBLAST_ANALYTICS_H

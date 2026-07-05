// enemy_types.h - data-driven enemy roster. Add a row to ETYPES (in enemy_types.c) to
// add a new enemy; behaviour/shape are selected by the enums below.
#ifndef ANGERBLAST_ENEMY_TYPES_H
#define ANGERBLAST_ENEMY_TYPES_H

#include "raylib.h"
#include "data/geometry.h"

// Movement behaviours
enum {
    MV_MARCH,      // Space Invaders: step side, drop a row, repeat
    MV_DIVE,       // Galaga: hold then swoop toward the player
    MV_WEAVE,      // Galaxian: sine weave straight down
    MV_CRAB,       // wide side-to-side while descending
    MV_SNAKE,      // Centipede: travel horizontally, drop a row at edges
    MV_HUNT,       // Robotron: slowly steer toward the player
    MV_HOVER,      // Touhou: descend to a y-band then hover/strafe
    MV_ORBIT,      // Galaga: orbit an anchor then dive
    MV_DRIFT,      // R-Type/Raiden: slow straight descent
    MV_STRAIGHT,   // Raiden popcorn: brisk straight down
    MV_LUNGE,      // creep down; when within trigger range, DASH-seek the player, recover, repeat
    MV_SWOOP,      // arc/swoop dive: curves across the screen toward the player side
    MV_SPIRAL,     // spiral inward while descending (corkscrew toward the bottom)
};

// Fire behaviours. ring/flower/spiral/down/aimed/spread all collapsed into ONE
// parameterised `radial` emitter (see FireParams) — the old op NAMES still parse
// (the loader maps them to FR_RADIAL + appropriate FireParams defaults), but the
// engine only knows two primitives now.
enum {
    FR_NONE,
    FR_RADIAL,     // count bullets fanned over `arc`, optionally aimed at player, with spin
};

// FireParams — the per-type danmaku numbers, loaded from `fire_params` in TOML.
//   count          base projectile count
//   countPerStage  +count per stage (was `8 + w->stage` for ring)
//   speedScale     multiplies the stage-scaled enemy bullet speed
//   arc            angular span of the fan, radians (2*PI = full ring)
//   spin           radians added to patternAng each shot (rotates the pattern)
//   aimed          fan is centred on the player (else centred straight down)
//   color          bullet colour (palette-resolved)
typedef struct {
    int   count, countPerStage;
    float speedScale, arc, spin;
    bool  aimed;
    Color color;
} FireParams;

// MoveParams — per-op movement numbers (was the baked literals in switch(T->move)).
// Slots are interpreted per-op; see the comments in enemies.c enemies_update().
typedef struct { float p[6]; } MoveParams;

typedef struct {
    const char *name;
    const char *sprite;        // pixel-art sprite path relative to assets/ (NULL = draw geo)
    const char *flavor;        // codex flavor line (NULL = stats only)
    Geometry geo;              // data-defined silhouette (fallback when sprite is missing)
    int move, fire;
    MoveParams movep;          // movement formula numbers (per move op)
    FireParams firep;          // danmaku emitter numbers
    int  emitter;              // EMITTERS index for composable danmaku, or -1 (uses firep)
    float hp, speed, radius;
    float fireInterval;        // base seconds between shots (0 = never)
    int minStage;              // earliest stage this type appears
    Color color;
    float xp;
    bool  elite;               // miniboss-tier: big, tanky, rare; spawns in symmetric pairs
    int   threatId;            // stable global analytics slot (by NAME, across biomes)
    int   deathBurst;          // bullets sprayed radially on death (0 = none) — gives melee
                               // enemies danger so they aren't free XP. Speed scales with stage.
    float lungeEvery;          // periodic lunge: seconds between dash-at-player bursts (0 = off).
                               // A behaviour LAYER on top of any move op (creep-then-pounce feel).
} EnemyType;

#define MAX_ENEMY_TYPES 64
// ETYPES is a VIEW of the current biome's roster (set by enemy_set_roster on biome enter).
extern EnemyType *ETYPES;
extern int NUM_ENEMY_TYPES;                 // size of the current roster
#define NUM_ETYPES NUM_ENEMY_TYPES          // compat alias for existing call sites

// point the ETYPES view at a biome's roster (call on biome enter / reset)
void enemy_set_roster(EnemyType *roster, int count);

// global enemy-NAME registry for analytics (per-biome indices alias distinct enemies, so we
// blame stats by a stable name slot instead). Loader assigns each enemy its threatId.
int  enemy_threat_id(const char *name);   // stable slot for a name (creates on first use)
const char *enemy_threat_name(int id);
int  enemy_threat_count(void);
void enemy_threat_reset(void);            // clear the registry (call at load start)

// name -> enum mapping (used by the loader); returns -1 if unknown.
int enemy_move_id(const char *name);
int enemy_fire_id(const char *name);
// fill `fp` with the faithful defaults for a fire op NAME (loader applies these,
// then overlays any TOML `fire_params`).
void enemy_fire_defaults(const char *name, FireParams *fp);

#endif // ANGERBLAST_ENEMY_TYPES_H

// enemy_types.c - the enemy roster is loaded from a biome's enemies/*.toml. This file holds
// the runtime table + the op-name -> enum maps the loader uses.
#include "data/enemy_types.h"
#include "data/palette.h"
#include <string.h>
#include <math.h>

EnemyType *ETYPES;        // points at the current biome's roster
int NUM_ENEMY_TYPES;

void enemy_set_roster(EnemyType *roster, int count) {
    ETYPES = roster; NUM_ENEMY_TYPES = count;
}

// ---- global enemy-name registry (analytics) ----
// Per-biome rosters reuse indices (idx 1 = Diver in biome0, a different enemy in biome1), so
// indexing analytics by roster index ALIASES distinct enemies together. This registry assigns
// each unique enemy NAME a stable global slot so per-enemy stats are blamed correctly.
#define MAX_ENEMY_NAMES 64
static const char *g_enemyNames[MAX_ENEMY_NAMES];
static int g_enemyNameCount;

int enemy_threat_id(const char *name) {
    if (!name) return -1;
    for (int i = 0; i < g_enemyNameCount; i++)
        if (g_enemyNames[i] && strcmp(g_enemyNames[i], name) == 0) return i;
    if (g_enemyNameCount < MAX_ENEMY_NAMES) { g_enemyNames[g_enemyNameCount] = name; return g_enemyNameCount++; }
    return -1;
}
const char *enemy_threat_name(int id) {
    return (id >= 0 && id < g_enemyNameCount) ? g_enemyNames[id] : NULL;
}
int enemy_threat_count(void) { return g_enemyNameCount; }
void enemy_threat_reset(void) { g_enemyNameCount = 0; for (int i=0;i<MAX_ENEMY_NAMES;i++) g_enemyNames[i]=NULL; }

static const char *MOVES[]  = { "march","dive","weave","crab","snake","hunt","hover","orbit","drift","straight",
                                "lunge","swoop","spiral" };

static int find(const char *name, const char **tab, int n) {
    for (int i = 0; i < n; i++) if (strcmp(name, tab[i]) == 0) return i;
    return -1;
}
int enemy_move_id(const char *name)  { return find(name, MOVES, 13); }

// Fire op name -> enum. All the old danmaku names collapse to the one FR_RADIAL
// primitive; "none" stays. Unknown -> -1 (loader flags it).
int enemy_fire_id(const char *name) {
    if (strcmp(name, "none") == 0) return FR_NONE;
    if (strcmp(name, "down")   == 0 || strcmp(name, "aimed")  == 0 ||
        strcmp(name, "spread") == 0 || strcmp(name, "ring")   == 0 ||
        strcmp(name, "flower") == 0 || strcmp(name, "spiral") == 0 ||
        strcmp(name, "radial") == 0) return FR_RADIAL;
    return -1;
}

// Default FireParams for a fire op NAME — a faithful translation of the old
// hardcoded danmaku so an enemy that names "ring"/"flower"/... and omits
// `fire_params` behaves exactly as before. `fire_params` in TOML overrides these.
// (The stage-scaling `count_per_stage` reproduces ring's `8 + w->stage`.)
void enemy_fire_defaults(const char *name, FireParams *fp) {
    // base default: 1 aimed shot at full speed, coral (matches old FR_AIMED)
    fp->count = 1; fp->countPerStage = 0; fp->speedScale = 1.0f;
    fp->arc = 0; fp->spin = 0; fp->aimed = true; fp->color = palette_by_name("coral");
    if (strcmp(name, "down") == 0) {
        fp->aimed = false;                                   // straight down, 1 shot
    } else if (strcmp(name, "aimed") == 0) {
        // defaults already match
    } else if (strcmp(name, "spread") == 0) {
        fp->count = 3; fp->arc = 0.44f;                      // base±0.22 -> total span 0.44
    } else if (strcmp(name, "ring") == 0) {
        fp->count = 8; fp->countPerStage = 1; fp->speedScale = 0.8f;
        fp->arc = 2*PI; fp->spin = 0.4f; fp->aimed = false;
        fp->color = palette_by_name("hotpink");
    } else if (strcmp(name, "flower") == 0) {
        fp->count = 5; fp->speedScale = 0.7f;
        fp->arc = 2*PI; fp->spin = 0.35f; fp->aimed = false;
        fp->color = palette_by_name("hotpink");
    } else if (strcmp(name, "spiral") == 0) {
        fp->count = 2; fp->speedScale = 0.85f;
        fp->arc = 2*PI; fp->spin = 0.28f; fp->aimed = false;
        fp->color = palette_by_name("sky");
    }
    // "radial" / unknown: keep the neutral default above.
}

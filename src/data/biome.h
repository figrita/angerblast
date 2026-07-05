// biome.h - a biome is a self-contained world: theme (background + stars), its own enemy
// roster (loaded from its enemies/ folder), director tuning, and a boss. Biomes live in
// assets/biomes/<name>/ and are ordered alphabetically. Beating a biome's boss advances.
#ifndef ANGERBLAST_BIOME_H
#define ANGERBLAST_BIOME_H

#include "data/enemy_types.h"
#include "data/anim.h"

#define MAX_BIOMES        16
#define MAX_BIOME_ROSTER  32
#define MAX_FORMATIONS    8
#define MAX_BOSS_PHASES   4

typedef struct {
    int        fireOp;     // enemy fire op id (the boss reuses danmaku ops), or -1
    int        count;      // projectiles in the main pattern
    bool       plusFlower; // overlay a flower burst
    float      hpAbove;    // this phase active while hp/maxHp >= hpAbove
    FireParams mainFp;     // built from fire/count: the phase's primary danmaku
    FireParams flowerFp;   // the overlaid flower burst (used when plusFlower)
} BossPhase;

// A destructible boss part (head / hands). Multi-part bosses (e.g. head + two hands) are
// authored as a `[[boss.part]]` list. The HEAD is invulnerable while any non-head part
// (hand) is still alive; killing the head ends the boss. Each part fires its own danmaku.
#define MAX_BOSS_PARTS 5
typedef struct {
    float      offX, offY;   // spawn offset from the boss anchor
    float      hp, radius;
    bool       isHead;       // the head: vulnerable only after the hands die; its death = win
    int        fireOp;       // enemy fire op id, or -1 for no fire
    int        count;        // projectiles
    int        emitter;      // optional composable emitter id, or -1
    float      interval;     // seconds between this part's bursts
    FireParams fp;           // built from fireOp/count
    Color      color;
    const char *sprite;      // pixel-art sprite path relative to assets/ (NULL = geo/circle)
    Geometry   geo;          // optional per-part silhouette (count==0 => plain circle)
    BossAnimSeq anim;        // optional keyframed motion (frameCount==0 => static part)
} BossPartDef;

typedef struct {
    const char *name;
    const char *sprite;      // pixel-art body sprite (single-body bosses; NULL = circles)
    float hp, radius;
    BossPhase phases[MAX_BOSS_PHASES];
    int   phaseCount;
    BossPartDef parts[MAX_BOSS_PARTS];   // multi-part layout (empty => single-body boss)
    int   partCount;
} BossDef;

typedef struct {
    int   fieldCapBase, fieldCapPerStage, fieldCapMax, wavesToBoss;
    float waveIntervalBase;
    int   formations[MAX_FORMATIONS];   // formation op ids this biome uses
    int   formationCount;
} BiomeDirector;

// A formation shape's tunables (per op id: row/vee/stream/grid/swarm). The shape geometry
// stays a C primitive in spawn.c; these numbers (counts, spacing, per-stage scaling, caps)
// come from formations.toml. `p[]` slots are interpreted per-op (see spawn.c spawn_formation).
#define NUM_FORMATION_OPS 5
#define FORMATION_SLOTS   8
typedef struct { float p[FORMATION_SLOTS]; } FormationDef;
extern FormationDef FORMATIONS[NUM_FORMATION_OPS];

typedef struct {
    const char *name;
    const char *slug;      // asset folder name (e.g. "03_frost"); keys generated art
    Color background;
    Color stars[3];
    // readability theme: EVERY enemy bullet and EVERY gem in this biome uses these,
    // hand-picked to contrast the biome's terrain AND each other (both palette colors)
    Color bulletColor;
    Color gemColor;
    // roster: this biome's enemies (loaded from its enemies/ folder)
    EnemyType roster[MAX_BIOME_ROSTER];
    int       rosterCount;
    BiomeDirector director;
    BossDef       boss;
} Biome;

extern Biome BIOMES[MAX_BIOMES];
extern int   NUM_BIOMES;

#endif // ANGERBLAST_BIOME_H

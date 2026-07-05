// common.h - shared types, constants, and small helpers used across all modules.
#ifndef ANGERBLAST_COMMON_H
#define ANGERBLAST_COMMON_H

#include "raylib.h"
#include "raymath.h"
#include "data/emitter.h"   // ChildMode enum, EMITTER_* caps (bullets carry child-emitter refs)

// ---- arena / pool sizes ----
#define SCREEN 720

#define MAX_ENEMIES   2048
#define MAX_BULLETS   4096
#define MAX_GEMS      1024
#define MAX_PARTICLES 2048
#define MAX_DMGNUM    512
#define MAX_ORBITALS  16
#define MAX_PUDDLES   64

#define GRID_CELLS 16
#define CELL_SIZE  (SCREEN / GRID_CELLS)
#define CELL_CAP   96

#define PAD 0   // first gamepad index

// ---- game states ----
typedef enum { STATE_TITLE, STATE_PLAY, STATE_LEVELUP, STATE_BOSS_INTRO, STATE_DEAD, STATE_PAUSE, STATE_CODEX, STATE_SURVEY } GameState;

// which weapon emitted a bullet (drives tint/behaviour)
enum { WK_MAIN, WK_SIDE, WK_TAIL, WK_HOMING, WK_ENEMY };

// ---- entity structs (plain data) ----
// on-hit effect for evolved projectiles
enum { HIT_NONE, HIT_EXPLODE, HIT_BURN, HIT_SPLASH };  // SPLASH: charged shot, radius = splashR
// visual style for bullets (evolved shots look distinct)
enum { BSTYLE_DOT, BSTYLE_BEAM, BSTYLE_SLUG, BSTYLE_STAR, BSTYLE_DIAMOND, BSTYLE_RING, BSTYLE_BLOB };

// status effects that can be active on an enemy (see data/effects.{h,c})
typedef enum {
    EFFECT_SLOW,    // movement + fire-rate reduction (mag = fraction, 0.4 = 40% slower)
    EFFECT_BURN,    // fire DoT (mag = dps); doubled if the target is also gooed
    EFFECT_POISON,  // stacking DoT (mag = dps per stack)
    EFFECT_FREEZE,  // hard stop: movement AND firing halted
    EFFECT_SHOCK,   // stun: movement halted, brief
    EFFECT_WEAKEN,  // vulnerability: incoming damage * (1 + mag)
    EFFECT_MARK,    // tag consumed by detonate / focus effects
    EFFECT_GOO,     // flammable coating: slows + weakens; ignites (2x burn) when set on fire
    EFFECT_COUNT
} EffectId;

typedef struct {
    Vector2 pos, vel;
    float radius, life;
    bool active, friendly, piercing, homing;
    int pierceLeft;
    float damage;
    int weaponKind;
    int onHit;            // HIT_* effect when this bullet strikes
    int style;            // BSTYLE_* draw shape
    int effect;           // EffectId to apply on hit, or -1
    float effectMag;      // magnitude for that effect
    bool magnetKill;      // Prospector: kills by this bullet magnetise nearby gems
    int onKill;           // OnKill behaviour to run when this bullet kills
    int bounceLeft;       // Ricochet: remaining bounces (0 = none)
    bool crit;            // guaranteed-crit shot (Marksman)
    bool destroysBullets; // Aegis gem: this friendly bullet shoots down enemy bullets
    float splashR;        // HIT_SPLASH blast radius (charged main-cannon shot)
    bool chargeShot;      // this is THE charged main-cannon slug (only one may live)
    bool seek;            // charged shot: steer at the locked target (not just nearest)
    bool seekIsBoss;      // locked target is the boss (part)
    int  seekIdx;         // enemy index / boss part index (-1 = core)
    // ---- composable emitter graph (data-driven sub-emitters) ----
    int   childEmitter;   // EMITTERS index this bullet fires, or -1
    int   childMode;      // ChildMode (timer / on-death)
    float childTimer;     // countdown to the next CHILD_ON_TIMER burst
    float childInterval;  // reset value for childTimer
    float childSpeed;     // base speed handed to the child burst
    unsigned char depth;  // recursion depth (0 = top-level); capped to stop runaway
    short srcType;         // enemy type that fired this (analytics blame); -1 = friendly/boss
    Color color;
} Bullet;

typedef struct {
    Vector2 pos, vel;
    float life, maxLife, size;
    Color color;
} Particle;

typedef struct {
    Vector2 pos, vel;
    float radius, hp, maxHp;
    int type;                  // index into ETYPES
    float fireTimer, phase;
    float t;                   // age, for movement timing
    double spawnTime;          // runTime at spawn (analytics: enemy lifespan)
    Vector2 anchor;            // formation slot / orbit center / hover target
    int stepDir;               // for march/snake direction
    float patternAng;          // accumulating angle for danmaku
    float lungeTimer;          // MV_LUNGE state clock / periodic-lunge cooldown
    Color color;
    bool active;
    unsigned char eliteTier;   // 0 = not elite; else 1 + w->loopCount at spawn (caps visuals/toughness)
    // ---- status effects ----
    float effTimer[EFFECT_COUNT];          // seconds remaining per EffectId (0 = inactive)
    float effMag[EFFECT_COUNT];            // magnitude per EffectId
    unsigned char effStacks[EFFECT_COUNT]; // stacks for STACK_ADD effects
    float dotTick;                         // shared sub-second DoT accumulator
    // ---- game-feel (cosmetic, no sim impact) ----
    float hitFlash;                        // seconds of white damage-flash remaining
    float spawnScale;                      // grows 0->1 over the spawn pop-in
} Enemy;

typedef struct {
    Vector2 pos, vel;
    double value;
    bool active, magnetized;
    Color color;
} Gem;

// Power-up pickups (rarer than gems). Each kind triggers an effect on collection.
typedef enum { PICK_MAGNET, PICK_WIPE, PICK_LEVELUP, PICK_FRENZY, PICK_HEAL, PICKUP_KIND_COUNT } PickupKind;
typedef struct {
    Vector2 pos, vel;
    int  kind;          // PickupKind
    float bob;          // bob/spin phase
    bool active, magnetized;
} Pickup;
#define MAX_PICKUPS 32

typedef struct { Vector2 pos; float life; int amount; Color color; bool crit; } DamageNum;
// a transient lightning arc segment between two points
typedef struct { Vector2 a, b; float life, maxLife; Color color; } Bolt;
#define MAX_BOLTS 256
// an expanding shockwave ring (kill pops, explosions, stage clears)
typedef struct { Vector2 pos; float r0, r1, life, maxLife, thick; Color color; } Ring;
#define MAX_RINGS 128

// flammable-goo puddle: a persistent ground zone that burns enemies inside it
typedef struct { Vector2 pos; float radius, life, dps, tick; bool active; } Puddle;

// runtime state for one destructible boss part (multi-part bosses)
typedef struct {
    Vector2 pos;             // current world position (anchor + part offset)
    float   hp, hpMax;
    float   radius;          // current radius (base radius x anim scale)
    bool    alive, isHead;
    float   fireTimer, patternAng;
    float   animRot;         // whole-part rotation from the keyframe anim (deg)
} BossPartState;

#define MAX_BOSS_PARTS_RT 5
typedef struct {
    Vector2 pos, vel;
    float radius, hp, hpMax;
    int phase;
    float fireTimer, patternTimer;
    float patternAng, flowerAng;   // rotating-danmaku phases for emit_radial (main + flower)
    bool active;
    // multi-part: when partCount>0 the boss is head+hands; the single-body fields above are
    // ignored for combat (parts carry the HP) but pos still drives the anchor sweep.
    BossPartState parts[MAX_BOSS_PARTS_RT];
    int  partCount;
    float animTime;   // shared clock for the parts' keyframe sequences
} Boss;

typedef struct {
    Vector2 pos, vel;
    float radius;
    float shield, shieldMax, shieldRegenTimer, shieldRegenDelay, shieldRegenRate;
    int hp, hpMax;
    float iframe, moveSpeed, pickupRadius;
    float bank;            // smoothed horizontal lean [-1,1] for the sprite's pixel shear
    // main-cannon charge shot (Star Fox / Megaman-style; see weapons.c)
    float charge;          // seconds accrued toward CHARGE_FULL_TIME
    float chargePulse;     // countdown to the next charging audio pulse
    bool  autofire;        // rapid auto-fire stream on/off (toggle: T / pad Triangle)
    bool  fireHeldPrev;    // fire-button edge detection
    int   chargeShotIdx;   // bullet slot of the live charged shot, or -1
    bool  chargePending;   // release happened while one was live: fire when it dies
    bool  lockOn;          // charge lock-on acquired (valid while charge >= LV1)
    bool  lockIsBoss;      // locked target is the boss (part) vs the enemy pool
    int   lockIdx;         // enemy index, or boss part index (-1 = single-body core)
    // barrel-roll dodge
    float rollTimer;       // >0 while rolling (drives the spin visual + i-frames)
    float rollCooldown;    // >0 while the dodge is on cooldown
    Vector2 rollDir;       // unit dash direction for the active roll
    // main gun (innate; auto-fires straight up). Not a socketable weapon.
    float fireTimer, fireInterval, bulletDamage, bulletSpeed;
    int spreadLevel;
    bool piercing; int pierceCount;
    // global player passives (still apply to the main gun + survivability)
    float globalDamageMul, globalRateMul, xpMul;
    float armor;           // damage-reduction stat; DR = armor/(armor+ARMOR_K), diminishing
    float frenzyTimer;     // >0 while the Frenzy pickup buff is active (fire-rate + damage)
    bool  aegis;           // Aegis upgrade: your bullets shoot down incoming enemy bullets
    // xp / level
    int level; double xp, xpToNext;
    // auto-weapons (the socketed loadout) live in GameWorld.loadout, not here.
} Player;

// Drone movement AIs (a companion that carries one socketed weapon of its own).
typedef enum { DRONE_FOLLOW, DRONE_AUTO, DRONE_SWEEP, DRONE_AI_COUNT } DroneAI;

// ---- deterministic RNG (xorshift64*) ----
// Seeded per run and OWNED by the world (see GameWorld.rng), so a run is fully reproducible
// from its seed and Monte-Carlo forks each carry their own independent stream. Gameplay uses
// the w-taking frand()/frandr()/irand() wrappers in world.h — never raylib's global RNG.
typedef struct { unsigned long long s; } Rng;
static inline void  rng_seed(Rng *r, unsigned long long seed) { r->s = seed ? seed : 0x9E3779B97F4A7C15ULL; }
static inline unsigned long long rng_next(Rng *r) {
    unsigned long long x = r->s;
    x ^= x >> 12; x ^= x << 25; x ^= x >> 27;
    r->s = x;
    return x * 0x2545F4914F6CDD1DULL;
}
static inline float rng_f(Rng *r) {            // [0,1)
    return (float)((rng_next(r) >> 40) / (double)(1ULL << 24));
}
static inline float rng_fr(Rng *r, float a, float b) { return a + (b - a) * rng_f(r); }
static inline int   rng_i(Rng *r, int lo, int hi) {    // inclusive [lo,hi], like GetRandomValue
    if (hi < lo) { int t = lo; lo = hi; hi = t; }
    return lo + (int)(rng_next(r) % (unsigned long long)(hi - lo + 1));
}

#endif // ANGERBLAST_COMMON_H

// emitter.h - the composable danmaku emitter graph. An Emitter is a named, parameterised
// burst of bullets (a generalisation of the enemy FireParams). The key feature: a bullet an
// emitter spawns can itself carry a CHILD emitter id, run on a timer or on the bullet's death
// — so "bullets that split", "trailing spawners", and "a shot that bursts" are pure data.
//
// This is NOT a scripting VM: emitters reference each other only BY NAME (an index into the
// EMITTERS table), the engine walks the graph, and runaway is bounded by a depth cap +
// the bullet pool cap. Every reference is resolved + validated at load (fail loud).
#ifndef ANGERBLAST_EMITTER_H
#define ANGERBLAST_EMITTER_H

#include "raylib.h"

#define MAX_EMITTERS      48
#define EMITTER_NAME_MAX  32
#define EMITTER_MAX_DEPTH 4     // hard recursion cap: a spawned bullet's child chain stops here

// How a spawned bullet runs its child emitter (if it has one).
typedef enum {
    CHILD_NONE,     // the bullet carries no child emitter
    CHILD_ON_TIMER, // fire the child every `childInterval` seconds while alive
    CHILD_ON_DEATH, // fire the child once when the bullet expires / leaves the screen
} ChildMode;

// One emitter: a fan of `count` bullets over `arc`, optionally aimed, with rotation. Each
// bullet it spawns may carry a child emitter (by id) that runs per `childMode`.
typedef struct {
    char  name[EMITTER_NAME_MAX];
    int   count, countPerStage;
    float speedScale;   // × a base speed supplied by the caller
    float arc;          // fan span (rad); >= 2*PI = full ring
    float spin;         // rad added to the rotating phase each burst
    bool  aimed;        // fan centred on the player (else on the caller's base angle)
    float radius;       // bullet radius
    Color color;
    // child: the emitter each spawned bullet carries (or -1)
    int       child;        // EMITTERS index, or -1
    int       childMode;    // ChildMode
    float     childInterval;// seconds between child bursts (CHILD_ON_TIMER)
    float     childSpeed;   // base speed handed to the child burst
} Emitter;

extern Emitter EMITTERS[MAX_EMITTERS];
extern int     NUM_EMITTERS;

// name -> EMITTERS index, or -1 if unknown / empty name. Used by the loader to resolve refs.
int emitter_id(const char *name);

#endif // ANGERBLAST_EMITTER_H

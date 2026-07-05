// anim.h - keyframed animation sequences for boss parts. A sequence is a sorted list of
// keyframes (offset/rotation/scale relative to the boss anchor); the runtime evaluates it
// at the fight clock and layers the result over the part's base offset. Missing sequence
// (frameCount == 0) means the part is static — old TOML files load unchanged.
#ifndef ANGERBLAST_ANIM_H
#define ANGERBLAST_ANIM_H

#include <stdbool.h>

typedef enum {
    TWEEN_LINEAR,
    TWEEN_EASE_IN,
    TWEEN_EASE_OUT,
    TWEEN_EASE_INOUT,
    TWEEN_COUNT
} TweenType;

typedef struct {
    float     t;           // time in seconds from sequence start
    float     offX, offY;  // position relative to the boss anchor at this keyframe
    float     rotation;    // whole-part rotation (deg); 0 = upright
    float     scale;       // multiplier on base radius; 1.0 = normal
    TweenType tween;       // interpolation toward the NEXT keyframe
} BossKeyframe;

#define MAX_BOSS_KEYFRAMES 16

typedef struct {
    BossKeyframe frames[MAX_BOSS_KEYFRAMES];   // kept sorted by t
    int          frameCount;
    bool         loops;      // wrap back to frame 0 at sequence end (tweening across the seam)
    float        duration;   // total length (seconds); 0 = auto from the last frame's t
} BossAnimSeq;

// effective sequence length: explicit duration, else the last keyframe's t
float anim_seq_duration(const BossAnimSeq *a);

// Evaluate the sequence at time t. Outputs are left UNTOUCHED when frameCount == 0, so
// callers preset them to the part's static base (offX/offY, rot 0, scale 1) and call this.
void anim_eval(const BossAnimSeq *a, float t,
               float *offX, float *offY, float *rot, float *scale);

// tween name <-> id (TOML uses names); tween_id returns -1 for an unknown name
const char *tween_name(int id);
int         tween_id(const char *name);

#endif // ANGERBLAST_ANIM_H

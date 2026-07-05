// anim.c - boss-part keyframe evaluation (lerp with easing curves).
#include "data/anim.h"
#include <math.h>
#include <string.h>

static const char *TWEEN_NAMES[TWEEN_COUNT] = {
    "linear", "ease_in", "ease_out", "ease_inout"
};

const char *tween_name(int id) {
    return (id >= 0 && id < TWEEN_COUNT) ? TWEEN_NAMES[id] : "linear";
}

int tween_id(const char *name) {
    for (int i = 0; i < TWEEN_COUNT; i++)
        if (strcmp(name, TWEEN_NAMES[i]) == 0) return i;
    return -1;
}

float anim_seq_duration(const BossAnimSeq *a) {
    if (a->duration > 0) return a->duration;
    return a->frameCount > 0 ? a->frames[a->frameCount-1].t : 0;
}

static float ease(TweenType tw, float u) {
    switch (tw) {
        case TWEEN_EASE_IN:    return u*u;
        case TWEEN_EASE_OUT:   return 1 - (1-u)*(1-u);
        case TWEEN_EASE_INOUT: return u*u*(3 - 2*u);
        default:               return u;
    }
}

static void apply(const BossKeyframe *k, float *ox, float *oy, float *rot, float *sc) {
    *ox = k->offX; *oy = k->offY; *rot = k->rotation; *sc = k->scale;
}

static void blend(const BossKeyframe *a, const BossKeyframe *b, float u,
                  float *ox, float *oy, float *rot, float *sc) {
    u = ease(a->tween, u < 0 ? 0 : u > 1 ? 1 : u);
    *ox  = a->offX     + (b->offX     - a->offX)    *u;
    *oy  = a->offY     + (b->offY     - a->offY)    *u;
    *rot = a->rotation + (b->rotation - a->rotation)*u;
    *sc  = a->scale    + (b->scale    - a->scale)   *u;
}

void anim_eval(const BossAnimSeq *a, float t,
               float *ox, float *oy, float *rot, float *sc) {
    int n = a->frameCount;
    if (n <= 0) return;                       // static part: keep the caller's base values
    const BossKeyframe *F = a->frames;
    if (n == 1) { apply(&F[0], ox, oy, rot, sc); return; }
    float dur = anim_seq_duration(a);
    if (dur <= 0.0001f) { apply(&F[0], ox, oy, rot, sc); return; }
    if (a->loops) { t = fmodf(t, dur); if (t < 0) t += dur; }
    // seam segment: last frame -> first frame across the wrap (looping sequences only)
    float tail = dur - F[n-1].t;          // time after the last frame before the wrap
    float seam = tail + F[0].t;           // full seam span (wrap + lead-in to frame 0)
    if (t <= F[0].t) {
        if (a->loops && seam > 0.0001f)
            blend(&F[n-1], &F[0], (t + tail) / seam, ox, oy, rot, sc);
        else apply(&F[0], ox, oy, rot, sc);
        return;
    }
    if (t >= F[n-1].t) {
        if (a->loops && seam > 0.0001f)
            blend(&F[n-1], &F[0], (t - F[n-1].t) / seam, ox, oy, rot, sc);
        else apply(&F[n-1], ox, oy, rot, sc);
        return;
    }
    for (int i = 0; i < n-1; i++) {
        if (t < F[i+1].t) {
            float span = F[i+1].t - F[i].t;
            blend(&F[i], &F[i+1], span > 0 ? (t - F[i].t)/span : 1, ox, oy, rot, sc);
            return;
        }
    }
    apply(&F[n-1], ox, oy, rot, sc);
}

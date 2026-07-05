// emit.c - runtime for the composable emitter graph. See emit.h / data/emitter.h.
#include "systems/emit.h"
#include "systems/bullets.h"
#include "data/emitter.h"
#include "data/balance.h"
#include <math.h>

// Spawn one bullet for emitter `em` and stamp the child emitter onto it so it recurses.
static void spawn_child_bullet(GameWorld *w, const Emitter *em, Vector2 origin, float a,
                               float speed, bool friendly, int weaponKind, int depth) {
    int slot = bullets_add(w, origin, (Vector2){cosf(a)*speed, sinf(a)*speed},
                           em->radius > 0 ? em->radius : 5, 1, em->color,
                           friendly, weaponKind, 0, false);
    if (slot < 0) return;
    Bullet *b = &w->bullets.items[slot];
    b->depth = (unsigned char)depth;
    // attach the child emitter (only if there's depth budget left for it to actually fire)
    if (em->child >= 0 && depth + 1 < EMITTER_MAX_DEPTH) {
        b->childEmitter  = em->child;
        b->childMode     = em->childMode;
        b->childInterval = em->childInterval > 0 ? em->childInterval : 0.3f;
        b->childTimer    = b->childInterval;
        b->childSpeed    = em->childSpeed > 0 ? em->childSpeed : speed;
    }
}

void emitter_fire(GameWorld *w, int id, Vector2 origin, float baseAng, float baseSpeed,
                  bool friendly, int weaponKind, int depth, float *patternAng) {
    if (id < 0 || id >= NUM_EMITTERS) return;
    if (depth >= EMITTER_MAX_DEPTH) return;            // hard recursion cap
    const Emitter *em = &EMITTERS[id];

    int n = em->count + em->countPerStage * w->stage;
    if (n < 1) n = 1;
    float speed = baseSpeed * (em->speedScale > 0 ? em->speedScale : 1.0f);

    float centre = baseAng;
    if (em->aimed) {
        Vector2 toP = Vector2Subtract(w->player.pos, origin);
        centre = atan2f(toP.y, toP.x);
    }

    bool fullRing = em->arc >= 2*PI - 0.001f;
    float ang = patternAng ? *patternAng : 0;
    for (int k = 0; k < n; k++) {
        float a;
        if (fullRing)      a = ang + (float)k / n * 2*PI;
        else if (n > 1)    a = centre - em->arc*0.5f + em->arc * ((float)k / (n - 1));
        else               a = centre;
        spawn_child_bullet(w, em, origin, a, speed, friendly, weaponKind, depth);
    }
    if (patternAng) *patternAng += em->spin;
}

void emitter_bullet_tick(GameWorld *w, Bullet *b, float dt) {
    if (b->childEmitter < 0 || b->childMode != CHILD_ON_TIMER) return;
    b->childTimer -= dt;
    if (b->childTimer <= 0) {
        b->childTimer += b->childInterval;
        float ang = atan2f(b->vel.y, b->vel.x);
        // heading drives fans; full rings get a random phase so bursts never look stamped
        float phase = frand(w)*2*PI;
        emitter_fire(w, b->childEmitter, b->pos, ang, b->childSpeed,
                     b->friendly, b->weaponKind, b->depth + 1, &phase);
    }
}

void emitter_bullet_death(GameWorld *w, Bullet *b) {
    if (b->childEmitter < 0 || b->childMode != CHILD_ON_DEATH) return;
    float ang = atan2f(b->vel.y, b->vel.x);
    float phase = frand(w)*2*PI;   // random ring orientation per burst
    emitter_fire(w, b->childEmitter, b->pos, ang, b->childSpeed,
                 b->friendly, b->weaponKind, b->depth + 1, &phase);
}

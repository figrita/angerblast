// fx.c - particle + damage-number pools (purely cosmetic).
#include "systems/fx.h"
#include "data/palette.h"
#include "core/settings.h"
#include "raymath.h"
#include <math.h>

void fx_spawn_particles(GameWorld *w, Vector2 pos, int count, Color base, float speed) {
    // pure ring buffer: write at the cursor and OVERWRITE the oldest particle.
    // The old version scanned up to MAX_PARTICLES per spawned particle when the
    // pool was saturated — kill storms (the exact fun moment) went quadratic.
    FxPool *fx = &w->fx;
    for (int n = 0; n < count; n++) {
        Particle *p = &fx->parts[fx->partCursor];
        fx->partCursor = (fx->partCursor + 1) % MAX_PARTICLES;
        float a = frand(w)*2*PI, sp = speed*(0.3f+frand(w));
        p->pos = pos;
        p->vel = (Vector2){ cosf(a)*sp, sinf(a)*sp };
        p->maxLife = 0.3f + frand(w)*0.4f;
        p->life = p->maxLife;
        p->color = base;
        p->size = 2.0f + frand(w)*3.0f;
    }
}

void fx_spawn_damagenum(GameWorld *w, Vector2 pos, int amount, bool crit) {
    // same ring-buffer policy: overwrite the oldest number, never scan
    FxPool *fx = &w->fx;
    int slot = fx->dmgCursor;
    fx->dmgCursor = (slot + 1) % MAX_DMGNUM;
    fx->dmg[slot] = (DamageNum){ .pos={ pos.x+frandr(w, -6,6), pos.y-6 }, .life=0.6f, .amount=amount,
        .crit=crit, .color = crit ? PALETTE[PC_GOLD] : PALETTE[PC_WHITE] };
}

// spawn a jagged lightning bolt between a and b (several offset segments)
void fx_spawn_bolt(GameWorld *w, Vector2 a, Vector2 b, Color col) {
    FxPool *fx = &w->fx;
    int segs = 5;
    Vector2 dir = Vector2Subtract(b, a);
    Vector2 perp = { -dir.y, dir.x };
    float len = Vector2Length(perp); if (len > 0.001f) perp = Vector2Scale(perp, 1.0f/len);
    Vector2 prev = a;
    for (int s = 1; s <= segs; s++) {
        float tt = (float)s / segs;
        Vector2 base = Vector2Lerp(a, b, tt);
        float jitter = (s == segs) ? 0 : frandr(w, -8, 8);
        Vector2 pt = Vector2Add(base, Vector2Scale(perp, jitter));
        int idx = fx->boltCursor; fx->boltCursor = (idx + 1) % MAX_BOLTS;
        fx->bolts[idx] = (Bolt){ .a = prev, .b = pt, .life = 0.12f, .maxLife = 0.12f, .color = col };
        prev = pt;
    }
}

void fx_spawn_ring(GameWorld *w, Vector2 pos, float r0, float r1, float life, float thick, Color col) {
    FxPool *fx = &w->fx;
    int idx = fx->ringCursor; fx->ringCursor = (idx + 1) % MAX_RINGS;
    fx->rings[idx] = (Ring){ .pos=pos, .r0=r0, .r1=r1, .life=life, .maxLife=life, .thick=thick, .color=col };
}

// the standard kill punch: shockwave ring + spark burst + a beat of hitstop, all
// scaled by the victim's size so big deaths FEEL big.
void fx_kill_pop(GameWorld *w, Vector2 pos, float radius, Color col) {
    float k = radius / 12.0f; if (k > 3) k = 3;
    fx_spawn_ring(w, pos, radius*0.5f, radius*2.4f + 14, 0.22f + 0.05f*k, 3, col);
    fx_spawn_ring(w, pos, radius*0.3f, radius*1.6f + 8, 0.16f, 2, PALETTE[PC_WHITE]);
    fx_spawn_particles(w, pos, 6 + (int)(6*k), col, 200 + 60*k);
    fx_spawn_particles(w, pos, 3 + (int)(3*k), PALETTE[PC_WHITE], 260);
    fx_hitstop(w, 0.018f + 0.02f*(k > 1 ? 1 : k));
    w->shakeAmount = fmaxf(w->shakeAmount, 1.5f + 2.5f*k);
}

void fx_hitstop(GameWorld *w, float amount) {
    switch (SETTINGS.hitstopMode) {
        case HITSTOP_OFF: return;
        case HITSTOP_SINGLE:
            // refractory: while a freeze is in flight, new impacts don't extend it —
            // a swarm clear gives ONE collective beat, then flows.
            if (w->hitstop > 0.03f) return;
            break;
        case HITSTOP_ALL: break;   // every kill stacks (heaviest feel)
    }
    w->hitstop += amount;
    if (w->hitstop > 0.12f) w->hitstop = 0.12f;   // never freeze long enough to feel laggy
}

void fx_update(GameWorld *w, float dt) {
    for (int i = 0; i < MAX_RINGS; i++) {
        Ring *r = &w->fx.rings[i]; if (r->life <= 0) continue;
        r->life -= dt;
    }
    for (int i = 0; i < MAX_PARTICLES; i++) {
        Particle *p = &w->fx.parts[i]; if (p->life <= 0) continue;
        p->life -= dt;
        p->pos = Vector2Add(p->pos, Vector2Scale(p->vel, dt));
        p->vel = Vector2Scale(p->vel, 0.92f);
    }
    for (int i = 0; i < MAX_DMGNUM; i++) {
        DamageNum *d = &w->fx.dmg[i]; if (d->life <= 0) continue;
        d->life -= dt; d->pos.y -= 40*dt;
    }
    for (int i = 0; i < MAX_BOLTS; i++) {
        Bolt *b = &w->fx.bolts[i]; if (b->life <= 0) continue;
        b->life -= dt;
    }
}

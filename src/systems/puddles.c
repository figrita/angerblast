// puddles.c - burning goo puddles.
#include "systems/puddles.h"
#include "systems/enemies.h"   // enemies_damage_near

void puddles_spawn(GameWorld *w, Vector2 pos, float radius, float life, float dps) {
    PuddlePool *pp = &w->puddles;
    int slot = -1;
    for (int k = 0; k < MAX_PUDDLES; k++) {
        int idx = (pp->cursor + k) % MAX_PUDDLES;
        if (!pp->items[idx].active) { slot = idx; pp->cursor = (idx + 1) % MAX_PUDDLES; break; }
    }
    if (slot < 0) return;
    pp->items[slot] = (Puddle){ .pos=pos, .radius=radius, .life=life, .dps=dps, .tick=0, .active=true };
}

void puddles_update(GameWorld *w, float dt) {
    for (int i = 0; i < MAX_PUDDLES; i++) {
        Puddle *q = &w->puddles.items[i]; if (!q->active) continue;
        q->life -= dt;
        if (q->life <= 0) { q->active = false; continue; }
        q->tick -= dt;
        if (q->tick <= 0) { q->tick = 0.2f; enemies_damage_near(w, q->pos, q->radius, q->dps*0.2f); }
    }
}

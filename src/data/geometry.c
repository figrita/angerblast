// geometry.c - draw a data-defined silhouette from its primitive layers.
#include "data/geometry.h"
#include "raymath.h"

static Color resolve(ColorRef ref, Color pal, Color body, float alpha) {
    Color c = (ref == COLREF_BODY) ? body : (ref == COLREF_WHITE) ? (Color){255,255,255,255} : pal;
    c.a = (unsigned char)(c.a * alpha);
    return c;
}

void geometry_draw(const Geometry *g, Vector2 p, float radius, Color body, float t) {
    for (int i = 0; i < g->count; i++) {
        const ShapeLayer *L = &g->layers[i];
        Color c = resolve(L->colorRef, L->paletteCol, body, L->alpha);
        float rot = L->spinOffset + L->spin * t;
        switch (L->op) {
            case GOP_POLY:
                DrawPoly(p, L->sides, L->r * radius, rot, c);
                if (L->hasOutline)
                    DrawPolyLines(p, L->sides, L->r * radius, rot, resolve(L->outlineRef,(Color){255,255,255,255},body,L->alpha*0.6f));
                break;
            case GOP_CIRCLE:
                DrawCircleV(p, L->r * radius, c);
                break;
            case GOP_RECT: {
                float w = L->w * radius, h = L->h * radius;
                DrawRectangle((int)(p.x - w/2), (int)(p.y - h/2), (int)w, (int)h, c);
                if (L->hasOutline)
                    DrawRectangleLines((int)(p.x - w/2), (int)(p.y - h/2), (int)w, (int)h,
                                       resolve(L->outlineRef,(Color){255,255,255,255},body,L->alpha*0.6f));
                break;
            }
            case GOP_RING:
                DrawRing(p, (L->r - L->thick)*radius, L->r*radius, 0, 360, 24, c);
                break;
            case GOP_LINE:
                DrawLineEx((Vector2){ p.x + L->x1*radius, p.y + L->y1*radius },
                           (Vector2){ p.x + L->x2*radius, p.y + L->y2*radius },
                           L->thick*radius, c);
                break;
        }
    }
}

#include "platform/sprites.h"
#include <math.h>
#include <stdio.h>
#include <string.h>

#define MAX_SPRITES 192
typedef struct { char path[96]; Texture2D tex; bool failed; } SpriteSlot;
static SpriteSlot g_slots[MAX_SPRITES];
static int g_count = 0;

const Texture2D *sprite_get(const char *path) {
    if (!path || !path[0] || !IsWindowReady()) return NULL;
    for (int i = 0; i < g_count; i++)
        if (strcmp(g_slots[i].path, path) == 0)
            return g_slots[i].failed ? NULL : &g_slots[i].tex;
    if (g_count >= MAX_SPRITES) return NULL;
    SpriteSlot *s = &g_slots[g_count++];
    snprintf(s->path, sizeof(s->path), "%s", path);
    char full[160];
    snprintf(full, sizeof(full), "assets/%s", path);
    s->tex = LoadTexture(full);
    s->failed = (s->tex.id == 0);
    if (s->failed) fprintf(stderr, "sprites: missing %s (falling back to geometry)\n", full);
    return s->failed ? NULL : &s->tex;
}

bool sprite_draw(const char *path, Vector2 center, float diameter, float rotation, Color tint) {
    const Texture2D *t = sprite_get(path);
    if (!t) return false;
    // diameter <= 0 draws at native texture size: sprites are authored at their exact
    // on-screen size, so 1 texture pixel == 1 screen pixel and the grid stays uniform.
    // diameter > 0 snaps to the nearest INTEGER scale of the texture — fractional
    // scaling shreds the pixel grid (some texels 1px wide, some 2px).
    int k = 1;
    if (diameter > 0) {
        float m = (float)(t->width > t->height ? t->width : t->height);
        k = (int)(diameter / m + 0.5f);
        if (k < 1) k = 1;
    }
    float w = (float)(t->width * k), h = (float)(t->height * k);
    // land the top-left corner on a whole pixel so texels map 1:N exactly
    float left = roundf(center.x - w*0.5f), top = roundf(center.y - h*0.5f);
    Rectangle src = { 0, 0, (float)t->width, (float)t->height };
    Rectangle dst = { left + w*0.5f, top + h*0.5f, w, h };
    DrawTexturePro(*t, src, dst, (Vector2){ w*0.5f, h*0.5f }, rotation, tint);
    return true;
}

// row-sheared draw for the player ship: bank in [-1,1] shifts each row horizontally
// by whole pixels (nose toward the turn, tail away) — a pixel-safe lean, no rotation.
bool sprite_draw_banked(const char *path, Vector2 center, float bank, Color tint) {
    const Texture2D *t = sprite_get(path);
    if (!t) return false;
    int w = t->width, h = t->height;
    float left = roundf(center.x - w*0.5f), top = roundf(center.y - h*0.5f);
    for (int y = 0; y < h; y++) {
        float u = ((h-1)*0.5f - y) / ((h-1)*0.5f);           // +1 top .. -1 bottom
        int dx = (int)roundf(bank * 2.0f * u);
        Rectangle src = { 0, (float)y, (float)w, 1 };
        Rectangle dst = { left + dx, top + y, (float)w, 1 };
        DrawTexturePro(*t, src, dst, (Vector2){ 0, 0 }, 0, tint);
    }
    return true;
}

void sprites_unload(void) {
    for (int i = 0; i < g_count; i++)
        if (!g_slots[i].failed) UnloadTexture(g_slots[i].tex);
    g_count = 0;
}

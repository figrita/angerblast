// palette.c - the 64-colour palette. These are compile-time DEFAULTS; the loader
// overwrites PALETTE (and registers names) from assets/palette.toml at startup.
#include "data/palette.h"
#include <string.h>
#include <stdio.h>

#define HEX(r,g,b) (Color){ 0x##r, 0x##g, 0x##b, 0xFF }

Color PALETTE[PALETTE_COUNT] = {
    // row 0
    HEX(00,00,00), HEX(12,17,3d), HEX(29,32,68), HEX(46,4b,8c), HEX(6b,74,b2), HEX(90,9e,dd), HEX(c1,d9,f2), HEX(ff,ff,ff),
    // row 1
    HEX(a2,93,c4), HEX(7b,6a,a5), HEX(53,42,7f), HEX(3c,2c,68), HEX(43,1e,66), HEX(5d,2f,8c), HEX(85,4c,bf), HEX(b4,83,ef),
    // row 2
    HEX(8c,ff,9b), HEX(42,bc,7f), HEX(22,89,6e), HEX(14,66,5b), HEX(0f,4a,4c), HEX(0a,2a,33), HEX(1d,1a,59), HEX(32,2d,89),
    // row 3
    HEX(35,4a,b2), HEX(3e,83,d1), HEX(50,b9,eb), HEX(8c,da,ff), HEX(53,a1,ad), HEX(3b,76,8f), HEX(21,52,6b), HEX(16,37,55),
    // row 4
    HEX(00,87,82), HEX(00,aa,a5), HEX(27,d3,cb), HEX(78,fa,e6), HEX(cd,c5,99), HEX(98,8f,64), HEX(5c,5d,41), HEX(35,3f,23),
    // row 5
    HEX(91,9b,45), HEX(af,d3,70), HEX(ff,e0,91), HEX(ff,aa,6e), HEX(ff,69,5a), HEX(b2,3c,40), HEX(ff,66,75), HEX(dd,37,45),
    // row 6
    HEX(a5,26,39), HEX(72,1c,2f), HEX(b2,2e,69), HEX(e5,42,86), HEX(ff,6e,af), HEX(ff,a5,d5), HEX(ff,d3,ad), HEX(cc,81,7a),
    // row 7
    HEX(89,56,54), HEX(61,39,3b), HEX(3f,1f,3c), HEX(72,33,52), HEX(99,4c,69), HEX(c3,72,89), HEX(f2,9f,aa), HEX(ff,cc,d0),
};

// ---- name -> index registry (filled by the loader from palette.toml [names]) ----
#define MAX_PAL_NAMES 80
static struct { char name[24]; int index; } g_names[MAX_PAL_NAMES];
static int g_nameCount;

void palette_register_name(const char *name, int index) {
    if (g_nameCount >= MAX_PAL_NAMES || index < 0 || index >= PALETTE_COUNT) return;
    snprintf(g_names[g_nameCount].name, sizeof(g_names[0].name), "%s", name);
    g_names[g_nameCount].index = index;
    g_nameCount++;
}

Color palette_by_name(const char *name) {
    for (int i = 0; i < g_nameCount; i++)
        if (strcmp(g_names[i].name, name) == 0) return PALETTE[g_names[i].index];
    return (Color){ 255, 255, 255, 255 };   // unknown -> white (visible, not silent black)
}

int palette_name_count(void) { return g_nameCount; }
const char *palette_name_at(int i) { return (i>=0&&i<g_nameCount)?g_names[i].name:NULL; }
int palette_index_at(int i) { return (i>=0&&i<g_nameCount)?g_names[i].index:-1; }

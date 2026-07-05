// palette.h - the game's 64-colour palette (single source of truth for all colours).
// PALETTE[0..63] is the raw grid (row-major). The PAL_* names below are semantic handles
// so call sites read by meaning, not index. Tweak a mapping here to re-tint everywhere.
#ifndef ANGERBLAST_PALETTE_H
#define ANGERBLAST_PALETTE_H

#include "raylib.h"

#define PALETTE_COUNT 64
extern Color PALETTE[PALETTE_COUNT];   // filled from assets/palette.toml at load (defaults baked in)

// resolve a palette colour by its semantic name (e.g. "coral"); white if unknown.
Color palette_by_name(const char *name);
void  palette_register_name(const char *name, int index);  // loader calls this from [names]

// enumerate registered names (for editor tooling)
int         palette_name_count(void);
const char *palette_name_at(int i);
int         palette_index_at(int i);

// ---- raw grid indices (8x8, row-major) ----
// row 0: blues/violets light ramp
enum {
    PC_BLACK=0, PC_INK, PC_NAVY, PC_INDIGO, PC_PERIWINKLE, PC_LILAC, PC_ICE, PC_WHITE,
    // row 1: purples
    PC_MAUVE, PC_PLUM, PC_GRAPE, PC_AUBERGINE, PC_DEEPPURPLE, PC_VIOLET, PC_ORCHID, PC_LAVENDER,
    // row 2: greens / teals / deep navies
    PC_MINT, PC_SEAGREEN, PC_PINE, PC_TEALDK, PC_TEALDDK, PC_ABYSS, PC_MIDBLUE, PC_BLUEVIOLET,
    // row 3: blue ramp / cyans
    PC_AZURE, PC_SKY, PC_CYAN, PC_PALECYAN, PC_TEAL, PC_STEEL, PC_DEEPTEAL, PC_NIGHTTEAL,
    // row 4: teal-greens / olives
    PC_AQUA, PC_AQUABRT, PC_TURQUOISE, PC_AQUALT, PC_KHAKI, PC_OLIVE, PC_OLIVEDK, PC_OLIVEDDK,
    // row 5: yellow-greens / warm
    PC_LIME, PC_LEAF, PC_GOLD, PC_PEACH, PC_CORAL, PC_RED, PC_MAROON, PC_PINKRED,
    // row 6: reds / pinks
    PC_CRIMSON, PC_BLOOD, PC_WINE, PC_MAGENTA, PC_HOTPINK, PC_PINK, PC_PINKLT, PC_SKIN,
    // row 7: muted warms / pinks
    PC_CLAY, PC_BROWN, PC_UMBER, PC_PLUMDK, PC_MAUVEDK, PC_ROSE, PC_BLUSH, PC_BLUSHLT
};

// ---- semantic handles (what things are coloured by meaning) ----
#define PAL_BG          PALETTE[PC_BLACK]        // arena background
#define PAL_BG_HEAT     PALETTE[PC_DEEPPURPLE]   // background tint as difficulty climbs
#define PAL_STAR        PALETTE[PC_PERIWINKLE]   // starfield
#define PAL_STAR_DIM    PALETTE[PC_NAVY]

#define PAL_PLAYER      PALETTE[PC_PALECYAN]      // ship
#define PAL_PLAYER_GLOW PALETTE[PC_SKY]
#define PAL_SHIELD      PALETTE[PC_SKY]
#define PAL_HP          PALETTE[PC_CRIMSON]
#define PAL_HP_EMPTY    PALETTE[PC_WINE]
#define PAL_XP          PALETTE[PC_LEAF]
#define PAL_HITFLASH    PALETTE[PC_CORAL]

#define PAL_BULLET_MAIN PALETTE[PC_PALECYAN]      // friendly main-gun bullets
#define PAL_BULLET_SIDE PALETTE[PC_PEACH]
#define PAL_BULLET_TAIL PALETTE[PC_ORCHID]
#define PAL_BULLET_ENEMY PALETTE[PC_CORAL]
#define PAL_BOSS        PALETTE[PC_CRIMSON]
#define PAL_BOSS_CORE   PALETTE[PC_PEACH]
#define PAL_BOSSBAR     PALETTE[PC_PINKRED]

#define PAL_GOLD        PALETTE[PC_GOLD]          // accents / level-up
#define PAL_GOOD        PALETTE[PC_MINT]          // success / evolve
#define PAL_TEXT        PALETTE[PC_WHITE]
#define PAL_TEXT_DIM    PALETTE[PC_LILAC]
#define PAL_PANEL       PALETTE[PC_INK]
#define PAL_PANEL_HI    PALETTE[PC_PERIWINKLE]

#endif // ANGERBLAST_PALETTE_H

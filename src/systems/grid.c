// grid.c - rebuild the spatial grid from the active enemy pool.
#include "systems/grid.h"
#include <string.h>

void grid_rebuild(GameWorld *w) {
    Grid *g = &w->grid;
    memset(g->n, 0, sizeof(g->n));
    for (int i = 0; i < MAX_ENEMIES; i++) {
        if (!w->enemies.items[i].active) continue;
        int c = grid_cell_index(w->enemies.items[i].pos);
        if (g->n[c] < CELL_CAP) g->cell[c][g->n[c]++] = (short)i;
    }
}

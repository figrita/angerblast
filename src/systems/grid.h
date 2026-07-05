// grid.h - uniform spatial grid over the arena holding enemy indices, rebuilt each
// frame. Lets weapons/bullets query only nearby cells instead of all enemies.
#ifndef ANGERBLAST_GRID_H
#define ANGERBLAST_GRID_H

#include "world.h"

static inline int grid_cell_index(Vector2 p) {
    int cx = (int)(p.x/CELL_SIZE), cy = (int)(p.y/CELL_SIZE);
    if (cx<0) cx=0; else if (cx>=GRID_CELLS) cx=GRID_CELLS-1;
    if (cy<0) cy=0; else if (cy>=GRID_CELLS) cy=GRID_CELLS-1;
    return cy*GRID_CELLS + cx;
}

void grid_rebuild(GameWorld *w);   // populate from the active enemy pool

#endif // ANGERBLAST_GRID_H

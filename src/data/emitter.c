// emitter.c - storage + name lookup for the emitter table (filled by the loader).
#include "data/emitter.h"
#include <string.h>

Emitter EMITTERS[MAX_EMITTERS];
int     NUM_EMITTERS;

int emitter_id(const char *name) {
    if (!name || !name[0]) return -1;
    for (int i = 0; i < NUM_EMITTERS; i++)
        if (strcmp(EMITTERS[i].name, name) == 0) return i;
    return -1;
}

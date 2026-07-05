// trifectas.h - the 12 named rune combos. A weapon whose 3 sockets match a trifecta's
// colour multiset evolves. Matching is order-independent (compare sorted rune counts).
#ifndef ANGERBLAST_TRIFECTAS_H
#define ANGERBLAST_TRIFECTAS_H

#include "data/runes.h"

typedef enum {
    TRI_INFERNO, TRI_STREAM, TRI_SWARM, TRI_BLITZ, TRI_LANCE, TRI_NAPALM,
    TRI_SCATTER, TRI_RAILGUN, TRI_TEMPEST, TRI_VOID, TRI_PRISM, TRI_CHAOS,
    TRI_COUNT
} TrifectaId;

typedef struct {
    const char *name;
    const char *sprite;       // codex/UI icon path relative to assets/ (NULL = swatch only)
    int counts[RUNE_COUNT];   // how many of each rune colour this trifecta needs (sums to 3)
} Trifecta;

extern Trifecta TRIFECTAS[TRI_COUNT];

// Returns the TrifectaId matched by the three runes (any order), or -1 if none / incomplete.
int trifecta_match(const int gems3[3]);

#endif // ANGERBLAST_TRIFECTAS_H

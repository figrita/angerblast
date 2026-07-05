// trifectas.c - the matcher. The 12 combos are loaded from assets/trifectas.toml.
#include "data/trifectas.h"

Trifecta TRIFECTAS[TRI_COUNT];   // filled by the loader

int trifecta_match(const int gems3[3]) {
    // need all three sockets filled
    for (int i = 0; i < 3; i++) if (gems3[i] < 0) return -1;
    int counts[RUNE_COUNT] = { 0 };
    for (int i = 0; i < 3; i++) counts[gems3[i]]++;
    for (int t = 0; t < TRI_COUNT; t++) {
        bool ok = true;
        for (int c = 0; c < RUNE_COUNT; c++)
            if (counts[c] != TRIFECTAS[t].counts[c]) { ok = false; break; }
        if (ok) return t;
    }
    return -1;
}

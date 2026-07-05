// biome.c - storage for the loaded biomes (filled by the loader).
#include "data/biome.h"

Biome BIOMES[MAX_BIOMES];
int   NUM_BIOMES;
FormationDef FORMATIONS[NUM_FORMATION_OPS];   // filled by the loader from formations.toml

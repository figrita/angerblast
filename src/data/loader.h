// loader.h - loads all game data from assets/*.toml into the runtime tables at startup
// (and on hot-reload). Fails loud (prints file + key) and returns false on malformed data.
#ifndef ANGERBLAST_LOADER_H
#define ANGERBLAST_LOADER_H

#include <stdbool.h>

// Parse every assets/*.toml into the global tables (Balance BAL, palette, runes, ...).
// `assetsDir` is the directory containing the .toml files (e.g. "assets").
// Returns false (and prints the problem) if any file is missing or malformed.
bool loader_load_all(const char *assetsDir);

#endif // ANGERBLAST_LOADER_H

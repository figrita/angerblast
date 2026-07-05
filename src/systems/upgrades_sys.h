// upgrades_sys.h - XP/leveling, the level-up card model, the rune loadout, and trifecta
// evolution. A card is one of: a rune to socket, a new weapon, a weapon level-up, or a
// global passive.
#ifndef ANGERBLAST_UPGRADES_SYS_H
#define ANGERBLAST_UPGRADES_SYS_H

#include "world.h"
#include "data/upgrades.h"

typedef enum { CARD_RUNE, CARD_NEW_WEAPON, CARD_LEVEL_WEAPON, CARD_PASSIVE, CARD_DRONE } CardKind;

// A single offered card. `kind` selects which of the value fields is meaningful.
typedef struct {
    CardKind kind;
    int      value;     // RuneId | WeaponId | WeaponId(owned) | PassiveId | DroneAI
} Card;

extern Card g_cards[3];           // the three currently-offered cards (parallel to cardsel.cards indices)

double xp_for_level(int level);
void   weapon_grant(GameWorld *w, int type);   // add a weapon to the loadout (empty sockets)
void   xp_gain(GameWorld *w, double v);        // may enter STATE_LEVELUP
void   cards_roll(GameWorld *w);               // build 3 fresh offers

// Apply the chosen card. For a rune card the caller must then run the socket placement
// step; returns true if a rune was chosen (placement needed), false otherwise.
bool   card_choose(GameWorld *w, int cardIndex);

// Place the rune currently being placed into (weaponIndex, socketIndex); detects trifectas.
void   rune_place(GameWorld *w, int weaponIndex, int socketIndex);

// First empty socket of weapon[weaponIndex], or -1 if it's full.
int    rune_first_open_socket(GameWorld *w, int weaponIndex);

// Would placing `rune` into weapon[weaponIndex] complete a trifecta? (for the glow hint)
int    rune_would_evolve(GameWorld *w, int weaponIndex, int socketIndex, int rune);

// Rune-placement target list = loadout weapons then drone weapons (combined index space).
int     placement_count(GameWorld *w);                 // total socketable guns (weapons + drones)
Weapon *placement_weapon(GameWorld *w, int idx);       // combined-index -> Weapon* (NULL if oob)

#endif // ANGERBLAST_UPGRADES_SYS_H

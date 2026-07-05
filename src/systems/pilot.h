// pilot.h - input abstraction so either a HUMAN (keyboard/pad) or a BOT (heuristic AI) can
// drive the ship. The sim calls pilot_* instead of input_* directly; headless runs use BOT.
// This is the seam that lets the game playtest itself.
#ifndef ANGERBLAST_PILOT_H
#define ANGERBLAST_PILOT_H

#include "world.h"

typedef enum { PILOT_HUMAN, PILOT_BOT } PilotMode;

// per-frame control queries (dispatch on w->pilot)
Vector2 pilot_move(GameWorld *w);              // desired move direction (unit-ish)
bool    pilot_dodge(GameWorld *w);             // request a barrel-roll this frame
Vector2 pilot_aim(GameWorld *w, Vector2 from); // aim direction for the Aim Cannon
bool    pilot_fire_down(GameWorld *w);         // fire held (charge); bot never holds
bool    pilot_autofire_toggle(GameWorld *w);   // toggle auto-fire this frame

// menu decisions (the bot resolves level-up cards + rune sockets itself)
int     pilot_pick_card(GameWorld *w);                 // card index 0-2 (bot), or -1 = wait (human)
int     pilot_pick_socket(GameWorld *w, int placeCount);// placement target index (bot), or -1 = wait

#endif // ANGERBLAST_PILOT_H

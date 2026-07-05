// render.h - all drawing. Pure read of GameWorld; never mutates game state.
#ifndef ANGERBLAST_RENDER_H
#define ANGERBLAST_RENDER_H

#include "world.h"

void render_arena(GameWorld *w);        // background + starfield (call before shake)
void render_world(GameWorld *w);        // entities, player, fx
void render_hud(GameWorld *w);
void render_card_screen(GameWorld *w);
void render_title(GameWorld *w);
void render_dead(GameWorld *w);
void render_boss_warn(GameWorld *w);
void render_evo_banner(GameWorld *w);   // transient "WEAPON EVOLVED" announcement
void render_stage_banner(GameWorld *w); // stage-clear flash + HELL YEAH slam banner
void render_pause(GameWorld *w);        // pause overlay: stats + loadout
void render_toggle_fps(void);           // flip the FPS counter on/off (toggled from pause)
void render_set_result_rank(int rank);  // call before first render_survey() with the score rank

// In-game encyclopedia (reached from pause). Tabs over all data categories.
typedef enum { CODEX_ENEMIES, CODEX_WEAPONS, CODEX_EFFECTS, CODEX_PICKUPS,
               CODEX_RUNES, CODEX_DRONES, CODEX_PASSIVES, CODEX_TAB_COUNT } CodexTab;
void render_codex(GameWorld *w);
int  codex_entry_count(GameWorld *w, int tab);   // # of entries in a tab (for scroll clamping)

// Post-run survey form (ratings + notes), shown before the report is written.
void render_survey(GameWorld *w);   // the post-run results screen (name kept for state machine)

#endif // ANGERBLAST_RENDER_H

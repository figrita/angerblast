// input.h - device-agnostic input. Keyboard/mouse and DualSense are interchangeable;
// the keyboard path is always a fallback. No game state lives here.
#ifndef ANGERBLAST_INPUT_H
#define ANGERBLAST_INPUT_H

#include "core/common.h"

enum { PROMPT_KB, PROMPT_PS, PROMPT_XBOX };   // button-prompt icon sets (ui/ sprites)

bool    pad_on(void);
int     input_active_pad(void);   // first connected pad slot, or -1
Vector2 input_move(void);                 // unit-ish movement vector
Vector2 input_aim_dir(Vector2 fromPos);   // aim for the Aim Cannon (right stick > mouse > up)
bool    input_confirm_pressed(void);      // start / confirm / retry
bool    input_pause_pressed(void);        // toggle pause (Esc / P / pad Start)
bool    input_dodge_pressed(void);        // barrel-roll dodge (Shift / pad R1)
bool    input_fire_down(void);            // charge/fire held (Space or J / pad R2)
bool    input_autofire_toggle_pressed(void); // toggle the auto-fire stream (T / pad Triangle)
bool    input_codex_pressed(void);        // open/close the codex (C / pad Square)
int     input_menu_step(void);            // -1 / 0 / +1 for card navigation
Vector2 input_mouse(void);                // cursor position (menu hover-follow)
bool    input_mouse_moved(void);          // cursor moved since last call (once per frame)
void    input_update_prompt_device(void);  // once per frame: tracks the last-touched device
int     input_prompt_device(void);         // PROMPT_* driving the button-prompt icons
void    input_rumble(float lo, float hi, float dur);

#endif // ANGERBLAST_INPUT_H

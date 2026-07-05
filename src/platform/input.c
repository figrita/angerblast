// input.c - keyboard/mouse + any gamepad, behind a small device-agnostic API.
// Controller support is deliberately GENERIC: we scan all pad slots and take the
// first live one (controllers can connect/disconnect mid-run), and every action
// accepts the buttons different pads commonly map to it — DualSense, Xbox,
// Switch Pro, and generic HID pads all work without a mapping screen.
#include "platform/input.h"
#include <string.h>

// First USABLE pad this frame, or -1. Controllers like the DualSense expose their
// motion sensors and touchpad as SEPARATE "gamepads", usually in LOWER slots than
// the real pad. Their long names ("... Controller Motion Sensors") also overflow
// raylib's 64-char name buffer and come back unterminated — so name-substring
// checks are unreliable. Rule that works: the REAL device is the one with the
// shortest sane name (sub-devices are always "<real name> <suffix>").
static size_t padname_len(const char *n) {   // bounded strlen (c99: no strnlen)
    size_t i = 0; while (i < 65 && n[i]) i++; return i;
}
static int active_pad(void) {
    int best = -1, fallback = -1;
    size_t bestLen = (size_t)-1;
    for (int i = 0; i < 8; i++) {
        if (!IsGamepadAvailable(i)) continue;
        if (fallback < 0) fallback = i;
        const char *n = GetGamepadName(i);
        size_t len = n ? padname_len(n) : 65;
        if (len >= 64) continue;              // truncated/unterminated = sub-device
        if (len < bestLen) { bestLen = len; best = i; }
    }
    return best >= 0 ? best : fallback;
}

bool pad_on(void) { return active_pad() >= 0; }
int input_active_pad(void) { return active_pad(); }

Vector2 input_move(void) {
    Vector2 m = { 0, 0 };
    int pad = active_pad();
    if (pad >= 0) {
        float ax = GetGamepadAxisMovement(pad, GAMEPAD_AXIS_LEFT_X);
        float ay = GetGamepadAxisMovement(pad, GAMEPAD_AXIS_LEFT_Y);
        if (ax*ax + ay*ay > 0.15f*0.15f) { m.x = ax; m.y = ay; }
        if (IsGamepadButtonDown(pad, GAMEPAD_BUTTON_LEFT_FACE_LEFT))  m.x -= 1;
        if (IsGamepadButtonDown(pad, GAMEPAD_BUTTON_LEFT_FACE_RIGHT)) m.x += 1;
        if (IsGamepadButtonDown(pad, GAMEPAD_BUTTON_LEFT_FACE_UP))    m.y -= 1;
        if (IsGamepadButtonDown(pad, GAMEPAD_BUTTON_LEFT_FACE_DOWN))  m.y += 1;
    }
    if (IsKeyDown(KEY_W) || IsKeyDown(KEY_UP))    m.y -= 1;
    if (IsKeyDown(KEY_S) || IsKeyDown(KEY_DOWN))  m.y += 1;
    if (IsKeyDown(KEY_A) || IsKeyDown(KEY_LEFT))  m.x -= 1;
    if (IsKeyDown(KEY_D) || IsKeyDown(KEY_RIGHT)) m.x += 1;
    if (Vector2LengthSqr(m) > 1.0f) m = Vector2Normalize(m);
    return m;
}

Vector2 input_aim_dir(Vector2 fromPos) {
    int pad = active_pad();
    if (pad >= 0) {
        float ax = GetGamepadAxisMovement(pad, GAMEPAD_AXIS_RIGHT_X);
        float ay = GetGamepadAxisMovement(pad, GAMEPAD_AXIS_RIGHT_Y);
        if (ax*ax + ay*ay > 0.3f*0.3f) return Vector2Normalize((Vector2){ ax, ay });
    }
    Vector2 d = Vector2Subtract(GetMousePosition(), fromPos);
    if (Vector2LengthSqr(d) > 16.0f) return Vector2Normalize(d);
    return (Vector2){ 0, -1 };
}

// ---- button-prompt device: whatever the player touched last ----------------
static int g_promptDev = PROMPT_KB;

static bool name_is_xbox(const char *n) {
    if (!n) return false;
    char low[128]; size_t i = 0;
    for (; n[i] && i < sizeof(low) - 1; i++)
        low[i] = (n[i] >= 'A' && n[i] <= 'Z') ? n[i] + 32 : n[i];
    low[i] = '\0';
    return strstr(low, "xbox") || strstr(low, "x-box") || strstr(low, "xinput") || strstr(low, "microsoft");
}

void input_update_prompt_device(void) {
    int pad = active_pad();
    if (pad >= 0) {
        bool act = false;
        for (int b = 1; b <= 17 && !act; b++) act = IsGamepadButtonDown(pad, b);
        if (!act) {
            float x = GetGamepadAxisMovement(pad, GAMEPAD_AXIS_LEFT_X);
            float y = GetGamepadAxisMovement(pad, GAMEPAD_AXIS_LEFT_Y);
            act = x*x + y*y > 0.2f*0.2f;
        }
        if (act) g_promptDev = name_is_xbox(GetGamepadName(pad)) ? PROMPT_XBOX : PROMPT_PS;
    }
    static const int KB_KEYS[] = { KEY_W, KEY_A, KEY_S, KEY_D, KEY_UP, KEY_DOWN, KEY_LEFT, KEY_RIGHT,
        KEY_SPACE, KEY_J, KEY_T, KEY_C, KEY_P, KEY_ESCAPE, KEY_ENTER, KEY_ONE, KEY_TWO, KEY_THREE,
        KEY_LEFT_SHIFT, KEY_RIGHT_SHIFT };
    for (size_t i = 0; i < sizeof(KB_KEYS)/sizeof(KB_KEYS[0]); i++)
        if (IsKeyDown(KB_KEYS[i])) { g_promptDev = PROMPT_KB; break; }
}

int input_prompt_device(void) { return g_promptDev; }

Vector2 input_mouse(void) { return GetMousePosition(); }

// true when the cursor moved since the previous call. Menus use this so hover
// follows the MOUSE only when the mouse is actually in use — otherwise the
// keyboard/pad cursor would be fought every frame. Call once per frame.
bool input_mouse_moved(void) {
    static Vector2 last = { -1, -1 };
    Vector2 m = GetMousePosition();
    bool moved = (m.x - last.x)*(m.x - last.x) + (m.y - last.y)*(m.y - last.y) > 1.0f;
    last = m;
    return moved;
}

bool input_confirm_pressed(void) {
    if (IsKeyPressed(KEY_SPACE) || IsKeyPressed(KEY_ENTER) || IsMouseButtonPressed(MOUSE_BUTTON_LEFT)) return true;
    int pad = active_pad();
    // bottom face button = accept on every layout (Cross/A/B-switch)
    if (pad >= 0 && IsGamepadButtonPressed(pad, GAMEPAD_BUTTON_RIGHT_FACE_DOWN)) return true;
    return false;
}

bool input_codex_pressed(void) {
    if (IsKeyPressed(KEY_C)) return true;
    int pad = active_pad();
    if (pad >= 0 && (IsGamepadButtonPressed(pad, GAMEPAD_BUTTON_RIGHT_FACE_LEFT) ||   // Square/X
                     IsGamepadButtonPressed(pad, GAMEPAD_BUTTON_RIGHT_FACE_UP)))      // Triangle/Y
        return true;
    return false;
}

bool input_fire_down(void) {
    if (IsKeyDown(KEY_SPACE) || IsKeyDown(KEY_J)) return true;
    int pad = active_pad();
    return pad >= 0 && IsGamepadButtonDown(pad, GAMEPAD_BUTTON_RIGHT_FACE_DOWN);   // Cross/A
}

bool input_autofire_toggle_pressed(void) {
    if (IsKeyPressed(KEY_T)) return true;
    int pad = active_pad();
    return pad >= 0 && IsGamepadButtonPressed(pad, GAMEPAD_BUTTON_RIGHT_FACE_UP);  // Triangle/Y
}

bool input_dodge_pressed(void) {
    if (IsKeyPressed(KEY_LEFT_SHIFT) || IsKeyPressed(KEY_RIGHT_SHIFT)) return true;
    int pad = active_pad();
    if (pad >= 0) {
        // any shoulder or trigger dodges — pads disagree about where "the fun button" is
        if (IsGamepadButtonPressed(pad, GAMEPAD_BUTTON_RIGHT_TRIGGER_1)) return true;
        if (IsGamepadButtonPressed(pad, GAMEPAD_BUTTON_LEFT_TRIGGER_1))  return true;
        if (IsGamepadButtonPressed(pad, GAMEPAD_BUTTON_RIGHT_TRIGGER_2)) return true;
        if (IsGamepadButtonPressed(pad, GAMEPAD_BUTTON_LEFT_TRIGGER_2))  return true;
    }
    return false;
}

bool input_pause_pressed(void) {
    if (IsKeyPressed(KEY_ESCAPE) || IsKeyPressed(KEY_P)) return true;
    int pad = active_pad();
    if (pad >= 0) {
        // any middle-cluster button pauses (Start/Options, Select/Share, Guide)
        if (IsGamepadButtonPressed(pad, GAMEPAD_BUTTON_MIDDLE_RIGHT)) return true;
        if (IsGamepadButtonPressed(pad, GAMEPAD_BUTTON_MIDDLE_LEFT))  return true;
        if (IsGamepadButtonPressed(pad, GAMEPAD_BUTTON_MIDDLE))       return true;
    }
    return false;
}

int input_menu_step(void) {
    if (IsKeyPressed(KEY_LEFT) || IsKeyPressed(KEY_A))  return -1;
    if (IsKeyPressed(KEY_RIGHT) || IsKeyPressed(KEY_D)) return 1;
    int pad = active_pad();
    if (pad >= 0) {
        if (IsGamepadButtonPressed(pad, GAMEPAD_BUTTON_LEFT_FACE_LEFT))  return -1;
        if (IsGamepadButtonPressed(pad, GAMEPAD_BUTTON_LEFT_FACE_RIGHT)) return 1;
    }
    return 0;
}

void input_rumble(float lo, float hi, float dur) {
    int pad = active_pad();
    if (pad >= 0) SetGamepadVibration(pad, lo, hi, dur);
}

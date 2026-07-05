// settings.h - player-facing options, persisted to settings.cfg next to the binary.
#ifndef ANGERBLAST_SETTINGS_H
#define ANGERBLAST_SETTINGS_H

enum { HITSTOP_OFF, HITSTOP_SINGLE, HITSTOP_ALL };   // freeze-frame modes

typedef struct {
    int showFps;       // 0/1: FPS overlay
    int hitstopMode;   // HITSTOP_*: kill freeze-frames — off / one-at-a-time / every kill stacks
    float sfxVol;      // 0..1 master sfx level
    float musicVol;    // 0..1 music level
    int autoCharge;    // 0/1: main cannon charges on its own while idle (autofire off)
} Settings;

extern Settings SETTINGS;

void settings_load(void);
void settings_save(void);

#endif // ANGERBLAST_SETTINGS_H

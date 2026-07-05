#include "core/settings.h"
#include <stdio.h>

Settings SETTINGS = { .showFps = 0, .hitstopMode = HITSTOP_SINGLE, .sfxVol = 1.0f, .musicVol = 0.8f, .autoCharge = 1 };

static float clamp01(float v) { return v < 0 ? 0 : v > 1 ? 1 : v; }

void settings_load(void) {
    FILE *f = fopen("settings.cfg", "r");
    if (!f) return;
    int fps = 0, hs = HITSTOP_SINGLE;
    float sv = SETTINGS.sfxVol, mv = SETTINGS.musicVol;
    int ac = SETTINGS.autoCharge;
    // old files stop matching after the first two fields; defaults cover the rest
    int n = fscanf(f, "fps=%d hitstop=%d sfxvol=%f musicvol=%f autocharge=%d", &fps, &hs, &sv, &mv, &ac);
    if (n >= 2) {
        SETTINGS.showFps = fps ? 1 : 0;
        SETTINGS.hitstopMode = (hs >= 0 && hs <= 2) ? hs : HITSTOP_SINGLE;
        SETTINGS.sfxVol = clamp01(sv);
        SETTINGS.musicVol = clamp01(mv);
        SETTINGS.autoCharge = ac ? 1 : 0;
    }
    fclose(f);
}

void settings_save(void) {
    FILE *f = fopen("settings.cfg", "w");
    if (!f) return;
    fprintf(f, "fps=%d hitstop=%d sfxvol=%.2f musicvol=%.2f autocharge=%d\n",
            SETTINGS.showFps, SETTINGS.hitstopMode, SETTINGS.sfxVol, SETTINGS.musicVol, SETTINGS.autoCharge);
    fclose(f);
}

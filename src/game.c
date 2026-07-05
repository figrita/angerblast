// Angerblast - a vertical schmup roguelite (Raiden/Touhou x Vampire Survivors) in raylib.
//
// This file is the entry point and game loop only. All gameplay lives in modules:
//   data/      - data-driven configs (enemy roster, upgrade catalogue, balance knobs)
//   platform/  - input, audio, rendering
//   systems/   - player, weapons, enemies, boss, spawn director, bullets, gems, etc.
//   world.h    - the GameWorld aggregate threaded through every system
//
// Fixed 720x720, no asset files (audio + starfield are procedural). A uniform spatial
// grid keeps collision flat at thousands of enemies.

#include "world.h"
#include "rlgl.h"

#include "platform/input.h"
#include "platform/audio.h"
#include "platform/render.h"
#include "platform/sprites.h"
#include "core/settings.h"
#include "systems/player.h"
#include "systems/weapons.h"
#include "systems/enemies.h"
#include "systems/boss.h"
#include "systems/drones.h"
#include "systems/spawn.h"
#include "systems/bullets.h"
#include "systems/gems.h"
#include "systems/pickups.h"
#include "systems/puddles.h"
#include "systems/fx.h"
#include "systems/grid.h"
#include "systems/upgrades_sys.h"
#include "systems/analytics.h"
#include "systems/pilot.h"
#include "systems/search.h"
#include "data/biome.h"
#include "data/runes.h"
#include "data/enemy_types.h"
#include "data/effects.h"
#include "data/weapons_def.h"
#include "data/loader.h"
#include "platform/input.h"

#include <string.h>
#include <stdio.h>
#include <stdlib.h>
#include <time.h>
#ifdef PLATFORM_WEB
#include <emscripten/emscripten.h>   // browser main-loop driver
#endif

static void frame(void);   // one game frame (native while-loops it; web schedules it)
#ifndef PLATFORM_WEB
static bool g_quit = false; // set to exit the native loop (web can't quit a tab)
#endif

static GameWorld world;

// Per-run RNG seed. 0 = pick a fresh time-based seed each run (normal play). Headless mode
// sets this to a fixed value for reproducible runs. Incremented per reset so successive
// human runs differ even with a fixed base.
static unsigned long long g_seed = 0;
static unsigned long long g_runCounter = 0;

// Full reset for a new run: clear all pools and re-arm player + director.
static void game_reset(GameWorld *w) {
    AudioBank savedAudio = w->audio;   // keep loaded Sounds across reset
    double bestScore = w->bestScore;
    memset(w, 0, sizeof(*w));
    w->audio = savedAudio;
    w->bestScore = bestScore;

    unsigned long long seed = g_seed ? g_seed : (unsigned long long)time(NULL);
    rng_seed(&w->rng, seed + g_runCounter*0x9E3779B97F4A7C15ULL);
    g_runCounter++;

    player_reset(w);
    director_reset(w);
    analytics_reset(w);
    w->state = STATE_PLAY;
    w->stage = 1;
}

// One simulation step while in PLAY / BOSS_INTRO.
static void update_play(GameWorld *w, float dt) {
    // hitstop: kills/explosions freeze the sim for a beat (time crawls at 12%),
    // which makes impacts read. Cosmetic timers below still run on real dt.
    float realDt = dt;
    if (w->hitstop > 0) { w->hitstop -= realDt; dt *= 0.12f; }
    w->runTime += dt;
    audio_update_throttles(&w->audio, dt);
    analytics_sample(w, dt);

    player_update_movement(w, dt);

    grid_rebuild(w);
    weapons_update(w, dt);
    drones_update(w, dt);
    puddles_update(w, dt);
    director_update(w, dt);
    enemies_update(w, dt);
    boss_update(w, dt);
    bullets_update(w, dt);
    gems_update(w, dt);
    pickups_update(w, dt);
    fx_update(w, dt);

    w->hitFlash *= 0.9f;
    if (w->evoBannerTimer > 0) w->evoBannerTimer -= realDt;
    if (w->stageBannerTimer > 0) w->stageBannerTimer -= realDt;
    if (w->muzzleFlash > 0) w->muzzleFlash -= realDt;
    w->clearFlash *= powf(0.05f, realDt);   // white flash fades fast but smooth
    if (w->clearFlash < 0.01f) w->clearFlash = 0;
}

// ---- headless bot run: no window, fixed timestep, the BOT pilots. Runs until the player
// dies or `maxFrames` elapses, then writes an analytics report. Returns 0 on success.
// This is the engine the self-balancing / Monte-Carlo work builds on: fully deterministic
// from `seed`, window-free, and as fast as the CPU allows. NATIVE-only (fork/files/no DOM).
#ifndef PLATFORM_WEB
static int run_headless(unsigned long long seed, int maxFrames) {
    GameWorld *w = &world;
    RunResult r;
    sim_run(w, seed, maxFrames, &r);   // bot plays a full deterministic game (search.c)
    char path[256];
    analytics_write_report(w, w->analytics.causeOfDeath, path, sizeof(path));
    printf("headless run: seed=%llu -> stage %d, level %d, %s\nreport: %s\n",
           seed, w->stage, w->player.level,
           r.survived ? "(survived to frame cap)" : w->analytics.causeOfDeath, path);
    return 0;
}
#endif

int main(int argc, char **argv) {
    // load all game data from assets/*.toml before anything reads it (fail loud)
    if (!loader_load_all("assets")) {
        fprintf(stderr, "FATAL: could not load game data from assets/\n");
        return 1;
    }

    // ---- offline modes (NATIVE only: fork()/file output don't exist in the browser) ----
    //   --headless [--seed N] [--frames M]            one bot run -> a full report
    //   --batch N  [--jobs J] [--seed N] [--frames M] N bot runs across J procs -> batch report
    //   --search   [--seed N] [--frames M] [--depth D] fork the build tree -> build-tree report
#ifndef PLATFORM_WEB
    bool headless=false, batch=false, search=false, evomatrix=false;
    unsigned long long seed = 1; int maxFrames = 60*60*10;  // ~10 min cap
    int count = 64, jobs = 24, depth = 3;
    for (int i = 1; i < argc; i++) {
        if (strcmp(argv[i], "--headless") == 0) headless = true;
        else if (strcmp(argv[i], "--batch") == 0 && i+1 < argc) { batch = true; count = atoi(argv[++i]); }
        else if (strcmp(argv[i], "--search") == 0) search = true;
        else if (strcmp(argv[i], "--evomatrix") == 0) evomatrix = true;
        else if (strcmp(argv[i], "--jobs") == 0 && i+1 < argc) jobs = atoi(argv[++i]);
        else if (strcmp(argv[i], "--depth") == 0 && i+1 < argc) depth = atoi(argv[++i]);
        else if (strcmp(argv[i], "--seed") == 0 && i+1 < argc) seed = strtoull(argv[++i], NULL, 10);
        else if (strcmp(argv[i], "--frames") == 0 && i+1 < argc) maxFrames = atoi(argv[++i]);
    }
    if (evomatrix) return evo_matrix(seed, maxFrames);
    if (batch)    return batch_run(seed, count, jobs, maxFrames);
    if (search)   return fork_search(seed, maxFrames, depth);
    if (headless) return run_headless(seed, maxFrames);
#else
    (void)argc; (void)argv;
#endif

    SetConfigFlags(FLAG_MSAA_4X_HINT | FLAG_VSYNC_HINT);
    InitWindow(SCREEN, SCREEN, "Angerblast");
    InitAudioDevice();
    SetTargetFPS(60);
    SetExitKey(KEY_NULL);

    settings_load();
    audio_load(&world.audio);
    game_reset(&world);
    world.state = STATE_TITLE;

#ifdef PLATFORM_WEB
    emscripten_set_main_loop(frame, 0, 1);   // browser drives the loop (no blocking while)
#else
    while (!WindowShouldClose() && !g_quit) frame();
#endif

    audio_unload(&world.audio);
    sprites_unload();
    CloseAudioDevice();
    CloseWindow();
    return 0;
}

// One frame of the game: input, state update, render. Native while-loops it; the web build
// registers it with emscripten_set_main_loop (browsers forbid a blocking loop).
static void frame(void) {
    {
        float dt = GetFrameTime(); if (dt > 0.05f) dt = 0.05f;
        GameWorld *w = &world;
        input_update_prompt_device();   // keeps button-prompt icons on the live device

        // per-biome music stream (crossfades on biome change; silent w/o files)
        audio_music_update(&w->audio, dt, BIOMES[w->biome].slug);

        // F: FPS overlay, any state, any time
        if (IsKeyPressed(KEY_F)) render_toggle_fps();

        // F5: hot-reload all data from assets/*.toml without restarting
        if (IsKeyPressed(KEY_F5)) {
            if (loader_load_all("assets")) TraceLog(LOG_INFO, "data reloaded from assets/");
            else TraceLog(LOG_WARNING, "data reload had errors (see stderr)");
        }

        // Pause toggle. Only PLAY / BOSS_INTRO can pause (and pause itself unpauses).
        // We remember which state to return to so a boss intro resumes correctly.
        static GameState resumeState = STATE_PLAY;
        if (input_pause_pressed()) {
            if (w->state == STATE_PLAY || w->state == STATE_BOSS_INTRO) {
                resumeState = w->state; w->state = STATE_PAUSE; w->pauseDragRow = -1;
            } else if (w->state == STATE_PAUSE) {
                w->state = resumeState; w->pauseDragRow = -1;
            }
        }

        switch (w->state) {
            case STATE_TITLE:
                if (input_confirm_pressed()) game_reset(w);
#ifndef PLATFORM_WEB
                // ESC from the menu quits the app (native only; you can't "quit" a browser tab);
                // if a run happened and wasn't already reported via the survey, write one out.
                if (IsKeyPressed(KEY_ESCAPE)) {
                    if (w->analytics.spawned > 0 && !w->analytics.surveyDone) {
                        char path[256]; analytics_write_report(w, "quit", path, sizeof(path));
                        TraceLog(LOG_INFO, "balance report written: %s", path);
                    }
                    g_quit = true;
                }
#endif
                break;
            case STATE_PLAY:
                // B: hand the stick to the bot (debug/spectate; also drives autoplay tests)
                if (IsKeyPressed(KEY_B)) w->pilot = (w->pilot == PILOT_HUMAN) ? PILOT_BOT : PILOT_HUMAN;
                update_play(w, dt);
                break;
            case STATE_BOSS_INTRO:
                update_play(w, dt);
                w->bossWarnTimer -= dt;
                if (w->bossWarnTimer <= 0) { boss_spawn(w); w->state = STATE_PLAY; }
                break;
            case STATE_LEVELUP: {
                fx_update(w, dt);
                audio_update_throttles(&w->audio, dt);
                Cards *cs = &w->cardsel;
                if (cs->phase == PICK_CARD) {
                    // bot picks immediately; human navigates + confirms
                    int botPick = pilot_pick_card(w);
                    bool confirm = input_confirm_pressed();
                    {   // mouse: hover follows the cursor; clicks only accept the card
                        // under it (geometry mirrors render_card_screen)
                        Vector2 mp = input_mouse();
                        int cw=200, ch=240, cgap=16, cx0=SCREEN/2-(cw*3+cgap*2)/2, mCard=-1;
                        for (int i=0;i<3;i++)
                            if (CheckCollisionPointRec(mp,(Rectangle){(float)(cx0+i*(cw+cgap)),210,(float)cw,(float)ch})) mCard=i;
                        if (mCard >= 0 && input_mouse_moved() && mCard != cs->hover)
                            { cs->hover = mCard; audio_play(&w->audio, SFX_UI_MOVE); }
                        if (IsMouseButtonPressed(MOUSE_BUTTON_LEFT))
                            { confirm = (mCard >= 0); if (mCard >= 0) cs->hover = mCard; }
                    }
                    if (botPick >= 0) { cs->hover = botPick; confirm = true; }
                    int step = input_menu_step();
                    if (step) { cs->hover = (cs->hover + step + 3) % 3; audio_play(&w->audio, SFX_UI_MOVE); }
                    if (IsKeyPressed(KEY_ONE))   cs->hover = 0;
                    if (IsKeyPressed(KEY_TWO))   cs->hover = 1;
                    if (IsKeyPressed(KEY_THREE)) cs->hover = 2;
                    if (confirm) {
                        audio_play(&w->audio, SFX_UI_SELECT);
                        bool placing = card_choose(w, cs->hover);
                        if (!placing) {                         // card applied immediately
                            cs->pending--;
                            if (cs->pending > 0) cards_roll(w); else w->state = STATE_PLAY;
                        }
                        // if placing, phase is now PICK_SOCKET (set in card_choose)
                    }
                } else { // PICK_SOCKET — pick a WEAPON (or drone); rune drops in its next open socket
                    int placeN = placement_count(w);
                    int botPick = pilot_pick_socket(w, placeN);
                    bool confirm = input_confirm_pressed();
                    {   // mouse hover/click on the placement boxes (mirrors render geometry)
                        Vector2 mp = input_mouse();
                        int bw=210, bh=96, bgap=14, bx0=SCREEN/2-(bw*2+bgap)/2, by0=210, mBox=-1;
                        for (int i=0;i<placeN;i++)
                            if (CheckCollisionPointRec(mp,(Rectangle){(float)(bx0+(i%2)*(bw+bgap)),(float)(by0+(i/2)*(bh+bgap)),(float)bw,(float)bh})) mBox=i;
                        if (mBox >= 0 && input_mouse_moved() && mBox != cs->hover)
                            { cs->hover = mBox; audio_play(&w->audio, SFX_UI_MOVE); }
                        if (IsMouseButtonPressed(MOUSE_BUTTON_LEFT))
                            { confirm = (mBox >= 0); if (mBox >= 0) cs->hover = mBox; }
                    }
                    if (botPick >= 0) { cs->hover = botPick; confirm = true; }
                    int step = input_menu_step();
                    if (IsKeyPressed(KEY_UP) || IsKeyPressed(KEY_W)) step = -1;
                    if (IsKeyPressed(KEY_DOWN) || IsKeyPressed(KEY_S)) step = 1;
                    if (step && placeN > 0) cs->hover = (cs->hover + step + placeN) % placeN;
                    if (confirm) {
                        int s = rune_first_open_socket(w, cs->hover);
                        if (s >= 0) {                               // weapon has a free socket
                            rune_place(w, cs->hover, s);
                            cs->pending--;
                            if (cs->pending > 0) { cards_roll(w); }  // resets phase to PICK_CARD
                            else w->state = STATE_PLAY;
                        }
                    }
                }
                break;
            }
            case STATE_DEAD:
                fx_update(w, dt);
                if (input_confirm_pressed()) game_reset(w);
                if (IsKeyPressed(KEY_ESCAPE)) w->state = STATE_TITLE;  // back to menu (report already written)
                break;
            case STATE_PAUSE: {
                // frozen: no simulation. The pause screen doubles as the settings panel;
                // codex.scroll is reused as the row cursor (reset on unpause is harmless).
                int rows = 5;
                if (IsKeyPressed(KEY_UP)   || IsKeyPressed(KEY_W)) w->codex.scroll = (w->codex.scroll + rows - 1) % rows;
                if (IsKeyPressed(KEY_DOWN) || IsKeyPressed(KEY_S)) w->codex.scroll = (w->codex.scroll + 1) % rows;
                if (pad_on()) {
                    if (IsGamepadButtonPressed(input_active_pad(), GAMEPAD_BUTTON_LEFT_FACE_UP))   w->codex.scroll = (w->codex.scroll + rows - 1) % rows;
                    if (IsGamepadButtonPressed(input_active_pad(), GAMEPAD_BUTTON_LEFT_FACE_DOWN)) w->codex.scroll = (w->codex.scroll + 1) % rows;
                }
                int step = input_menu_step();
                bool tap = input_confirm_pressed();
                // slider bar geometry -- MUST mirror render_pause()'s settings panel exactly
                // (sx2/sw2/sy2/rh/bw2 math) so hit-testing and drag line up with what's drawn.
                int sx2=120, sw2=SCREEN-240, sy2=410, rh=28, bw2=110;
                int bx2 = sx2+sw2-28-bw2-44;
                if (!IsMouseButtonDown(MOUSE_BUTTON_LEFT)) w->pauseDragRow = -1;   // released: stop dragging
                {   // mouse hover/click on the settings rows (mirrors render geometry)
                    Vector2 mp = input_mouse();
                    int mRow=-1;
                    for (int r=0;r<rows;r++)
                        if (CheckCollisionPointRec(mp,(Rectangle){(float)(sx2+8),(float)(sy2+36+r*rh-4),(float)(sw2-16),(float)(rh-4)})) mRow=r;
                    if (mRow >= 0 && input_mouse_moved()) w->codex.scroll = mRow;
                    if (IsMouseButtonPressed(MOUSE_BUTTON_LEFT)) {
                        tap = (mRow >= 0); if (mRow >= 0) w->codex.scroll = mRow;
                        if (mRow == 3 || mRow == 4) w->pauseDragRow = mRow;   // grabbed a volume slider
                    }
                }
                if (w->pauseDragRow == 3 || w->pauseDragRow == 4) {
                    // dragging: track the mouse continuously (not just the click frame), and
                    // keep tracking even if the cursor slips off the bar's exact y-range —
                    // standard slider UX once you've grabbed the handle.
                    float *v = w->pauseDragRow == 3 ? &SETTINGS.sfxVol : &SETTINGS.musicVol;
                    float mx = input_mouse().x;
                    float nv = (mx - bx2) / (float)bw2;
                    if (nv < 0) nv = 0;
                    if (nv > 1) nv = 1;
                    if (nv != *v) { *v = nv; settings_save();
                        if (w->pauseDragRow == 3) audio_play(&w->audio, SFX_UI_SELECT); }
                    tap = false;   // the drag itself already applied the change; don't also toggle/step below
                }
                if (step || tap) {
                    int row = w->codex.scroll;
                    if (row == 0) SETTINGS.showFps = !SETTINGS.showFps;
                    else if (row == 1) SETTINGS.hitstopMode = (SETTINGS.hitstopMode + (step < 0 ? 2 : 1)) % 3;
                    else if (row == 2) SETTINGS.autoCharge = !SETTINGS.autoCharge;
                    else {                              // volume sliders: steps nudge, taps cycle
                        float *v = row == 3 ? &SETTINGS.sfxVol : &SETTINGS.musicVol;
                        if (step) *v += step * 0.1f;
                        else { *v += 0.1f; if (*v > 1.049f) *v = 0; }   // tap wraps past 100%
                        if (*v < 0) *v = 0;
                        if (*v > 1) *v = 1;
                        *v = (float)((int)(*v * 10 + 0.5f)) / 10.0f;    // snap to 10% notches
                        if (row == 3) audio_play(&w->audio, SFX_UI_SELECT);   // preview the level
                    }
                    settings_save();
                }
                if (input_codex_pressed()) { w->codex.tab = 0; w->codex.scroll = 0; w->state = STATE_CODEX; }
                break;
            }
            case STATE_CODEX: {
                // encyclopedia navigation; closes back to pause
                if (input_codex_pressed() || IsKeyPressed(KEY_ESCAPE)) { w->state = STATE_PAUSE; break; }
                int step = 0;
                if (IsKeyPressed(KEY_LEFT)||IsKeyPressed(KEY_A)) step=-1;
                if (IsKeyPressed(KEY_RIGHT)||IsKeyPressed(KEY_D)) step=1;
                if (pad_on()) { if (IsGamepadButtonPressed(input_active_pad(),GAMEPAD_BUTTON_LEFT_TRIGGER_1)) step=-1;
                                if (IsGamepadButtonPressed(input_active_pad(),GAMEPAD_BUTTON_RIGHT_TRIGGER_1)) step=1; }
                if (step) { w->codex.tab=(w->codex.tab+step+CODEX_TAB_COUNT)%CODEX_TAB_COUNT; w->codex.scroll=0; }
                int sc = 0;
                if (IsKeyPressed(KEY_UP)||IsKeyPressed(KEY_W)) sc=-1;
                if (IsKeyPressed(KEY_DOWN)||IsKeyPressed(KEY_S)) sc=1;
                if (pad_on()) { if (IsGamepadButtonPressed(input_active_pad(),GAMEPAD_BUTTON_LEFT_FACE_UP)) sc=-1;
                                if (IsGamepadButtonPressed(input_active_pad(),GAMEPAD_BUTTON_LEFT_FACE_DOWN)) sc=1; }
                if (sc) w->codex.scroll += sc*3;
                if (w->codex.scroll < 0) w->codex.scroll = 0;
                break;
            }
            case STATE_SURVEY: {
                // Results screen (replaces old survey form). Write the report + submit high
                // score exactly once when we first enter this state.
                Analytics *a = &w->analytics;
                if (!a->surveyDone) {
                    a->surveyDone = true;
                    char path[256];
                    analytics_write_report(w, a->causeOfDeath, path, sizeof(path));
                    TraceLog(LOG_INFO, "balance report written: %s", path);
                    int rank = analytics_scores_submit(w);
                    render_set_result_rank(rank);
                }
                // any confirm press continues to the death/retry screen
                if (input_confirm_pressed() || IsKeyPressed(KEY_ENTER) ||
                    input_pause_pressed())
                    w->state = STATE_DEAD;
                break;
            }
        }

        // analytics: time spent in flow-interrupting modals
        if (w->state == STATE_LEVELUP) w->analytics.timeInLevelup += dt;
        else if (w->state == STATE_PAUSE) w->analytics.timeInPause += dt;
        else if (w->state == STATE_CODEX) w->analytics.timeInCodex += dt;

        // Screen shake decays every frame regardless of state, so a modal (level-up, death,
        // pause) never jitters forever. The offset is only *applied* while actively playing.
        w->shakeAmount *= 0.88f; if (w->shakeAmount < 0.1f) w->shakeAmount = 0;
        // full-screen shake removed by design: impacts speak through hitstop, hit-flash,
        // shockwave rings and rumble instead. shakeAmount still decays (analytics/rumble use it).
        Vector2 shake = { 0, 0 };

        BeginDrawing();
        render_arena(w);
        rlPushMatrix();
        rlTranslatef(shake.x, shake.y, 0);
        if (w->state == STATE_TITLE) render_title(w);
        else {
            render_world(w);
            render_hud(w);
            render_evo_banner(w);
            render_stage_banner(w);
            if (w->state == STATE_BOSS_INTRO) render_boss_warn(w);
            if (w->state == STATE_LEVELUP)    render_card_screen(w);
            if (w->state == STATE_DEAD)       render_dead(w);
            if (w->state == STATE_PAUSE)      render_pause(w);
            if (w->state == STATE_CODEX)      render_codex(w);
            if (w->state == STATE_SURVEY)     render_survey(w);
        }
        rlPopMatrix();
        EndDrawing();
    }
}

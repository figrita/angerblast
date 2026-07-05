// audio.h - the sound bank: wav files from assets/audio/ (tools/audio/gen_sfx.py)
// with a procedural synth fallback per event, plus per-biome streamed music.
#ifndef ANGERBLAST_AUDIO_H
#define ANGERBLAST_AUDIO_H

#include "world.h"

void audio_load(AudioBank *a);            // load wavs (synthesise any that are missing)
void audio_unload(AudioBank *a);
void audio_update_throttles(AudioBank *a, float dt);

// play one event: applies the event's volume, random pitch variance (own RNG —
// never touches the sim RNG) and the global sfx volume setting
void audio_play(AudioBank *a, SfxId id);
// explicit pitch/volume control (charge pulses deepen as the shot fills)
void audio_play_pitched(AudioBank *a, SfxId id, float pitch, float volMul);

// throttled one-shots used by hot paths (kills, pickups) to avoid sound spam
void audio_play_kill(AudioBank *a);
void audio_play_pickup(AudioBank *a);

// stream + crossfade the biome's music (assets/audio/music_<slug>.ogg, falling
// back to music.ogg; silent when neither exists). Call every frame.
void audio_music_update(AudioBank *a, float dt, const char *biomeSlug);

#endif // ANGERBLAST_AUDIO_H

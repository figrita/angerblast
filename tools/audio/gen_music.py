#!/usr/bin/env python3
"""Render the per-biome music loops from editable note-event songs.

Two stages:
  1. compose: the procedural tracker writes tools/audio/songs/<slug>.json —
     note events on a 16th-note grid (t/d in steps, n = midi, v = velocity).
     Only runs when the JSON is missing (or --recompose): hand edits from the
     piano-roll editor (tools/audio/piano_roll.html, build_piano_roll.py) are
     the source of truth once they exist.
  2. render: synthesize the events (bass/pad/arp + kick/snare/hat drums) at
     22050 Hz mono and encode assets/audio/music_<slug>.ogg via ffmpeg.
     Release tails WRAP onto the loop start — loops are seamless by math.

Usage: gen_music.py [--only SLUG] [--recompose]
"""

import argparse
import json
import subprocess
import wave
from pathlib import Path

import numpy as np

REPO = Path(__file__).resolve().parent.parent.parent
OUT = REPO / "assets" / "audio"
SONGS = Path(__file__).resolve().parent / "songs"
SR = 22050

MODES = {
    "aeolian":    [0, 2, 3, 5, 7, 8, 10],
    "phrygian":   [0, 1, 3, 5, 7, 8, 10],
    "lydian":     [0, 2, 4, 6, 7, 9, 11],
    "dorian":     [0, 2, 3, 5, 7, 9, 10],
    "mixolydian": [0, 2, 4, 5, 7, 9, 10],
}

DEFAULT_FX = dict(arp_wave="sq", vibrato_rate=0.0, vibrato_depth=0.0,
                   chorus=0.0, glide=0.0, filter_sweep_bars=0.0)

BIOMES = {
    # arp_wave: lead timbre per biome (sq/tri/saw/pulse25). vibrato_rate (Hz) +
    # vibrato_depth (semitones) wobble the lead pitch. chorus (0-1) blends in
    # detuned+delayed voices (pad always has a little; this adds more, and to
    # the lead too). glide (seconds) portamento-slides into each lead note from
    # the previous one instead of hard-retriggering pitch.
    "01_void":    dict(root=45, mode="aeolian",    bpm=84,  prog=[0, 5, 2, 6],
                       bass_wave="tri", arp_duty=0.25, arp_rate=2, hat_rate=2,
                       pad_gain=0.30, arp_gain=0.18, drum_gain=0.5, seed=101,
                       arp_wave="tri", vibrato_rate=4.5, vibrato_depth=0.12,
                       chorus=0.35, glide=0.05, filter_sweep_bars=8.0),
    "02_ember":   dict(root=40, mode="phrygian",   bpm=104, prog=[0, 1, 0, 6],
                       bass_wave="sq",  arp_duty=0.30, arp_rate=4, hat_rate=4,
                       pad_gain=0.22, arp_gain=0.22, drum_gain=0.8, seed=202,
                       arp_wave="saw", vibrato_rate=0.0, vibrato_depth=0.0,
                       chorus=0.0, glide=0.0, filter_sweep_bars=0.0),
    "03_frost":   dict(root=48, mode="lydian",     bpm=76,  prog=[0, 3, 4, 1],
                       bass_wave="tri", arp_duty=0.50, arp_rate=2, hat_rate=1,
                       pad_gain=0.34, arp_gain=0.16, drum_gain=0.35, seed=303,
                       arp_wave="tri", vibrato_rate=5.5, vibrato_depth=0.2,
                       chorus=0.55, glide=0.10, filter_sweep_bars=16.0),
    "04_tempest": dict(root=50, mode="dorian",     bpm=112, prog=[0, 3, 5, 4],
                       bass_wave="sq",  arp_duty=0.25, arp_rate=4, hat_rate=4,
                       pad_gain=0.24, arp_gain=0.24, drum_gain=0.75, seed=404,
                       arp_wave="pulse25", vibrato_rate=6.5, vibrato_depth=0.08,
                       chorus=0.2, glide=0.0, filter_sweep_bars=4.0),
    "05_abyss":   dict(root=38, mode="phrygian",   bpm=68,  prog=[0, 0, 5, 6],
                       bass_wave="tri", arp_duty=0.50, arp_rate=1, hat_rate=1,
                       pad_gain=0.36, arp_gain=0.12, drum_gain=0.35, seed=505,
                       arp_wave="sine", vibrato_rate=3.2, vibrato_depth=0.25,
                       chorus=0.6, glide=0.16, filter_sweep_bars=16.0),
    "06_dino":    dict(root=43, mode="mixolydian", bpm=96,  prog=[0, 4, 0, 3],
                       bass_wave="sq",  arp_duty=0.30, arp_rate=2, hat_rate=2,
                       pad_gain=0.26, arp_gain=0.20, drum_gain=0.7, seed=606,
                       arp_wave="saw", vibrato_rate=0.0, vibrato_depth=0.0,
                       chorus=0.15, glide=0.03, filter_sweep_bars=0.0),
}

BARS = 16
STEPS = BARS * 16     # 16th-note grid


def midi_hz(m):
    return 440.0 * 2 ** ((m - 69) / 12)


class Rng:
    def __init__(s, seed): s.s = seed & 0xFFFFFFFF
    def next(s):
        s.s = (s.s * 1664525 + 1013904223) & 0xFFFFFFFF
        return s.s
    def pick(s, seq): return seq[s.next() % len(seq)]
    def chance(s, p): return (s.next() % 1000) < p * 1000


# ---- stage 1: compose a song (note events on the 16th grid) -----------------

def compose(slug, p):
    rng = Rng(p["seed"])
    scale = MODES[p["mode"]]
    prog = [p["prog"][b % len(p["prog"])] for b in range(BARS)]

    def deg_note(degree, octave=0):
        return p["root"] + scale[degree % 7] + 12 * (degree // 7 + octave)

    bass, pad, arp, drums = [], [], [], []
    for bar, deg in enumerate(prog):
        b0 = bar * 16
        # bass: eighth pulses with passing fifths
        for e8 in range(8):
            if e8 % 2 == 1 and not rng.chance(0.35):
                continue
            note = deg_note(deg, -1)
            if e8 == 6 and rng.chance(0.5):
                note = deg_note(deg + 4, -1)
            bass.append({"t": b0 + e8 * 2, "n": note, "d": 2, "v": 1.0})
        # pad: sustained triad per bar
        for iv in (0, 2, 4):
            pad.append({"t": b0, "n": deg_note(deg + iv, 0), "d": 16, "v": 1.0})
        # arp: chord tones with rests + eighth-note echoes
        spb_steps = 4 // max(p["arp_rate"], 1) if p["arp_rate"] else 0
        if p["arp_rate"]:
            step = 4 // p["arp_rate"]
            for st in range(0, 16, step):
                if rng.chance(0.25):
                    continue
                off = rng.pick([0, 2, 4, 7]) + (7 if rng.chance(0.15) else 0)
                arp.append({"t": b0 + st, "n": deg_note(deg + off, 1), "d": step, "v": 1.0})
                if rng.chance(0.5):
                    arp.append({"t": b0 + st + 2, "n": deg_note(deg + off, 1), "d": step, "v": 0.4})
        # drums
        for beat in range(4):
            t0 = b0 + beat * 4
            if beat in (0, 2): drums.append({"t": t0, "k": "kick", "v": 0.9})
            if beat in (1, 3): drums.append({"t": t0, "k": "snare", "v": 0.5})
            for h in range(p["hat_rate"]):
                drums.append({"t": t0 + h * 4 // max(p["hat_rate"], 1), "k": "hat", "v": 1.0})

    return {
        "slug": slug, "bpm": p["bpm"], "bars": BARS,
        "meta": {k: p[k] for k in ("bass_wave", "arp_duty", "pad_gain", "arp_gain", "drum_gain",
                                    "arp_wave", "vibrato_rate", "vibrato_depth", "chorus", "glide",
                                    "filter_sweep_bars")},
        "tracks": {"bass": bass, "pad": pad, "arp": arp},
        "drums": drums,
    }


# ---- stage 2: render a song to a seamless loop ------------------------------

def osc(kind, freq, n, duty=0.5):
    t = np.arange(n) / SR
    ph = (freq * t) % 1.0
    if kind == "sq":      return np.where(ph < duty, 1.0, -1.0)
    if kind == "pulse25": return np.where(ph < 0.25, 1.0, -1.0)
    if kind == "tri":     return 2.0 * np.abs(2.0 * ph - 1.0) - 1.0
    if kind == "saw":     return 2.0 * ph - 1.0
    return np.sin(2 * np.pi * freq * t)


def osc_vibrato(kind, freq, n, duty=0.5, vibrato_rate=0.0, vibrato_depth=0.0,
                 glide_from=None, glide_time=0.0):
    """Like osc(), but the instantaneous frequency can wobble (vibrato: an LFO on
    pitch, in semitones) and/or glide (portamento: ramp from a previous note's
    pitch up/down into this one over glide_time seconds)."""
    t = np.arange(n) / SR
    f = np.full(n, freq)
    if glide_from and glide_time > 0:
        ramp_n = min(n, int(glide_time * SR))
        frac = np.linspace(0.0, 1.0, ramp_n)
        f[:ramp_n] = glide_from + (freq - glide_from) * frac
    if vibrato_rate > 0 and vibrato_depth > 0:
        semis = vibrato_depth * np.sin(2 * np.pi * vibrato_rate * t)
        f = f * (2.0 ** (semis / 12.0))
    ph = np.cumsum(f) / SR % 1.0
    if kind == "sq":      return np.where(ph < duty, 1.0, -1.0)
    if kind == "pulse25": return np.where(ph < 0.25, 1.0, -1.0)
    if kind == "tri":     return 2.0 * np.abs(2.0 * ph - 1.0) - 1.0
    if kind == "saw":     return 2.0 * ph - 1.0
    return np.sin(2 * np.pi * ph)


def sweep_lowpass(sig, sweep_period_s, cutoff_lo=300.0, cutoff_hi=6000.0, phase=0.0):
    """One-pole lowpass whose cutoff rides a slow LFO (period = several bars) between
    cutoff_lo and cutoff_hi -- a bars-long filter sweep (classic build/breakdown
    "filter opening up" move) instead of a static tone."""
    n = len(sig)
    t = np.arange(n) / SR
    lfo = (np.sin(2 * np.pi * t / sweep_period_s + phase) + 1) / 2       # 0..1
    cutoff = cutoff_lo * (cutoff_hi / cutoff_lo) ** lfo                  # log sweep
    alpha = 1.0 - np.exp(-2 * np.pi * cutoff / SR)
    out = np.empty(n)
    prev = 0.0
    for i in range(n):
        prev = prev + alpha[i] * (sig[i] - prev)
        out[i] = prev
    return out


def chorus_mix(voice_fn, freq, n, amount):
    """Sum the dry voice with 2 detuned+delayed copies, blended by `amount` (0-1).
    Detune spreads the voices in pitch (like 2+ oscillators drifting together);
    the small delay gives the doubled/ensemble comb-filtered chorus character."""
    dry = voice_fn(freq)
    if amount <= 0:
        return dry
    wet = voice_fn(freq * 1.006) + voice_fn(freq * 0.994)
    delay = int(0.012 * SR)
    wet_delayed = np.zeros(n)
    wet_delayed[delay:] = wet[:n - delay] if n > delay else 0
    return dry * (1 - amount * 0.5) + wet_delayed * (amount * 0.5)


def env(n, a, d, sustain=0.0, s_level=0.6):
    t = np.arange(n) / SR
    e = np.minimum(t / max(a, 1e-4), 1.0) * np.exp(-np.maximum(t - a, 0) / max(d, 1e-4))
    if sustain > 0:
        e = np.maximum(e, np.where(t < sustain, s_level, s_level * np.exp(-(t - sustain) * 3)))
    return e


def add(buf, start, sig):
    n = len(buf)
    i = start % n
    first = min(len(sig), n - i)
    buf[i:i + first] += sig[:first]
    if len(sig) > first:
        buf[:len(sig) - first] += sig[first:]   # wrap: keeps the loop seamless


def render(song):
    m = song["meta"]
    spb = 60.0 / song["bpm"]
    step_s = spb / 4
    total = int(round(song["bars"] * spb * 4 * SR))
    buf = np.zeros(total, dtype=np.float64)

    for ev in song["tracks"]["bass"]:
        n = int(ev["d"] * step_s * 0.9 * SR)
        sig = osc(m["bass_wave"], midi_hz(ev["n"]), n) * env(n, 0.004, 0.20) * 0.30 * ev["v"]
        add(buf, int(ev["t"] * step_s * SR), sig)

    for ev in song["tracks"]["pad"]:
        dur = ev["d"] * step_s
        n = int((dur + 0.5) * SR)
        f = midi_hz(ev["n"])
        sig = (osc("tri", f * 1.003, n) + osc("tri", f * 0.997, n)) / 2
        sig *= env(n, 0.30, dur, sustain=dur * 0.8) / 3 * m["pad_gain"] * ev["v"]
        add(buf, int(ev["t"] * step_s * SR), sig)

    arp_wave = m.get("arp_wave", "sq")
    vib_rate = m.get("vibrato_rate", 0.0)
    vib_depth = m.get("vibrato_depth", 0.0)
    chorus_amt = m.get("chorus", 0.0)
    glide_t = m.get("glide", 0.0)
    filter_sweep_bars = m.get("filter_sweep_bars", 0.0)
    arp_buf = np.zeros(total, dtype=np.float64) if filter_sweep_bars > 0 else buf
    prev_freq = None
    for ev in sorted(song["tracks"]["arp"], key=lambda e: e["t"]):
        n = int(ev["d"] * step_s * 0.9 * SR)
        freq = midi_hz(ev["n"])
        glide_from = prev_freq if (glide_t > 0 and prev_freq is not None) else None

        def voice(f, _n=n, _duty=m["arp_duty"], _gf=glide_from):
            return osc_vibrato(arp_wave, f, _n, duty=_duty, vibrato_rate=vib_rate,
                                vibrato_depth=vib_depth, glide_from=_gf, glide_time=glide_t)

        sig = chorus_mix(voice, freq, n, chorus_amt) if chorus_amt > 0 else voice(freq)
        sig = sig * env(n, 0.002, ev["d"] * step_s * 0.6) * m["arp_gain"] * ev["v"]
        add(arp_buf, int(ev["t"] * step_s * SR), sig)
        prev_freq = freq

    if filter_sweep_bars > 0:
        sweep_period_s = filter_sweep_bars * 4 * spb
        buf += sweep_lowpass(arp_buf, sweep_period_s)

    nrng = np.random.default_rng(1234)
    kick_len = int(0.12 * SR)
    kick = np.sin(2 * np.pi * np.cumsum(np.linspace(95, 38, kick_len)) / SR) * env(kick_len, 0.001, 0.09)
    snare_len = int(0.10 * SR)
    snare = nrng.uniform(-1, 1, snare_len) * env(snare_len, 0.001, 0.05) * 0.8
    hat_len = int(0.03 * SR)
    hat = nrng.uniform(-1, 1, hat_len) * env(hat_len, 0.001, 0.012) * 0.35
    kits = {"kick": kick, "snare": snare, "hat": hat}
    g = m["drum_gain"]
    for ev in song["drums"]:
        add(buf, int(ev["t"] * step_s * SR), kits[ev["k"]] * ev["v"] * g)

    buf = np.tanh(buf * 1.4)
    buf *= 0.85 / max(np.abs(buf).max(), 1e-6)
    return buf


def write_ogg(slug, buf):
    OUT.mkdir(parents=True, exist_ok=True)
    tmp = OUT / f"_music_{slug}.wav"
    with wave.open(str(tmp), "wb") as f:
        f.setnchannels(1); f.setsampwidth(2); f.setframerate(SR)
        f.writeframes((np.clip(buf, -1, 1) * 32767).astype("<i2").tobytes())
    ogg = OUT / f"music_{slug}.ogg"
    subprocess.run(["ffmpeg", "-y", "-loglevel", "error", "-i", str(tmp),
                    "-c:a", "libvorbis", "-q:a", "3", str(ogg)], check=True)
    tmp.unlink()
    print(f"wrote {ogg} ({ogg.stat().st_size // 1024} KB, {len(buf)/SR:.1f}s loop)")


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--only", default=None)
    ap.add_argument("--recompose", action="store_true",
                    help="overwrite the song JSONs with fresh procedural compositions")
    args = ap.parse_args()
    SONGS.mkdir(parents=True, exist_ok=True)
    for slug, p in BIOMES.items():
        if args.only and args.only not in slug:
            continue
        sj = SONGS / f"{slug}.json"
        if sj.exists() and not args.recompose:
            song = json.loads(sj.read_text())
        else:
            song = compose(slug, p)
            sj.write_text(json.dumps(song, separators=(",", ":")))
            print(f"composed {sj}")
        write_ogg(slug, render(song))


if __name__ == "__main__":
    main()

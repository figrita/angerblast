#!/usr/bin/env python3
"""Generate the game's SFX bank: sfxr-style parametric synthesis -> 16-bit wavs.

Each preset below is a small parameter dict driving one generic synth
(waveform + freq slide + arpeggio + vibrato + punch/decay envelope + filter
sweep). Deterministic: same params -> same wav, so the sound "palette" is this
file, tuned in code review like the sprite prompts.

Files land in assets/audio/<name>.wav, matching the SfxDef table in
src/platform/audio.c (which falls back to bleeps for any missing file).

Usage: gen_sfx.py [--only NAME] [--outdir DIR]
"""

import argparse
import wave
from pathlib import Path

import numpy as np

REPO = Path(__file__).resolve().parent.parent.parent
SR = 22050


def synth(wave_kind="square", freq=440.0, slide=0.0, duty=0.5,
          arp=None, arp_time=0.06,
          vib_speed=0.0, vib_depth=0.0,
          length=0.2, punch=0.0, decay=6.0, attack=0.0,
          lp=None, lp_sweep=1.0, hp=None,
          noise_seed=1, sub=0.0, gain=0.8):
    """One retro one-shot. arp = list of absolute freqs stepped every arp_time.
    lp/hp in Hz (None = off); lp_sweep multiplies lp per second (0.1 = closes fast).
    punch adds an initial spike on top of the exponential decay envelope."""
    n = int(SR * length)
    t = np.arange(n) / SR

    f = np.full(n, float(freq))
    f *= np.exp(np.log(2) * slide * t)              # slide in octaves/sec
    if arp:
        idx = np.minimum((t / arp_time).astype(int), len(arp) - 1)
        f = np.array(arp, dtype=float)[idx]
    if vib_depth > 0:
        f *= 1.0 + vib_depth * np.sin(2 * np.pi * vib_speed * t)

    ph = np.cumsum(f) / SR                          # integrated phase (cycles)
    frac = ph % 1.0
    if wave_kind == "square":
        s = np.where(frac < duty, 1.0, -1.0)
    elif wave_kind == "saw":
        s = 2.0 * frac - 1.0
    elif wave_kind == "sine":
        s = np.sin(2 * np.pi * ph)
    else:  # noise: value noise stepped at the (sliding) frequency
        rng = np.random.default_rng(noise_seed)
        steps = np.floor(ph * 2).astype(int)
        table = rng.uniform(-1, 1, steps.max() + 1)
        s = table[steps]
    if sub > 0:
        s = s + sub * np.sin(np.pi * ph)            # one octave down, thickens

    env = np.exp(-decay * t)
    if punch > 0:
        env += punch * np.exp(-60.0 * t)
    if attack > 0:
        env *= np.clip(t / attack, 0, 1)
    s = s * env

    if lp:                                          # one-pole lowpass, swept cutoff
        cut = lp * np.power(lp_sweep, t)
        alpha = np.clip(2 * np.pi * cut / SR, 0, 1)
        out = np.empty_like(s); acc = 0.0
        for i in range(n):
            acc += alpha[i] * (s[i] - acc)
            out[i] = acc
        s = out
    if hp:                                          # one-pole highpass
        alpha = 1.0 / (1.0 + 2 * np.pi * hp / SR)
        out = np.empty_like(s); prev_in = 0.0; acc = 0.0
        for i in range(n):
            acc = alpha * (acc + s[i] - prev_in)
            prev_in = s[i]
            out[i] = acc
        s = out

    peak = np.abs(s).max()
    if peak > 0:
        s = s / peak * gain
    s[-min(n, 256):] *= np.linspace(1, 0, min(n, 256))   # declick tail
    return s


# name -> params. Names must match the SfxDef table in src/platform/audio.c.
PRESETS = {
    # softer, rounder pew: lower start, hard downward slide, closed-down filter
    "shoot":     dict(wave_kind="square", freq=560, slide=-9.0, duty=0.42,
                      length=0.09, decay=30, lp=2400, lp_sweep=0.04, sub=0.5, gain=0.5),
    # charge pulse: short round tick; the engine pitches it 1.25 -> 0.6 as charge fills
    "charge":    dict(wave_kind="sine", freq=460, length=0.09, decay=22,
                      sub=0.7, gain=0.5),
    # step 1 thunk
    "charge_step": dict(wave_kind="square", freq=220, slide=-2.0, duty=0.45,
                        length=0.16, punch=0.4, decay=15, lp=1500, lp_sweep=0.2, sub=0.6),
    # full-charge boom-hum: deep, resonant, unmistakable
    "charge_full": dict(wave_kind="sine", freq=130, slide=-0.6, length=0.42,
                        punch=0.3, decay=6, sub=0.9, vib_speed=13, vib_depth=0.02, gain=0.85),
    # charged release: a real boom (engine drops pitch further for full shots)
    "shoot_big": dict(wave_kind="noise", freq=200, slide=-2.2, length=0.5,
                      punch=0.7, decay=6.5, lp=2000, lp_sweep=0.05, sub=0.9,
                      noise_seed=5, gain=0.95),
    "hit":       dict(wave_kind="noise", freq=340, slide=-3.0, length=0.22,
                      punch=0.5, decay=17, lp=1300, lp_sweep=0.15),
    "explode":   dict(wave_kind="noise", freq=180, slide=-1.5, length=0.55,
                      punch=0.6, decay=7, lp=2400, lp_sweep=0.03, sub=0.4, noise_seed=7),
    "pickup":    dict(wave_kind="square", freq=660, arp=[660, 990], arp_time=0.045,
                      duty=0.5, length=0.14, decay=14, gain=0.6),
    "levelup":   dict(wave_kind="square", freq=523, arp=[523, 659, 784, 1046],
                      arp_time=0.07, duty=0.4, length=0.42, decay=4.5,
                      vib_speed=9, vib_depth=0.004, gain=0.75),
    "boss_die":  dict(wave_kind="noise", freq=140, slide=-1.2, length=1.3,
                      punch=0.5, decay=2.8, lp=1500, lp_sweep=0.08, sub=0.8, noise_seed=3),
    "dodge":     dict(wave_kind="noise", freq=900, slide=2.5, length=0.18,
                      decay=12, hp=500, lp=6000, lp_sweep=1.6, gain=0.5, noise_seed=11),
    "ui_move":   dict(wave_kind="square", freq=990, duty=0.5, length=0.045,
                      decay=45, gain=0.45),
    "ui_select": dict(wave_kind="square", freq=660, arp=[660, 990], arp_time=0.05,
                      duty=0.5, length=0.12, decay=16, gain=0.55),
}


def write_wav(path, samples):
    path.parent.mkdir(parents=True, exist_ok=True)
    data = (np.clip(samples, -1, 1) * 32767).astype("<i2")
    with wave.open(str(path), "wb") as f:
        f.setnchannels(1)
        f.setsampwidth(2)
        f.setframerate(SR)
        f.writeframes(data.tobytes())


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--only", default=None, help="only presets whose name contains this")
    ap.add_argument("--outdir", default=str(REPO / "assets" / "audio"))
    args = ap.parse_args()
    out = Path(args.outdir)
    for name, params in PRESETS.items():
        if args.only and args.only not in name:
            continue
        p = out / f"{name}.wav"
        write_wav(p, synth(**params))
        print(f"wrote {p} ({p.stat().st_size} bytes)")


if __name__ == "__main__":
    main()

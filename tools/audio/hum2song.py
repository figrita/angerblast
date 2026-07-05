#!/usr/bin/env python3
"""hum2song: sing/hum a melody -> quantized, harmonized biome song.

Pipeline:
  1. Basic Pitch (Spotify, local ONNX) transcribes the recording to notes.
  2. Tempo fit: unless --bpm is given, search 60-140 for the tempo whose 16th
     grid best explains the note onsets, then snap onsets/durations to it.
  3. Pitches snap to the style biome's scale and register.
  4. Harmonize: each bar gets the chord (scale triad) that best fits the
     melody notes in it -> pad triads + eighth-pulse bass; drums follow the
     style biome's pattern.
  5. Writes tools/audio/songs/<slug>.json (old file backed up to .bak),
     renders assets/audio/music_<slug>.ogg, and rebuilds piano_roll.html so
     the result is immediately editable.

MUST run under the basic-pitch venv (Python 3.14 can't build its deps):
  ~/venvs/basicpitch/bin/python tools/audio/hum2song.py hum.wav --slug 01_void
Use a biome slug to replace that biome's music, or any other slug for a
standalone track.
"""

import argparse
import json
import math
import shutil
import subprocess
import sys
from pathlib import Path

HERE = Path(__file__).resolve().parent
sys.path.insert(0, str(HERE))
import gen_music as gm

try:
    from basic_pitch.inference import predict
except ImportError:
    sys.exit("basic_pitch not importable — run with ~/venvs/basicpitch/bin/python")


def transcribe(path):
    """-> list of (onset_s, dur_s, midi_pitch, amplitude), cleaned up."""
    # normalize any container/codec (browser webm/opus, m4a...) to wav first
    import tempfile
    tmp = tempfile.NamedTemporaryFile(suffix=".wav", delete=False)
    tmp.close()
    subprocess.run(["ffmpeg", "-y", "-loglevel", "error", "-i", str(path),
                    "-ac", "1", "-ar", "22050", tmp.name], check=True)
    _, _, note_events = predict(tmp.name)
    Path(tmp.name).unlink()
    notes = []
    for start, end, pitch, amp, _bends in note_events:
        if end - start < 0.07 or amp < 0.12:      # breaths, clicks, ghosts
            continue
        notes.append((float(start), float(end - start), int(pitch), float(amp)))
    notes.sort()
    if not notes:
        sys.exit("no notes survived transcription — sing louder / closer?")
    return notes


def fit_bpm(notes, forced):
    """Pick the bpm whose 16th grid the onsets sit closest to."""
    if forced:
        return float(forced)
    t0 = notes[0][0]
    onsets = [n[0] - t0 for n in notes]
    cands = []
    for bpm10 in range(600, 1401, 5):             # 60.0 .. 140.0 in 0.5 steps
        bpm = bpm10 / 10.0
        step = 60.0 / bpm / 4
        err = sum(min((o % step), step - (o % step)) / step for o in onsets) / len(onsets)
        cands.append((err, bpm))
    best_err = min(e for e, _ in cands)
    # slow grids explain anything; among near-ties take the FASTEST tempo so
    # the 16th grid actually resolves the phrasing
    best = max(b for e, b in cands if e < best_err * 1.15 + 0.02)
    print(f"tempo fit: {best:.1f} bpm (grid error {best_err:.3f})")
    if best >= 139.5 or best_err > 0.18:
        print("  (fit is unsure — free-form humming drifts; consider forcing --bpm,"
              " e.g. the biome tempo)")
    return best


def quantize(notes, bpm, anchored=False):
    """-> list of (t_step, d_steps, pitch, vel) on the 16th grid, deduped.
    anchored: the recording began exactly on bar 1 (hum studio) — quantize
    against absolute time so rests and bar alignment are preserved."""
    step = 60.0 / bpm / 4
    t0 = 0.0 if anchored else notes[0][0]
    grid = {}
    for onset, dur, pitch, amp in notes:
        t = max(0, round((onset - t0) / step))
        d = min(16, max(1, round(dur / step)))
        if t not in grid or amp > grid[t][2]:     # collision: keep the louder one
            grid[t] = (d, pitch, amp)
    return [(t, d, p, min(1.0, a + 0.3)) for t, (d, p, a) in sorted(grid.items())]


def snap_scale(events, root, mode):
    """Snap pitches to the mode's scale and center the melody an octave above root."""
    scale = gm.MODES[mode]
    tones = sorted({(root + s + o * 12) for s in scale for o in range(-3, 5)})
    med = sorted(e[2] for e in events)[len(events) // 2]
    shift = round(((root + 12) - med) / 12) * 12   # whole octaves only: keep the contour
    out = []
    for t, d, p, v in events:
        p += shift
        p = min(tones, key=lambda s: abs(s - p))
        out.append({"t": t, "n": p, "d": d, "v": round(v, 2)})
    return out


def harmonize(melody, bars, root, mode):
    """Per bar: the scale triad holding the most melody notes -> pad + bass."""
    scale = gm.MODES[mode]

    def chord_tones(deg):
        return {(root + scale[(deg + iv) % 7]) % 12 for iv in (0, 2, 4)}

    pad, bass, degs = [], [], []
    for bar in range(bars):
        b0, b1 = bar * 16, bar * 16 + 16
        in_bar = [e for e in melody if b0 <= e["t"] < b1]
        best_deg, best_score = 0, -1
        for deg in range(7):
            ct = chord_tones(deg)
            score = sum(e["d"] for e in in_bar if e["n"] % 12 in ct)
            if bar and deg == degs[-1]:
                score -= 1                         # nudge away from droning one chord
            if score > best_score:
                best_deg, best_score = deg, score
        degs.append(best_deg)
        for iv in (0, 2, 4):
            deg = best_deg + iv
            pad.append({"t": b0, "n": root + scale[deg % 7] + 12 * (deg // 7), "d": 16, "v": 1.0})
        for e8 in range(0, 8, 2):                  # bass: root pulses on the beats
            bass.append({"t": b0 + e8 * 2, "n": root + scale[best_deg % 7] - 12, "d": 2, "v": 1.0})
    print("chords:", " ".join(["i ii iii iv v vi vii".split()[d] for d in degs]))
    return pad, bass


def style_prog_harmony(bars, style):
    """pad + bass straight from the style's chord progression (what the hum
    studio played under the take) instead of re-deriving chords."""
    scale = gm.MODES[style["mode"]]
    root = style["root"]
    pad, bass = [], []
    for bar in range(bars):
        deg0 = style["prog"][bar % len(style["prog"])]
        b0 = bar * 16
        for iv in (0, 2, 4):
            deg = deg0 + iv
            pad.append({"t": b0, "n": root + scale[deg % 7] + 12 * (deg // 7), "d": 16, "v": 1.0})
        for e8 in range(0, 8, 2):
            bass.append({"t": b0 + e8 * 2, "n": root + scale[deg0 % 7] - 12, "d": 2, "v": 1.0})
    return pad, bass


def drums_for(bars, style):
    ev = []
    for bar in range(bars):
        b0 = bar * 16
        for beat in range(4):
            t0 = b0 + beat * 4
            if beat in (0, 2): ev.append({"t": t0, "k": "kick", "v": 0.9})
            if beat in (1, 3): ev.append({"t": t0, "k": "snare", "v": 0.5})
            for h in range(style["hat_rate"]):
                ev.append({"t": t0 + h * 4 // max(style["hat_rate"], 1), "k": "hat", "v": 1.0})
    return ev


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("audio", help="your hum/singing (wav/mp3/ogg/m4a)")
    ap.add_argument("--slug", default="hum", help="output song slug (a biome slug replaces its music)")
    ap.add_argument("--style", default=None, help="biome preset for mode/voices (default: slug if it's a biome, else 01_void)")
    ap.add_argument("--bpm", type=float, default=0, help="force tempo instead of fitting")
    ap.add_argument("--no-drums", action="store_true")
    ap.add_argument("--anchored", action="store_true",
                    help="recording started exactly on bar 1 (hum studio): keep absolute grid + the style's own chords")
    ap.add_argument("--reharmonize", action="store_true",
                    help="with --anchored: derive chords from the melody anyway")
    args = ap.parse_args()
    if args.anchored and not args.bpm:
        sys.exit("--anchored requires --bpm (the studio backing tempo)")

    style_key = args.style or (args.slug if args.slug in gm.BIOMES else "01_void")
    style = gm.BIOMES[style_key]
    root, mode = style["root"], style["mode"]
    print(f"style: {style_key} ({mode} on midi {root})")

    notes = transcribe(args.audio)
    print(f"transcribed {len(notes)} notes over {notes[-1][0] - notes[0][0]:.1f}s")
    bpm = fit_bpm(notes, args.bpm)
    events = quantize(notes, bpm, anchored=args.anchored)
    melody = snap_scale(events, root, mode)

    bars = max(4, math.ceil((max(e["t"] + e["d"] for e in melody)) / 16))
    bars = ((bars + 3) // 4) * 4                   # round up to 4-bar phrases
    if args.anchored and not args.reharmonize:
        pad, bass = style_prog_harmony(bars, style)
        print("chords: style progression (what the studio played under you)")
    else:
        pad, bass = harmonize(melody, bars, root, mode)

    song = {
        "slug": args.slug, "bpm": round(bpm, 1), "bars": bars,
        "meta": {k: style[k] for k in ("bass_wave", "arp_duty", "pad_gain", "arp_gain", "drum_gain")},
        "tracks": {"bass": bass, "pad": pad, "arp": melody},
        "drums": [] if args.no_drums else drums_for(bars, style),
    }

    gm.SONGS.mkdir(parents=True, exist_ok=True)
    sj = gm.SONGS / f"{args.slug}.json"
    if sj.exists():
        shutil.copy2(sj, sj.with_suffix(".json.bak"))
        print(f"backed up old song to {sj.with_suffix('.json.bak')}")
    sj.write_text(json.dumps(song, separators=(",", ":")))
    print(f"wrote {sj}")

    gm.write_ogg(args.slug, gm.render(song))
    subprocess.run([sys.executable, str(HERE / "build_piano_roll.py")], check=False)


if __name__ == "__main__":
    main()

#!/usr/bin/env python3
"""Rebuild tools/audio/songs/<slug>.json for all 6 biomes from introsong.mid's real
melodic material, then call gen_music.py to render them to assets/audio/music_*.ogg.

introsong.mid (10 tracks, 120bpm, ticks_per_beat=4 so 1 MIDI tick == 1 engine 16th
step) supplies TWO distinct melodic layers that both get used, layered, here:
  - ARP:  track 6 "Retro Synth 9" -- a continuous 270-note 16th-note line running
          the whole 40-bar song, with its OWN bar-to-bar variation already baked
          in (bars 3 and 7 of its 4-bar cell are denser turnaround fills). Used
          as a constantly-running rhythmic/melodic bed.
  - LEAD: track 2/4 "Retro Synth 13" -- the reusable 8-bar HOOK (43 notes),
          played twice verbatim in the source (bars 8-15, then again 32-39).
          Arpeggiated (each note -> an ascending broken chord) and echoed (a
          delayed, higher, quieter copy later in the loop), and only sounded in
          a subset of bars per biome so it reads as a hook riding on top of the
          arp bed, not a second wash of notes.
  - bass:  track 1 "Retro Bass 9" -- 4-bar descending riff (root, -2, -7, -9
           semitones), looped for the whole song. Re-used directly per biome root.
  - pad:   track 5 "Retro Synth 8" -- whole-note parallel triads over the same
           4-bar cycle.
  - drums: kick (track 7), snare (track 10, including a real accelerating
           snare-roll fill around t=183-196) and 2 hi-hat tracks (8, 9) --
           track 9 gets genuinely dense (16 hits/bar) once it kicks in, which
           is the source for the "dense hat wall" drum section below.

Per biome, both the arp and the lead get their OWN per-bar-group variation via a
16-bar SECTION MAP (shared shape with the drum section map, but independently
authored): arp sections thin out, thicken, or register-shift; lead sections
turn the hook on/off, or shift its contour (straight/inverted/retrograde) bar
group to bar group -- so no two 4-bar spans in a loop sound identical. Drums use
their own per-biome 16-bar SECTION MAP from a small vocabulary (four-on-the-
floor, backbeat, hats-out breakdown, dense 16th-note hat wall, marching-band
snare, introsong's real snare-roll fill, syncopated).

Instrument timbre/vibrato/chorus/glide/filter-sweep per biome live in
gen_music.py's BIOMES dict (arp_wave/vibrato_*/chorus/glide/filter_sweep_bars),
not here -- this script only emits note events.

Usage: from_introsong.py [--no-render]
"""
import argparse
import json
import sys
from pathlib import Path

import mido

REPO = Path(__file__).resolve().parent.parent.parent
SONGS = REPO / "tools" / "audio" / "songs"
sys.path.insert(0, str(REPO / "tools" / "audio"))
import gen_music as gm

MIDI_PATH = REPO / "introsong.mid"

BASS_TRACK, LEAD_HOOK_TRACK, ARP_TRACK, PAD_TRACK = 1, 2, 6, 5
HOOK_STEPS = 128           # 8 bars, the lead hook's own length
ARP_CELL_STEPS = 64        # 4 bars, the busy arp's repeating (and self-varying) cell
HOOK_ROOT_REF = 72         # C5: the lead hook's opening/tonic note, transpose anchor
ARP_ROOT_REF = 65          # F4: the busy arp's opening note, transpose anchor

BASS_INTERVALS = [0, -2, -7, -9]
PAD_TRIADS = {0: [0, 4, 7], -2: [0, 4, 7], -7: [0, 3, 7], -9: [0, 4, 7]}


def get_notes(track):
    t, notes, on = 0, [], {}
    for msg in track:
        t += msg.time
        if msg.type == "note_on" and msg.velocity > 0:
            on[msg.note] = (t, msg.velocity)
        elif msg.type == "note_off" or (msg.type == "note_on" and msg.velocity == 0):
            if msg.note in on:
                t0, vel = on.pop(msg.note)
                notes.append({"t": t0, "d": t - t0, "n": msg.note, "v": vel})
    return notes


def load_source():
    mid = mido.MidiFile(str(MIDI_PATH))
    hook = get_notes(mid.tracks[LEAD_HOOK_TRACK])
    base_t = hook[0]["t"]
    hook = [{"t": ev["t"] - base_t, "d": ev["d"], "n": ev["n"], "v": ev["v"]} for ev in hook]
    arp_full = get_notes(mid.tracks[ARP_TRACK])
    return hook, arp_full


def make_bass(root, bars, cycle_bars=4):
    return [{"t": bar * 16, "n": root - 12 + BASS_INTERVALS[bar % cycle_bars], "d": 2, "v": 1.0}
            for bar in range(bars)]


def make_pad(root, bars, cycle_bars=4):
    out = []
    for bar in range(bars):
        deg = BASS_INTERVALS[bar % cycle_bars]
        for iv in PAD_TRIADS[deg]:
            out.append({"t": bar * 16, "n": root + deg + iv, "d": 16, "v": 1.0})
    return out


def contour(src, ref, span, shape):
    notes = sorted(src, key=lambda e: e["t"])
    out = []
    for ev in notes:
        iv = ev["n"] - ref
        if shape == "inverted":
            iv = -iv
        out.append((ev["t"], ev["d"], iv, ev["v"] / 127))
    if shape == "retrograde":
        out = sorted([(span - t - d, d, iv, v) for t, d, iv, v in out])
    return out


# ---- ARP layer: continuous busy line, thinned/thickened/register-shifted per
# 4-bar group so it varies across the loop instead of looping identically ------

ARP_TREATMENTS = {
    "full":    dict(keep=1.0, octave=0,  gain=1.0),   # every note, as-is
    "thin":    dict(keep=0.5, octave=0,  gain=0.85),  # drop every other note -- breathing room
    "sparse":  dict(keep=0.25, octave=0, gain=0.7),   # only the strongest beats
    "high":    dict(keep=0.75, octave=12, gain=0.8),  # same density, up an octave -- brighter turn
    "low":     dict(keep=0.75, octave=-12, gain=0.9), # down an octave -- darker, weightier
    "off":     dict(keep=0.0, octave=0,  gain=0.0),   # silence -- let the lead/pad breathe alone
}

def make_arp(arp_full, root, bars, shape, group_map, lead_octave=0):
    """group_map: one ARP_TREATMENTS key per 4-bar group (len == bars//4)."""
    target_steps = bars * 16
    src = contour(arp_full, ARP_ROOT_REF, ARP_CELL_STEPS * 10, shape)  # shape applied once, globally
    out = []
    group_bars = 4
    for gi, kind in enumerate(group_map):
        treat = ARP_TREATMENTS[kind]
        if treat["keep"] <= 0:
            continue
        g0 = gi * group_bars * 16
        cell_reps = group_bars * 16 // ARP_CELL_STEPS
        for rep in range(cell_reps):
            cell_off = g0 + rep * ARP_CELL_STEPS
            for idx, (t, d, iv, v) in enumerate(src):
                if t >= ARP_CELL_STEPS:
                    continue
                # deterministic thinning: keep every note whose position within
                # its beat-of-4 falls inside the treatment's `keep` fraction
                if treat["keep"] < 1.0 and (idx % 4) >= round(treat["keep"] * 4):
                    continue
                tt = cell_off + t
                if tt >= target_steps:
                    continue
                out.append({"t": tt, "n": root + lead_octave + iv + treat["octave"],
                            "d": max(1, d), "v": round(v * treat["gain"], 3)})
    return out


# ---- LEAD layer: the 8-bar hook, arpeggiated + echoed, gated on/off per 4-bar
# group so it reads as a hook riding over the arp bed, not a second arp -------

def make_lead(hook, root, bars, group_map, arp_ivs, echo_delay, echo_ivs, echo_gain,
              lead_octave=12):
    """group_map: one of "straight"/"inverted"/"retrograde"/"off" per 4-bar group."""
    target_steps = bars * 16
    group_bars = 4
    out = []
    for gi, shape in enumerate(group_map):
        if shape == "off":
            continue
        g0 = gi * group_bars * 16
        src = contour(hook, HOOK_ROOT_REF, HOOK_STEPS, shape)
        # a 4-bar group is half the 8-bar hook -- take the matching half so
        # consecutive groups cover the whole hook instead of always bar 0-3
        half = (gi % 2) * (group_bars * 16)
        for t, d, iv, v in src:
            if not (half <= t < half + group_bars * 16):
                continue
            base_t = g0 + (t - half)
            if base_t >= target_steps:
                continue
            note = root + lead_octave + iv
            step_len = max(1, d // len(arp_ivs)) if d > len(arp_ivs) else 1
            for k, a_iv in enumerate(arp_ivs):
                tt = base_t + k * step_len
                if tt >= target_steps:
                    continue
                out.append({"t": tt, "n": note + a_iv, "d": max(1, step_len), "v": round(v, 3)})
            for e_iv in echo_ivs:
                et = base_t + echo_delay
                if et < target_steps:
                    out.append({"t": et, "n": note + e_iv, "d": max(1, d), "v": round(v * echo_gain, 3)})
    return out


# ---- drum section vocabulary ------------------------------------------------
ROLL_STEPS = [0, 8, 12, 14, 15]  # introsong's real snare-roll rhythm, from track 10 ~t183-196

def sec_four_on_floor(): return dict(kick=[0, 4, 8, 12], snare=[4, 12], hat=[0,2,4,6,8,10,12,14])
def sec_backbeat():      return dict(kick=[0, 7], snare=[4, 12], hat=[0,2,4,6,8,10,12,14])
def sec_no_hat():        return dict(kick=[0, 8], snare=[4, 12], hat=[])
def sec_dense_hat():     return dict(kick=[0, 8], snare=[4, 12], hat=list(range(16)))
def sec_march():         return dict(kick=[0], snare=[2,3,6,7,10,11,14,15], hat=[])
def sec_fill():          return dict(kick=[0], snare=ROLL_STEPS, hat=[0,2,4,6,8,10,12,14])
def sec_syncopated():    return dict(kick=[0,3,6,10,12], snare=[4,12], hat=[0,2,3,4,6,8,9,10,12,14])

SECTIONS = {
    "floor": sec_four_on_floor, "back": sec_backbeat, "nohat": sec_no_hat,
    "dense": sec_dense_hat, "march": sec_march, "fill": sec_fill, "sync": sec_syncopated,
}


def make_drums(bars, section_map):
    out = []
    for bar, kind in enumerate(section_map):
        b0 = bar * 16
        pat = SECTIONS[kind]()
        for h in pat["kick"]:
            out.append({"t": b0 + h, "k": "kick", "v": 0.9})
        for h in pat["snare"]:
            out.append({"t": b0 + h, "k": "snare", "v": 0.75 if kind == "fill" else 0.55})
        for h in pat["hat"]:
            out.append({"t": b0 + h, "k": "hat", "v": 1.0 if h % 4 == 0 else 0.6})
    return out


BARS = 16
N_GROUPS = BARS // 4   # 4 groups of 4 bars each -- both arp and lead vary per group

# per-biome 16-bar drum section map
SECTION_MAPS = {
    "01_void":    ["nohat","nohat","back","back", "floor","floor","floor","fill",
                   "march","march","back","back", "dense","dense","fill","nohat"],
    "02_ember":   ["dense","dense","dense","fill", "dense","dense","nohat","nohat",
                   "floor","floor","dense","fill", "march","march","dense","fill"],
    "03_frost":   ["nohat","nohat","nohat","march", "nohat","nohat","fill","nohat",
                   "back","back","nohat","march", "nohat","nohat","fill","nohat"],
    "04_tempest": ["floor","sync","floor","fill", "dense","sync","floor","fill",
                   "march","sync","floor","fill", "dense","dense","sync","fill"],
    "05_abyss":   ["nohat","nohat","nohat","nohat", "nohat","nohat","fill","nohat",
                   "nohat","nohat","nohat","nohat", "nohat","nohat","fill","nohat"],
    "06_dino":    ["march","march","march","fill", "floor","floor","floor","fill",
                   "march","march","dense","fill", "floor","floor","floor","fill"],
}

# per-biome ARP group map (4 groups): how the continuous busy line varies
ARP_GROUP_MAPS = {
    "01_void":    ["sparse", "full",  "high",  "thin"],
    "02_ember":   ["full",   "full",  "low",   "full"],
    "03_frost":   ["off",    "sparse","off",   "thin"],
    "04_tempest": ["full",   "high",  "full",  "low"],
    "05_abyss":   ["off",    "off",   "sparse","off"],
    "06_dino":    ["thin",   "full",  "high",  "full"],
}

# per-biome LEAD group map (4 groups): "off" mutes the hook for that group so it
# reads as a returning hook, not wallpaper; shape picks the contour for that pass
LEAD_GROUP_MAPS = {
    "01_void":    ["off", "straight",   "off", "straight"],
    "02_ember":   ["straight", "off", "straight", "straight"],
    "03_frost":   ["off", "inverted", "off", "inverted"],
    "04_tempest": ["retrograde", "retrograde", "off", "retrograde"],
    "05_abyss":   ["off", "off", "inverted", "off"],
    "06_dino":    ["retrograde", "off", "retrograde", "straight"],
}

# per-biome lead arp/echo treatment (arpeggio shape + echo delay/transposition/gain)
PLAN = {
    "01_void":    dict(arp_ivs=[0, 7, 12],     echo_delay=48, echo_ivs=[12],     echo_gain=0.45),
    "02_ember":   dict(arp_ivs=[0, 4, 7, 12],  echo_delay=24, echo_ivs=[12, 19], echo_gain=0.40),
    "03_frost":   dict(arp_ivs=[0, 7],         echo_delay=64, echo_ivs=[12],     echo_gain=0.50),
    "04_tempest": dict(arp_ivs=[0, 5, 7, 12],  echo_delay=16, echo_ivs=[12],     echo_gain=0.40),
    "05_abyss":   dict(arp_ivs=[0, 12],        echo_delay=96, echo_ivs=[12, 24], echo_gain=0.35),
    "06_dino":    dict(arp_ivs=[0, 4, 7],      echo_delay=32, echo_ivs=[7, 12],  echo_gain=0.42),
}

META_KEYS = ("bass_wave", "arp_duty", "pad_gain", "arp_gain", "drum_gain",
             "arp_wave", "vibrato_rate", "vibrato_depth", "chorus", "glide",
             "filter_sweep_bars")


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--no-render", action="store_true", help="write song JSONs only, skip ogg render")
    args = ap.parse_args()

    hook, arp_full = load_source()
    SONGS.mkdir(parents=True, exist_ok=True)
    for slug, p in gm.BIOMES.items():
        plan = PLAN[slug]
        root = p["root"]

        arp_notes = make_arp(arp_full, root, BARS, "straight", ARP_GROUP_MAPS[slug])
        lead_notes = make_lead(hook, root, BARS, LEAD_GROUP_MAPS[slug], plan["arp_ivs"],
                                plan["echo_delay"], plan["echo_ivs"], plan["echo_gain"])
        # both layers land in the "arp" track slot (gen_music.py's render() only
        # knows bass/pad/arp) -- merge them, sorted by time
        combined = sorted(arp_notes + lead_notes, key=lambda e: e["t"])

        song = {
            "slug": slug, "bpm": p["bpm"], "bars": BARS,
            "meta": {k: p.get(k) for k in META_KEYS},
            "tracks": {
                "bass": make_bass(root, BARS),
                "pad": make_pad(root, BARS),
                "arp": combined,
            },
            "drums": make_drums(BARS, SECTION_MAPS[slug]),
        }
        out = SONGS / f"{slug}.json"
        out.write_text(json.dumps(song, separators=(",", ":")))
        print(f"wrote {out}: arp_layer={len(arp_notes)} lead_layer={len(lead_notes)} "
              f"combined={len(combined)} drums={len(song['drums'])}")

    if not args.no_render:
        for slug, p in gm.BIOMES.items():
            song = json.loads((SONGS / f"{slug}.json").read_text())
            gm.write_ogg(slug, gm.render(song))


if __name__ == "__main__":
    main()

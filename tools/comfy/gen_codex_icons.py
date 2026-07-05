#!/usr/bin/env python3
"""Generate the codex/UI badge icons for weapons, effects, runes, trifectas and
drone AIs (everything in the Codex that currently only shows a colour swatch),
via the same ComfyUI/Krea2 pixel-art pipeline as gen_all.py, then patch the
owning TOML with `sprite="..."` paths.

Usage:
  gen_codex_icons.py            generate every icon that is missing, then patch TOMLs
  gen_codex_icons.py --force    regenerate everything
  gen_codex_icons.py --only X   only entries whose sprite path contains X
  gen_codex_icons.py --no-toml  skip the TOML patching step
"""

import argparse
import re
import sys
import zlib
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parent))
import spritegen

REPO = Path(__file__).resolve().parent.parent.parent
ICON_SIZE = 26   # matches the existing pickup badges (COMMON list in gen_all.py)

# icons use their own style prefix: spritegen.STYLE_PREFIX asks for "chunky visible
# square pixels", which at 26px makes the diffusion draw 2x2/4x4 fake pixels that
# survive the regrid as off-scale blocks. Icons want fine 1:1 detail instead.
ICON_STYLE_PREFIX = (
    "pixel art game icon, retro 16-bit arcade interface badge, highly detailed miniature, "
    "fine single-pixel detail, flat shading with 3 tone steps, "
)
# reject regridded icons whose 2x2 block-match exceeds this and reroll the seed
# (crisp 1:1 icons sit ~0.60-0.76; visibly chunky ones ~0.85+)
MAX_CHUNK2 = 0.78

# pink/magenta-ish subjects get flooded away with the magenta chroma key
# (worse now that darkened-key shadows are keyed too) — key them on green
BG_OVERRIDES = {
    "pasv_magnetcoil": "green",
}

# ---- roster: path -> (toml file, array name, "name" match value, prompt) ----------------
# prompt style follows the pickup badges already in gen_all.py's COMMON list: a single
# bold icon/emblem on a plain background, not a creature — reads clearly at 26px.

WEAPONS = {
    "wpn_maincannon":   ("Main Cannon",   "sky-blue twin-barrel cannon nose icon, pointed forward, arcade weapon badge"),
    "wpn_sideguns":      ("Side Guns",     "peach twin side-mounted stub guns icon, pointing left and right, arcade weapon badge"),
    "wpn_tailgun":       ("Tail Gun",      "orchid purple single rear gun barrel icon pointing down, arcade weapon badge"),
    "wpn_homingswarm":   ("Homing Swarm",  "mint green cluster of three small homing missile icons converging, arcade weapon badge"),
    "wpn_chainbolt":     ("Chain Bolt",    "pale cyan jagged lightning chain-link bolt icon, arcade weapon badge"),
    "wpn_damageaura":    ("Damage Aura",   "coral pulsing radial shockwave rings icon around a bright core, arcade weapon badge"),
    "wpn_orbital":       ("Orbital",       "ice-blue two small orbiting balls circling a center point icon, arcade weapon badge"),
    "wpn_aimcannon":     ("Aim Cannon",    "gold cannon barrel icon crossed with a targeting reticle, arcade weapon badge"),
    "wpn_autoaim":       ("Auto-Aim Gun",  "leaf green gun barrel icon with a locking targeting bracket, arcade weapon badge"),
    "wpn_missiles":      ("Missiles",      "peach pair of sharp finned missile icons angled upward, arcade weapon badge"),
    "wpn_flamethrower":  ("Flamethrower",  "peach and orange cone of flame icon, arcade weapon badge"),
    "wpn_flammablegoo":  ("Flammable Goo", "lime green dripping goo blob icon with a small flame on top, arcade weapon badge"),
    "wpn_ioncannon":     ("Ion Cannon",    "bright cyan glowing energy cannon barrel icon with a charged core, arcade weapon badge"),
}

EFFECTS = {
    "fx_slow":    ("Slow",    "sky-blue swirling clock icon with motion-drag lines, arcade status-effect badge"),
    "fx_burn":    ("Burn",    "peach and orange single flame icon, arcade status-effect badge"),
    "fx_poison":  ("Poison",  "leaf green skull-and-crossbones drip icon inside a bubbling droplet, arcade status-effect badge"),
    "fx_freeze":  ("Freeze",  "pale cyan angular ice crystal shard icon, arcade status-effect badge"),
    "fx_shock":   ("Shock",   "gold jagged lightning bolt icon, arcade status-effect badge"),
    "fx_weaken":  ("Weaken",  "orchid purple downward broken arrow over a small shield icon, arcade status-effect badge"),
    "fx_mark":    ("Mark",    "white glowing crosshair target reticle icon, arcade status-effect badge"),
    "fx_goo":     ("Goo",     "lime green sticky dripping goo puddle icon, arcade status-effect badge"),
}

RUNES = {
    "rune_ember": ("Ember", "coral faceted gemstone icon glowing with inner fire, arcade rune badge"),
    "rune_frost": ("Frost", "azure blue faceted gemstone icon glowing with inner frost, arcade rune badge"),
    "rune_vine":  ("Vine",  "seagreen faceted gemstone icon glowing with inner nature energy, arcade rune badge"),
    "rune_spark": ("Spark", "gold faceted gemstone icon glowing with inner electricity, arcade rune badge"),
    "rune_void":  ("Void",  "violet faceted gemstone icon glowing with inner void swirl, arcade rune badge"),
}

# trifectas: named 3-rune combos; icon should evoke the name thematically while
# nodding at its dominant rune colour(s) (see trifectas.toml `counts`).
TRIFECTAS = {
    "tri_inferno":  ("Inferno",  "crimson red roaring triple-flame emblem icon, arcade badge"),
    "tri_stream":   ("Stream",   "azure blue flowing triple water-current spiral emblem icon, arcade badge"),
    "tri_swarm":    ("Swarm",    "seagreen green three small swarming insect wings emblem icon, arcade badge"),
    "tri_blitz":    ("Blitz",    "gold triple lightning bolt burst emblem icon, arcade badge"),
    "tri_lance":    ("Lance",    "violet purple single sharp piercing lance spike emblem icon, arcade badge"),
    "tri_napalm":   ("Napalm",   "crimson red and gold dripping fire explosion emblem icon, arcade badge"),
    "tri_scatter":  ("Scatter",  "seagreen green and coral red scattering fragments burst emblem icon, arcade badge"),
    "tri_railgun":  ("Railgun",  "azure blue and violet purple long energy rail spike emblem icon, arcade badge"),
    "tri_tempest":  ("Tempest",  "gold and seagreen green swirling storm vortex emblem icon, arcade badge"),
    "tri_void":     ("Void",     "violet purple and coral red collapsing dark portal emblem icon, arcade badge"),
    "tri_prism":    ("Prism",    "coral red azure blue and seagreen green faceted triangular prism emblem icon, arcade badge"),
    "tri_chaos":    ("Chaos",    "gold violet purple and azure blue jagged chaotic starburst emblem icon, arcade badge"),
}

DRONES = {
    "drone_follow": ("Follow Drone", "small white and gray diamond attack drone icon with a trailing loop arrow, arcade badge"),
    "drone_auto":   ("Auto Drone",   "small white and gray diamond attack drone icon with a homing crosshair, arcade badge"),
    "drone_sweep":  ("Sweep Drone",  "small white and gray diamond attack drone icon with left-right sweep arrows, arcade badge"),
}

# passives: global player buffs offered on level-up cards (src/data/upgrades.c). No TOML
# backing (pure C table) so this roster only drives generation, not any TOML patch.
PASSIVES = {
    "pasv_rapidfire":      ("Rapid Fire",       "gold stopwatch icon with a fast-forward double-arrow, arcade power-up badge"),
    "pasv_heavyrounds":    ("Heavy Rounds",     "coral red heavy bullet shell icon with bold impact lines, arcade power-up badge"),
    "pasv_spreadshot":     ("Spread Shot",      "peach fan of three small bullet icons spreading outward, arcade power-up badge"),
    "pasv_velocitycoils":  ("Velocity Coils",   "pale cyan coiled spring icon with speed motion lines, arcade power-up badge"),
    "pasv_thrusters":      ("Thrusters",        "aqua twin rocket-engine nozzle icon with small flame jets, arcade power-up badge"),
    "pasv_shieldcap":      ("Shield Capacitor", "sky blue hexagonal shield icon with a glowing charge core, arcade power-up badge"),
    "pasv_reinforcedhull": ("Reinforced Hull",  "crimson red riveted armor plate icon, arcade power-up badge"),
    "pasv_fastrecharge":   ("Fast Recharge",    "turquoise battery icon with a fast lightning-bolt charge symbol, arcade power-up badge"),
    "pasv_magnetcoil":     ("Magnet Coil",      "hot pink horseshoe magnet icon with small orbiting particles, arcade power-up badge"),
    "pasv_greed":          ("Greed",            "leaf green stack of glowing coins icon, arcade power-up badge"),
    "pasv_aegisrounds":    ("Aegis Rounds",     "lavender purple small round shield icon deflecting a bullet, arcade power-up badge"),
    "pasv_armorplating":   ("Armor Plating",    "steel gray overlapping armor scale plates icon, arcade power-up badge"),
}


def collect_jobs():
    """-> list of (sprite_rel_path, size, prompt)"""
    jobs = []
    for slug, (_name, prompt) in {**WEAPONS, **EFFECTS, **RUNES, **TRIFECTAS, **DRONES, **PASSIVES}.items():
        jobs.append((f"sprites/common/{slug}.png", ICON_SIZE, prompt))
    return jobs


# ---- TOML patching -------------------------------------------------------------

def patch_kv_by_name(text, block_key, name, sprite_rel):
    """Inside every `{block_key}` inline-table-array row (or [[block_key]] table)
    whose name="<name>" matches, add/replace sprite="sprite_rel"."""
    # inline-table style (weapons.toml): one row per line
    pattern = re.compile(
        r'(\{[^\n]*name\s*=\s*"' + re.escape(name) + r'"[^\n]*\})'
    )
    def inline_sub(m):
        row = m.group(1)
        if re.search(r'sprite\s*=', row):
            return re.sub(r'sprite\s*=\s*"[^"]*"', f'sprite="{sprite_rel}"', row)
        return re.sub(r'(name\s*=\s*"' + re.escape(name) + r'")', rf'\1, sprite="{sprite_rel}"', row, count=1)
    new_text, n = pattern.subn(inline_sub, text)
    if n:
        return new_text

    # [[block_key]] table style (effects.toml, runes.toml, trifectas.toml)
    chunks = re.split(r'(^\[\[' + re.escape(block_key) + r'\]\]\s*$)', text, flags=re.M)
    for i in range(2, len(chunks), 2):
        body = chunks[i]
        if re.search(r'^name\s*=\s*"' + re.escape(name) + r'"', body, re.M):
            if re.search(r'^sprite\s*=', body, re.M):
                chunks[i] = re.sub(r'^sprite\s*=.*$', f'sprite="{sprite_rel}"', body, count=1, flags=re.M)
            else:
                chunks[i] = re.sub(r'(^name\s*=.*$)', rf'\1\nsprite="{sprite_rel}"', body, count=1, flags=re.M)
    return "".join(chunks)


def patch_tomls():
    weapons_p = REPO / "assets" / "weapons.toml"
    t = weapons_p.read_text()
    for slug, (name, _prompt) in WEAPONS.items():
        t = patch_kv_by_name(t, "weapon", name, f"sprites/common/{slug}.png")
    weapons_p.write_text(t)
    print(f"patched {weapons_p.relative_to(REPO)}")

    effects_p = REPO / "assets" / "effects.toml"
    t = effects_p.read_text()
    for slug, (name, _prompt) in EFFECTS.items():
        t = patch_kv_by_name(t, "effect", name, f"sprites/common/{slug}.png")
    effects_p.write_text(t)
    print(f"patched {effects_p.relative_to(REPO)}")

    runes_p = REPO / "assets" / "runes.toml"
    t = runes_p.read_text()
    for slug, (name, _prompt) in RUNES.items():
        t = patch_kv_by_name(t, "rune", name, f"sprites/common/{slug}.png")
    runes_p.write_text(t)
    print(f"patched {runes_p.relative_to(REPO)}")

    tri_p = REPO / "assets" / "trifectas.toml"
    t = tri_p.read_text()
    for slug, (name, _prompt) in TRIFECTAS.items():
        t = patch_kv_by_name(t, "trifecta", name, f"sprites/common/{slug}.png")
    tri_p.write_text(t)
    print(f"patched {tri_p.relative_to(REPO)}")

    # drones have no TOML/struct -- sprite path is a fixed switch in drones.c, already
    # matching sprites/common/drone_{follow,auto,sweep}.png; nothing to patch.


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--force", action="store_true")
    ap.add_argument("--only", default=None)
    ap.add_argument("--no-toml", action="store_true")
    ap.add_argument("--seed-offset", type=int, default=0,
                    help="added to every icon's base seed (variant runs)")
    ap.add_argument("--suffix", default="",
                    help="filename suffix for variant runs (e.g. _2 -> wpn_maincannon_2.png)")
    args = ap.parse_args()

    jobs = collect_jobs()
    todo = [(rel, size, prompt) for rel, size, prompt in jobs
            if (args.only is None or args.only in rel)
            and (args.force or not (
                (REPO / "assets" / rel).with_name(Path(rel).stem + args.suffix + ".png")
                if args.suffix else (REPO / "assets" / rel)).exists())]
    print(f"{len(jobs)} icons total, generating {len(todo)}")
    failed = []
    for i, (rel, size, prompt) in enumerate(todo):
        if i and i % 25 == 0:
            spritegen.free_vram()
        out = REPO / "assets" / rel
        if args.suffix:
            out = out.with_name(out.stem + args.suffix + out.suffix)
        seed = zlib.crc32(rel.encode()) % 100000 + args.seed_offset
        print(f"[{i+1}/{len(todo)}] {rel} ({size}px)")
        bg = BG_OVERRIDES.get(Path(rel).stem, "magenta")
        full = ICON_STYLE_PREFIX + prompt + spritegen.STYLE_SUFFIX.format(bg=bg)
        if not spritegen.generate_sprite(full, size, out, seed=seed, max_tries=6, bg=bg,
                                         raw=True, max_chunk2=MAX_CHUNK2, strip_dark_outline=True):
            failed.append(rel)
            print(f"  FAILED {rel}", file=sys.stderr)
    if not args.no_toml and args.only is None and not args.suffix:
        patch_tomls()
    print(f"done. {len(todo) - len(failed)} generated, {len(failed)} failed: {failed}")
    sys.exit(1 if failed else 0)


if __name__ == "__main__":
    main()

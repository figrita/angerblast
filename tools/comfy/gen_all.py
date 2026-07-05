#!/usr/bin/env python3
"""Regenerate ALL Angerblast graphics as BLK NX64 pixel-art sprites via ComfyUI/Krea2,
and patch the TOML data with the new thematic names, codex flavor lines and sprite paths.

Usage:
  gen_all.py            generate every sprite that is missing, then patch TOMLs
  gen_all.py --force    regenerate everything
  gen_all.py --only X   only entries whose sprite path contains X
  gen_all.py --no-toml  skip the TOML patching step
"""

import argparse
import re
import sys
import tomllib
import zlib
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parent))
import spritegen

REPO = Path(__file__).resolve().parent.parent.parent
SPRITES = REPO / "assets" / "sprites"

# ---- the thematic roster -------------------------------------------------------
# per biome: mood tail appended to every prompt, then
# enemy file stem -> (new_name, flavor, subject prompt)
BIOMES = {
    "01_void": {
        "mood": "eldritch deep-space cult mood, dark void ambience",
        "enemies": {
            "01_grunt":     ("Riftmite",      "Chittering vanguard of the rift; marches in lockstep and dies in droves.",
                             "small alien beetle drone, seagreen carapace, single glowing white eye slit, tucked insectoid legs"),
            "02_diver":     ("Void Talon",    "Folds its wings and plunges the moment you look away.",
                             "diving bird-of-prey creature with folded angular wings, peach and coral plumage, sharp beak pointing down"),
            "03_weaver":    ("Threadwraith",  "Sews itself through space along a silken sine of unlight.",
                             "sinuous ribbon wraith, azure glow, trailing silk threads, ghostly serpentine body"),
            "04_crab":      ("Scuttlemaw",    "Sidles the starfield edge-on, all armor and appetite.",
                             "wide armored alien crab, turquoise shell, oversized pincers, cluster of small eyes"),
            "05_segment":   ("Coilspawn",     "One vertebra of something far too long to kill politely.",
                             "single armored centipede body-segment creature, clay-orange chitin plates, small legs, glowing seam"),
            "06_hunter":    ("Gazehound",     "It has your scent now. Walls will not help.",
                             "sleek four-legged hound stalker, magenta hide, whole head is one giant unblinking eyeball, crouched to pounce"),
            "07_spinner":   ("Whirling Idol", "A heretic shrine that spins itself into a fusillade.",
                             "floating carved stone idol totem, orchid purple glow, concentric carved rings"),
            "08_orbiter":   ("Carrion Moth",  "Circles dead satellites, then dives at whatever still moves.",
                             "eerie space moth, peach dusty wings spread, skull pattern on thorax"),
            "09_turret":    ("Null Sentry",   "A drifting watch-eye that never blinks and never stops firing.",
                             "floating mechanical eye turret, mauve armor plates, big central lens, twin small gun barrels"),
            "10_popcorn":   ("Sparkfly",      "Harmless alone. They are never alone.",
                             "tiny golden firefly wisp, simple round glowing body, little wings"),
            "11_blossom":   ("Nightbloom",    "Blooms once per heartbeat; its petals are bullets.",
                             "sinister flower creature, pink petals opening around a dark toothy core"),
            "12_bomber":    ("Doomcyst",      "A bloated sac of misfortune. Bursts on principle.",
                             "bloated red pustule sac monster, taut veined membrane, small angry eyes"),
            "13_sprayer":   ("Starspitter",   "Gargles starlight, spits grapeshot.",
                             "hovering squid-like alien, sky-blue body, siphon mouth full of glowing star sparks"),
            "14_warden":    ("Rift Warden",   "Keeper of the tear in the sky. It would like it shut, with you inside.",
                             "regal void sentinel knight, violet robes and armor, crowned helm, floating with arms crossed, imposing"),
            "15_breacher":  ("Hullripper",    "Built to open ships like tins. You are the tin.",
                             "brutish armored ram-beast, grape-purple plating, huge shearing claws held forward"),
        },
        "boss": ("sprites/01_void/boss.png",
                 "colossal void leviathan gaping maw ringed with crooked teeth, deep purple and violet flesh, single baleful eye above the mouth"),
    },
    "02_ember": {
        "mood": "volcanic hellscape mood, embers and smoke",
        "enemies": {
            "01_ember":      ("Cinder Wisp",   "A stray spark with delusions of arson.",
                              "small living flame wisp spirit, coral fire body, trailing embers"),
            "02_blaze":      ("Flarecaller",   "Conducts the firestorm like a choir.",
                              "fire priest imp, peach flame robes, arms raised conducting flames"),
            "02_pyrelord":   ("Pyrelord",      "Nobility of the burning court; its retinue is the fire itself.",
                              "regal fire demon lord, crimson armor, crown of flame, burning cape, imposing"),
            "03_shard":      ("Slagshard",     "Molten shrapnel that never learned to slow down.",
                              "jagged molten crystal shard, glowing gold cracks through dark slag crust"),
            "04_cinderling": ("Cinderling",    "Newly kindled and eager to prove it can burn.",
                              "small eager fire imp diving headfirst, coral flames for hair"),
            "05_scorcher":   ("Charhulk",      "A drifting kiln that never finished cooling.",
                              "hulking charcoal golem, maroon ember cracks across black coal body, smoke wisps"),
            "06_magmate":    ("Magma Stalker", "Follows the smell of unburned things.",
                              "four-legged lava predator, crimson magma hide under black rock plates"),
        },
        "boss_parts": [
            ("sprites/02_ember/boss_head.png", "obsidian tyrant demon head crowned with fire, crimson glowing eyes, cracked black stone face"),
            ("sprites/02_ember/boss_hand.png", "clenched molten rock fist, maroon stone with coral lava cracks"),
            ("sprites/02_ember/boss_hand.png", None),
        ],
    },
    "03_frost": {
        "mood": "arctic wasteland mood, aurora glow",
        "enemies": {
            "01_sleet":       ("Sleetshade",  "A cold ghost that arrives sideways.",
                               "ghostly ice phantom swooping, pale cyan wisp body, trailing frost"),
            "02_drillspike":  ("Icebore",     "Corkscrews through the floe, and anything softer.",
                               "corkscrew ice drill creature, cyan crystal spiral body tapering to a point"),
            "03_rimebeast":   ("Rimebeast",   "Shaggy with hoarfrost; lunges before it thinks. It never thinks.",
                               "shaggy frost-furred beast, steel-blue fur, icicle fangs, hunched to pounce"),
            "04_hailspinner": ("Hailspinner", "A cyclone of hail wound around a bad idea.",
                               "spinning hailstone orb creature ringed with azure ice blades"),
            "05_frostgrunt":  ("Frosthusk",   "A marching shell of ice; something old walks inside it.",
                               "animated suit of pale-blue ice armor marching forward, empty helmet visor glowing eerie cyan, ice chestplate with arms and legs, no wearer inside"),
            "06_glaciator":   ("Glaciator",   "The slow patience of ice, armed.",
                               "towering glacier golem with broad shoulders and massive arms, pale blue and midblue translucent ice slabs, glowing cyan core in its chest, imposing"),
        },
        "boss": ("sprites/03_frost/boss.png",
                 "coiled ice dragon wyrm head with crystalline horns, pale blue scales, frozen breath"),
    },
    "04_tempest": {
        "mood": "raging storm sky mood, charged air",
        "enemies": {
            "01_gust":        ("Zephyrling",   "A small wind with a smaller temper.",
                               "small wind spirit, bright aqua swirling body, cheeky scowl"),
            "02_thunderhead": ("Thunderhead",  "A pocket storm cloud that carries its own artillery.",
                               "small scowling storm cloud creature, golden lightning bolts underneath"),
            "03_maelstrom":   ("Maelstrom",    "The eye of the storm, hunting a new center: you.",
                               "swirling vortex beast, coral spiral body with a debris ring"),
            "04_voltspine":   ("Voltspine",    "Marches to the drum of its own thunder.",
                               "marching creature with electric blue spines along its peach body"),
            "05_squall":      ("Squallwing",   "Rain that learned to aim.",
                               "storm petrel bird creature, mint feathers, rain streaking off its wings"),
            "06_cyclops":     ("Storm Cyclops","One eye, all of it lightning.",
                               "one-eyed storm giant, gold skin, beard of grey storm cloud, one single huge glowing white eye centered on the forehead, no other eyes, imposing"),
        },
        "boss_parts": [
            ("sprites/04_tempest/boss_head.png", "storm sovereign king head with a crown of lightning, gold skin, thunderous glare"),
            ("sprites/04_tempest/boss_hand.png", "colossal clenched fist of storm-grey stone, crackling golden lightning arcs and white sparks around the knuckles"),
            ("sprites/04_tempest/boss_hand.png", None),
        ],
    },
    "05_abyss": {
        "mood": "deep sea abyss mood, bioluminescent darkness",
        "enemies": {
            "01_jellyfish":   ("Medusa",       "Drifting bell of stinging light; beautiful the way drowning is calm.",
                               "deep-sea jellyfish, deep teal translucent bell, trailing stingers, bioluminescent spots"),
            "02_lanternfish": ("Lanternfish",  "Its little light is bait. You are the catch.",
                               "small deep-sea fish, teal scales, glowing lantern lure over its head"),
            "03_angler":      ("Anglerfish",   "The dark's own lure, with the dark's own teeth.",
                               "monstrous anglerfish, near-black body, enormous fangs, glowing lure"),
            "04_siphon":      ("Siphonophore", "Not one animal but a parliament, all voting to eat you.",
                               "colonial sea organism, chain of aqua glowing segments in a gentle spiral"),
            "05_drifter":     ("Abyss Drifter","Falls upward out of the trench when it smells warmth.",
                               "dark trench ray creature, night-teal wings, pale glowing underside"),
        },
        "boss": ("sprites/05_abyss/boss.png",
                 "gargantuan deep-sea leviathan head, abyssal dark blue hide, bioluminescent teal markings, huge jaw"),
    },
    "06_dino": {
        "mood": "primeval jungle mood, prehistoric",
        "enemies": {
            "01_raptor":    ("Raptor",      "Pack instincts, singular appetite. It lunges first.",
                             "leaping velociraptor, leaf-green scales, sickle claws raised, feather crest"),
            "02_stomper":   ("Stomper",     "Each footfall files a complaint with the seismograph.",
                             "massive ankylosaur bruiser, clay-brown armor plates, club tail"),
            "03_ptero":     ("Pterodon",    "Rides thermals you can't see toward a lunch it can.",
                             "swooping pterosaur, khaki wings spread, long head crest, beak open"),
            "04_spitter":   ("Spitter",     "Keeps its venom pre-chewed.",
                             "dilophosaurus with olive neck frill extended, venom dripping from open jaws"),
            "05_hatchling": ("Hatchling",   "Fresh from the egg, faster than it has any right to be.",
                             "tiny sprinting baby dinosaur, leaf-green, big eyes, bit of eggshell on its head"),
            "06_bruiser":   ("Triceratops", "Three horns, zero negotiations.",
                             "armored triceratops charging head-on, brown hide, three horns, bone frill, imposing"),
        },
        "boss_parts": [
            ("sprites/06_dino/boss_head.png", "tyrant king tyrannosaurus head wearing a golden crown, olive-green scales, bared teeth"),
            ("sprites/06_dino/boss_hand.png", "clawed saurian fist, clay-brown scales, black talons"),
            ("sprites/06_dino/boss_hand.png", None),
        ],
    },
}

COMMON = [
    ("sprites/common/player.png", 36,
     "wide low retro muscle-car spaceship seen directly from above pointing up, bright orange body, black dual racing stripes, black hood scoop, small teal windshield, big chrome rocket booster pods on both sides, twin orange engine flames trailing from the rear, chunky silhouette, 70s street machine vibe"),
    ("sprites/common/gem.png", 16,
     "small faceted crystal gem, white and pale gray tones only, sparkling"),
    ("sprites/common/drone.png", 18,
     "small diamond-shaped attack drone, white and gray metal, tiny thrusters"),
    ("sprites/common/pickup_magnet.png", 26,
     "horseshoe magnet icon, red body with ice-white tips, arcade power-up badge"),
    ("sprites/common/pickup_wipe.png", 26,
     "radial shockwave blast icon, gold rings bursting outward, arcade power-up badge"),
    ("sprites/common/pickup_levelup.png", 26,
     "upward double-chevron with a star icon, lime green, arcade power-up badge"),
    ("sprites/common/pickup_heal.png", 26,
     "pixel heart icon, pinkred with white shine, arcade power-up badge"),
    ("sprites/common/pickup_freeze.png", 26,
     "six-pointed snowflake icon, pale cyan, arcade power-up badge"),
]


# subjects that are magenta/pink: key on green instead
BG_OVERRIDES = {
    "sprites/01_void/gazehound.png": "green",
    "sprites/01_void/nightbloom.png": "green",
    "sprites/common/pickup_heal.png": "green",
}


def slug(name):
    return re.sub(r"[^a-z0-9]+", "_", name.lower()).strip("_")


def enemy_radius(toml_path):
    with open(toml_path, "rb") as f:
        return float(tomllib.load(f)["radius"])


def boss_radii(biome_dir):
    with open(biome_dir / "biome.toml", "rb") as f:
        b = tomllib.load(f)["boss"]
    parts = [(float(p.get("radius", 28))) for p in b.get("part", [])]
    return float(b["radius"]), parts


def sprite_size(radius):
    return max(16, round(radius * 2.25))


def collect_jobs():
    """-> list of (sprite_rel_path, size, prompt)"""
    jobs = []
    for bslug, spec in BIOMES.items():
        bdir = REPO / "assets" / "biomes" / bslug
        mood = spec["mood"]
        for stem, (name, _flavor, subject) in spec["enemies"].items():
            r = enemy_radius(bdir / "enemies" / f"{stem}.toml")
            rel = f"sprites/{bslug}/{slug(name)}.png"
            jobs.append((rel, sprite_size(r), f"{subject}, {mood}"))
        bossR, partRs = boss_radii(bdir)
        if "boss" in spec:
            rel, subject = spec["boss"]
            jobs.append((rel, sprite_size(bossR), f"{subject}, {mood}"))
        for i, (rel, subject) in enumerate(spec.get("boss_parts", [])):
            if subject is None:
                continue  # shares a sprite generated by an earlier part
            jobs.append((rel, sprite_size(partRs[i]), f"{subject}, {mood}"))
    for rel, size, subject in COMMON:
        jobs.append((rel, size, subject))
    # dedup by path, keep first
    seen, out = set(), []
    for j in jobs:
        if j[0] not in seen:
            seen.add(j[0])
            out.append(j)
    return out


# ---- TOML patching -------------------------------------------------------------

def patch_kv(text, key, value, after_key):
    """Set key="value" in `text`: replace an existing line or insert after `after_key`'s line."""
    line = f'{key}="{value}"'
    if re.search(rf"^{key}\s*=", text, re.M):
        return re.sub(rf"^{key}\s*=.*$", line, text, count=1, flags=re.M)
    return re.sub(rf"(^{after_key}\s*=.*$)", rf"\1\n{line}", text, count=1, flags=re.M)


def patch_enemy_tomls():
    for bslug, spec in BIOMES.items():
        for stem, (name, flavor, _subject) in spec["enemies"].items():
            p = REPO / "assets" / "biomes" / bslug / "enemies" / f"{stem}.toml"
            t = p.read_text()
            t = re.sub(r'^name\s*=.*$', f'name="{name}"', t, count=1, flags=re.M)
            t = patch_kv(t, "sprite", f"sprites/{bslug}/{slug(name)}.png", "name")
            t = patch_kv(t, "flavor", flavor, "sprite")
            p.write_text(t)
            print(f"patched {p.relative_to(REPO)} -> {name}")


def patch_biome_tomls():
    for bslug, spec in BIOMES.items():
        p = REPO / "assets" / "biomes" / bslug / "biome.toml"
        t = p.read_text()
        if "boss" in spec:
            rel = spec["boss"][0]
            # add/replace sprite under [boss] (the boss name= line inside the [boss] table)
            m = re.search(r"(^\[boss\]\s*$)(.*?)(?=^\[|\Z)", t, re.M | re.S)
            block = m.group(2)
            if re.search(r"^sprite\s*=", block, re.M):
                block = re.sub(r"^sprite\s*=.*$", f'sprite="{rel}"', block, count=1, flags=re.M)
            else:
                block = re.sub(r"(^name\s*=.*$)", rf'\1\nsprite="{rel}"', block, count=1, flags=re.M)
            t = t[: m.start(2)] + block + t[m.end(2):]
        parts = spec.get("boss_parts", [])
        if parts:
            chunks = re.split(r"(^\[\[boss\.part\]\]\s*$)", t, flags=re.M)
            # chunks: [pre, header, body, header, body, ...]
            pi = 0
            for ci in range(2, len(chunks), 2):
                if pi >= len(parts):
                    break
                rel = parts[pi][0]
                body = chunks[ci]
                if re.search(r"^sprite\s*=", body, re.M):
                    body = re.sub(r"^sprite\s*=.*$", f'sprite="{rel}"', body, count=1, flags=re.M)
                else:
                    body = f'\nsprite="{rel}"' + body
                chunks[ci] = body
                pi += 1
            t = "".join(chunks)
        p.write_text(t)
        print(f"patched {p.relative_to(REPO)}")


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--force", action="store_true")
    ap.add_argument("--only", default=None)
    ap.add_argument("--no-toml", action="store_true")
    ap.add_argument("--outroot", default=None,
                    help="write sprites under this dir instead of assets/ (variant runs)")
    ap.add_argument("--seed-offset", type=int, default=0,
                    help="added to every sprite's base seed (variant runs)")
    ap.add_argument("--suffix", default="",
                    help="filename suffix for variant runs (e.g. _2 -> charhulk_2.png)")
    args = ap.parse_args()
    outroot = Path(args.outroot) if args.outroot else (REPO / "assets")

    jobs = collect_jobs()
    todo = [(rel, size, prompt) for rel, size, prompt in jobs
            if (args.only is None or args.only in rel)
            and (args.force or not (
                (outroot / rel).with_name(Path(rel).stem + args.suffix + ".png")
                if args.suffix else (outroot / rel)).exists())]
    print(f"{len(jobs)} sprites total, generating {len(todo)}")
    failed = []
    for i, (rel, size, prompt) in enumerate(todo):
        if i and i % 25 == 0:
            spritegen.free_vram()   # periodic flush: VRAM fragments over long batches
        out = outroot / rel
        if args.suffix:
            out = out.with_name(out.stem + args.suffix + out.suffix)
        seed = zlib.crc32(rel.encode()) % 100000 + args.seed_offset
        print(f"[{i+1}/{len(todo)}] {rel} ({size}px)")
        bg = BG_OVERRIDES.get(rel, "magenta")
        if not spritegen.generate_sprite(prompt, size, out, seed=seed, max_tries=4, bg=bg):
            failed.append(rel)
            print(f"  FAILED {rel}", file=sys.stderr)
    if not args.no_toml and args.only is None and args.outroot is None:
        patch_enemy_tomls()
        patch_biome_tomls()
    print(f"done. {len(todo) - len(failed)} generated, {len(failed)} failed: {failed}")
    sys.exit(1 if failed else 0)


if __name__ == "__main__":
    main()

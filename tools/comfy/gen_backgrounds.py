#!/usr/bin/env python3
"""Muted, seamless, interchangeable background tiles per biome (v4: inpainting).

Per biome, four different terrain canvases come from the krea2 pixel pipeline
(features — stars, pines, ember specks — are part of the terrain prompts).
Seam work is done by the MODEL, not procedurally:

  1. every canvas is made toroidal by rolling it half-and-half and inpainting
     the seam cross (denoise 0.75);
  2. canvas 0 becomes the anchor; every variant keeps the anchor's border ring
     and a different (rolled) canvas interior, and the transition band between
     them is inpainted (denoise 0.7) so krea2 paints an organic join.

All variants share byte-identical borders -> any arrangement is seamless, and
4 canvases x 3 interior shifts = 12 genuinely different tiles per biome.
Everything is muted toward the biome background color and palette-snapped last.

Output: assets/backgrounds/<biome>/bg_v00..11.png + a mixed preview.

Usage: gen_backgrounds.py [--only BIOME] [--preview-dir DIR] [--mute 0.45]
"""

import argparse
import sys
import tomllib
from pathlib import Path

import numpy as np
from PIL import Image

sys.path.insert(0, str(Path(__file__).resolve().parent))
import spritegen
import bg_inpaint

REPO = Path(__file__).resolve().parent.parent.parent
OUT = REPO / "assets" / "backgrounds"

TILE = 240          # shipped tile size
WORK = 720          # inpainting resolution (3x nearest)
RING = 40           # outer pixels (at WORK scale) that are pure anchor ring
BANDLO, BANDHI = 40, 152   # inpainted transition band (distance from border)

TEX_STYLE = (
    "pixel art aerial view of natural terrain for a retro 16-bit game background, "
    "chunky square pixels, flat shading, low contrast and muted, organic irregular composition, "
    "no repeating grid pattern, terrain fills the whole image edge to edge, "
    "no border, no frame, no text, "
)

BIOMES = {
    "01_void": [
        "deep space, near-black ink and navy, very faint dust wisps",
        "deep space with sparse tiny dim stars, ink and navy",
        "deep space with a faint wispy violet nebula haze, mostly dark",
        "deep space with a few scattered dim star clusters, periwinkle points on near-black",
    ],
    "02_ember": [
        "dark basalt volcanic rock ground, charcoal, subtle",
        "dark volcanic rock with faint dim ember specks glowing in cracks",
        "cooled lava field with thin faint maroon crack veins, mostly dark rock",
        "rough dark volcanic gravel and slabs, charcoal and deep grey",
    ],
    "03_frost": [
        "natural windswept snow field from above, smooth irregular drifts, pale blue-white",
        "snow field with shallow shadowed hollows and soft wind ripples",
        "snow field with a few scattered snow-covered pine treetops seen from directly above",
        "snow field crossed by a few thin pale ice cracks, mostly smooth drifts",
    ],
    "04_tempest": [
        "storm cloud tops seen from above, deep grey-blue billows",
        "storm clouds with a few lighter billowing puffs, grey-blue",
        "storm clouds with faint dim golden glow patches deep inside",
        "wind-streaked storm clouds, long soft grey-blue striations",
    ],
    "05_abyss": [
        "deep ocean floor, near-black teal sediment, very faint particulate",
        "dark seabed with faint dim aqua bioluminescent motes",
        "dark seabed with a few dim coral and rock silhouettes from above",
        "rippled dark teal sand seabed, subtle dune shadows",
    ],
    "06_dino": [
        "jungle floor seen from above, dark mossy greens",
        "jungle floor with scattered dark treetop canopies seen from directly above",
        "dense ferns and undergrowth from above, muted olive and dark green",
        "jungle floor with a few mossy boulders and roots from above",
    ],
}

ROLLS = [(0, 0), (1, 3), (3, 5)]   # interior shifts in eighths of the tile


def palette_names():
    with open(REPO / "assets" / "palette.toml", "rb") as f:
        pal = tomllib.load(f)
    return {n: tuple(int(c[i:i+2], 16) for i in (0, 2, 4))
            for n, c in zip(pal["names"], pal["colors"])}


def biome_bg(bslug, names):
    with open(REPO / "assets" / "biomes" / bslug / "biome.toml", "rb") as f:
        b = tomllib.load(f)
    return np.array(names[b["theme"]["background"]], np.float32)


def mute(arr, bg, f):
    out = (arr.astype(np.float32) * (1 - f) + bg[None, None, :] * f).astype(np.uint8)
    return spritegen.snap_to_palette(out)


def crop_chroma_frame(img, thresh=110):
    a = np.asarray(img.convert("RGB")).astype(np.int32)
    h, w, _ = a.shape
    edge = np.concatenate([a[0], a[-1], a[:, 0], a[:, -1]])
    colors, counts = np.unique(edge.reshape(-1, 3), axis=0, return_counts=True)
    if counts.max() < 0.3 * len(edge):
        return img
    bg = colors[counts.argmax()]
    near = ((a - bg) ** 2).sum(axis=2) <= thresh * thresh
    rows = np.where(~near.all(axis=1))[0]
    cols = np.where(~near.all(axis=0))[0]
    if len(rows) < h // 2 or len(cols) < w // 2:
        return img
    out = img.crop((cols[0], rows[0], cols[-1] + 1, rows[-1] + 1))
    b = min(out.width, out.height) // 20
    return out.crop((b, b, out.width - b, out.height - b))


def gen_canvas(subject, path, seed):
    if path.exists():
        return
    for attempt in range(3):
        wf = spritegen.build_workflow(TEX_STYLE + subject, seed + attempt * 977,
                                      width=640, height=640,
                                      prefix=f"bg_{path.stem}", raw=True)
        try:
            outs = spritegen.run_workflow(wf)
        except (RuntimeError, TimeoutError) as e:
            print(f"  canvas: {e}, retrying", file=sys.stderr)
            continue
        img = crop_chroma_frame(outs.get("30") or next(iter(outs.values())))
        rgba = np.dstack([np.asarray(img.convert("RGB")),
                          np.full(img.size[::-1], 255, np.uint8)])
        spritegen.regrid_to_size(Image.fromarray(rgba, "RGBA"), TILE).convert("RGB").save(path)
        return
    raise RuntimeError(f"canvas failed: {path}")


def up3(a240):
    return np.asarray(Image.fromarray(a240, "RGB").resize((WORK, WORK), Image.NEAREST))


def down3(a720):
    return a720[1::3, 1::3]   # center-of-cell sample; art pixels are >=3px at WORK


def dist_field():
    yy, xx = np.mgrid[:WORK, :WORK]
    return np.minimum(np.minimum(xx, WORK - 1 - xx), np.minimum(yy, WORK - 1 - yy))


def toroidal_via_inpaint(c240, prompt, seed, tag):
    """Roll half-and-half (seams move to a center cross) and let the model heal them."""
    rolled = np.roll(up3(c240), (WORK // 2, WORK // 2), (0, 1))
    yy, xx = np.mgrid[:WORK, :WORK]
    cross = (np.abs(xx - WORK // 2) < 40) | (np.abs(yy - WORK // 2) < 40)
    margin = (xx >= 24) & (xx < WORK - 24) & (yy >= 24) & (yy < WORK - 24)
    mask = Image.fromarray(np.where(cross & margin, 255, 0).astype(np.uint8), "L")
    healed = bg_inpaint.inpaint(Image.fromarray(rolled, "RGB"), mask,
                                TEX_STYLE + prompt, seed, denoise=0.75, tag=tag)
    return np.asarray(healed)


def variant_via_inpaint(anchor720, interior720, prompt, seed, tag):
    """Anchor ring + new interior; krea2 paints the transition band between them."""
    d = dist_field()
    ramp = np.clip((d - RING) / (BANDHI - RING), 0, 1)
    rng = np.random.default_rng(seed)
    scaffold = np.where((rng.random(d.shape) < ramp)[..., None], interior720, anchor720)
    mask = Image.fromarray(np.where((d >= BANDLO) & (d <= BANDHI), 255, 0).astype(np.uint8), "L")
    out = bg_inpaint.inpaint(Image.fromarray(scaffold.astype(np.uint8), "RGB"), mask,
                             TEX_STYLE + prompt, seed, denoise=0.7, tag=tag)
    return np.asarray(out)


def build_biome(bslug, prompts, bg, texdir, preview_dir):
    healed = []
    for i, subject in enumerate(prompts):
        hp = texdir / f"{bslug}_t{i}.png"
        if not hp.exists():
            c = np.asarray(Image.open(texdir / f"{bslug}_c{i}.png").convert("RGB"))
            Image.fromarray(toroidal_via_inpaint(c, subject, 500 + i, f"{bslug}_t{i}"), "RGB").save(hp)
        healed.append(np.asarray(Image.open(hp).convert("RGB")))
    anchor = healed[0]

    outdir = OUT / bslug
    outdir.mkdir(parents=True, exist_ok=True)
    v = 0
    for ci, c in enumerate(healed):
        for ry, rx in ROLLS:
            if ci == 0 and (ry, rx) == (0, 0):
                out720 = anchor                       # v00 is the pure anchor
            else:
                interior = np.roll(c, (ry * WORK // 8, rx * WORK // 8), (0, 1))
                out720 = variant_via_inpaint(anchor, interior, prompts[ci],
                                             seed=v * 131 + 17, tag=f"{bslug}_v{v:02d}")
            tile = mute(down3(out720), bg, ARGS.mute)
            Image.fromarray(tile, "RGB").save(outdir / f"bg_v{v:02d}.png")
            v += 1

    # borders must be byte-identical: stamp v00's ring onto every variant
    ring_px = RING // 3
    ref = np.asarray(Image.open(outdir / "bg_v00.png"))
    for k in range(v):
        p = outdir / f"bg_v{k:02d}.png"
        a = np.asarray(Image.open(p)).copy()
        a[:ring_px], a[-ring_px:] = ref[:ring_px], ref[-ring_px:]
        a[:, :ring_px], a[:, -ring_px:] = ref[:, :ring_px], ref[:, -ring_px:]
        Image.fromarray(a).save(p)

    rng = np.random.default_rng(9)
    prev = Image.new("RGB", (3 * TILE, 3 * TILE))
    for gy in range(3):
        for gx in range(3):
            k = int(rng.integers(0, v))
            prev.paste(Image.open(outdir / f"bg_v{k:02d}.png"), (gx * TILE, gy * TILE))
    prev.save(Path(preview_dir) / f"bg_preview_{bslug}.png")
    print(f"{bslug}: {v} tiles + preview")


def main():
    global ARGS
    ap = argparse.ArgumentParser()
    ap.add_argument("--only", default=None)
    ap.add_argument("--preview-dir", default=".")
    ap.add_argument("--mute", type=float, default=0.45)
    ARGS = ap.parse_args()

    names = palette_names()
    texdir = OUT / "_textures"
    texdir.mkdir(parents=True, exist_ok=True)
    for i, (bslug, prompts) in enumerate(BIOMES.items()):
        if ARGS.only and ARGS.only not in bslug:
            continue
        for j, subject in enumerate(prompts):
            gen_canvas(subject, texdir / f"{bslug}_c{j}.png", seed=7000 + i * 40 + j)
        build_biome(bslug, prompts, biome_bg(bslug, names), texdir, ARGS.preview_dir)
    print("done")


if __name__ == "__main__":
    main()

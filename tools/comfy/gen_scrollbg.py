#!/usr/bin/env python3
"""Vertically-scrolling background loops per biome.

Per biome, four same-material terrain canvases (features like pines / star
clusters / ember specks are IN the terrain prompts) are stacked into one tall
strip. Every junction — including bottom-to-top, so the strip loops — is healed
by krea2 inpainting a horizontal band between two segments of the same ground
material (same-material joins avoid the drawn-coastline failure of
cross-texture bands). The loop is then muted toward the biome background color
and palette-snapped.

Output: assets/backgrounds/<biome>/scroll.png (240 wide x 960 tall, drawn at 3x
in-game = 720 x 2880 on screen), plus a wrap-check preview.

Usage: gen_scrollbg.py [--only BIOME] [--preview-dir DIR] [--mute 0.5]
"""

import argparse
import zlib
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

TILE = 720          # shipped strip width == screen width: TRUE 1:1 pixels
WORK = 720          # inpainting resolution == shipped resolution
BAND = 130          # junction band height

TEX_STYLE = (
    "pixel art aerial view of natural terrain for a retro 16-bit game background, "
    "flat shading, organic irregular composition, "
    "no repeating grid pattern, one uniform ground material covering the whole image, "
    "the same material at every edge, no cliffs, no walls, no clearings, no border, no frame, no text, "
)

STYLE_OVERRIDE = {}

BIOMES = {   # ONE prompt per biome, x4 seeds — variation comes from the seed
    "01_void": ["floating islands of mossy alien terrain drifting in dark starry space, "
                "seen from above, teal and green vegetation on rocky island bases"] * 4,
    "02_ember": ["aerial view of a dark volcanic landscape, cooled lava plateaus and "
                 "rock formations of deep maroon and umber, winding glowing crimson lava "
                 "cracks between them, seen from above"] * 4,
    "03_frost": ["aerial view cropped from the middle of a vast endless arctic snowscape, "
                 "the snow continues past every edge of the frame, smooth windswept drifts, "
                 "a few scattered snow-covered pine treetops, only white and pale icy blue tones"] * 4,
    "04_tempest": ["windswept highland meadow of olive grass with scattered purple heather "
                   "bushes and pale rocks, seen from above"] * 4,
    "05_abyss": ["aerial view of deep dark abyssal water, swirling bioluminescent currents, "
                 "glowing teal and aqua eddies curling and spiraling through near-black water, "
                 "no land, no coastline, no shore"] * 4,
    "06_dino": ["aerial view of a lush prehistoric jungle, vibrant green treetop canopies "
                "and leafy ferns, a winding muddy river glinting between the trees, "
                "scattered sunlit clearings, lively saturated greens"] * 4,
}


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
    """f=0: ship the loop exactly as generated (palette snap only). f>0 dims
    multiplicatively, tinted toward the biome background hue (the palette has no
    neutral mid-grey, so the tint keeps mid-tones in the biome's color family)."""
    if f <= 0:
        return spritegen.snap_to_palette(arr.copy())
    bgn = bg / max(bg.max(), 1.0)
    target = (0.30 + 0.70 * bgn) * 0.69
    factors = (1.0 - f) + f * target
    out = np.clip(arr.astype(np.float32) * factors[None, None, :], 0, 255).astype(np.uint8)
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


def gen_canvas(subject, path, seed, style=None):
    if path.exists():
        return
    for attempt in range(4):
        wf = spritegen.build_workflow((style or TEX_STYLE) + subject, seed + attempt * 977,
                                      width=TILE, height=TILE,
                                      prefix=f"bg_{path.stem}", raw=True)
        try:
            outs = spritegen.run_workflow(wf)
        except (RuntimeError, TimeoutError) as e:
            if "OutOfMemory" in str(e) or "VRAM" in str(e) or "Allocation on device" in str(e):
                print("  canvas: GPU OOM, freeing VRAM", file=sys.stderr)
                spritegen.free_vram()
            else:
                print(f"  canvas: {e}, retrying", file=sys.stderr)
            continue
        # TRUE 1:1: raw decode at screen resolution, palette-snapped. No regrid.
        img = outs.get("30") or next(iter(outs.values()))
        if crop_chroma_frame(img).size != img.size and attempt < 3:
            print("  canvas has a border frame, retrying", file=sys.stderr)
            continue
        a = spritegen.snap_to_palette(np.asarray(img.convert("RGB")).copy())
        Image.fromarray(a, "RGB").save(path)
        return
    raise RuntimeError(f"canvas failed: {path}")


def match_colors(src, ref):
    """Per-channel histogram matching: every segment adopts the anchor segment's
    full tonal distribution (mean/std transfer left residual casts -> stripes)."""
    out = np.empty_like(src)
    for c in range(3):
        sv = src[..., c].ravel()
        sq = np.sort(sv)
        rq = np.sort(ref[..., c].ravel())
        pos = np.searchsorted(sq, sv, side="left") / max(len(sq) - 1, 1)
        out[..., c] = np.interp(pos, np.linspace(0, 1, len(rq)), rq).reshape(src[..., c].shape)
    return out.astype(np.uint8)


def up3(a):
    return a   # segments already at shipped/work resolution (1:1)


def min_error_seam(diff, r0, r1):
    """Left-to-right min-cost horizontal path through diff[r0:r1, :] (rows may
    step +-1 per column). Returns one row index per column."""
    D = diff[r0:r1, :].astype(np.float64)
    h, w = D.shape
    cost = D.copy()
    back = np.zeros((h, w), np.int8)
    for c in range(1, w):
        prev = cost[:, c - 1]
        up = np.roll(prev, 1);  up[0] = np.inf
        dn = np.roll(prev, -1); dn[-1] = np.inf
        stacked = np.stack([up, prev, dn])
        best = stacked.argmin(axis=0)
        cost[:, c] += stacked[best, np.arange(h)]
        back[:, c] = best - 1
    r = int(cost[:, -1].argmin())
    seam = np.empty(w, np.int64)
    for c in range(w - 1, -1, -1):
        seam[c] = r
        r += back[r, c]
    return seam + r0


def heal_junction(loop, y0, prompt, seed, tag):
    """img2img the junction window (model sees both textures in pixel space and
    paints a coherent re-render), then merge along MIN-ERROR SEAMS — hard cuts
    where re-render and original agree best. No dithering anywhere."""
    H = loop.shape[0]
    rows = (np.arange(y0 - WORK // 2, y0 + WORK // 2)) % H
    win = loop[rows]
    out = bg_inpaint.img2img_window(Image.fromarray(win, "RGB"),
                                    prompt, seed, denoise=0.6, tag=tag)
    a = np.asarray(out)
    diff = np.abs(a.astype(np.int32) - win.astype(np.int32)).sum(axis=2)
    mid = WORK // 2
    top = min_error_seam(diff, mid - BAND - 70, mid - BAND + 10)
    bot = min_error_seam(diff, mid + BAND - 10, mid + BAND + 70)
    yy = np.arange(WORK)[:, None]
    keep = (yy > top[None, :]) & (yy < bot[None, :])
    loop[rows] = np.where(keep[..., None], a, win)


def build_biome(bslug, prompts, bg, texdir, preview_dir, mute_f):
    loop_cache = texdir / f"{bslug}_loop.png"
    if loop_cache.exists():
        loop = np.asarray(Image.open(loop_cache).convert("RGB")).copy()
        H = loop.shape[0]
    else:
        segs = []
        for j, subject in enumerate(prompts):
            p = texdir / f"{bslug}_c{j}.png"
            gen_canvas(subject, p, seed=7000 + zlib.crc32(bslug.encode()) % 97 * 40 + j,
                       style=STYLE_OVERRIDE.get(bslug))
            c = np.asarray(Image.open(p).convert("RGB"))
            if segs:
                c = match_colors(c, ref)
            else:
                ref = c
            segs.append(up3(c))
        loop = np.vstack(segs).copy()           # 2880 x 720
        H = loop.shape[0]
        for k in range(len(segs)):
            y0 = (k * WORK) % H                 # junction above segment k (k=0 is the wrap)
            prompt = prompts[k]                 # material below the junction
            style = STYLE_OVERRIDE.get(bslug, TEX_STYLE)
            heal_junction(loop, y0, style + prompt, seed=300 + k, tag=f"{bslug}_j{k}")
        Image.fromarray(loop, "RGB").save(loop_cache)

    tile = mute(loop, bg, mute_f)
    outdir = OUT / bslug
    outdir.mkdir(parents=True, exist_ok=True)
    Image.fromarray(tile, "RGB").save(outdir / "scroll.png")

    # preview: the strip tiled twice vertically, squeezed side by side, so every
    # junction INCLUDING the wrap is visible
    prev = Image.new("RGB", (tile.shape[1] * 2 + 8, tile.shape[0]))
    doubled = np.vstack([tile, tile])
    left = Image.fromarray(doubled[: tile.shape[0]], "RGB")
    right = Image.fromarray(doubled[tile.shape[0] // 2 : tile.shape[0] // 2 + tile.shape[0]], "RGB")
    prev.paste(left, (0, 0))
    prev.paste(right, (tile.shape[1] + 8, 0))
    prev = prev.resize((prev.width * 2 // 5, prev.height * 2 // 5), Image.LANCZOS)
    prev.save(Path(preview_dir) / f"scroll_preview_{bslug}.png")
    print(f"{bslug}: scroll.png ({tile.shape[1]}x{tile.shape[0]}) + preview")


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--only", default=None)
    ap.add_argument("--preview-dir", default=".")
    ap.add_argument("--mute", type=float, default=0.5)
    args = ap.parse_args()

    names = palette_names()
    texdir = REPO / "tools" / "comfy" / "bg_cache"
    texdir.mkdir(parents=True, exist_ok=True)
    for bslug, prompts in BIOMES.items():
        if args.only and args.only not in bslug:
            continue
        try:
            build_biome(bslug, prompts, biome_bg(bslug, names), texdir,
                        args.preview_dir, args.mute)
        except Exception as e:
            print(f"BIOME FAILED {bslug}: {e}", file=sys.stderr)
    print("done")


if __name__ == "__main__":
    main()

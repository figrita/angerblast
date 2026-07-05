#!/usr/bin/env python3
"""Generate palette-locked pixel-art sprites for Angerblast via ComfyUI + Krea2 turbo.

Pipeline per sprite:
  1. Queue the pixelart workflow (krea2 turbo -> PixelArtDetectorToImage grid
     detection/downscale -> PixelArtDetectorConverter BLK NX64 quantize).
  2. Fetch the 1:1 pixel-art result from ComfyUI.
  3. Post-process: flood-fill background removal from the edges, crop to
     content, pad square, NEAREST-resize to the exact in-game size, re-snap
     every opaque pixel to the BLK NX64 palette.
  4. Verify: every opaque pixel is one of the 64 palette colors.

Usage:
  spritegen.py --prompt "..." --size 22 --out sprite.png [--seed N] [--keep-bg]
"""

import argparse
import json
import sys
import time
import tomllib
import urllib.request
import urllib.parse
from pathlib import Path

from PIL import Image
import numpy as np

HERE = Path(__file__).resolve().parent
REPO = HERE.parent.parent
SERVER = "http://127.0.0.1:8188"

STYLE_PREFIX = (
    "pixel art sprite, retro 16-bit arcade shmup enemy design, chunky visible square pixels, "
    "crisp 1-pixel dark outline, flat shading with 3 tone steps, "
)
STYLE_SUFFIX = (
    ", centered single subject on a plain solid bright {bg} background, no text, no watermark, "
    "no frame, symmetrical top-down view, game asset"
)


def load_palette():
    with open(REPO / "assets" / "palette.toml", "rb") as f:
        pal = tomllib.load(f)
    return np.array(
        [[int(h[i : i + 2], 16) for i in (0, 2, 4)] for h in pal["colors"]],
        dtype=np.int32,
    )


PALETTE = load_palette()


def api(path, data=None, retries=6):
    req = urllib.request.Request(SERVER + path)
    if data is not None:
        req.data = json.dumps(data).encode()
        req.add_header("Content-Type", "application/json")
    for i in range(retries):
        try:
            with urllib.request.urlopen(req, timeout=300) as r:
                body = r.read()
                return json.loads(body) if body else {}
        except (urllib.error.URLError, ConnectionError) as e:
            if i == retries - 1:
                raise
            wait = 10 * (i + 1)  # server may be restarting; give it up to ~3.5 min
            print(f"  api {path}: {e}; retry in {wait}s", file=sys.stderr)
            time.sleep(wait)


def free_vram(unload=True):
    """Ask ComfyUI to drop cached models/VRAM (recovers from fragmentation creep)."""
    try:
        api("/free", {"unload_models": unload, "free_memory": True}, retries=1)
    except Exception as e:
        print(f"  /free failed: {e}", file=sys.stderr)


def build_workflow(prompt, seed, width=1024, height=1024, prefix="angerblast_sprite", bg="magenta", raw=False):
    """raw=True uses the prompt verbatim (textures/backgrounds); otherwise the
    sprite style prefix/suffix (incl. the chroma-key backdrop) wraps it."""
    wf = json.loads((HERE / "pixelart_workflow_api.json").read_text())
    wf.pop("_meta_comment", None)
    wf["6"]["inputs"]["text"] = prompt if raw else STYLE_PREFIX + prompt + STYLE_SUFFIX.format(bg=bg)
    wf["3"]["inputs"]["seed"] = seed
    wf["5"]["inputs"]["width"] = width
    wf["5"]["inputs"]["height"] = height
    wf["29"]["inputs"]["filename_prefix"] = prefix
    return wf


def run_workflow(wf, poll=1.0, timeout=600):
    """Run the workflow; return {node_id: PIL.Image} for every SaveImage output."""
    pid = api("/prompt", {"prompt": wf})["prompt_id"]
    t0 = time.time()
    while time.time() - t0 < timeout:
        hist = api(f"/history/{pid}")
        if pid in hist:
            entry = hist[pid]
            status = entry.get("status", {})
            if status.get("status_str") == "error":
                msgs = [m for m in status.get("messages", []) if m[0] == "execution_error"]
                raise RuntimeError(f"workflow error: {json.dumps(msgs)[:800]}")
            outputs = entry.get("outputs", {})
            if status.get("status_str") == "success" and not any(
                n.get("images") for n in outputs.values()
            ):
                # fully cache-hit execution: nothing was saved, nothing to fetch
                raise RuntimeError("workflow completed with no outputs (execution cached)")
            if outputs:
                images = {}
                for nid, node in outputs.items():
                    for img in node.get("images", []):
                        q = urllib.parse.urlencode(
                            {"filename": img["filename"], "subfolder": img["subfolder"], "type": img["type"]}
                        )
                        with urllib.request.urlopen(f"{SERVER}/view?{q}", timeout=60) as r:
                            raw = r.read()
                        import io
                        images[nid] = Image.open(io.BytesIO(raw)).convert("RGB")
                return images
        time.sleep(poll)
    raise TimeoutError("workflow did not finish")


def palette_indices(arr):
    """Nearest BLK NX64 palette index for every pixel of an HxWx3 uint8 array."""
    flat = arr.reshape(-1, 3).astype(np.int32)
    idx = np.empty(len(flat), dtype=np.int64)
    for i in range(0, len(flat), 65536):  # chunked: full 1024^2 broadcast is ~800MB
        chunk = flat[i : i + 65536]
        d = ((chunk[:, None, :] - PALETTE[None, :, :]) ** 2).sum(axis=2)
        idx[i : i + 65536] = d.argmin(axis=1)
    return idx.reshape(arr.shape[:2])


def snap_to_palette(arr):
    """Nearest BLK NX64 color for every pixel of an HxWx3 uint8 array."""
    return PALETTE[palette_indices(arr)].astype(np.uint8)


def remove_background(img, tol=48):
    """Flood-fill transparent from all edge pixels whose color is near the
    dominant edge color. Returns RGBA."""
    rgb = np.asarray(img.convert("RGB")).astype(np.int32)
    h, w, _ = rgb.shape
    edge = np.concatenate([rgb[0], rgb[-1], rgb[:, 0], rgb[:, -1]])
    colors, counts = np.unique(edge.reshape(-1, 3), axis=0, return_counts=True)
    bg = colors[counts.argmax()]
    near_bg = ((rgb - bg) ** 2).sum(axis=2) <= tol * tol
    # drop shadows painted over the chroma key are just a darkened key color
    # (~s*bg, s<1): treat them as background so the flood fill eats them too
    bgf = bg.astype(np.float64)
    s = (rgb * bgf).sum(axis=2) / max((bgf * bgf).sum(), 1.0)
    resid2 = ((rgb - s[..., None] * bgf) ** 2).sum(axis=2)
    near_bg |= (resid2 <= tol * tol) & (s >= 0.25) & (s <= 0.92)

    # BFS flood fill from edges through near-bg pixels
    mask = np.zeros((h, w), dtype=bool)  # True = background
    stack = [(y, x) for y in (0, h - 1) for x in range(w) if near_bg[y, x]]
    stack += [(y, x) for x in (0, w - 1) for y in range(h) if near_bg[y, x]]
    for y, x in stack:
        mask[y, x] = True
    while stack:
        y, x = stack.pop()
        for ny, nx in ((y - 1, x), (y + 1, x), (y, x - 1), (y, x + 1)):
            if 0 <= ny < h and 0 <= nx < w and near_bg[ny, nx] and not mask[ny, nx]:
                mask[ny, nx] = True
                stack.append((ny, nx))

    # debleed: peel off opaque border pixels still blended toward the key color
    # (anti-aliased halo), so key-tinted mixes can't vote in the regrid
    halo = ((rgb - bg) ** 2).sum(axis=2) <= (tol * 2.2) ** 2
    for _ in range(2):
        t = mask
        nb = np.zeros_like(mask)
        nb[1:, :] |= t[:-1, :]; nb[:-1, :] |= t[1:, :]
        nb[:, 1:] |= t[:, :-1]; nb[:, :-1] |= t[:, 1:]
        mask = mask | (nb & halo)

    rgba = np.dstack([rgb.astype(np.uint8), np.where(mask, 0, 255).astype(np.uint8)])
    return Image.fromarray(rgba, "RGBA")


def regrid_to_size(img, size):
    """Crop RGBA to opaque bbox, pad square, then k-centroid regrid to exactly
    size x size: each output pixel is the DOMINANT palette color of one uniform
    source cell. The result is true pixel art on a single 1:1 grid — no mixed
    pixel scales, regardless of how (in)consistent the diffusion 'pixels' were."""
    a = np.asarray(img)
    ys, xs = np.where(a[:, :, 3] > 0)
    if len(ys) == 0:
        raise ValueError("sprite is fully transparent after background removal")
    a = a[ys.min() : ys.max() + 1, xs.min() : xs.max() + 1]
    h, w, _ = a.shape
    side = max(h, w)
    sq = np.zeros((side, side, 4), dtype=np.uint8)
    oy, ox = (side - h) // 2, (side - w) // 2
    sq[oy : oy + h, ox : ox + w] = a

    idx = palette_indices(sq[:, :, :3])
    opaque = sq[:, :, 3] > 127
    yb = np.linspace(0, side, size + 1).astype(int)
    xb = np.linspace(0, side, size + 1).astype(int)
    out = np.zeros((size, size, 4), dtype=np.uint8)
    for i in range(size):
        for j in range(size):
            ca = opaque[yb[i] : yb[i + 1], xb[j] : xb[j + 1]]
            if ca.size == 0 or ca.mean() < 0.5:
                continue  # cell is majority-transparent
            ci = idx[yb[i] : yb[i + 1], xb[j] : xb[j + 1]][ca]
            vals, counts = np.unique(ci, return_counts=True)
            out[i, j, :3] = PALETTE[vals[counts.argmax()]]
            out[i, j, 3] = 255
    return Image.fromarray(out, "RGBA")


def strip_outline(img, max_lum=46, passes=4):
    """Delete the dark outline ring: iteratively remove dark opaque pixels that
    touch transparency (4-neighborhood). Multiple passes eat chunky multi-pixel
    outlines and drop shadows; interior dark detail (not reachable from the
    silhouette edge) is untouched."""
    a = np.asarray(img.convert("RGBA")).copy()
    for _ in range(passes):
        op = a[:, :, 3] > 0
        pad = np.pad(op, 1, constant_values=False)
        exposed = ~(pad[:-2, 1:-1] & pad[2:, 1:-1] & pad[1:-1, :-2] & pad[1:-1, 2:])
        rgb = a[:, :, :3].astype(np.float32)
        lum = rgb[:, :, 0] * 0.299 + rgb[:, :, 1] * 0.587 + rgb[:, :, 2] * 0.114
        kill = op & exposed & (lum < max_lum)
        if not kill.any():
            break
        a[kill] = 0
    # sweep shadow crumbs: tiny detached dark components (bright sparkle
    # accents pass the luminance test and survive)
    op = a[:, :, 3] > 0
    h, w = op.shape
    seen = np.zeros_like(op)
    for sy in range(h):
        for sx in range(w):
            if not op[sy, sx] or seen[sy, sx]:
                continue
            comp, stack = [(sy, sx)], [(sy, sx)]
            seen[sy, sx] = True
            while stack:
                y, x = stack.pop()
                for ny, nx in ((y-1, x), (y+1, x), (y, x-1), (y, x+1)):
                    if 0 <= ny < h and 0 <= nx < w and op[ny, nx] and not seen[ny, nx]:
                        seen[ny, nx] = True
                        comp.append((ny, nx))
                        stack.append((ny, nx))
            if len(comp) <= 4:
                px = np.array([a[y, x, :3] for y, x in comp], dtype=np.float32)
                if (px[:, 0] * 0.299 + px[:, 1] * 0.587 + px[:, 2] * 0.114).mean() < 90:
                    for y, x in comp:
                        a[y, x] = 0
    return Image.fromarray(a, "RGBA")


def chunk_score(img, k):
    """Best-offset fraction of pixels reproducible by a uniform k x k block grid.
    ~0.5-0.75 for true 1:1 pixel art (flat fills), ~0.9+ when the sprite is
    effectively (k)x-scaled lower-res art (diffusion drew fake pixels spanning
    multiple cells of the final grid)."""
    a = np.asarray(img.convert("RGBA"))
    v = (a[:, :, 0].astype(np.int64) << 16) | (a[:, :, 1].astype(np.int64) << 8) | a[:, :, 2]
    v = np.where(a[:, :, 3] > 127, v, -1)
    h, w = v.shape
    best = 0.0
    for oy in range(k):
        for ox in range(k):
            match = tot = 0
            ys = ([oy - k] if oy else []) + list(range(oy, h, k))
            xs = ([ox - k] if ox else []) + list(range(ox, w, k))
            for y0 in ys:
                for x0 in xs:
                    blk = v[max(y0, 0):y0 + k, max(x0, 0):x0 + k]
                    if blk.size == 0 or not (blk >= 0).any():
                        continue  # fully transparent block: don't let bg inflate the score
                    _, counts = np.unique(blk, return_counts=True)
                    match += counts.max()
                    tot += blk.size
            if tot and match / tot > best:
                best = match / tot
    return round(best, 3)


def verify(img):
    a = np.asarray(img.convert("RGBA"))
    opaque = a[a[:, :, 3] == 255][:, :3].astype(np.int32)
    if len(opaque) == 0:
        return False, "no opaque pixels"
    pal = {tuple(c) for c in PALETTE.tolist()}
    bad = {tuple(c) for c in np.unique(opaque, axis=0).tolist()} - pal
    ncol = len(np.unique(opaque, axis=0))
    cov = len(opaque) / (a.shape[0] * a.shape[1])
    return (not bad), f"colors={ncol} coverage={cov:.2f} offpalette={len(bad)}"


def generate_sprite(prompt, size, out_path, seed=0, keep_bg=False, max_tries=3, bg="magenta",
                    min_cov=0.15, raw=False, max_chunk2=None, strip_dark_outline=False):
    """max_chunk2: if set, reject results whose 2x2 chunk_score exceeds it (fake
    low-res 'pixels') and retry with a new seed; after max_tries, the least
    chunky passing attempt is saved anyway.
    strip_dark_outline: erode the dark outline ring + drop shadow off the result
    (icons: outlines come out as 2-4px chunks at 26px)."""
    best = None   # (score, sprite) fallback when no attempt beats max_chunk2
    for attempt in range(max_tries):
        s = seed + attempt * 977
        # unique per-submission prefix: defeats ComfyUI's whole-graph execution cache
        # (a resubmitted identical graph "succeeds" without producing any outputs)
        wf = build_workflow(prompt, s, prefix=f"angerblast_{int(time.time())}_{s}_{Path(out_path).stem}", bg=bg, raw=raw)
        try:
            outs = run_workflow(wf)
        except (RuntimeError, TimeoutError) as e:
            msg = str(e)
            if "OutOfMemory" in msg or "VRAM" in msg or "Allocation on device" in msg:
                print(f"  attempt {attempt}: GPU OOM, freeing VRAM and retrying", file=sys.stderr)
                free_vram()
                time.sleep(5)
            else:
                print(f"  attempt {attempt}: {e}, retrying", file=sys.stderr)
            continue
        img = outs.get("30") or next(iter(outs.values()))  # raw decode preferred
        try:
            if keep_bg:
                rgba = np.dstack(
                    [np.asarray(img.convert("RGB")), np.full(img.size[::-1], 255, np.uint8)]
                )
                sprite = regrid_to_size(Image.fromarray(rgba, "RGBA"), size)
            else:
                sprite = regrid_to_size(remove_background(img), size)
        except ValueError as e:
            print(f"  attempt {attempt}: {e}, retrying", file=sys.stderr)
            continue
        pre = None
        if strip_dark_outline:
            pre = sprite
            sprite = strip_outline(sprite)
            if np.array_equal(np.asarray(sprite), np.asarray(pre)):
                pre = None   # strip was a no-op: no point saving a duplicate twin
        ok, info = verify(sprite)
        if "coverage=" not in info:   # e.g. outline strip ate everything
            print(f"  attempt {attempt}: {info}, retrying", file=sys.stderr)
            continue
        cov = float(info.split("coverage=")[1].split()[0])
        if not keep_bg and (cov < min_cov or cov > 0.98):  # sanity floor: slender subjects sit ~0.2
            print(f"  attempt {attempt}: bad coverage ({info}), retrying", file=sys.stderr)
            continue
        if ok:
            if max_chunk2 is not None:
                s2 = chunk_score(sprite, 2)
                if best is None or s2 < best[0]:
                    best = (s2, sprite, pre)
                if s2 > max_chunk2:
                    print(f"  attempt {attempt}: chunky fake pixels (s2={s2:.2f} > {max_chunk2}), retrying",
                          file=sys.stderr)
                    continue
                info += f" s2={s2:.2f}"
            _save_with_twin(sprite, pre, out_path)
            print(f"  saved {out_path} ({info})")
            return True
        print(f"  attempt {attempt}: verify failed ({info})", file=sys.stderr)
    if best is not None:
        _save_with_twin(best[1], best[2], out_path)
        print(f"  saved {out_path} (best of {max_tries}, still chunky: s2={best[0]:.2f})")
        return True
    return False


def _save_with_twin(sprite, pre, out_path):
    """Save the sprite; if an unstripped pre-outline-removal twin exists, save it
    next to it with an 'o' suffix (wpn_x_5.png -> wpn_x_5o.png) as an extra
    picker option."""
    out = Path(out_path)
    out.parent.mkdir(parents=True, exist_ok=True)
    sprite.save(out)
    if pre is not None:
        pre.save(out.with_name(out.stem + "o" + out.suffix))


def main():
    p = argparse.ArgumentParser()
    p.add_argument("--prompt", required=True)
    p.add_argument("--size", type=int, default=32)
    p.add_argument("--out", required=True)
    p.add_argument("--seed", type=int, default=0)
    p.add_argument("--keep-bg", action="store_true")
    args = p.parse_args()
    ok = generate_sprite(args.prompt, args.size, args.out, args.seed, args.keep_bg)
    sys.exit(0 if ok else 1)


if __name__ == "__main__":
    main()

#!/usr/bin/env python3
"""Assemble every generated sprite into labeled contact sheets (4x nearest upscale)
and verify BLK NX64 palette compliance. Usage: qc_sheet.py [outdir]"""

import sys
from pathlib import Path

import numpy as np
from PIL import Image, ImageDraw

sys.path.insert(0, str(Path(__file__).resolve().parent))
import spritegen

REPO = Path(__file__).resolve().parent.parent.parent
SPRITES = REPO / "assets" / "sprites"
BG = (18, 23, 61, 255)  # palette "ink"
SCALE = 4
CELL = 150
COLS = 8


def main():
    outdir = Path(sys.argv[1]) if len(sys.argv) > 1 else Path(".")
    files = sorted(SPRITES.rglob("*.png"))
    print(f"{len(files)} sprites")
    bad = []
    rows = (len(files) + COLS - 1) // COLS
    sheet = Image.new("RGBA", (COLS * CELL, rows * (CELL + 14)), BG)
    d = ImageDraw.Draw(sheet)
    for i, f in enumerate(files):
        im = Image.open(f).convert("RGBA")
        ok, info = spritegen.verify(im)
        if not ok:
            bad.append((str(f.relative_to(SPRITES)), info))
        big = im.resize((im.width * SCALE, im.height * SCALE), Image.NEAREST)
        if big.width > CELL - 4:
            big = im.resize((im.width * 2, im.height * 2), Image.NEAREST)
        cx, cy = (i % COLS) * CELL, (i // COLS) * (CELL + 14)
        sheet.alpha_composite(big, (cx + (CELL - big.width) // 2, cy + (CELL - big.height) // 2))
        label = f"{f.parent.name}/{f.stem} {im.width}px"
        d.text((cx + 4, cy + CELL - 2), label, fill=(193, 217, 242, 255))
    out = outdir / "qc_sheet.png"
    sheet.convert("RGB").save(out)
    print(f"wrote {out}")
    if bad:
        print("OFF-PALETTE:", bad)
        sys.exit(1)
    print("all sprites palette-clean")


if __name__ == "__main__":
    main()

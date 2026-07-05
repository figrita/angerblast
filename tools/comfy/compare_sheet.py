#!/usr/bin/env python3
"""One row per sprite, one column per run (base file, then _2.._8 suffix variants),
nearest upscale, so you can eyeball all seeds of each sprite side by side.
Usage: compare_sheet.py [out.png]"""

import sys
from pathlib import Path
from PIL import Image, ImageDraw

REPO = Path(__file__).resolve().parent.parent.parent
SPRITES = REPO / "assets" / "sprites"
SUFFIXES = [""] + [f"_{i}" for i in range(2, 9)]
BG = (18, 23, 61, 255)
CELL = 148
LABEL_W = 240


def main():
    out = Path(sys.argv[1]) if len(sys.argv) > 1 else Path("compare_sheet.png")
    rels = sorted(
        p.relative_to(SPRITES) for p in SPRITES.rglob("*.png")
        if not p.stem.rsplit("_", 1)[-1].isdigit()  # base sprites only
    )
    sheet = Image.new("RGBA", (LABEL_W + CELL * len(SUFFIXES), (len(rels) + 1) * CELL), BG)
    d = ImageDraw.Draw(sheet)
    for c, suf in enumerate(SUFFIXES):
        d.text((LABEL_W + c * CELL + CELL // 2 - 14, 8), suf or "base", fill=(255, 224, 145, 255))
    for r, rel in enumerate(rels):
        y = (r + 1) * CELL
        d.text((8, y + CELL // 2 - 6), str(rel), fill=(193, 217, 242, 255))
        for c, suf in enumerate(SUFFIXES):
            f = (SPRITES / rel).with_name(rel.stem + suf + ".png")
            if not f.exists():
                continue
            im = Image.open(f).convert("RGBA")
            k = max(1, min(4, (CELL - 8) // im.width))
            big = im.resize((im.width * k, im.height * k), Image.NEAREST)
            sheet.alpha_composite(
                big,
                (LABEL_W + c * CELL + (CELL - big.width) // 2, y + (CELL - big.height) // 2),
            )
    sheet.convert("RGB").save(out)
    print(f"wrote {out} ({sheet.width}x{sheet.height})")


if __name__ == "__main__":
    main()

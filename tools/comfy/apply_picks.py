#!/usr/bin/env python3
"""Apply picks pasted from the sprite-picker page.

Reads lines like `02_ember/charhulk.png = 5` (or `= base`) from stdin or a file,
promotes each chosen variant over the base file, and (with --prune) deletes the
remaining _2.._8 variants.

Usage: apply_picks.py [picks.txt] [--prune]
"""

import shutil
import sys
from pathlib import Path

REPO = Path(__file__).resolve().parent.parent.parent
SPRITES = REPO / "assets" / "sprites"


def main():
    args = [a for a in sys.argv[1:] if a != "--prune"]
    prune = "--prune" in sys.argv
    text = Path(args[0]).read_text() if args else sys.stdin.read()
    n = 0
    for line in text.splitlines():
        line = line.strip()
        if not line or "=" not in line:
            continue
        rel, _, choice = (x.strip() for x in line.partition("="))
        base = SPRITES / rel
        if not base.exists():
            print(f"skip (no such sprite): {rel}", file=sys.stderr)
            continue
        if choice not in ("base", ""):
            variant = base.with_name(f"{base.stem}_{choice}.png")
            if not variant.exists():
                print(f"skip (missing variant _{choice}): {rel}", file=sys.stderr)
                continue
            shutil.copy2(variant, base)
            n += 1
    pruned = 0
    if prune:
        for i in range(2, 9):
            for pat in (f"*_{i}.png", f"*_{i}o.png"):
                for v in SPRITES.rglob(pat):
                    v.unlink()
                    pruned += 1
    print(f"applied {n} picks" + (f", pruned {pruned} variant files" if prune else ""))


if __name__ == "__main__":
    main()

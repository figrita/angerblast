#!/usr/bin/env python3
"""Generate the device button-prompt icons (assets/sprites/ui/): keyboard
keycaps, PlayStation buttons and Xbox buttons, drawn deterministically in BLK
NX64 palette colors with a 3x5 pixel font — no diffusion (glyphs must be
exact). The game shows the set matching whatever device was touched last
(render.c draw_hint / input_prompt_device).

All icons are 18px tall (inline with 18-20px hint text; integer-scaled 2x for
bigger text). Rerun after palette or roster changes: gen_button_icons.py
"""

import tomllib
from pathlib import Path

from PIL import Image

REPO = Path(__file__).resolve().parent.parent
OUT = REPO / "assets" / "sprites" / "ui"
H = 18
C = (H - 1) / 2.0    # pixel-grid center of an HxH icon

PALETTE = {n: tuple(int(c[i:i+2], 16) for i in (0, 2, 4)) + (255,)
           for n, c in zip(*(lambda p: (p["names"], p["colors"]))(
               tomllib.load(open(REPO / "assets" / "palette.toml", "rb"))))}

FONT = {  # 3x5 pixel glyphs
    "A": ["010","101","111","101","101"], "B": ["110","101","110","101","110"],
    "C": ["011","100","100","100","011"], "D": ["110","101","101","101","110"],
    "E": ["111","100","110","100","111"], "F": ["111","100","110","100","100"],
    "H": ["101","101","111","101","101"], "I": ["111","010","010","010","111"],
    "P": ["110","101","110","100","100"], "R": ["110","101","110","101","101"],
    "S": ["011","100","010","001","110"], "T": ["111","010","010","010","010"],
    "W": ["101","101","101","111","101"], "X": ["101","101","010","101","101"],
    "Y": ["101","101","010","010","010"], "1": ["010","110","010","010","111"],
}


def text_px(img, text, x, y, color):
    for ch in text:
        for ry, row in enumerate(FONT[ch]):
            for rx, bit in enumerate(row):
                if bit == "1":
                    img.putpixel((x + rx, y + ry), color)
        x += 4


def blank(w):
    return Image.new("RGBA", (w, H), (0, 0, 0, 0))


def rounded_box(img, x0, y0, x1, y1, r, face, rim):
    """Filled rounded rect [x0..x1]x[y0..y1] inclusive: face fill + 1px rim."""
    def inside(x, y, shrink):
        cx = min(max(x, x0 + r), x1 - r)
        cy = min(max(y, y0 + r), y1 - r)
        return (x - cx) ** 2 + (y - cy) ** 2 <= (r - shrink + 0.4) ** 2
    for x in range(x0, x1 + 1):
        for y in range(y0, y1 + 1):
            if inside(x, y, 0):
                img.putpixel((x, y), rim if not inside(x, y, 1.2) else face)


def keycap(text):
    """Keycap: rounded ink face, lilac rim, grape 'depth' bottom rows, gold legend."""
    tw = len(text) * 4 - 1
    w = max(H, tw + 8)
    img = blank(w)
    rounded_box(img, 0, 0, w - 1, H - 1, 4, PALETTE["ink"], PALETTE["lilac"])
    for x in range(2, w - 2):          # bottom depth edge (inside the rim)
        for y in (H - 4, H - 3):
            img.putpixel((x, y), PALETTE["grape"])
    text_px(img, text, (w - tw) // 2, (H - 5) // 2 - 1, PALETTE["gold"])
    return img


def circle_btn(draw_glyph):
    """Round pad button: ink disc, bright lilac rim."""
    img = blank(H)
    for x in range(H):
        for y in range(H):
            d2 = (x - C) ** 2 + (y - C) ** 2
            if d2 <= 6.9 ** 2: img.putpixel((x, y), PALETTE["ink"])
            elif d2 <= 8.4 ** 2: img.putpixel((x, y), PALETTE["lilac"])
    draw_glyph(img)
    return img


def pill(text=None, lines=False):
    """Shoulder / menu button: rounded pill."""
    w = (len(text) * 4 + 9) if text else 22
    img = blank(w)
    rounded_box(img, 0, 3, w - 1, H - 4, 4, PALETTE["ink"], PALETTE["lilac"])
    if text:
        text_px(img, text, (w - (len(text) * 4 - 1)) // 2, (H - 5) // 2, PALETTE["white"])
    if lines:
        for y in (7, 9, 11):
            for x in range(6, w - 6):
                img.putpixel((x, y), PALETTE["white"])
    return img


def ps_glyph(kind):
    def g(img):
        col = PALETTE[{"cross": "palecyan", "triangle": "mint", "square": "lavender"}[kind]]
        if kind == "cross":
            for i in range(7):          # diagonals meet exactly at the disc center
                img.putpixel((5 + i, 5 + i), col)
                img.putpixel((11 - i, 5 + i), col)
        elif kind == "triangle":
            # apex at the top-center pair, edges interpolated down to the base row
            # so the sides CONNECT to the base corners
            top, bot, halfw = 4, 12, 4.5
            for y in range(top, bot + 1):
                u = (y - top) / (bot - top)
                lx = int(round(8.5 - halfw * u - 0.5))
                rx = int(round(8.5 + halfw * u + 0.5)) - 1
                if y == bot:
                    for x in range(lx, rx + 1):
                        img.putpixel((x, y), col)
                else:
                    img.putpixel((lx, y), col)
                    img.putpixel((rx, y), col)
        else:  # square: 8x8 outline centered on the disc
            for x in range(5, 13):
                img.putpixel((x, 5), col); img.putpixel((x, 12), col)
            for y in range(5, 13):
                img.putpixel((5, y), col); img.putpixel((12, y), col)
    return g


def xb_letter(letter, colname):
    def g(img):   # 2x-scaled glyph (6x10) sits dead-center in the 18px disc
        for ry, row in enumerate(FONT[letter]):
            for rx, bit in enumerate(row):
                if bit == "1":
                    for dx in (0, 1):
                        for dy in (0, 1):
                            img.putpixel((6 + rx*2 + dx, 4 + ry*2 + dy), PALETTE[colname])
    return g


def dpad():
    """Plus shape, fully traced: rim = every silhouette pixel touching outside."""
    img = blank(H)
    a, b = 6, 12
    mask = [[(a <= x < b or a <= y < b) and 1 <= x < H - 1 and 1 <= y < H - 1
             for y in range(H)] for x in range(H)]
    for x in range(H):
        for y in range(H):
            if not mask[x][y]:
                continue
            edge = False
            for dx, dy in ((1, 0), (-1, 0), (0, 1), (0, -1)):
                nx, ny = x + dx, y + dy
                if nx < 0 or ny < 0 or nx >= H or ny >= H or not mask[nx][ny]:
                    edge = True
            img.putpixel((x, y), PALETTE["lilac"] if edge else PALETTE["ink"])
    for x, y in [(8, 8), (9, 8), (8, 9), (9, 9)]:
        img.putpixel((x, y), PALETTE["gold"])
    return img


def stick():
    img = blank(H)
    for x in range(H):
        for y in range(H):
            d2 = (x - C) ** 2 + (y - C) ** 2
            if d2 <= 4.4 ** 2: img.putpixel((x, y), PALETTE["ice"])
            elif d2 <= 6.0 ** 2: img.putpixel((x, y), PALETTE["ink"])
            elif d2 <= 7.2 ** 2: img.putpixel((x, y), PALETTE["lilac"])
    for x, y in [(8, 0), (9, 0), (8, 17), (9, 17), (0, 8), (0, 9), (17, 8), (17, 9)]:
        img.putpixel((x, y), PALETTE["gold"])   # nudge arrows: it moves
    return img


def arrows():
    img = blank(24)
    col = PALETTE["gold"]
    for i in range(5):                       # left + right triangles, tips meeting mid-height
        for y in range(9 - i, 9 + i + 1):
            img.putpixel((3 + i, y), col)
            img.putpixel((20 - i, y), col)
    return img


ICONS = {
    "kb_wasd":   lambda: keycap("WASD"),
    "kb_space":  lambda: keycap("SPACE"),
    "kb_shift":  lambda: keycap("SHIFT"),
    "kb_t":      lambda: keycap("T"),
    "kb_c":      lambda: keycap("C"),
    "kb_esc":    lambda: keycap("ESC"),
    "kb_arrows": arrows,
    "ps_cross":    lambda: circle_btn(ps_glyph("cross")),
    "ps_triangle": lambda: circle_btn(ps_glyph("triangle")),
    "ps_square":   lambda: circle_btn(ps_glyph("square")),
    "ps_r1":       lambda: pill("R1"),
    "ps_options":  lambda: pill(lines=True),
    "ps_dpad":     dpad,
    "xb_a":    lambda: circle_btn(xb_letter("A", "mint")),
    "xb_y":    lambda: circle_btn(xb_letter("Y", "gold")),
    "xb_x":    lambda: circle_btn(xb_letter("X", "palecyan")),
    "xb_rb":   lambda: pill("RB"),
    "xb_menu": lambda: pill(lines=True),
    "xb_dpad": dpad,
    "stick":   stick,
}


def main():
    OUT.mkdir(parents=True, exist_ok=True)
    for name, fn in ICONS.items():
        p = OUT / f"{name}.png"
        fn().save(p)
        print(f"wrote {p}")


if __name__ == "__main__":
    main()

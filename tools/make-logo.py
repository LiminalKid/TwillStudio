#!/usr/bin/env python3
"""Draws the Twill Studio logo: a swatch of 2/2 twill (five threads each way: cream warp; orange and
green weft, each weft going over two warps and under two, a warp further along on each row, so the
floats line up in diagonal ribs) above a pixel "TWILL" with an orange thread stitched through it
and a finer pixel "STUDIO". Everything sits on a pixel grid; colours are the app's (src/app/Theme.h).

Writes:
  src/app/gen/logo.cpp                    the logo as palette indices, for the launch splash
  packaging/portmaster/twill/cover.png    PortMaster's cover: the splash at 640x480

Needs Pillow.

    python3 tools/make-logo.py
"""
import math
import os

from PIL import Image

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))

# Palette indices (0 is transparent). Keep in step with kLogoPalette in src/app/Splash.cpp.
BG, PAPER, ORANGE, GREEN = 1, 2, 3, 4
COLOURS = {BG: 0x1B1B1D, PAPER: 0xF5EFE1, ORANGE: 0xE58A55, GREEN: 0x7DAE88}
TEXTURE = 0x202023  # the cloth texture's second shade, behind the logo

# The swatch, in cells (one cell = 2 logo pixels).
THREAD, GAP, THREADS, FRINGE = 3, 1, 5, 5
WARP = [PAPER] * THREADS
WEFT = [ORANGE, ORANGE, GREEN, GREEN, ORANGE]

# "TWILL", in cells: 9 rows, 3-cell strokes.
TWILL = [
    "ccccccc.ccc....ccc..ccc.ccc......ccc....",
    "ccccccc.ccc....ccc..ccc.ccc......ccc....",
    "..ccc...ccc....ccc..ccc.ccc......ccc....",
    "..ccc...ccc.cc.ccc..ccc.ccc......ccc....",
    "..ccc...ccc.cc.ccc..ccc.ccc......ccc....",
    "..ccc...ccc.cc.ccc..ccc.ccc......ccc....",
    "..ccc...ccc.cc.ccc..ccc.ccc......ccc....",
    "..ccc...cccccccccc..ccc.cccccccc.ccccccc",
    "..ccc....cccc.ccc...ccc.cccccccc.ccccccc",
]
# "STUDIO", in logo pixels (half a cell): 7 rows, letters 3 apart.
SMALL = {
    "S": [".####", "#....", "#....", ".###.", "....#", "....#", "####."],
    "T": ["#####", "..#..", "..#..", "..#..", "..#..", "..#..", "..#.."],
    "U": ["#...#", "#...#", "#...#", "#...#", "#...#", "#...#", ".###."],
    "D": ["####.", "#...#", "#...#", "#...#", "#...#", "#...#", "####."],
    "I": ["###", ".#.", ".#.", ".#.", ".#.", ".#.", "###"],
    "O": [".###.", "#...#", "#...#", "#...#", "#...#", "#...#", ".###."],
}


def weft_over(i, j):
    """Does weft row j pass over warp i? Over two, under two, one further along each row."""
    return (i - j) % 4 < 2


def swatch():
    """The swatch in cells, as a list of rows of palette indices."""
    pitch = THREAD + GAP
    size = THREADS * pitch - GAP
    n = size + 2 * FRINGE
    g = [[0] * n for _ in range(n)]
    for y in range(size):
        for x in range(size):
            i, xi = divmod(x, pitch)
            j, yj = divmod(y, pitch)
            col = BG
            if xi < THREAD and yj < THREAD:  # a crossing: whichever thread is on top
                col = WEFT[j] if weft_over(i, j) else WARP[i]
            elif yj < THREAD:  # between two warps on a weft row: the weft bridges a float
                if i + 1 < THREADS and weft_over(i, j) and weft_over(i + 1, j):
                    col = WEFT[j]
            elif xi < THREAD:  # between two wefts on a warp: the warp bridges a float
                if j + 1 < THREADS and not weft_over(i, j) and not weft_over(i, j + 1):
                    col = WARP[i]
            g[y + FRINGE][x + FRINGE] = col
    # The loose ends, alternately long and short.
    for i in range(THREADS):
        for f in range(FRINGE - (2 if i % 2 else 0)):
            for k in range(THREAD):
                g[FRINGE - 1 - f][FRINGE + i * pitch + k] = WARP[i]
                g[FRINGE + size + f][FRINGE + i * pitch + k] = WARP[i]
    for j in range(THREADS):
        for f in range(FRINGE - (2 if j % 2 == 0 else 0)):
            for k in range(THREAD):
                g[FRINGE + j * pitch + k][FRINGE - 1 - f] = WEFT[j]
                g[FRINGE + j * pitch + k][FRINGE + size + f] = WEFT[j]
    return g


def logo():
    """The whole logo in logo pixels, as rows of palette indices."""
    sw = swatch()
    sw_w, word_w = len(sw[0]), len(TWILL[0])
    small_w = sum(len(SMALL[ch][0]) for ch in "STUDIO") + 3 * 5
    W = 2 * max(sw_w, word_w) + 4
    word_y = 2 * len(sw) + 14
    small_y = word_y + 2 * len(TWILL) + 5
    H = small_y + 7 + 2
    img = [[0] * W for _ in range(H)]

    def put(x, y, c):
        if 0 <= x < W and 0 <= y < H:
            img[y][x] = c

    sx = (W - 2 * sw_w) // 2
    for y, row in enumerate(sw):
        for x, c in enumerate(row):
            if c:
                for a in range(2):
                    for b in range(2):
                        put(sx + 2 * x + a, 2 * y + b, c)
    wx = (W - 2 * word_w) // 2
    for y, row in enumerate(TWILL):
        for x, v in enumerate(row):
            if v == "c":
                for a in range(2):
                    for b in range(2):
                        put(wx + 2 * x + a, word_y + 2 * y + b, PAPER)
    # The thread: a thin stitch with a slight sag, from below the T up past the W, over the letters.
    x0, y0 = wx + 14, word_y + 2 * len(TWILL) + 3
    x1, y1 = wx + 36, word_y - 5
    n = max(abs(x1 - x0), abs(y1 - y0))
    for k in range(n + 1):
        x = round(x0 + (x1 - x0) * k / n)
        y = round(y0 + (y1 - y0) * k / n + 1.2 * math.sin(k / n * math.pi))
        put(x, y, ORANGE)
        put(x + 1, y, ORANGE)
    x = (W - small_w) // 2
    for ch in "STUDIO":
        for y, row in enumerate(SMALL[ch]):
            for xx, v in enumerate(row):
                if v == "#":
                    put(x + xx, small_y + y, PAPER)
        x += len(SMALL[ch][0]) + 3
    return img


def splash(w, h, img):
    """The splash as the app draws it (src/app/Splash.cpp): the logo on a faint twill."""
    lh, lw = len(img), len(img[0])
    scale = max(1, min(int(w * 0.6) // lw, int(h * 0.75) // lh))
    cell = max(2, scale)
    out = Image.new("RGB", (w, h))
    px = out.load()
    bg, tex = COLOURS[BG], TEXTURE
    for y in range(h):
        for x in range(w):
            c = tex if ((x // cell) - (y // cell)) % 4 < 2 else bg
            px[x, y] = ((c >> 16) & 255, (c >> 8) & 255, c & 255)
    ox, oy = (w - lw * scale) // 2, (h - lh * scale) // 2
    for y in range(lh):
        for x in range(lw):
            c = img[y][x]
            if c:
                rgb = COLOURS[c]
                for b in range(scale):
                    for a in range(scale):
                        px[ox + x * scale + a, oy + y * scale + b] = ((rgb >> 16) & 255, (rgb >> 8) & 255, rgb & 255)
    return out


def main():
    img = logo()
    h, w = len(img), len(img[0])
    data = bytes(c for row in img for c in row)
    lines = []
    for i in range(0, len(data), 32):
        lines.append("    " + ", ".join(str(b) for b in data[i:i + 32]) + ",")
    src = [
        "// Generated by tools/make-logo.py. Do not edit.",
        "#include <cstdint>",
        "",
        "namespace twill {",
        "",
        "extern const int kLogoWidth = %d, kLogoHeight = %d;" % (w, h),
        "// Palette indices: 0 transparent, 1 graphite, 2 paper, 3 orange, 4 green.",
        "extern const uint8_t kLogoPixels[] = {",
        *lines,
        "};",
        "",
        "}  // namespace twill",
        "",
    ]
    out = os.path.join(ROOT, "src", "app", "gen", "logo.cpp")
    os.makedirs(os.path.dirname(out), exist_ok=True)
    with open(out, "w") as f:
        f.write("\n".join(src))
    splash(640, 480, img).save(os.path.join(ROOT, "packaging", "portmaster", "twill", "cover.png"), optimize=True)
    print("wrote %s (%dx%d) and cover.png" % (out, w, h))


if __name__ == "__main__":
    main()

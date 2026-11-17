#!/usr/bin/env python3
"""Draws the Twill Studio logo: a swatch of 2/2 twill (five threads each way: cream warp; orange and
green weft, each weft going over two warps and under two, a warp further along on each row, so the
floats line up in diagonal ribs) above a pixel "TWILL" with an orange thread stitched through it
and a finer pixel "STUDIO". Everything sits on a pixel grid; colours are the app's (src/app/Theme.h).

Writes:
  src/app/gen/logo.cpp                    the logo as palette indices, for the launch splash
  packaging/portmaster/twill/cover.png    PortMaster's cover: the splash at 640x480
  site/logo.js                            the logo's pixels, for the website's weaving animation
  site/favicon.svg, favicon.png,          the website's icon: the swatch alone (the SVG's pale
    apple-touch-icon.png                  threads turn to ink where the browser is light)
  Docs/images/logo/                       the logo for use elsewhere, for dark and light
                                          backgrounds: on the cloth texture, on a plain ground,
                                          and transparent (cropped, at 4x, 8x and 16x)

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
# The light version, for paper: the cream threads and lettering become ink.
LIGHT = {BG: 0xF5EFE1, PAPER: 0x151412, ORANGE: 0xE58A55, GREEN: 0x7DAE88}
LIGHT_TEXTURE = 0xEFE8D7
# Sizes for Docs/images/logo/: name, width, height, and how much of the image the logo may fill.
SIZES = [("square-512", 512, 512, 0.75), ("square-1024", 1024, 1024, 0.75),
         ("wide-1920x1080", 1920, 1080, 0.75), ("social-1280x640", 1280, 640, 0.8)]

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


def rgb(c):
    return ((c >> 16) & 255, (c >> 8) & 255, c & 255)


def render(w, h, img, scale, colours=COLOURS, ground="texture"):
    """The logo at an integer scale, centred. ground: "texture" (the ground colour with a faint
    twill), "plain", or "none" (transparent, the swatch's gaps too)."""
    lh, lw = len(img), len(img[0])
    texture = TEXTURE if colours is COLOURS else LIGHT_TEXTURE
    if ground == "none":
        out = Image.new("RGBA", (w, h), (0, 0, 0, 0))
    else:
        out = Image.new("RGB", (w, h), rgb(colours[BG]))
    px = out.load()
    if ground == "texture":
        cell = max(2, scale)
        for y in range(h):
            for x in range(w):
                if ((x // cell) - (y // cell)) % 4 < 2:
                    px[x, y] = rgb(texture)
    ox, oy = (w - lw * scale) // 2, (h - lh * scale) // 2
    for y in range(lh):
        for x in range(lw):
            c = img[y][x]
            if not c or (ground == "none" and c == BG):
                continue
            col = rgb(colours[c]) + ((255,) if ground == "none" else ())
            for b in range(scale):
                for a in range(scale):
                    px[ox + x * scale + a, oy + y * scale + b] = col
    return out


def fit(w, h, img, fill):
    return max(1, min(int(w * fill) // len(img[0]), int(h * fill) // len(img)))


def splash(w, h, img):
    """The splash as the app draws it (src/app/Splash.cpp): the logo on a faint twill."""
    lh, lw = len(img), len(img[0])
    return render(w, h, img, max(1, min(int(w * 0.6) // lw, int(h * 0.75) // lh)))


def write_images(img, out):
    """The logo for use elsewhere: cream for dark backgrounds, ink for light ones."""
    os.makedirs(out, exist_ok=True)
    lh, lw = len(img), len(img[0])
    for theme, colours in (("dark", COLOURS), ("light", LIGHT)):
        for name, w, h, fill in SIZES:
            s = fit(w, h, img, fill)
            for ground, suffix in (("texture", ""), ("plain", "-plain")):
                path = os.path.join(out, "twill-logo-%s-%s%s.png" % (theme, name, suffix))
                render(w, h, img, s, colours, ground).save(path, optimize=True)
        for s in (4, 8, 16):
            path = os.path.join(out, "twill-logo-%s-transparent-%dx.png" % (theme, s))
            render(lw * s, lh * s, img, s, colours, "none").save(path, optimize=True)


def write_icons(out):
    """The website's icons: the swatch alone, one square per cell."""
    sw = swatch()
    n = len(sw)
    runs = {c: [] for c in (PAPER, ORANGE, GREEN)}
    for y, row in enumerate(sw):
        x = 0
        while x < n:
            c = row[x]
            if c in runs:
                x1 = x
                while x1 < n and row[x1] == c:
                    x1 += 1
                runs[c].append("M%d %dh%dv1h-%dz" % (x, y, x1 - x, x1 - x))
                x = x1
            else:
                x += 1
    svg = ('<svg xmlns="http://www.w3.org/2000/svg" viewBox="0 0 %d %d" shape-rendering="crispEdges">'
           '<style>.p{fill:#151412}@media (prefers-color-scheme:dark){.p{fill:#F5EFE1}}</style>'
           '<path class="p" d="%s"/><path fill="#E58A55" d="%s"/><path fill="#7DAE88" d="%s"/></svg>\n'
           % (n, n, "".join(runs[PAPER]), "".join(runs[ORANGE]), "".join(runs[GREEN])))
    with open(os.path.join(out, "favicon.svg"), "w") as f:
        f.write(svg)
    def png(size, cell, ground, path):
        img = Image.new("RGBA", (size, size), ground)
        px = img.load()
        o = (size - n * cell) // 2
        cols = {PAPER: 0x151412, ORANGE: COLOURS[ORANGE], GREEN: COLOURS[GREEN]}
        for y, row in enumerate(sw):
            for x, c in enumerate(row):
                if c in cols:
                    for b in range(cell):
                        for a in range(cell):
                            px[o + x * cell + a, o + y * cell + b] = rgb(cols[c]) + (255,)
        img.save(path, optimize=True)
    png(64, 2, (0, 0, 0, 0), os.path.join(out, "favicon.png"))
    png(180, 5, rgb(LIGHT[BG]) + (255,), os.path.join(out, "apple-touch-icon.png"))


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
    write_images(img, os.path.join(ROOT, "Docs", "images", "logo"))
    write_icons(os.path.join(ROOT, "site"))
    rows = ",\n  ".join('"%s"' % "".join(str(c) for c in row) for row in img)
    with open(os.path.join(ROOT, "site", "logo.js"), "w") as f:
        f.write("// Generated by tools/make-logo.py. Do not edit.\n"
                "// The logo, a row of palette indices per string: 0 none, 1 ground, 2 paper (ink on the\n"
                "// light theme), 3 orange, 4 green.\n"
                "window.TWILL_LOGO = [\n  %s\n];\n" % rows)
    print("wrote %s (%dx%d), cover.png and Docs/images/logo/" % (out, w, h))


if __name__ == "__main__":
    main()

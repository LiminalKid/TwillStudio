#!/usr/bin/env python3
"""Draws the Twill Studio wordmark (Docs/images/wordmark-{light,dark}.svg): a large "Twill" with a
small "studio" after it and the dot of the i in the app's blue, in the app's font (Inter) turned into outlines so no font is needed to show
it. Colours are the app's theme colours. Needs fontTools (pip install fonttools).

    python3 tools/make-wordmark.py
"""
import os
from fontTools.ttLib import TTFont
from fontTools.pens.svgPathPen import SVGPathPen
from fontTools.pens.transformPen import TransformPen
from fontTools.pens.boundsPen import BoundsPen
from fontTools.pens.recordingPen import RecordingPen, DecomposingRecordingPen

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
FONTS = os.path.join(ROOT, "third_party", "fonts", "inter")
OUT = os.path.join(ROOT, "Docs", "images")

THEMES = {  # text, secondary text, note (the accent): src/app/Theme.h
    "light": ("#23221F", "#77746D", "#2F80ED"),
    "dark": ("#ECEBE7", "#9B9993", "#4C8FF0"),
}

BIG = 120          # "Twill" size, px
SMALL = 0.30       # "studio" size, relative to BIG
TRACK_BIG = -0.02  # letter spacing, em
TRACK_SMALL = 0.06
GAP = 0.10         # between the words, em of BIG
PAD = 8


def contours(glyph, glyphset):
    """Splits a glyph's outline into its contours (each a RecordingPen value)."""
    rec = DecomposingRecordingPen(glyphset)  # "i" is built from a stem and a dot
    glyph.draw(rec)
    out, cur = [], []
    for op in rec.value:
        cur.append(op)
        if op[0] in ("closePath", "endPath"):
            out.append(cur)
            cur = []
    return out


def word(path, text, size, track, x, baseline, dot=False):
    """Sets text at (x, baseline). Returns (path data, dot path data, advance width, ink bounds);
    with dot, the dot of each i goes in the second path instead of the first."""
    font = TTFont(path)
    cmap, gs, hmtx = font.getBestCmap(), font.getGlyphSet(), font["hmtx"]
    upm = font["head"].unitsPerEm
    xheight = font["OS/2"].sxHeight
    scale = size / upm
    pen, dot_pen, bounds = SVGPathPen(gs), SVGPathPen(gs), BoundsPen(gs)
    pos = 0.0
    for ch in text:
        g = cmap[ord(ch)]
        t = (scale, 0, 0, -scale, x + pos * scale, baseline)
        gs[g].draw(TransformPen(bounds, t))
        for c in contours(gs[g], gs):
            above = min(pt[1] for _, args in c for pt in args) > xheight
            target = dot_pen if dot and ch == "i" and above else pen
            rec = RecordingPen()
            rec.value = c
            rec.replay(TransformPen(target, t))
        pos += hmtx[g][0] + track * upm
    return pen.getCommands(), dot_pen.getCommands(), (pos - track * upm) * scale, bounds.bounds


def main():
    os.makedirs(OUT, exist_ok=True)
    base = BIG
    big, dot, big_w, (left, top, _, bottom) = word(os.path.join(FONTS, "Inter-SemiBold.ttf"), "Twill",
                                                   BIG, TRACK_BIG, 0, base, dot=True)
    sx = big_w + GAP * BIG
    small, _, _, (_, _, right, _) = word(os.path.join(FONTS, "Inter-Regular.ttf"), "studio",
                                         BIG * SMALL, TRACK_SMALL, sx, base)
    x0, y0 = left - PAD, top - PAD
    width, height = right + PAD - x0, bottom + PAD - y0
    for name, (text, secondary, note) in THEMES.items():
        svg = (f'<svg xmlns="http://www.w3.org/2000/svg" viewBox="{x0:.1f} {y0:.1f} {width:.1f} {height:.1f}" '
               f'width="{width:.0f}" height="{height:.0f}" role="img" aria-label="Twill Studio">\n'
               f'<path fill="{text}" d="{big}"/>\n'
               f'<path fill="{note}" d="{dot}"/>\n'
               f'<path fill="{secondary}" d="{small}"/>\n</svg>\n')
        with open(os.path.join(OUT, f"wordmark-{name}.svg"), "w") as f:
            f.write(svg)
    print(f"{OUT}/wordmark-{{light,dark}}.svg: {width:.0f} x {height:.0f}")


if __name__ == "__main__":
    main()

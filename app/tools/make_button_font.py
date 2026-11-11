#!/usr/bin/env python3
"""Builds app/resources/font/icon.ttf: PlayStation Vita button glyphs at the
private-use code points borealis uses for its button hints.

borealis draws hints with U+E0E0 (A), U+E0E1 (B), ... On the Vita it maps
A=Cross, B=Circle, X=Square, Y=Triangle, LB=L, RB=R, START, BACK=SELECT.
Without this font those code points fall back to unrelated icons.

All shapes are drawn here; letters are taken from M PLUS Rounded 1c (OFL),
so the output is covered by the same OFL as app/resources/font/font.ttf.

    pip install fonttools
    python3 app/tools/make_button_font.py
"""
import math
import os

from fontTools.fontBuilder import FontBuilder
from fontTools.pens.reverseContourPen import ReverseContourPen
from fontTools.pens.transformPen import TransformPen
from fontTools.pens.ttGlyphPen import TTGlyphPen
from fontTools.ttLib import TTFont

HERE = os.path.dirname(os.path.abspath(__file__))
TEXT_FONT = os.path.join(HERE, "..", "resources", "font", "font.ttf")
OUT = os.path.join(HERE, "..", "resources", "font", "icon.ttf")

UPM = 1000
CY = 340      # vertical center, roughly the middle of lowercase text
R = 430       # badge radius

text_font = TTFont(TEXT_FONT)
text_glyphs = text_font.getGlyphSet()
text_cmap = text_font.getBestCmap()


def polygon(pen, pts):
    pen.moveTo(pts[0])
    for p in pts[1:]:
        pen.lineTo(p)
    pen.closePath()


def circle(pen, cx, cy, r, n=64):
    polygon(pen, [(round(cx + r * math.cos(2 * math.pi * i / n)),
                   round(cy + r * math.sin(2 * math.pi * i / n))) for i in range(n)])


def rounded_rect(pen, x0, y0, x1, y1, r, n=10):
    pts = []
    for cx, cy, a0 in ((x1 - r, y0 + r, -90), (x1 - r, y1 - r, 0), (x0 + r, y1 - r, 90), (x0 + r, y0 + r, 180)):
        for i in range(n + 1):
            a = math.radians(a0 + 90 * i / n)
            pts.append((round(cx + r * math.cos(a)), round(cy + r * math.sin(a))))
    polygon(pen, pts)


def text(pen, s, x_center, size):
    """Draws `s` from the text font, centered at (x_center, CY)."""
    scale = size / text_font["head"].unitsPerEm
    names = [text_cmap[ord(c)] for c in s]
    width = sum(text_glyphs[n].width for n in names) * scale
    x = x_center - width / 2
    cap = 700 * scale  # approximate cap height of the text font
    for n in names:
        text_glyphs[n].draw(TransformPen(pen, (scale, 0, 0, scale, x, CY - cap / 2)))
        x += text_glyphs[n].width * scale


def hole(pen):
    """Contours drawn through this pen are cut out of the badge."""
    return ReverseContourPen(pen)


def badge(draw_symbol):
    pen = TTGlyphPen(None)
    circle(pen, 500, CY, R)
    draw_symbol(pen)
    return pen.glyph(), 1000


def cross(pen):
    a, w = 210, 52  # arm half-length, half-thickness
    pts = []
    for k in range(4):
        ang = math.radians(45 + 90 * k)
        ux, uy = math.cos(ang), math.sin(ang)          # arm direction
        vx, vy = -uy, ux                               # perpendicular
        pts += [(500 + ux * a - vx * w, CY + uy * a - vy * w),
                (500 + ux * a + vx * w, CY + uy * a + vy * w)]
        nang = math.radians(45 + 90 * k + 45)          # inner corner between arms
        pts.append((500 + math.cos(nang) * w * math.sqrt(2), CY + math.sin(nang) * w * math.sqrt(2)))
    polygon(hole(pen), [(round(x), round(y)) for x, y in pts])


def ring(pen):
    circle(hole(pen), 500, CY, 220)
    circle(pen, 500, CY, 140)


def square(pen):
    polygon(hole(pen), [(310, CY - 190), (690, CY - 190), (690, CY + 190), (310, CY + 190)])
    polygon(pen, [(390, CY - 110), (390, CY + 110), (610, CY + 110), (610, CY - 110)])


def triangle(pen):
    o = [(500, CY + 230), (290, CY - 150), (710, CY - 150)]
    i = [(500, CY + 110), (605, CY - 85), (395, CY - 85)]
    polygon(hole(pen), o)
    polygon(pen, i[::-1])


def arrow(direction):
    def draw(pen):
        base = [(0, 190), (-170, -60), (-60, -60), (-60, -190), (60, -190), (60, -60), (170, -60)]
        a = {"up": 0, "left": 90, "down": 180, "right": 270}[direction]
        c, s = math.cos(math.radians(a)), math.sin(math.radians(a))
        polygon(hole(pen), [(round(500 + x * c - y * s), round(CY + x * s + y * c)) for x, y in base])
    return draw


def pill(label, width):
    pen = TTGlyphPen(None)
    rounded_rect(pen, 40, CY - 330, width - 40, CY + 330, 300)
    # Our shapes wind counter-clockwise; TrueType letters already wind
    # clockwise, so drawing them as-is cuts them out of the pill.
    text(pen, label, width / 2, 560 if len(label) == 1 else 420)
    return pen.glyph(), width


GLYPHS = {
    0xE0E0: ("cross", badge(cross)),
    0xE0E1: ("circle", badge(ring)),
    0xE0E2: ("square", badge(square)),
    0xE0E3: ("triangle", badge(triangle)),
    0xE0E4: ("l", pill("L", 1000)),
    0xE0E5: ("r", pill("R", 1000)),
    0xE0E6: ("l2", pill("L", 1000)),
    0xE0E7: ("r2", pill("R", 1000)),
    0xE0EB: ("up", badge(arrow("up"))),
    0xE0EC: ("down", badge(arrow("down"))),
    0xE0ED: ("left", badge(arrow("left"))),
    0xE0EE: ("right", badge(arrow("right"))),
    0xE0EF: ("start", pill("START", 2300)),
    0xE0F0: ("select", pill("SELECT", 2500)),
}


def main():
    order = [".notdef"] + [name for name, _ in GLYPHS.values()]
    fb = FontBuilder(UPM, isTTF=True)
    fb.setupGlyphOrder(order)
    fb.setupCharacterMap({cp: name for cp, (name, _) in GLYPHS.items()})
    glyphs = {".notdef": TTGlyphPen(None).glyph()}
    metrics = {".notdef": (500, 0)}
    for name, (glyph, width) in GLYPHS.values():
        glyphs[name] = glyph
        metrics[name] = (width, 0)
    fb.setupGlyf(glyphs)
    for name in order:
        glyph = fb.font["glyf"][name]
        glyph.recalcBounds(fb.font["glyf"])
        metrics[name] = (metrics[name][0], getattr(glyph, "xMin", 0))
    fb.setupHorizontalMetrics(metrics)
    fb.setupHorizontalHeader(ascent=880, descent=-120)
    fb.setupNameTable({
        "familyName": "MiiMakerVitaButtons",
        "styleName": "Regular",
        "copyright": "Copyright 2016 The Rounded M+ Project Authors (letter outlines); "
                     "button shapes by the mii-maker-vita contributors",
        "licenseDescription": "This Font Software is licensed under the SIL Open Font License, Version 1.1.",
    })
    fb.setupOS2(sTypoAscender=880, sTypoDescender=-120, usWinAscent=880, usWinDescent=120)
    fb.setupPost()
    fb.save(OUT)
    print("wrote", OUT)


if __name__ == "__main__":
    main()

#!/usr/bin/env python3
"""Builds app/resources/font/icon.ttf: PlayStation Vita button glyphs at the
private-use code points borealis uses for its button hints.

borealis draws hints with U+E0E0 (A), U+E0E1 (B), ... On the Vita it maps
A=Cross, B=Circle, X=Square, Y=Triangle, LB=L, RB=R, START, BACK=SELECT.
Without this font those code points fall back to unrelated icons.

The glyphs are PromptFont's, by Yukari Hafner (https://shinmera.com/projects/promptfont),
licensed under the SIL Open Font License 1.1: this script only copies them
into a small font, moves them to the code points above and enlarges the
small ones (see SCALE), so the output is
covered by the same licence (see app/resources/font/PromptFont-LICENSE.txt).

    pip install fonttools
    npm pack promptfont && tar xzf promptfont-*.tgz   # gives package/promptfont.ttf
    python3 app/tools/make_button_font.py package/promptfont.ttf
"""
import os
import sys

from fontTools import subset
from fontTools.pens.transformPen import TransformPen
from fontTools.pens.ttGlyphPen import TTGlyphPen
from fontTools.ttLib import TTFont

HERE = os.path.dirname(os.path.abspath(__file__))
OUT = os.path.join(HERE, "..", "resources", "font", "icon.ttf")

# borealis code point -> PromptFont code point
REMAP = {
    0xE0E0: 0x21E3,  # cross
    0xE0E1: 0x21E2,  # circle
    0xE0E2: 0x21E0,  # square
    0xE0E3: 0x21E1,  # triangle
    0xE0E4: 0x219C,  # L
    0xE0E5: 0x219D,  # R
    0xE0E6: 0x219C,  # L (the Vita has no L2)
    0xE0E7: 0x219D,  # R (the Vita has no R2)
    0xE0EB: 0x219F,  # d-pad up
    0xE0EC: 0x21A1,  # d-pad down
    0xE0ED: 0x219E,  # d-pad left
    0xE0EE: 0x21A0,  # d-pad right
    0xE0EF: 0x21F8,  # START
    0xE0F0: 0x21F7,  # SELECT
    0xE105: 0x21CC,  # right stick (RSB; the editor's "Rotate" hint)
}

# PromptFont's shoulder buttons are drawn small; at hint size they need to
# be larger to read. Glyphs are scaled about their middle (y = 290).
SCALE = {0xE105: 1.5, 0xE0E0: 1.12, 0xE0E1: 1.12, 0xE0E2: 1.12, 0xE0E3: 1.12, 0xE0E4: 1.6, 0xE0E5: 1.6}
MID_Y = 290


def scale_glyph(font, name, factor):
    glyf, hmtx = font["glyf"], font["hmtx"]
    advance, _ = hmtx[name]
    new_advance = round(advance * factor)
    dx = new_advance / 2 - factor * advance / 2
    dy = MID_Y * (1 - factor)
    pen = TTGlyphPen(glyf)
    glyf[name].draw(TransformPen(pen, (factor, 0, 0, factor, dx, dy)), glyf)
    glyph = pen.glyph()
    glyph.recalcBounds(glyf)
    glyf[name] = glyph
    hmtx[name] = (new_advance, getattr(glyph, "xMin", 0))


def main():
    if len(sys.argv) != 2:
        sys.exit(__doc__)
    font = TTFont(sys.argv[1])

    opts = subset.Options()
    opts.name_IDs = ["*"]
    opts.notdef_outline = True
    opts.layout_features = []
    sub = subset.Subsetter(opts)
    sub.populate(unicodes=sorted(set(REMAP.values())))
    sub.subset(font)

    cmap = font.getBestCmap()
    names = {dst: cmap[src] for dst, src in REMAP.items()}
    for cp, factor in SCALE.items():
        scale_glyph(font, names[cp], factor)
    for table in font["cmap"].tables:
        if table.isUnicode():
            table.cmap = dict(names)

    # A modified version: give it its own name (the OFL asks derivatives
    # not to pass themselves off as the original).
    name = font["name"]
    for rec in list(name.names):
        if rec.nameID in (1, 3, 4, 6, 16, 17):
            name.removeNames(nameID=rec.nameID)
    name.setName("MiiMakerVitaButtons", 1, 3, 1, 0x409)
    name.setName("MiiMakerVitaButtons Regular", 4, 3, 1, 0x409)
    name.setName("MiiMakerVitaButtons-Regular", 6, 3, 1, 0x409)
    name.setName("MiiMakerVitaButtons; glyphs from PromptFont by Yukari Hafner", 3, 3, 1, 0x409)
    font.save(OUT)
    print("wrote", OUT, len(names), "code points")


if __name__ == "__main__":
    main()

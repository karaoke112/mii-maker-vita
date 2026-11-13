#!/usr/bin/env python3
"""Builds the app's icons from published, human-made icon sets.

Nothing here is drawn by this project: each icon is an SVG from one of the
sets below, rasterized as a white mask (the app tints it when drawing).
app/resources/icons/LICENSES.md lists every icon with its author and
licence; keep it in sync with ICONS.

Sets (Iconify's JSON packages of the upstream sets):
  ms   Material Symbols, Google, Apache-2.0            @iconify-json/material-symbols
  mdi  Material Design Icons, Pictogrammers, Apache-2.0 @iconify-json/mdi
  he   Health Icons, Resolve to Save Lives, CC0-1.0    @iconify-json/healthicons
  tb   Tabler Icons, Paweł Kuna, MIT                   @iconify-json/tabler

    pip install cairosvg pillow
    for p in material-symbols mdi healthicons tabler; do
        npm pack @iconify-json/$p && mkdir -p $p && tar xzf iconify-json-$p-*.tgz -C $p
    done
    python3 app/tools/make_icons.py .       # the folder holding those four
"""
import io
import json
import os
import sys

import cairosvg
from PIL import Image, ImageDraw, ImageFont

HERE = os.path.dirname(os.path.abspath(__file__))
APP = os.path.join(HERE, "..")
OUT = os.path.join(APP, "resources", "icons")
FONT = os.path.join(APP, "resources", "font", "font.ttf")

SETS = {"ms": "material-symbols", "mdi": "mdi", "he": "healthicons", "tb": "tabler"}

# app name -> (icon, size in px)
ICONS = {
    # editor menu
    "face": ("ms:sentiment-satisfied-outline", 128),
    "hair": ("ms:face-6-outline", 128),
    "eyebrows": ("ms:eyebrow-outline", 128),
    "eyes": ("ms:visibility-outline", 128),
    "nose": ("he:nose-outline", 128),
    "mouth": ("ms:lips-outline", 128),
    "facial_hair": ("mdi:mustache", 128),
    "mole": ("tb:circle-dot", 128),
    "style": ("ms:wc", 128),
    "body": ("mdi:human-male-height", 128),
    "glasses": ("ms:eyeglasses-outline", 128),
    "star": ("ms:star", 128),
    "star_outline": ("ms:star-outline", 128),
    # Style page
    "man": ("ms:man", 192),
    "woman": ("ms:woman", 192),
    # Body page slider ends
    "shorter": ("ms:unfold-less", 128),
    "taller": ("ms:unfold-more", 128),
    "thinner": ("ms:width-normal", 128),
    "wider": ("ms:width-wide", 128),
    # "none" tiles
    "none": ("ms:block-outline", 128),
}

# app icon and LiveArea art
APP_ICON = "ms:face-6"
ACCENT = (10, 100, 220)
LIGHT = (235, 235, 235)


class Sets:
    def __init__(self, root):
        self.root = root
        self.cache = {}

    def svg(self, ref):
        prefix, name = ref.split(":")
        if prefix not in self.cache:
            for cand in (SETS[prefix], "iconify-json-" + SETS[prefix]):
                for sub in ("package/icons.json", "icons.json"):
                    p = os.path.join(self.root, cand, sub)
                    if os.path.exists(p):
                        self.cache[prefix] = json.load(open(p))
                        break
                if prefix in self.cache:
                    break
            else:
                # versioned folder names, e.g. iconify-json-mdi-1.2.3
                for d in os.listdir(self.root):
                    p = os.path.join(self.root, d, "package", "icons.json")
                    if d.startswith("iconify-json-" + SETS[prefix] + "-") and os.path.exists(p):
                        self.cache[prefix] = json.load(open(p))
                        break
        data = self.cache[prefix]
        aliases = data.get("aliases", {})
        while name in aliases:
            name = aliases[name]["parent"]
        icon = data["icons"][name]
        w = icon.get("width", data.get("width", 24))
        h = icon.get("height", data.get("height", 24))
        return ('<svg xmlns="http://www.w3.org/2000/svg" viewBox="0 0 %s %s" width="%s" height="%s" '
                'color="black">%s</svg>' % (w, h, w, h, icon["body"]))

    def mask(self, ref, px):
        """The icon as an alpha mask, white."""
        png = cairosvg.svg2png(bytestring=self.svg(ref).encode(), output_width=px, output_height=px)
        alpha = Image.open(io.BytesIO(png)).convert("RGBA").split()[3]
        out = Image.new("RGBA", (px, px), (255, 255, 255, 0))
        out.putalpha(alpha)
        return out


def tinted(mask, color):
    out = Image.new("RGBA", mask.size, color + (0,))
    out.putalpha(mask.split()[3])
    return out


def text(draw, xy, s, size, fill):
    font = ImageFont.truetype(FONT, size)
    w = draw.textlength(s, font=font)
    draw.text((xy[0] - w / 2, xy[1]), s, font=font, fill=fill)


def save_indexed(im, path):
    im.convert("RGB").quantize(colors=255, method=Image.Quantize.MEDIANCUT).save(path, optimize=True)


def main():
    if len(sys.argv) != 2:
        sys.exit(__doc__)
    sets = Sets(sys.argv[1])
    os.makedirs(OUT, exist_ok=True)
    for name, (ref, px) in ICONS.items():
        sets.mask(ref, px).save(os.path.join(OUT, name + ".png"), optimize=True)

    # App icon: the white face on the accent color.
    face = sets.mask(APP_ICON, 96)
    icon = Image.new("RGB", (128, 128), ACCENT)
    icon.paste(face, (16, 16), face)
    save_indexed(icon, os.path.join(APP, "sce_sys", "icon0.png"))
    big = Image.new("RGB", (256, 256), ACCENT)
    face2 = sets.mask(APP_ICON, 192)
    big.paste(face2, (32, 32), face2)
    big.save(os.path.join(APP, "resources", "icon", "icon.png"), optimize=True)

    # LiveArea background (840x500) and start button (280x158).
    bg = Image.new("RGB", (840, 500), LIGHT)
    d = ImageDraw.Draw(bg)
    face3 = tinted(sets.mask(APP_ICON, 200), ACCENT)
    bg.paste(face3, (320, 90), face3)
    text(d, (420, 320), "Mii Maker Vita", 56, (30, 30, 30))
    save_indexed(bg, os.path.join(APP, "sce_sys", "livearea", "contents", "bg.png"))
    # Boot splash (960x544), shown while the app loads: the same picture.
    pic0 = Image.new("RGB", (960, 544), LIGHT)
    pic0.paste(bg, (60, 22))
    save_indexed(pic0, os.path.join(APP, "sce_sys", "pic0.png"))
    start = Image.new("RGB", (280, 158), ACCENT)
    d = ImageDraw.Draw(start)
    text(d, (140, 52), "Mii Maker Vita", 30, (255, 255, 255))
    save_indexed(start, os.path.join(APP, "sce_sys", "livearea", "contents", "startup.png"))
    print("wrote", len(ICONS), "icons and the app / LiveArea images")


if __name__ == "__main__":
    main()

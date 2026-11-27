#!/usr/bin/env python3
"""Draws the Last.fm tile (tiles/lastfm.png) for each icon pack on the card,
in that pack's tile style, with the lower-case "as" of the Last.fm mark as the
glyph (no pack has a Last.fm glyph of its own).

usage: python3 lastfm_pack_tiles.py <repo root> <bold font> <regular font> <light font>
"""
import os, sys
from PIL import Image, ImageDraw, ImageFont

ROOT, BOLD, REG, LIGHT = sys.argv[1:5]
PACKS = os.path.join(ROOT, "ap80/sdcard/sonix-resource-overlay/icons")
RED = (229, 57, 53, 255)       # Last.fm red, lifted a little for dark screens
NOTHING_RED = (215, 25, 33, 255)
SS = 4


def text_mask(size, font_path, frac):
    S = size * SS
    m = Image.new("L", (S, S), 0)
    f = ImageFont.truetype(font_path, int(S * frac))
    ImageDraw.Draw(m).text((S / 2, S / 2), "as", font=f, fill=255, anchor="mm")
    return m


def plain(size, font, frac, colour):
    S = size * SS
    im = Image.new("RGBA", (S, S), (0, 0, 0, 0))
    im.paste(Image.new("RGBA", (S, S), colour), (0, 0), text_mask(size, font, frac))
    return im.resize((size, size), Image.LANCZOS)


def filled(size, font, frac, colour, box, radius):
    S = size * SS
    im = Image.new("RGBA", (S, S), (0, 0, 0, 0))
    ImageDraw.Draw(im).rounded_rectangle([v * SS for v in box], radius * SS, fill=colour)
    im.paste(Image.new("RGBA", (S, S), (255, 255, 255, 255)), (0, 0), text_mask(size, font, frac))
    return im.resize((size, size), Image.LANCZOS)


def duotone(size, font, frac, colour):
    S = size * SS
    im = Image.new("RGBA", (S, S), (0, 0, 0, 0))
    d = ImageDraw.Draw(im)
    m = S * 0.16
    d.rounded_rectangle([m, m, S - m, S - m], S * 0.16, fill=colour[:3] + (64,), outline=colour, width=int(S * 0.04))
    im.paste(Image.new("RGBA", (S, S), colour), (0, 0), text_mask(size, font, frac))
    return im.resize((size, size), Image.LANCZOS)


def dotted(size, font, mode):
    # As ap80/tools/nothing_tiles.py: a disc, the glyph as a dot matrix, a red dot.
    S = size * SS
    bg = (242, 242, 242, 255) if mode == "dark" else (20, 20, 20, 255)
    fg = (0, 0, 0, 255) if mode == "dark" else (242, 242, 242, 255)
    im = Image.new("RGBA", (S, S), (0, 0, 0, 0))
    d = ImageDraw.Draw(im)
    m = int(S * 0.04)
    d.ellipse([m, m, S - m, S - m], fill=bg)
    gs = int(S * 0.50)
    mask = text_mask(size, font, 0.36).resize((S, S)).crop(((S - gs) // 2, (S - gs) // 2, (S + gs) // 2, (S + gs) // 2))
    pitch = int(gs / 19)
    r = gs / 19 * 0.42
    dots = Image.new("L", (gs, gs), 0)
    dd = ImageDraw.Draw(dots)
    small = mask.resize((gs // pitch, gs // pitch), Image.BOX)
    for y in range(small.height):
        for x in range(small.width):
            if small.getpixel((x, y)) > 85:
                cx, cy = x * pitch + pitch / 2, y * pitch + pitch / 2
                dd.ellipse([cx - r, cy - r, cx + r, cy + r], fill=255)
    im.paste(Image.new("RGBA", (gs, gs), fg), ((S - gs) // 2, (S - gs) // 2), dots)
    rr, cx, cy = S * 0.065, S * 0.80, S * 0.20
    d.ellipse([cx - rr, cy - rr, cx + rr, cy + rr], fill=NOTHING_RED)
    return im.resize((size, size), Image.LANCZOS)


tiles = {
    "Lucide Bold": plain(160, BOLD, 0.50, RED),
    "Lucide Thin": plain(160, LIGHT, 0.50, RED),
    "Phosphor Duotone": duotone(160, REG, 0.40, RED),
    "Phosphor Fill": filled(160, BOLD, 0.44, (213, 16, 7, 255), (10, 10, 151, 151), 28),
    "Nothing": dotted(256, BOLD, "dark"),
    "Nothing Light": dotted(256, BOLD, "light"),
}
for pack, im in tiles.items():
    path = os.path.join(PACKS, pack, "tiles", "lastfm.png")
    im.save(path)
    print(path)

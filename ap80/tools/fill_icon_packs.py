"""Fills the gaps in the icon packs on the card.

A pack replaces a built-in icon only when it has a PNG of that name; anything
it lacks is drawn with Sonix's own icon, which has a different line weight and,
for the big menu tiles, a different look altogether. This script adds:

  * glyphs: every built-in icon drawn with strokes (battery, Wi-Fi bars, quick
    panel buttons, skip 10/30/60 s, ...) redrawn from sonix-player/assets/icons
    with the pack's line weight, for the Lucide and Nothing packs;
  * tiles: Navidrome (new in 1.6) and the metronome (Nothing had a stopwatch),
    plus the big DAC / SonixLink / Bluetooth receiver page pictures where a
    pack had none, in each pack's own tile style.

Existing files are left alone unless they are listed in REDRAW.

usage: python3 fill_icon_packs.py <repo root> <lucide-static icons dir> <phosphor-core assets dir>
"""
import io, os, re, sys
import cairosvg
from PIL import Image, ImageDraw

ROOT, LUC, PHO = sys.argv[1:4]
PACKS = os.path.join(ROOT, "ap80/sdcard/sonix-resource-overlay/icons")
BUILTIN = os.path.join(ROOT, "sonix-player/assets/icons")
OWN = os.path.join(os.path.dirname(os.path.abspath(__file__)), "svg")
TABLE = os.path.join(ROOT, "sonix-player/src/gui/shell/iconpack_table.c")

# Line weight of each pack's glyphs (Lucide units, 24 per icon).
STROKE = {"Lucide Bold": 2.75, "Lucide Thin": 1.25, "Nothing": 1.25, "Nothing Light": 1.25}
REDRAW = {("Nothing", "tiles/metronome"), ("Nothing Light", "tiles/metronome")}


def table():
    rows = re.findall(r'\{&icon_\w+, "([^"]+)", (true|false)\}', open(TABLE).read())
    return [(f, g == "true") for f, g in rows]


def svg_png(svg, px, stroke=None, colour="#fff", scale_from=2.0):
    if stroke is not None:
        k = stroke / scale_from
        svg = re.sub(r'stroke-width="([\d.]+)"', lambda m: f'stroke-width="{float(m.group(1)) * k:.3f}"', svg)
        svg = re.sub(r'stroke-width:([\d.]+)px', lambda m: f'stroke-width:{float(m.group(1)) * k:.3f}px', svg)
    svg = svg.replace("currentColor", colour).replace("stroke:black", "stroke:" + colour).replace(
        "fill:black", "fill:" + colour)
    png = cairosvg.svg2png(bytestring=svg.encode(), output_width=px, output_height=px)
    return Image.open(io.BytesIO(png)).convert("RGBA")


def own_or_lucide(name):
    p = os.path.join(OWN, name + ".svg")
    return open(p if os.path.exists(p) else os.path.join(LUC, name + ".svg")).read()


# ---- glyphs ---------------------------------------------------------------
def fill_glyphs(pack, done):
    for f, glyph in table():
        out = os.path.join(PACKS, pack, f + ".png")
        if not glyph or os.path.exists(out):
            continue
        src = os.path.join(BUILTIN, f + ".svg")
        if not os.path.exists(src):
            continue
        svg = open(src).read()
        if "stroke-width" not in svg:
            continue  # filled artwork (AirPods, quality badges): no line weight to match
        svg_png(svg, 96, STROKE[pack]).save(out)
        done.append(f"{pack}/{f}.png")


# ---- tiles ----------------------------------------------------------------
NOTHING_MAP = {"metronome": "metronome", "navidrome": "disc-album"}
LUCIDE_MAP = {"metronome": "metronome", "navidrome": "disc-album"}
PHOSPHOR_MAP = {"navidrome": "vinyl-record"}
NAVIDROME_BLUE = {"Lucide": (96, 165, 250), "Fill": (52, 110, 210), "Duotone": (96, 160, 247)}
PAGE_COPIES = {"dac-icon-page": "dac", "sonixlink-icon-page": "sonixlink", "bluetooth-receiver-icon-page": "bluetooth"}


def nothing_tile(luc_svg, dark):
    # Same drawing as nothing_tiles.py: a light (dark pack) or black circle,
    # the glyph as a dot matrix, one red dot on the ring.
    SS, N = 4, 256
    S = N * SS
    bg = (242, 242, 242, 255) if dark else (20, 20, 20, 255)
    fg = (0, 0, 0, 255) if dark else (242, 242, 242, 255)
    im = Image.new("RGBA", (S, S), (0, 0, 0, 0))
    d = ImageDraw.Draw(im)
    m = int(S * 0.04)
    d.ellipse([m, m, S - m, S - m], fill=bg)
    gs = int(S * 0.50)
    mask = svg_png(luc_svg, gs, 1.9, "#000").split()[3]
    pitch, r = int(gs / 19), gs / 19 * 0.42
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
    d.ellipse([cx - rr, cy - rr, cx + rr, cy + rr], fill=(215, 25, 33, 255))
    return im.resize((N, N), Image.LANCZOS)


def lucide_tile(luc_svg, stroke, colour):
    im = Image.new("RGBA", (160, 160), (0, 0, 0, 0))
    g = svg_png(luc_svg, 124, stroke, "#%02x%02x%02x" % colour)
    im.paste(g, (18, 18), g)
    return im


def phosphor_tile(name, style, like):
    if style == "Fill":
        # The pack's own rounded square, recoloured, with the white glyph.
        base = Image.open(like).convert("RGBA")
        im = Image.new("RGBA", base.size, NAVIDROME_BLUE["Fill"] + (255,))
        im.putalpha(base.split()[3])
        g = svg_png(open(os.path.join(PHO, "fill", name + "-fill.svg")).read(), 80, None, "#fff")
        im.paste(g, (40, 40), g)
        return im
    svg = open(os.path.join(PHO, "duotone", name + "-duotone.svg")).read()
    g = svg_png(svg, 128, None, "#%02x%02x%02x" % NAVIDROME_BLUE["Duotone"])
    im = Image.new("RGBA", (160, 160), (0, 0, 0, 0))
    im.paste(g, (16, 16), g)
    return im


def save_tile(pack, name, make, done):
    out = os.path.join(PACKS, pack, "tiles", name + ".png")
    if os.path.exists(out) and (pack, "tiles/" + name) not in REDRAW:
        return
    make().save(out)
    done.append(f"{pack}/tiles/{name}.png")


def main():
    done = []
    for pack in STROKE:
        fill_glyphs(pack, done)
    for pack, dark in (("Nothing", True), ("Nothing Light", False)):
        for tile, luc in NOTHING_MAP.items():
            save_tile(pack, tile, lambda: nothing_tile(own_or_lucide(luc), dark), done)
    for pack in ("Lucide Bold", "Lucide Thin"):
        save_tile(pack, "navidrome", lambda: lucide_tile(own_or_lucide("disc-album"), STROKE[pack],
                                                          NAVIDROME_BLUE["Lucide"]), done)
    for pack, style in (("Phosphor Fill", "Fill"), ("Phosphor Duotone", "Duotone")):
        like = os.path.join(PACKS, pack, "tiles", "album.png")
        save_tile(pack, "navidrome", lambda: phosphor_tile("vinyl-record", style, like), done)
    # The big pictures on the DAC, SonixLink and Bluetooth receiver pages: the
    # pack's own menu tile for the same thing.
    for pack in ("Lucide Bold", "Lucide Thin", "Phosphor Fill", "Phosphor Duotone"):
        for page, tile in PAGE_COPIES.items():
            src = os.path.join(PACKS, pack, "tiles", tile + ".png")
            if os.path.exists(src):
                save_tile(pack, page, lambda: Image.open(src).convert("RGBA"), done)
    print(len(done), "files")
    for d in done:
        print(" ", d)


main()

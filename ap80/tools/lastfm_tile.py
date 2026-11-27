#!/usr/bin/env python3
"""Draws the Last.fm tile (assets/icons/lastfm.png, 140x140) in the style of
the other streaming tiles: a tilted coloured card behind a frosted glass card
with a white glyph. Needs Pillow and a bold sans font (path as argument)."""
import sys
from PIL import Image, ImageDraw, ImageFilter, ImageFont, ImageChops

S = 4            # supersampling
W = 140 * S
font_path = sys.argv[1] if len(sys.argv) > 1 else "/usr/share/fonts/truetype/dejavu/DejaVuSans-Bold.ttf"
out = sys.argv[2] if len(sys.argv) > 2 else "lastfm.png"

def rrect(size, box, r, fill):
    im = Image.new("RGBA", size, (0, 0, 0, 0))
    ImageDraw.Draw(im).rounded_rectangle(box, r * S, fill=fill)
    return im

# back card: vertical gradient, tilted
card = Image.new("RGBA", (W, W), (0, 0, 0, 0))
grad = Image.new("RGBA", (W, W))
top, bot = (255, 112, 92), (213, 16, 7)
for y in range(W):
    t = y / (W - 1)
    c = tuple(int(top[i] + (bot[i] - top[i]) * t) for i in range(3)) + (255,)
    ImageDraw.Draw(grad).line([(0, y), (W, y)], fill=c)
mask = Image.new("L", (W, W), 0)
ImageDraw.Draw(mask).rounded_rectangle((12 * S, 18 * S, 92 * S, 98 * S), 20 * S, fill=255)
card.paste(grad, (0, 0), mask)
card = card.rotate(14, resample=Image.BICUBIC, center=(52 * S, 58 * S))

# front glass card
fbox = (48 * S, 44 * S, 132 * S, 128 * S)
fmask = Image.new("L", (W, W), 0)
ImageDraw.Draw(fmask).rounded_rectangle(fbox, 18 * S, fill=255)
blur = card.filter(ImageFilter.GaussianBlur(9 * S))
glass = Image.new("RGBA", (W, W), (190, 40, 32, 150))
glass.alpha_composite(blur)
glass.alpha_composite(Image.new("RGBA", (W, W), (255, 235, 230, 46)))
# rim
rim = Image.new("RGBA", (W, W), (0, 0, 0, 0))
ImageDraw.Draw(rim).rounded_rectangle(fbox, 18 * S, outline=(255, 255, 255, 70), width=int(1.5 * S))
# glyph: lower-case "as", like the Last.fm mark
g = Image.new("RGBA", (W, W), (0, 0, 0, 0))
f = ImageFont.truetype(font_path, 44 * S)
d = ImageDraw.Draw(g)
cx, cy = (fbox[0] + fbox[2]) / 2, (fbox[1] + fbox[3]) / 2
d.text((cx, cy), "as", font=f, fill=(255, 255, 255, 235), anchor="mm")

front = Image.new("RGBA", (W, W), (0, 0, 0, 0))
front.paste(glass, (0, 0), fmask)
front.alpha_composite(rim)
front.alpha_composite(g)

outim = Image.new("RGBA", (W, W), (0, 0, 0, 0))
outim.alpha_composite(card)
outim.alpha_composite(front)
outim.resize((140, 140), Image.LANCZOS).save(out)

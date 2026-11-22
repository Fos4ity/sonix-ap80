import sys, io, os, re
import cairosvg
from PIL import Image, ImageDraw, ImageFilter
LUC = sys.argv[1]; OUT = sys.argv[2]; MODE = sys.argv[3]  # dark | light
MAP = {
 "music":"music","all":"library","album":"disc-3","artist":"mic-vocal","album_artist":"users",
 "genre":"guitar","year":"calendar","favourites":"heart","most-played":"trending-up","explorer":"folder",
 "playlist":"list-music","streaming":"cast","wireless":"link","wifi-settings":"wifi","bluetooth":"bluetooth",
 "airplay":"airplay","wifi-transfer":"arrow-down-up","sonixlink":"smartphone","dlna":"server","tidal":"waves",
 "ytmusic":"circle-play","qobuz":"disc","radio":"radio","podcast":"podcast","audiobooks":"book-headphones",
 "audiobook-library":"library-big","audiobook-series":"layers","audiobook-author":"user-pen",
 "audiobook-continue":"history","settings":"settings","more":"layout-grid","dac":"audio-lines",
 "gearboy":"gamepad-2","flappy-bird":"bird","ebook":"book-open","file-explorer":"folder-open",
 "metronome":"timer","dac-icon-page":"audio-lines","sonixlink-icon-page":"smartphone",
 "bluetooth-receiver-icon-page":"bluetooth",
}
RED=(215,25,33,255)
SS=4; N=256  # output size; supersample
def glyph(name, px, stroke):
    svg=open(os.path.join(LUC,name+".svg")).read()
    svg=re.sub(r'stroke-width="[\d.]+"', f'stroke-width="{stroke}"', svg)
    svg=svg.replace('stroke="currentColor"','stroke="#000"')
    png=cairosvg.svg2png(bytestring=svg.encode(), output_width=px, output_height=px)
    return Image.open(io.BytesIO(png)).convert("RGBA").split()[3]
def dotify(mask, pitch, r):
    # dot-matrix: sample coverage per cell, draw a dot where the glyph is
    w,h=mask.size; out=Image.new("L",(w,h),0); d=ImageDraw.Draw(out)
    small=mask.resize((w//pitch,h//pitch), Image.BOX)
    for y in range(small.height):
        for x in range(small.width):
            v=small.getpixel((x,y))
            if v>85:
                cx=x*pitch+pitch/2; cy=y*pitch+pitch/2
                d.ellipse([cx-r,cy-r,cx+r,cy+r],fill=255)
    return out
os.makedirs(OUT,exist_ok=True)
for tile,luc in MAP.items():
    S=N*SS
    bg=(242,242,242,255) if MODE=="dark" else (20,20,20,255)
    fg=(0,0,0,255) if MODE=="dark" else (242,242,242,255)
    im=Image.new("RGBA",(S,S),(0,0,0,0)); d=ImageDraw.Draw(im)
    m=int(S*0.04); d.ellipse([m,m,S-m,S-m],fill=bg)
    gs=int(S*0.50); g=glyph(luc,gs,1.9)
    g=dotify(g, pitch=int(gs/19), r=gs/19*0.42)
    col=Image.new("RGBA",(gs,gs),fg); im.paste(col,((S-gs)//2,(S-gs)//2),g)
    # the red element: one dot on the ring, Nothing-style
    rr=S*0.065; cx=S*0.80; cy=S*0.20
    d.ellipse([cx-rr,cy-rr,cx+rr,cy+rr],fill=RED)
    im.resize((N,N),Image.LANCZOS).save(os.path.join(OUT,tile+".png"))
print("ok",len(MAP))

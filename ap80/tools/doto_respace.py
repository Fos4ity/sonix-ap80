# Doto (OFL) -> proportional spacing, lighter punctuation. Usage: respace.py in.ttf out.ttf
import sys
from fontTools.ttLib import TTFont
from fontTools.pens.recordingPen import DecomposingRecordingPen
from fontTools.pens.ttGlyphPen import TTGlyphPen
src,dst=sys.argv[1:3]
t=TTFont(src); gs=t.getGlyphSet(); glyf=t['glyf']; hm=t['hmtx']
SIMPLE={ # glyph -> dot centres to keep (the originals are 5-dot crosses)
 'period':[(250,50)], 'colon':[(250,450),(250,150)],
 'semicolon':[(250,450),(250,50),(150,-50)], 'comma':[(250,50),(150,-50)],
}
def contours(name):
    p=DecomposingRecordingPen(gs); gs[name].draw(p)
    out=[];cur=None
    for op,a in p.value:
        if op=='moveTo': cur=[(op,a)]
        elif op in('closePath','endPath'): cur.append((op,a)); out.append(cur); cur=None
        else: cur.append((op,a))
    return out
def centre(c):
    pts=[q for op,a in c for q in a if q is not None]
    xs=[q[0] for q in pts]; ys=[q[1] for q in pts]
    return (round((min(xs)+max(xs))/2), round((min(ys)+max(ys))/2)), min(xs), max(xs)
new={}
for name in t.getGlyphOrder():
    cs=contours(name)
    if name in SIMPLE:
        keep=set(SIMPLE[name]); cs=[c for c in cs if centre(c)[0] in keep]
    if not cs:
        adv=hm[name][0]
        if name=='space': adv=300
        new[name]=(TTGlyphPen(None).glyph(), adv, 0); continue
    xmin=min(centre(c)[1] for c in cs); xmax=max(centre(c)[2] for c in cs)
    col0=round((xmin-5)/100)*100            # left edge of the first used column
    cols=round((xmax-xmin+10)/100)
    if name in ('zero','one','two','three','four','five','six','seven','eight','nine'):
        col0=0; cols=5                       # tabular digits, clocks do not jitter
    dx=-col0
    pen=TTGlyphPen(None)
    for c in cs:
        for op,a in c:
            if op=='moveTo': pen.moveTo((a[0][0]+dx,a[0][1]))
            elif op=='lineTo': pen.lineTo((a[0][0]+dx,a[0][1]))
            elif op=='qCurveTo': pen.qCurveTo(*[(q[0]+dx,q[1]) if q else None for q in a])
            elif op=='curveTo': pen.curveTo(*[(q[0]+dx,q[1]) for q in a])
            else: pen.closePath()
    new[name]=(pen.glyph(), cols*100+100, 5)
for name,(g,adv,lsb) in new.items():
    glyf[name]=g
for name,(g,adv,lsb) in new.items():
    g.recalcBounds(glyf)
    hm[name]=(adv, g.xMin if g.numberOfContours else 0)
for tag in ('GPOS','kern','HVAR','STAT','gvar','fvar','avar'):
    if tag in t: del t[tag]
t['post'].isFixedPitch=0
t['OS/2'].panose.bProportion=0
for rec in t['name'].names:
    if rec.nameID in (1,4,16): rec.string='Doto Sonix'
    elif rec.nameID==6: rec.string='DotoSonix-Black'
    elif rec.nameID==3: rec.string='DotoSonix-Black;respaced for Sonix'
t.save(dst); print('ok', dst)

"""Render InputHalo's README artwork; no application capture or benchmark runs.

Requires Python, Pillow and NumPy. Uses installed Windows fonts. The GIFs are
designed feature illustrations, not recordings or measured frame-rate demos.
Run from any directory: python Tools/Generate-ReadmeAssets.py
"""
from pathlib import Path
from functools import lru_cache
import math
import numpy as np
from PIL import Image, ImageDraw, ImageFont

ROOT = Path(__file__).resolve().parents[1]
OUT = ROOT / "Assets" / "Readme"
OUT.mkdir(parents=True, exist_ok=True)
S = 2
W = 1120
PURPLE = "#AA9AFF"
MINT = "#75E6C8"
AMBER = "#FFCB78"
WHITE = "#F2F0FF"
MUTED = "#9BABC3"
RED = "#FF5A5F"


@lru_cache(maxsize=64)
def font(size, bold=False, cjk=False):
    name = ("msyhbd.ttc" if bold else "msyh.ttc") if cjk else ("segoeuib.ttf" if bold else "segoeui.ttf")
    return ImageFont.truetype(str(Path("C:/Windows/Fonts") / name), round(size * S))


def box(d, xy, fill, radius=0, outline=None, width=1):
    d.rounded_rectangle(tuple(round(v*S) for v in xy), round(radius*S), fill=fill, outline=outline, width=max(1, round(width*S)))


def line(d, points, fill, width=1):
    d.line([(round(x*S), round(y*S)) for x, y in points], fill=fill, width=max(1, round(width*S)), joint="curve")


def ellipse(d, xy, fill=None, outline=None, width=1):
    d.ellipse(tuple(round(v*S) for v in xy), fill=fill, outline=outline, width=max(1, round(width*S)))


def text(d, xy, value, size=18, fill=WHITE, bold=False, anchor=None):
    d.text((round(xy[0]*S), round(xy[1]*S)), value, font=font(size, bold, any(ord(c)>127 for c in value)), fill=fill, anchor=anchor)


def label(d, x, y, value, size=13, fg=MUTED, bg="#182234", outline="#2B3850", pad=12):
    f = font(size, False, any(ord(c)>127 for c in value))
    tw = d.textlength(value, font=f)/S
    box(d, (x,y,x+tw+pad*2,y+32), bg, 9, outline)
    text(d, (x+pad,y+6), value, size, fg)
    return tw+pad*2


def blend(a, b, u):
    a = tuple(int(a[i:i+2],16) for i in (1,3,5))
    b = tuple(int(b[i:i+2],16) for i in (1,3,5))
    return tuple(round(x+(y-x)*u) for x,y in zip(a,b))


def background(h):
    y,x=np.mgrid[0:h*S,0:W*S].astype(np.float32)
    u=x/(W*S); v=y/(h*S)
    glow=np.exp(-((u-.78)**2/.12+(v-.35)**2/.6))
    rgb=np.stack([11+15*glow,16+14*glow,28+31*glow],axis=-1).astype(np.uint8)
    im=Image.fromarray(rgb)
    d=ImageDraw.Draw(im)
    box(d,(1,1,W-1,h-1),None,23,"#30354C")
    return im


def pointer(d,x,y,color=WHITE,scale=1):
    points=[(0,0),(2,29),(10,22),(17,35),(23,32),(16,19),(27,18)]
    p=[((x+a*scale)*S,(y+b*scale)*S) for a,b in points]
    d.polygon(p,fill=color,outline="#0A101D",width=round(2*S))


def halo(d,x,y,r=28):
    for extra in (9,6,3):
        ellipse(d,(x-r-extra,y-r-extra,x+r+extra,y+r+extra),outline=blend("#14232E",MINT,.08),width=2)
    ellipse(d,(x-r,y-r,x+r,y+r),outline=MINT,width=2)


def ease(v):
    v=max(0,min(1,v)); return v*v*(3-2*v)


def arrow(d,a,b,color=PURPLE,width=3):
    line(d,[a,b],color,width)
    theta=math.atan2(b[1]-a[1],b[0]-a[0])
    pts=[(b[0]-19*math.cos(theta+q),b[1]-19*math.sin(theta+q)) for q in (-.48,.48)]
    line(d,[pts[0],b,pts[1]],color,width)


def make_palette(base, sample):
    probe=sample.resize((W,base.height//S),Image.Resampling.LANCZOS)
    d=ImageDraw.Draw(probe)
    for i,c in enumerate((PURPLE,MINT,AMBER,WHITE,MUTED,RED,"#FFFFFF")):
        d.rectangle((i*60,0,i*60+59,40),fill=c)
    return probe.quantize(colors=256,method=Image.Quantize.MEDIANCUT)


def export_gif(name, base, render, frames, fps=20):
    palette=make_palette(base,render(base.copy(),.5))
    encoded=[]
    for i in range(frames):
        im=render(base.copy(),i/frames).resize((W,base.height//S),Image.Resampling.LANCZOS)
        if i==round(frames*.62):
            im.save(OUT/(name+"-poster.png"),optimize=True)
        encoded.append(im.quantize(palette=palette,dither=Image.Dither.NONE))
    encoded[0].save(OUT/(name+".gif"),save_all=True,append_images=encoded[1:],duration=round(1000/fps),loop=0,optimize=True,disposal=1)
    print(f"{name}.gif: {frames} frames, {(OUT/(name+'.gif')).stat().st_size:,} bytes",flush=True)


def hero():
    base=background(430); d=ImageDraw.Draw(base)
    text(d,(42,27),"LIMXTEAM  /  WINDOWS DESKTOP UTILITY",13,MUTED,True)
    text(d,(40,83),"InputHalo",78,WHITE,True)
    text(d,(44,202),"让操作，被看见。",32,WHITE,True)
    text(d,(45,265),"输入可视化  /  激光指示  /  即时标注",18,MUTED)
    x=44
    for value in ("1.0.0 Preview","Windows x64","C++23 · Vulkan"):
        x+=label(d,x,345,value,13,fg=PURPLE)+10
    for x in range(644,1090,24):
        for y in range(44,400,24):
            ellipse(d,(x,y,x+1.5,y+1.5),fill="#2E344B")
    box(d,(672,75,1054,349),"#111B2C",18,"#364059")
    text(d,(694,93),"FOLLOW. HIGHLIGHT. EXPLAIN.",12,MUTED,True)
    for y,ww in ((151,133),(183,208),(215,169),(247,108)):
        box(d,(704,y,704+ww,y+8),"#29354C",4)
    box(d,(901,242,1021,302),"#20263D",10,"#3E395F")
    text(d,(925,258),"FOCUS",14,PURPLE,True)
    text(d,(1075,385),"INPUT IN FOCUS",12,MUTED,True,anchor="ra")

    def frame(im,p):
        d=ImageDraw.Draw(im)
        ang=p*math.tau
        x=875+81*math.cos(ang); y=201+38*math.sin(ang)
        # Slow orbit is decorative motion; no simulated FPS or telemetry.
        for r,start in ((148,0),(164,130)):
            d.arc(((873-r)*S,(218-r)*S,(873+r)*S,(218+r)*S),start+p*360,start+p*360+66,fill="#555078",width=2*S)
        halo(d,x,y,37)
        for offset in (0,.5):
            q=(p+offset)%1
            r=38+39*q
            ellipse(d,(x-r,y-r,x+r,y+r),outline=blend(MINT,"#172336",q),width=1)
        pointer(d,x-6,y-10,scale=1.12)
        label(d,x-40,y+56,"Ctrl",14,WHITE)
        label(d,x+26,y+56,"C",14,WHITE)
        return im
    export_gif("inputhalo-hero",base,frame,120)


def walkthrough():
    base=background(550); d=ImageDraw.Draw(base)
    text(d,(35,21),"操作更直观，讲解更连贯",23,WHITE,True)
    text(d,(1082,29),"功能交互示意 · 非软件录屏",13,MUTED,anchor="ra")
    box(d,(28,76,1092,421),"#101827",14,"#2B3850")
    for x in range(46,1080,24):
        for y in range(95,409,24):
            ellipse(d,(x,y,x+1,y+1),fill="#243147")
    for x,title,idx in ((100,"操作区域","01"),(440,"讲解重点","02"),(780,"关键细节","03")):
        box(d,(x,147,x+240,342),"#172235",11,"#303C55")
        text(d,(x+19,166),idx,12,PURPLE,True)
        text(d,(x+19,194),title,20,WHITE,True)
        for y,w in ((245,184),(264,147),(283,167)):
            box(d,(x+20,y,x+20+w,y+6),"#35415A",3)
        box(d,(x+20,306,x+94,322),"#414260",4)

    def frame(im,p):
        t=p*12; d=ImageDraw.Draw(im)
        if t<3:
            part=0; title="跟随指针，显示操作"; desc="光圈、键帽和滚轮提示，与操作一起移动。"; badge="Ctrl + C"
            v=t/3
            x=185+550*ease(v); y=220+42*math.sin(v*math.tau)
            if 1.25<t<2.2:
                q=(t-1.25)/.95; cx=185+550*ease(1.25/3); cy=220+42*math.sin(1.25/3*math.tau)
                ellipse(d,(cx-52*q,cy-52*q,cx+52*q,cy+52*q),outline=blend(MINT,"#101827",q),width=2)
            halo(d,x,y,26); pointer(d,x-4,y-8,WHITE,.85)
            label(d,x-41,y+46,"Ctrl",13,WHITE)
            label(d,x+21,y+46,"C",13,WHITE)
            if t>1.45:
                mx=x+76; my=y+43
                box(d,(mx,my,mx+32,my+47),"#1E2C40",10,"#5E708A")
                line(d,[(mx+16,my+4),(mx+16,my+17)],MINT,2)
                cy=my+29
                line(d,[(mx+10,cy+3),(mx+16,cy-3),(mx+22,cy+3)],MINT,2)
        elif t<10:
            badge="Ctrl + Shift + Space"; part=1 if t<8 else 2
            title="按住连续画，重点留在画面上" if t<8 else "右键撤销，松开即清空"
            desc="矩形、箭头、椭圆；颜色、线宽和快捷键可调。" if t<8 else "保持快捷键时右键撤销；松开后清除本轮标注。"
            x=120; y=124
            if t<9:
                q=ease((t-3)/1.5)
                x=84+(356-84)*q; y=133+(356-133)*q
                box(d,(84,133,max(85,x),max(134,y)),None,2,PURPLE,3)
                if t>=5:
                    q=ease((t-5)/1.15); x=365+(553-365)*q; y=364+(234-364)*q
                    if q>.01: arrow(d,(365,364),(x,y))
                if 6.5<=t<8:
                    q=ease((t-6.5)/1.1); x=763+272*q; y=130+227*q
                    if q>.01: ellipse(d,(763,130,max(764,x),max(131,y)),outline=PURPLE,width=3)
                if t>=8:
                    x,y=948,340; badge="保持快捷键 + 右键"
            else:
                x,y=935,323; badge="已松开 · 标注清空"
            halo(d,x,y,19); pointer(d,x,y,scale=.78)
        else:
            part=3; title="沿着轨迹，把重点讲清楚"; desc="激光轨迹与指示点两种模式，支持颜色与消退时间。"; badge="Ctrl + Alt + Space"
            q=(t-10)/2
            points=[]
            for i in range(37):
                u=max(0,q-.32+.32*i/36)
                points.append((128+868*u,246+63*math.sin(u*math.pi*3)))
            for i in range(1,len(points)):
                k=i/(len(points)-1)
                line(d,points[i-1:i+1],blend("#172235",RED,k*.17),13)
                line(d,points[i-1:i+1],blend("#172235",RED,k),4)
            x,y=points[-1]
            ellipse(d,(x-5,y-5,x+5,y+5),fill="#FFF1EA")
            pointer(d,x+4,y+4,scale=.8)
        text(d,(35,446),f"0{part+1}",23,PURPLE,True)
        text(d,(79,445),title,22,WHITE,True)
        text(d,(35,483),desc,15,MUTED)
        f=font(14,False,any(ord(c)>127 for c in badge))
        tw=d.textlength(badge,font=f)/S+24
        label(d,1083-tw,451,badge,14,PURPLE)
        boundaries=(0,3,8,10,12)
        for i in range(4):
            x=35+i*269
            box(d,(x,525,x+243,528),"#2C354B",1)
            amount=max(0,min(1,(t-boundaries[i])/(boundaries[i+1]-boundaries[i])))
            if amount>0: box(d,(x,525,x+243*amount,528),PURPLE,1)
        return im
    export_gif("inputhalo-interactions",base,frame,240)


def performance():
    cards=[
        ("01 / DISTRIBUTION","3.09","MB","安装包体积","3,091,024 字节 · 2.95 MiB",AMBER),
        ("02 / PRIVATE WORKING SET","44.33","MiB","私有工作集","Release · 本机启动快照",MINT),
        ("03 / TOTAL WORKING SET","89.90","MiB","总工作集","含共享内存页",PURPLE),
        ("04 / GLYPH ATLAS","1","MiB / 页","字形图集","4 → 1 MiB · 单页容量减少 75%",AMBER),
    ]
    svg=['<svg xmlns="http://www.w3.org/2000/svg" width="1120" height="292" viewBox="0 0 1120 292" role="img" aria-labelledby="title desc">',
    '<title id="title">InputHalo 已测数据与资源优化</title><desc id="desc">安装包 3.09 MB；历史 Release 启动快照私有工作集 44.33 MiB，总工作集 89.90 MiB；字形图集每页 1 MiB。</desc>',
    '<style>text{font-family:Segoe UI,Microsoft YaHei,Arial,sans-serif}.value{font-weight:700;letter-spacing:-2px}</style>',
    '<rect x="1" y="1" width="1118" height="290" rx="22" fill="#0e1524" stroke="#30354c"/>',
    '<text x="28" y="35" font-size="12" letter-spacing="2" fill="#9babc3">BUILT NATIVE. MEASURED HONESTLY.</text>']
    for i,(eyebrow,num,unit,label_,sub,c) in enumerate(cards):
        x=28+i*274
        if i: svg.append(f'<path d="M{x-15}65V220" stroke="#2c354b"/>')
        svg.append(f'<text x="{x}" y="76" font-size="10" fill="{c}" letter-spacing=".4">{eyebrow}</text>')
        svg.append(f'<text x="{x}" y="143" font-size="51" class="value" fill="#f2f0ff">{num}</text>')
        unit_x=x+({"3.09":119,"44.33":147,"89.90":147,"1":40}[num])
        svg.append(f'<text x="{unit_x}" y="142" font-size="17" fill="{c}">{unit}</text>')
        svg.append(f'<text x="{x}" y="181" font-size="18" fill="#f2f0ff">{label_}</text>')
        svg.append(f'<text x="{x}" y="209" font-size="12" fill="#9babc3">{sub}</text>')
    svg.append('<path d="M28 238H1092" stroke="#2c354b"/><text x="28" y="267" font-size="12" fill="#9babc3">内存：2026-09-23 · Windows 11 / RTX 3060 · 单次快照，非长期平均或峰值；图集比例仅指单页容量。</text></svg>')
    (OUT/"inputhalo-performance.svg").write_text("\n".join(svg),encoding="utf-8")
    print("inputhalo-performance.svg",flush=True)


if __name__=="__main__":
    hero()
    walkthrough()
    performance()

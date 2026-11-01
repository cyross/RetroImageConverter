# RetroImageConv アイコン生成スクリプト (Pillow が必要)
# 使い方: python tools/make_icon.py  → カレントに RetroImageConv.ico と PNG を出力
# SPDX-License-Identifier: MIT
from PIL import Image

OUT=(30,26,40,255)
BODY=(226,214,190,255); BODY_HI=(246,240,226,255); BODY_SH=(170,154,128,255)
BEZEL=(52,50,64,255)
SKY0=(28,30,110,255); SKY1=(120,40,150,255); SKY2=(214,60,140,255); SKY3=(250,130,60,255)
SUN=(255,226,80,255); MT=(54,22,74,255); MT2=(30,14,44,255)
LED=(80,230,110,255)

def art(N):
    im=Image.new('RGBA',(N,N),(0,0,0,0)); p=im.load()
    def rect(x0,y0,x1,y1,c):
        for y in range(y0,y1+1):
            for x in range(x0,x1+1): p[x,y]=c
    if N==32:
        bx0,by0,bx1,by1=1,2,30,24
        rect(bx0,by0,bx1,by1,OUT)
        rect(bx0+1,by0+1,bx1-1,by1-1,BODY)
        rect(bx0+1,by0+1,bx1-1,by0+1,BODY_HI); rect(bx0+1,by0+1,bx0+1,by1-1,BODY_HI)
        rect(bx0+1,by1-1,bx1-1,by1-1,BODY_SH); rect(bx1-1,by0+1,bx1-1,by1-1,BODY_SH)
        for c in [(bx0,by0),(bx1,by0),(bx0,by1),(bx1,by1)]: p[c]=(0,0,0,0)
        sx0,sy0,sx1,sy1=4,5,27,20
        rect(sx0,sy0,sx1,sy1,BEZEL)
        ix0,iy0,ix1,iy1=5,6,26,19
        bands=[SKY0,SKY0,SKY1,SKY1,SKY2,SKY2,SKY3,SKY3,SKY3,SKY3,SKY3,SKY3,SKY3,SKY3]
        h=iy1-iy0+1
        for j in range(h):
            y=iy0+j
            for x in range(ix0,ix1+1):
                c=bands[j]
                # タイリングで帯の境目をつなぐ
                if j in (1,3,5) and (x+y)%2==0: c=bands[j+1]
                p[x,y]=c
        # 太陽
        cx,cy,r=15.5,14.0,5.0
        for y in range(iy0,iy1+1):
            for x in range(ix0,ix1+1):
                d=((x+0.5-cx-0.5)**2+(y+0.5-cy-0.5)**2)**0.5
                if d<=r and y not in (15,17):
                    p[x,y]=SUN
        # 山
        peaks=[(7,14,MT),(22,15,MT)]
        for x in range(ix0,ix1+1):
            top=iy1+1
            for (px,py,_) in peaks:
                top=min(top, py+abs(x-px))
            for y in range(max(top,iy0),iy1+1):
                c=MT if y<18 else (MT2 if (x+y)%2==0 or y>=19 else MT)
                p[x,y]=c
        # 走査線ハイライト
        p[6,7]=(255,255,255,140)
        p[25,22]=LED; p[24,22]=(40,120,60,255)
        # スタンド
        rect(12,25,19,26,OUT); rect(13,25,18,26,BODY_SH)
        rect(8,27,23,29,OUT); rect(9,27,22,28,BODY); rect(9,28,22,28,BODY_SH)
        p[8,27]=(0,0,0,0); p[23,27]=(0,0,0,0)
    else:  # 16
        rect(0,1,15,12,OUT); rect(1,2,14,11,BODY)
        rect(1,2,14,2,BODY_HI); rect(1,11,14,11,BODY_SH); rect(14,2,14,11,BODY_SH)
        for c in [(0,1),(15,1),(0,12),(15,12)]: p[c]=(0,0,0,0)
        rect(2,3,13,9,BEZEL)
        rows=[SKY0,SKY1,SKY2,SKY3,SKY3,SKY3]
        for j,c in enumerate(rows):
            for x in range(3,13):
                cc=c
                if j in (0,1) and (x+j)%2==0: cc=rows[j+1]
                p[x,4+j]=cc
        for (x,y) in [(7,5),(8,5),(6,6),(7,6),(8,6),(9,6),(6,8),(7,8),(8,8),(9,8)]: p[x,y]=SUN
        for x in range(3,13):
            top=min(8+abs(x-4),8+abs(x-11))
            for y in range(max(top,4),10): p[x,y]=MT
        p[12,10]=LED
        rect(6,13,9,13,OUT); rect(4,14,11,15,OUT); rect(5,14,10,14,BODY)
        p[4,15]=(0,0,0,0); p[11,15]=(0,0,0,0)
    return im

a32=art(32); a16=art(16)
sizes={16:a16,32:a32,48:a16.resize((48,48),Image.NEAREST),64:a32.resize((64,64),Image.NEAREST),
       128:a32.resize((128,128),Image.NEAREST),256:a32.resize((256,256),Image.NEAREST)}
a32.save('icon32.png'); a16.save('icon16.png')
sizes[256].save('RetroImageConv.ico', sizes=[(s,s) for s in sizes], append_images=[sizes[s] for s in [16,32,48,64,128]])
# preview
pv=Image.new('RGBA',(256+128+64+48+32+16+60,260),(200,200,200,255)); x=4
for s in [256,128,64,48,32,16]:
    pv.alpha_composite(sizes[s],(x,2)); x+=s+10
dark=Image.new('RGBA',pv.size,(40,40,48,255)); x=4
for s in [256,128,64,48,32,16]:
    dark.alpha_composite(sizes[s],(x,2)); x+=s+10
both=Image.new('RGBA',(pv.width,520)); both.paste(pv,(0,0)); both.paste(dark,(0,260)); both.save('/tmp/icon_preview.png')

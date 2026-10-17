import sys, math, random
from PIL import Image, ImageDraw, ImageFilter
S=1024
im=Image.new('RGBA',(S,S),(0,0,0,0)); d=ImageDraw.Draw(im)
def P(x,y): return (x*S/256,y*S/256)
# shadow
sh=Image.new('RGBA',(S,S),(0,0,0,0)); sd=ImageDraw.Draw(sh)
sd.polygon([P(40,212),P(150,240),P(232,196),P(122,168)],fill=(0,0,0,110)); sh=sh.filter(ImageFilter.GaussianBlur(S/60)); im.alpha_composite(sh)
# base plate (isometric slab)
top=[P(30,190),P(140,218),P(226,174),P(116,146)]
d.polygon(top,fill=(214,214,208,255))
d.polygon([P(30,190),P(140,218),P(140,228),P(30,200)],fill=(150,150,146,255))
d.polygon([P(140,218),P(226,174),P(226,184),P(140,228)],fill=(112,112,110,255))
d.line(top+[top[0]],fill=(40,40,40,255),width=int(S/170))
# dotted grille on the plate
for i in range(9):
    for j in range(4):
        x=70+i*9+j*9.5; y=186-i*4.7+j*2.4
        d.ellipse([P(x-1.6,y-1.0),P(x+1.6,y+1.0)],fill=(120,120,118,255))
# panel: tile pattern texture mapped onto a leaning parallelogram
T=256; tex=Image.new('RGB',(T,T)); px=tex.load(); random.seed(7)
pal=[(18,52,44),(36,104,82),(70,160,120),(170,222,196)]
for y in range(T):
    for x in range(T):
        u=x/T; v=y/T
        w=0.18*math.sin(6.283*(u*2+0.3))+0.12*math.sin(6.283*(v*3+u))+0.05*math.sin(6.283*(u*7-v*5))
        b=math.sin(6.283*(2.2*u+1.4*v)+5*w)
        t=(b*0.5+0.5)*3+ (random.random()-0.5)*0.9
        px[x,y]=pal[max(0,min(3,int(round(t))))]
quad=[P(86,30),P(206,8),P(196,150),P(76,172)]  # tl,tr,br,bl
# perspective-free affine mapping via transform QUAD (maps dest quad <- src rect)
W2=int(S); panel=tex.resize((S,S),Image.NEAREST)
mask=Image.new('L',(S,S),0); ImageDraw.Draw(mask).polygon(quad,fill=255)
mapped=panel.transform((S,S),Image.QUAD,[c for pt in [quad[0],quad[3],quad[2],quad[1]] for c in pt],Image.NEAREST)
# Image.QUAD expects source corners for each dest corner; build inverse by mapping full image into quad
dst=Image.new('RGBA',(S,S),(0,0,0,0))
from PIL import ImageTransform
# Build: for dest pixel find source via bilinear inverse - do it numerically
import numpy as np
qa=np.array(quad,dtype=float)
ys,xs=np.mgrid[0:S,0:S].astype(float)
# inverse bilinear (approx via iterative Newton)
u=np.full_like(xs,0.5); v=np.full_like(xs,0.5)
for _ in range(12):
    p=(1-u)[...,None]*(1-v)[...,None]*qa[0]+u[...,None]*(1-v)[...,None]*qa[1]+u[...,None]*v[...,None]*qa[2]+(1-u)[...,None]*v[...,None]*qa[3]
    du=(1-v)[...,None]*(qa[1]-qa[0])+v[...,None]*(qa[2]-qa[3]); dv=(1-u)[...,None]*(qa[3]-qa[0])+u[...,None]*(qa[2]-qa[1])
    r=np.stack([xs,ys],-1)-p
    det=du[...,0]*dv[...,1]-du[...,1]*dv[...,0]
    u+= (r[...,0]*dv[...,1]-r[...,1]*dv[...,0])/det
    v+= (du[...,0]*r[...,1]-du[...,1]*r[...,0])/det
ta=np.array(tex)
tx=np.clip((u*T*1.0).astype(int)%T,0,T-1); ty=np.clip((v*T).astype(int)%T,0,T-1)
rgb=ta[ty,tx]; m=np.array(mask)
out=np.array(im)
sel=m>0
out[sel,:3]=rgb[sel]; out[sel,3]=255
im=Image.fromarray(out); d=ImageDraw.Draw(im)
# panel thickness (right edge) + frame
d.polygon([quad[1],P(212,12),P(202,153),quad[2]],fill=(90,90,90,255))
d.line(quad+[quad[0]],fill=(30,30,30,255),width=int(S/150))
d.line([quad[1],P(212,12),P(202,153),quad[2]],fill=(30,30,30,255),width=int(S/200))
# foot of the panel into the base
d.polygon([P(76,172),P(196,150),P(200,160),P(80,182)],fill=(170,170,166,255),outline=(30,30,30,255))
sizes=[16,24,32,48,64,128,256]
imgs=[im.resize((s,s),Image.LANCZOS) for s in sizes]
imgs[-1].save(sys.argv[1],format='ICO',sizes=[(s,s) for s in sizes])
imgs[-1].save(sys.argv[2])

# Converts the IRIX system.backgrounds + XPM/XBM files into a C++ header.
# usage: python3 -I gen_irix_data.py <irix_root(with lib/ and include/)> <out.h>
import sys, os, re
sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
from irixfmt import parse_xpm, parse_xbm

X11 = {
 'sgilightblue': (125,158,192), 'sgidarkgray': (85,85,85), 'sgilightgray': (170,170,170),
 'black': (0,0,0), 'white': (255,255,255),
 'cornsilk2': (238,232,205), 'cornsilk3': (205,200,177), 'ivory3': (205,205,193),
 'lightsteelblue3': (162,181,205), 'lightsteelblue4': (110,123,139), 'dodgerblue4': (16,78,139),
 'lightcyan3': (180,205,205), 'lightcyan4': (122,139,139),
 'honeydew1': (240,255,240), 'honeydew2': (224,238,224), 'honeydew3': (193,205,193),
 'navajowhite3': (205,179,139), 'navajowhite4': (139,121,94),
 'chocolate4': (139,69,19), 'antiquewhite': (250,235,215), 'antiquewhite2': (238,223,204),
 'burlywood4': (139,115,85), 'slategray': (112,128,144), 'slategray1': (198,226,255),
 'indigo': (75,0,130), 'gainsboro': (220,220,220), 'oldlace': (253,245,230),
 'olivedrab4': (105,139,34), 'beige': (245,245,220), 'bisque3': (205,183,158),
 'blanchedalmond': (255,235,205), 'khaki4': (139,134,78), 'lemonchiffon': (255,250,205),
}
def color(c):
    c = c.strip()
    if c.startswith('#'):
        return tuple(int(c[i:i+2],16) for i in (1,3,5))
    k = c.lower().replace(' ','')
    m = re.fullmatch(r'gr[ae]y(\d+)', k)
    if m: v = int(int(m.group(1))*255/100+0.5); return (v,v,v)
    return X11[k]

def parse_backgrounds(path):
    out = []; cur = None
    for line in open(path):
        line = line.strip()
        m = re.match(r'background\s+"([^"]+)"', line)
        if m: cur = {'name': m.group(1)}; out.append(cur); continue
        m = re.match(r'default\s+"([^"]+)"', line)
        if m and cur: cur['default'] = m.group(1)
    return out

root, outp = sys.argv[1], sys.argv[2]
bgs = parse_backgrounds(os.path.join(root, 'lib/X11/system.backgrounds'))
H = ['// Generated from IRIX 6.5 system.backgrounds + pixmaps. Do not edit.',
     '#pragma once', '#include <cstdint>', '']
entries = []
for i, b in enumerate(bgs):
    d = b['default']
    if d.startswith('-execute'): continue          # GL demos (Twilight / SGI Logo / CPU Eater)
    if d.startswith('-solid'):
        entries.append((b['name'], 'K_SOLID', 0, 0, None, [color(d.split(None,1)[1])])); continue
    toks = d.split()
    f = toks[1].replace('/usr/include/X11/bitmaps', os.path.join(root, 'include/x11/bitmaps'))
    if toks[0] == '-xpm':
        w, h, idx = parse_xpm(f)
        cols = [color(c) for c in d.split('-colors',1)[1].split()]
        kind = 'K_FULL' if w >= 1024 else 'K_TILE4'
    else:
        w, h, idx = parse_xbm(f)
        bg = color(toks[toks.index('-bg')+1]); fg = color(toks[toks.index('-fg')+1])
        cols = [bg, fg]
        kind = 'K_TILE2'
    entries.append((b['name'], kind, w, h, idx, cols))

# IRIX Extras: desktop pixmaps IRIX shipped but never listed, and the classic X11 root-window
# bitmaps from /usr/include/X11/bitmaps (xsetroot -bitmap). (name, file, default colours, blurb)
def hx(h): return tuple(int(h[i:i+2], 16) for i in (1, 3, 5))
EXTRAS = [
    ('Mid Granite', 'sgidesktop/midgranitesmall.xpm', ['#a89a8c', '#6c6258', '#d8cfc4', '#2e2a26'], 'IRIX pixmap midgranitesmall.xpm - the finer-grained granite IRIX shipped but never listed'),
    ('Swirl II', 'sgidesktop/swirl2small.xpm', ['#b48ad0', '#8a66a8', '#e0d4ec', '#4a3460'], 'IRIX pixmap swirl2small.xpm - the unlisted second Swirl'),
    ('Twill', 'sgidesktop/pattern1', ['#c9d3dc', '#4f6273'], 'IRIX bitmap sgidesktop/pattern1 - diagonal twill cloth'),
    ('Root Weave', 'root_weave', ['#a8b4c0', '#46505a'], 'X11 root_weave - the classic X Window root background'),
    ('Cross Weave', 'cross_weave', ['#7d9ec0', '#2f4f6f'], 'X11 bitmap cross_weave'),
    ('Wide Weave', 'wide_weave', ['#c6c1aa', '#6b6650'], 'X11 bitmap wide_weave'),
    ('Boxes', 'boxes', ['#d3b5b5', '#7a4f4f'], 'X11 bitmap boxes'),
    ('Plaid', 'plaid', ['#6e8b5a', '#2f3f25'], 'X11 bitmap plaid'),
    ('Dimples', 'dimple3', ['#bdbdbd', '#6e6e6e'], 'X11 bitmap dimple3'),
    ('Polka Dots', 'dot', ['#1f2a44', '#e8c35a'], 'X11 bitmap dot'),
    ('Open Dots', 'opendot', ['#e6dcc8', '#8a5a3c'], 'X11 bitmap opendot'),
    ('Stars', 'star', ['#101a3a', '#f0e6a0'], 'X11 bitmap star'),
    ('Targets', 'target', ['#c0d0c0', '#3a5a3a'], 'X11 bitmap target'),
    ('Tie Fighter', 'tie_fighter', ['#141414', '#8c8c8c'], 'X11 bitmap tie_fighter'),
    ('Wingdogs', 'wingdogs', ['#e8e0d0', '#3c3228'], 'X11 bitmap wingdogs - Escher-style tessellating winged dogs'),
    ('Mens et Manus', 'mensetmanus', ['#a3b9cf', '#23364a'], 'X11 bitmap mensetmanus - the MIT seal, from the X Window System\'s home'),
    ('X Snow', 'xsnow', ['#2d4a73', '#f0f6ff'], 'X11 bitmap xsnow - snowflakes'),
    ('SGI Cube', 'sgilogo64', ['#3a3a6e', '#9ea8d8'], 'IRIX bitmap sgilogo64 - the SGI cube logo'),
    ('X Logo', 'xlogo64', ['#d9d9d9', '#303030'], 'X11 bitmap xlogo64 - the X Window System logo'),
    ('Graph Paper', 'grid16', ['#f4f1e6', '#9fb8c8'], 'X11 bitmap grid16'),
]
nclassic = len(entries)
blurbs = {}
for name, f, cols, blurb in EXTRAS:
    path = os.path.join(root, 'include/x11/bitmaps', f)
    if f.endswith('.xpm'):
        w, h, idx = parse_xpm(path); kind = 'K_TILE4'
    else:
        w, h, idx = parse_xbm(path); kind = 'K_TILE2'
    entries.append((name, kind, w, h, idx, [hx(c) for c in cols]))
    blurbs[name] = blurb

blobs = []
for n, (name, kind, w, h, idx, cols) in enumerate(entries):
    if idx is None: continue
    if kind == 'K_FULL':
        # run-length: varint(run) then index byte, per run
        data = bytearray(); i = 0; L = len(idx)
        while i < L:
            j = i
            while j < L and idx[j] == idx[i]: j += 1
            run = j - i
            while True:
                b7 = run & 0x7f; run >>= 7
                data.append(b7 | (0x80 if run else 0))
                if not run: break
            data.append(idx[i]); i = j
    else:
        data = bytearray((len(idx)+3)//4)
        for k, v in enumerate(idx): data[k>>2] |= v << ((k&3)*2)
    blobs.append(n)
    H.append(f'static const uint8_t irix_px_{n}[{len(data)}] = {{')
    for k in range(0, len(data), 40):
        H.append(''.join(f'{v},' for v in data[k:k+40]))
    H.append('};')
    entries[n] = entries[n] + (len(data),)

H.append('')
H.append('struct IrixEntry { const char* name; int kind; int w, h; const uint8_t* data; unsigned size; int ncolors; uint32_t colors[4]; int extra; const char* blurb; };')
H.append('static const IrixEntry kIrix[] = {')
for n, e in enumerate(entries):
    name, kind, w, h, idx, cols = e[:6]
    size = e[6] if len(e) > 6 else 0
    cs = ','.join('0x%02x%02x%02x' % c for c in cols + [(0,0,0)]*(4-len(cols)))
    dp = f'irix_px_{n}' if idx is not None else 'nullptr'
    ex = 1 if n >= nclassic else 0
    bl = '"' + blurbs[name].replace('"', '\\"') + '"' if name in blurbs else 'nullptr'
    H.append(f'  {{"{name}", {kind}, {w}, {h}, {dp}, {size}, {len(cols)}, {{{cs}}}, {ex}, {bl}}},')
H.append('};')
H.append(f'static const int kIrixCount = {len(entries)};')
open(outp, 'w').write('\n'.join(H) + '\n')
print(len(entries), 'entries;', os.path.getsize(outp)//1024, 'KB')
for e in entries: print(' ', e[0], e[1], e[2], e[3], e[5])

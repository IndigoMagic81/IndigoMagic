import re
def parse_xpm(path):
    txt=open(path,'r',errors='replace').read()
    strs=re.findall(r'"((?:[^"\\]|\\.)*)"',txt)
    w,h,nc,cpp=[int(x) for x in strs[0].split()[:4]]
    keys={}
    for i in range(nc):
        s=strs[1+i]; k=s[:cpp]; rest=s[cpp:]
        m=re.search(r'\bs\s+color(\d)',rest)
        keys[k]=int(m.group(1)) if m else i
    rows=strs[1+nc:1+nc+h]
    idx=bytearray(w*h)
    for y,r in enumerate(rows):
        for x in range(w):
            idx[y*w+x]=keys[r[x*cpp:(x+1)*cpp]]
    return w,h,idx
def parse_xbm(path):
    txt=open(path,'r',errors='replace').read()
    w=int(re.search(r'_width\s+(\d+)',txt).group(1)); h=int(re.search(r'_height\s+(\d+)',txt).group(1))
    body=txt[txt.index('{')+1:txt.rindex('}')]
    vals=[int(v,16) for v in re.findall(r'0x([0-9a-fA-F]+)',body)]
    bpr=(w+7)//8; idx=bytearray(w*h)
    for y in range(h):
        for x in range(w):
            b=vals[y*bpr+x//8]
            idx[y*w+x]=1 if (b>>(x%8))&1 else 0
    return w,h,idx

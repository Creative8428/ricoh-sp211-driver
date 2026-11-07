# SPDX-License-Identifier: GPL-2.0-or-later
# Copyright (C) 2026 Alexandru Hrisca
import re
import struct, subprocess, os, tempfile
J='/opt/homebrew/opt/jbigkit/bin/'
HDR=1796
def read_raster(path):
    d=open(path,'rb').read(); assert d[:4]==b'3SaR'; i=4; pages=[]
    while i<len(d):
        h=bytearray(d[i:i+HDR]); i+=HDR
        w,hgt,bpl=struct.unpack('<I',h[372:376])[0],struct.unpack('<I',h[376:380])[0],struct.unpack('<I',h[392:396])[0]
        pages.append((h,w,hgt,d[i:i+bpl*hgt])); i+=bpl*hgt
    return pages
def write_raster(path,pages):
    with open(path,'wb') as f:
        f.write(b'3SaR')
        for h,px in pages: f.write(h); f.write(px)
def setu(h,off,v): h[off:off+4]=struct.pack('<I',v)
# offsets in header (after magic): 256 + 4*idx
F={n:256+4*i for i,n in enumerate("AdvanceDistance AdvanceMedia Collate CutMedia Duplex HWRes0 HWRes1 BB0 BB1 BB2 BB3 InsertSheet Jog LeadingEdge Margins0 Margins1 ManualFeed MediaPosition MediaWeight MirrorPrint NegativePrint NumCopies Orientation OutputFaceUp PageSize0 PageSize1 Separations TraySwitch Tumble cupsWidth cupsHeight cupsMediaType cupsBitsPerColor cupsBitsPerPixel cupsBytesPerLine cupsColorOrder cupsColorSpace cupsCompression cupsRowCount cupsRowFeed cupsRowStep".split())}
F['cupsInteger0']=452
def blocks(d):
    i=0; out=[]
    while i<len(d):
        m=d[i:i+4]
        if m==b'GDIB':
            h=struct.unpack('>8I',d[i:i+32]); L=h[2]; out.append(('GDIB',h,d[i+32:i+32+L])); i+=32+L
        elif m==b'JIDG': out.append(('JIDG',None,d[i:])); break
        else:
            L=struct.unpack('>I',d[i+4:i+8])[0]; out.append((m.decode(),None,d[i:i+L])); i+=L
    return out
def decode_band(p):
    r=subprocess.run([J+'jbgtopbm'],input=p,capture_output=True,check=True).stdout
    # parse P4
    m=re.match(rb'P4\s+(?:#[^\n]*\n\s*)*(\d+)\s+(\d+)\s',r); return int(m.group(1)),int(m.group(2)),r[m.end():]
F_ORIG='/Library/Printers/Ricoh/Filters/rastertolilo2.app/Contents/MacOS/rastertolilo2'
def run_orig(ras,out,title='title',user='user',copies='1',opts=''):
    env=dict(os.environ,PPD=os.path.abspath('sp211.ppd'))
    with open(out,'wb') as f: r=subprocess.run([F_ORIG,'1',user,title,copies,opts,ras],stdout=f,stderr=subprocess.PIPE,env=env)
    return r.returncode, r.stderr
def page_bits(d):
    pages=[];cur=[]
    for k,hh,p in blocks(d):
        if k=='GDIB':
            bw,bh,bits=decode_band(p); cur.append(bits)
            if hh[1]==2: pages.append((bw,b''.join(cur))); cur=[]
    return pages
import json
_T=json.load(open(os.path.join(os.path.dirname(__file__),'thresh.json')))
TH=[[_T.get(f'{x},{y}',256) for x in range(24)] for y in range(24)]
def model_bits(w,H,px,W):
    bpl=W//8; out=bytearray(bpl*H)
    for y in range(H):
        t=TH[y%24]; row=px[y*w:(y+1)*w]; o=y*bpl
        for x,v in enumerate(row):
            if v and v>=t[x%24]: out[o+(x>>3)]|=0x80>>(x&7)
    return bytes(out)
def pattern_key(bits,bpl,v):
    return bytes(b''.join(bits[y*bpl:y*bpl+72] for y in range(v*24,v*24+24)))
def derive_lut(out,ref_out='levels_default.out'):
    (W,ref),=page_bits(open(ref_out,'rb').read()); (W2,b),=page_bits(open(out,'rb').read()); bpl=W//8
    refmap={}
    for v in range(256): refmap.setdefault(pattern_key(ref,bpl,v),v)
    lut=[]
    for v in range(256):
        k=pattern_key(b,bpl,v); lut.append(refmap.get(k,None))
    return lut

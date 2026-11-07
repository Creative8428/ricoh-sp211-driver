# SPDX-License-Identifier: GPL-2.0-or-later
# Copyright (C) 2026 Alexandru Hrisca
import sys,struct,os
d=open(sys.argv[1],'rb').read(); i=0; n=0; out=sys.argv[2] if len(sys.argv)>2 else None
while i<len(d):
    m=d[i:i+4]
    if m==b'GDIB':
        h=struct.unpack('>8I',d[i:i+32]); L=h[2]; p=d[i+32:i+32+L]
        print(hex(i),'GDIB',[hex(x) for x in h[1:]],'len',L,'BIH',p[:20].hex(),'tail',p[-8:].hex())
        if out: open(f'{out}_{n:03d}.jbg','wb').write(p)
        n+=1; i+=32+L; continue
    if m==b'JIDG': print(hex(i),'JIDG rest',d[i:].hex()); break
    L=struct.unpack('>I',d[i+4:i+8])[0]; print(hex(i),m,L); i+=L

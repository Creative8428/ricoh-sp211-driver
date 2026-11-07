# SPDX-License-Identifier: GPL-2.0-or-later
# Copyright (C) 2026 Alexandru Hrisca
from tools import *
import subprocess,glob,sys
ENC=sys.argv[1]
ok=bad=0; shown=0
for out in ['text.out','gray.out','levels.out','levels1200.out','blank.out']+glob.glob('var/*.out'):
    for k,hh,p in blocks(open(out,'rb').read()):
        if k!='GDIB': continue
        bw,bh,bits=decode_band(p)
        r=subprocess.run([ENC,'-q','-o','3','-p','64','-m','0','-s','256'],input=b'P4\n%d %d\n'%(bw,bh)+bits,capture_output=True).stdout
        pad=r+b'\0'*((-(len(r)-20))%32)
        if pad==p: ok+=1
        else:
            bad+=1
            if shown<6:
                shown+=1; k2=next((i for i in range(min(len(p),len(pad))) if p[i]!=pad[i]),None); print(out,len(p),len(pad),k2,p[k2-4:k2+8].hex() if k2 else '',pad[k2-4:k2+8].hex() if k2 else '')
print('identical',ok,'different',bad)

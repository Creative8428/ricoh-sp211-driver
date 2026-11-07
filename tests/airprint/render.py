#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-2.0-or-later
# Copyright (C) 2026 Alexandru Hrisca
"""Renders a lilo2 stream to PNG previews (one per page, 1/6 scale) using jbgtopbm."""
import struct, subprocess, sys, zlib, re
JBG = '/opt/homebrew/opt/jbigkit/bin/jbgtopbm'

def png(path, w, h, rows):
    raw = b''.join(b'\0' + r for r in rows)
    def ch(t, d): return struct.pack('>I', len(d)) + t + d + struct.pack('>I', zlib.crc32(t + d) & 0xffffffff)
    open(path, 'wb').write(b'\x89PNG\r\n\x1a\n' + ch(b'IHDR', struct.pack('>IIBBBBB', w, h, 8, 0, 0, 0, 0)) + ch(b'IDAT', zlib.compress(raw, 6)) + ch(b'IEND', b''))

d = open(sys.argv[1], 'rb').read(); base = sys.argv[2]; S = 6
i = 0; page = 0; bits = []; W = 0; meta = {}
while i < len(d):
    m = d[i:i+4]
    if m == b'GDIP':
        meta = dict(paper=d[i+8], tray=d[i+9], flags=d[i+33], num=struct.unpack('>H', d[i+34:i+36])[0]); i += 64; bits = []
    elif m == b'GDIB':
        flag, L = struct.unpack('>II', d[i+4:i+12])
        r = subprocess.run([JBG], input=d[i+32:i+32+L], capture_output=True).stdout
        mm = re.match(rb'P4\s+(\d+)\s+(\d+)\s', r); W = int(mm.group(1)); bits.append(r[mm.end():])
        i += 32 + L
        if flag in (2, 3):
            page += 1; data = b''.join(bits); bpl = W // 8; H = len(data) // bpl
            ow, oh = W // S, H // S; rows = []
            for oy in range(oh):
                row = bytearray(ow)
                for y in range(oy * S, oy * S + S):
                    line = data[y * bpl:(y + 1) * bpl]
                    for ox in range(ow):
                        x0 = ox * S
                        cnt = 0
                        for x in range(x0, x0 + S): cnt += (line[x >> 3] >> (7 - (x & 7))) & 1
                        row[ox] += cnt
                rows.append(bytes(255 - min(255, v * 255 // (S * S)) for v in row))
            out = f'{base}-p{page}.png'; png(out, ow, oh, rows)
            print(out, f'{W}x{H}', meta)
    elif m == b'JIDG': break
    else: i += struct.unpack('>I', d[i+4:i+8])[0]

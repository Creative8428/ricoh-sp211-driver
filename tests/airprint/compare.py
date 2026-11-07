#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-2.0-or-later
# Copyright (C) 2026 Alexandru Hrisca
"""Compares two lilo2 streams: image bands (GDIB) must be identical; prints
the differing header bytes (GDIJ/GJET/GDIP), which legitimately differ in
paper/tray codes, title and time."""
import struct, sys

def blocks(d):
    i = 0; out = []
    while i < len(d):
        m = d[i:i+4]
        if m == b'GDIB':
            L = struct.unpack('>I', d[i+8:i+12])[0]; out.append(('GDIB', d[i:i+32+L])); i += 32 + L
        elif m == b'JIDG': out.append(('JIDG', d[i:])); break
        else:
            L = struct.unpack('>I', d[i+4:i+8])[0]; out.append((m.decode('latin-1'), d[i:i+L])); i += L
    return out

a = blocks(open(sys.argv[1], 'rb').read()); b = blocks(open(sys.argv[2], 'rb').read())
ga = [x for k, x in a if k == 'GDIB']; gb = [x for k, x in b if k == 'GDIB']
same = ga == gb
print(f"bands: {len(ga)} vs {len(gb)} -> {'IDENTICAL' if same else 'DIFFERENT'}")
for (ka, xa), (kb, xb) in zip(a, b):
    if ka in ('GDIJ', 'GJET', 'GDIP') and xa != xb:
        print(f"  {ka} differs at", [(hex(i), xa[i], xb[i]) for i in range(min(len(xa), len(xb))) if xa[i] != xb[i]][:12])
sys.exit(0 if same else 1)

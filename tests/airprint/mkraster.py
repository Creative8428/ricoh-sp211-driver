#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-2.0-or-later
# Copyright (C) 2026 Alexandru Hrisca
"""Builds full-page AirPrint test documents (Apple raster / PWG raster, sgray 8-bit,
600 dpi) from a CUPS raster produced for the Ricoh PPD (K 8-bit, imageable area
only). The imageable area is placed at the PPD margin (100 px), so the AirPrint
server must reproduce exactly the bands the CUPS filter produces."""
import gzip, struct, sys

HDR = 1796

def read_cups(path):
    op = gzip.open if path.endswith('.gz') else open
    d = op(path, 'rb').read()
    assert d[:4] == b'3SaR'
    i = 4; pages = []
    while i < len(d):
        h = d[i:i + HDR]; i += HDR
        w, hh, bpl = struct.unpack('<III', h[372:384])[0], struct.unpack('<I', h[376:380])[0], struct.unpack('<I', h[392:396])[0]
        pw, ph = struct.unpack('<II', h[352:360])
        pages.append((w, hh, d[i:i + bpl * hh], pw, ph)); i += bpl * hh
    return pages

def pack_line(line, bpp=1):
    """PackBits variant used by URF/PWG raster (per-pixel, 1 byte pixels)."""
    out = bytearray(); n = len(line); i = 0
    while i < n:
        # trailing white -> 0x80
        if all(b == 0xff for b in line[i:]):
            out.append(0x80); break
        j = i + 1
        while j < n and j - i < 128 and line[j] == line[i]: j += 1
        if j - i >= 2:
            out.append(j - i - 1); out.append(line[i]); i = j; continue
        j = i
        while j < n and j - i < 128 and not (j + 1 < n and line[j + 1] == line[j]): j += 1
        if j - i <= 1:  # single pixel: encode as a run of 1
            out.append(0); out.append(line[i]); i += 1; continue
        out.append(257 - (j - i)); out += line[i:j]; i = j
    return bytes(out)

def full_page(w, h, px, page_w_px, page_h_px, off=100):
    rows = []
    white = b'\xff' * page_w_px
    for y in range(page_h_px):
        sy = y - off
        if 0 <= sy < h:
            src = px[sy * w:(sy + 1) * w]
            g = bytes(255 - v for v in src)
            row = b'\xff' * off + g + b'\xff' * (page_w_px - off - w)
            rows.append(row[:page_w_px])
        else:
            rows.append(white)
    return rows

def encode_rows(rows):
    out = bytearray(); i = 0
    while i < len(rows):
        j = i + 1
        while j < len(rows) and j - i < 256 and rows[j] == rows[i]: j += 1
        out.append(j - i - 1); out += pack_line(rows[i]); i = j
    return bytes(out)

def urf(pages, dpi=600, duplex=1):
    out = bytearray(b'UNIRAST\0' + struct.pack('>I', len(pages)))
    for rows, W, H in pages:
        hdr = bytes([8, 0, duplex, 4]) + b'\0' * 8 + struct.pack('>III', W, H, dpi) + b'\0' * 8
        out += hdr + encode_rows(rows)
    return bytes(out)

def pwg(pages, dpi=600, ptsize=None):
    out = bytearray(b'RaS2')
    for (rows, W, H), (pw, ph) in zip(pages, ptsize):
        h = bytearray(HDR)
        h[0:6] = b'PwgRaster'[:6]; h[0:9] = b'PwgRaster'
        struct.pack_into('>II', h, 276, dpi, dpi)
        struct.pack_into('>II', h, 352, pw, ph)
        struct.pack_into('>I', h, 340, 1)
        struct.pack_into('>II', h, 372, W, H)
        struct.pack_into('>IIIIII', h, 384, 8, 8, W, 0, 18, 0)
        out += h + encode_rows(rows)
    return bytes(out)

if __name__ == '__main__':
    src, base = sys.argv[1], sys.argv[2]
    page_w_px, page_h_px = int(sys.argv[3]), int(sys.argv[4])
    pages = []; sizes = []
    for (w, h, px, pw, ph) in read_cups(src):
        pages.append((full_page(w, h, px, page_w_px, page_h_px), page_w_px, page_h_px)); sizes.append((pw, ph))
    open(base + '.urf', 'wb').write(urf(pages))
    open(base + '.pwg', 'wb').write(pwg(pages, ptsize=sizes))
    print(base, len(pages), 'page(s)')

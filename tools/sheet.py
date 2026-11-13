#!/usr/bin/env python3
"""Tile shot.py captures into one contact sheet.

  sheet.py out.png COLS a.png b.png ...

Inputs: the RGB 8-bit PNGs shot.py writes, all the same size. White 4 px gaps.
See docs/SMALL-BOARDS-DEV.md.
"""
import struct, sys, zlib

def rd(p):
    d = open(p, 'rb').read(); i = 8; idat = b''
    while i < len(d):
        n = struct.unpack('>I', d[i:i+4])[0]; t = d[i+4:i+8]
        if t == b'IHDR': w, h = struct.unpack('>II', d[i+8:i+16])
        if t == b'IDAT': idat += d[i+8:i+8+n]
        i += 12 + n
    r = zlib.decompress(idat)
    return w, h, [r[y*(w*3+1)+1:(y+1)*(w*3+1)] for y in range(h)]

out, cols, files = sys.argv[1], int(sys.argv[2]), sys.argv[3:]
ims = [rd(f) for f in files]; w, h = ims[0][0], ims[0][1]; gap = 4
lines = []
for R in range((len(ims) + cols - 1) // cols):
    for y in range(h):
        line = bytearray()
        for C in range(cols):
            k = R * cols + C
            line += ims[k][2][y] if k < len(ims) else bytes(w * 3)
            line += b'\xff' * 3 * gap
        lines.append(bytes(line))
    lines += [b'\xff' * len(lines[-1])] * gap
W, H = len(lines[0]) // 3, len(lines)
def ch(t, d): c = t + d; return struct.pack('>I', len(d)) + c + struct.pack('>I', zlib.crc32(c) & 0xffffffff)
open(out, 'wb').write(b'\x89PNG\r\n\x1a\n' + ch(b'IHDR', struct.pack('>IIBBBBB', W, H, 8, 2, 0, 0, 0)) +
                      ch(b'IDAT', zlib.compress(b''.join(b'\0' + l for l in lines))) + ch(b'IEND', b''))

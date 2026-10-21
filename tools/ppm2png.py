#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-only
"""Turn the host run's PPM screens into PNG (scaled 2x), with only the standard library."""
import struct, sys, zlib
from pathlib import Path

def png(path, w, h, rgb, scale=2):
    rows = bytearray()
    for y in range(h):
        row = bytearray()
        for x in range(w):
            px = rgb[(y * w + x) * 3:(y * w + x) * 3 + 3]
            row += px * scale
        for _ in range(scale):
            rows += b"\x00" + row
    def chunk(t, d):
        return struct.pack(">I", len(d)) + t + d + struct.pack(">I", zlib.crc32(t + d) & 0xFFFFFFFF)
    out = b"\x89PNG\r\n\x1a\n" + chunk(b"IHDR", struct.pack(">IIBBBBB", w * scale, h * scale, 8, 2, 0, 0, 0))
    out += chunk(b"IDAT", zlib.compress(bytes(rows), 9)) + chunk(b"IEND", b"")
    Path(path).write_bytes(out)

d = Path(sys.argv[1] if len(sys.argv) > 1 else "build/host")
for p in sorted(d.glob("*.ppm")):
    data = p.read_bytes()
    head, _, body = data.partition(b"\n")
    _, w, h, _ = head.split()
    png(p.with_suffix(".png"), int(w), int(h), body)
    p.unlink()
    print("screen ->", p.with_suffix(".png"))

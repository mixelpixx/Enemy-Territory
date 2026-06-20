#!/usr/bin/env python3
"""Generate a q3map2-valid placeholder terrain alphamap PCX.

Oasis's dunes are an old-style `"terrain" "1"` entity whose 8-layer texture blend is
driven by `mp_siwa_thomasc.pcx` -- a Splash Damage *source-only* file that never
shipped in the retail paks. Without it q3map2 aborts compiling the terrain. This emits
a stand-in: all pixels = index 0, which selects terrain layer 0 (`sand_wave_desert`,
the base sand). The dune *geometry* and lighting are unaffected; only the multi-layer
blend (grass patches, etc.) is replaced by uniform base sand.

q3map2 is picky about PCX: it must be v5 / 8-bit / RLE with a 256-colour VGA-palette
trailer, AND it inherits id's classic LoadPCX size cap (xmax>=640 || ymax>=480 ->
"Bad pcx file"), so keep dimensions <= 512. q3map2 reads the alphamap from its VFS, so
place the output under your -fs_homepath's etmain/ (and etmain/maps/).

Usage:  python make_terrain_alphamap.py <out.pcx> [more_out.pcx ...]
"""
import struct
import sys


def write_pcx(path, w=256, h=256, index=0):
    hdr = bytearray(128)
    hdr[0] = 0x0A; hdr[1] = 5; hdr[2] = 1; hdr[3] = 8          # manuf, v5, RLE, 8bpp
    struct.pack_into('<HHHH', hdr, 4, 0, 0, w - 1, h - 1)      # window (< 640x480)
    struct.pack_into('<HH', hdr, 12, 72, 72)                  # dpi
    hdr[65] = 1                                                # nplanes
    bpl = w if w % 2 == 0 else w + 1
    struct.pack_into('<H', hdr, 66, bpl)                      # bytesPerLine (even)
    struct.pack_into('<H', hdr, 68, 1)                        # palette info
    line = bytearray(); n = bpl
    while n > 0:                                              # RLE: runs of <=63
        c = min(n, 63); line += bytes((0xC0 | c, index)); n -= c
    pal = bytes((0x0C,)) + bytes(sum(([i, i, i] for i in range(256)), []))
    open(path, 'wb').write(bytes(hdr) + bytes(line) * h + pal)


if __name__ == "__main__":
    outs = sys.argv[1:] or ["mp_siwa_thomasc.pcx"]
    for p in outs:
        write_pcx(p)
        print("wrote", p)

#!/usr/bin/env python3
"""Wrap an ARM zImage in the MTK kernel-partition header.

NOT normally needed: device/generalmobile/hulkbuster/mtkbootimg (mkbootimg.c,
load_mtk_file()) inspects the first 4 bytes of the kernel file and, when they
are not MTK_MAGIC, prepends the header itself. So passing a plain zImage
through mka bacon produces the correct partition layout.

This script exists for the cases where the header has to be applied outside
the ROM build (e.g. dropping a kernel into boot.img by hand, or producing a
standalone kernel partition image for fastboot).

Layout (mtkbootimg/bootimg.h):

    struct { unsigned int magic; unsigned int size; char name[32]; } info;
    padded out to a 512 byte union

The magic is 0x58881688 (not a timestamp), size is the payload length, and
the name is the "KERNEL" tag the bootloader looks for.

Usage:
    mk_mtk_kernel.py <zImage> <output>
"""

import struct
import sys

MTK_MAGIC = 0x58881688
HEADER_FMT = "<II8s"   # magic, size, "KERNEL" tag
HEADER_SIZE = 512      # union padding


def wrap(zimage: bytes) -> bytes:
    header = struct.pack(HEADER_FMT, MTK_MAGIC, len(zimage), b"KERNEL\0\0")
    out = header + zimage
    out += b"\xFF" * (HEADER_SIZE - len(header))   # mtkbootimg pads with 0xFF
    return out + b"\0" * ((-len(out)) % HEADER_SIZE)


def main() -> int:
    if len(sys.argv) != 3:
        print(__doc__)
        return 2
    src, dst = sys.argv[1], sys.argv[2]
    with open(src, "rb") as f:
        zimage = f.read()
    blob = wrap(zimage)
    with open(dst, "wb") as f:
        f.write(blob)
    magic, size, name = struct.unpack(HEADER_FMT, blob[:16])
    print("%s: %d bytes (payload %d)" % (dst, len(blob), size))
    print("  magic=0x%08x (expected 0x%08x)  name=%r" % (magic, MTK_MAGIC, name))
    assert magic == MTK_MAGIC, "bad magic"
    assert size == len(zimage), "size mismatch"
    return 0


if __name__ == "__main__":
    sys.exit(main())
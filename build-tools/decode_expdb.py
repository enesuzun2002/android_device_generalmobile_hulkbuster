#!/usr/bin/env python3
"""Decode an MTK AEE/expdb panic dump into readable records.

The dumps live on the "expdb" partition, which on this device is not in the
partition table at all - it is an LK-only region exposed as a character
device. From TWRP:

    adb shell "dd if=/dev/expdb of=/sdcard/expdb.bin bs=1024 count=4096"
    adb pull /sdcard/expdb.bin
    ./decode_expdb.py expdb.bin

Every record body is obfuscated with a 32-bit XOR against ipanic_iv
(0xaabbccdd, drivers/misc/mediatek/aee/ipanic/ipanic_rom.c:15 and :34-41),
applied per u32 word. The header itself is stored in the clear.

Layout (drivers/misc/mediatek/aee/ipanic/ipanic.h):

    struct ipanic_header {          /* 0x00 */
        u32 magic;                  /* 0xAEE0DEAD          */
        u32 version;
        u32 size;
        u32 datas;                  /* bitmap of valid records */
        u32 dhblk;
        u32 blksize;
        u32 partsize;
        u32 bufsize;
        u64 buf;
        struct ipanic_data_header data_hdr[IPANIC_NR_SECTIONS];  /* 0x28 */
    };

    struct ipanic_data_header {     /* 64 bytes each */
        u32 type, valid, offset, used, total, encrypt;
        u64 id;
        char name[32];
    };

Anything with valid=1 and used>0 is extracted and de-obfuscated. Records
worth reading first:

    SYS_KERNEL_LOG    the kernel ring buffer, in full (the important one)
    SYS_WDT_LOG       per-CPU register/stack dump from the FIQ watchdog
    _exp_detail.txt   the oops detail text
    PROC_CUR_TSK      the task that was running when it died
"""

import struct
import sys

IPANIC_MAGIC = 0xAEE0DEAD
IPANIC_IV = 0xAABBCCDD
HEADER_FIXED = 0x28
ENTRY_SIZE = 0x40  # struct ipanic_data_header


def unscramble(buf, iv=IPANIC_IV):
    """Reverse ipanic_block_scramble(): XOR each u32 with iv."""
    out = bytearray(buf)
    for i in range(0, len(out) - 3, 4):
        word = struct.unpack_from("<I", out, i)[0] ^ iv
        struct.pack_into("<I", out, i, word)
    return bytes(out)


def parse(blob):
    if len(blob) < HEADER_FIXED:
        raise SystemExit("file is too small to be an expdb dump")

    magic, version, size, datas, dhblk, blksize, partsize, bufsize = struct.unpack_from(
        "<8I", blob, 0
    )
    if magic != IPANIC_MAGIC:
        raise SystemExit("bad magic 0x%08x, expected 0x%08x" % (magic, IPANIC_MAGIC))

    print("magic=0x%08x version=%d hdrsize=%d datas=0x%x dhblk=%d blksize=%d "
          "partsize=0x%x bufsize=%d" % (magic, version, size, datas, dhblk,
                                       blksize, partsize, bufsize))

    records = []
    index = 0
    while True:
        base = HEADER_FIXED + ENTRY_SIZE * index
        if base + ENTRY_SIZE > len(blob):
            break
        dtype, valid, offset, used, total, encrypt = struct.unpack_from(
            "<6I", blob, base
        )
        name = blob[base + 32:base + 64].split(b"\x00")[0].decode("ascii", "replace")
        if not name:
            index += 1
            continue
        if valid and used and offset + used <= len(blob):
            body = blob[offset:offset + used]
            if encrypt:
                body = unscramble(body)
            records.append((name, dtype, total, body))
        index += 1
    return records


def main():
    if len(sys.argv) < 2:
        raise SystemExit(__doc__)

    with open(sys.argv[1], "rb") as handle:
        blob = handle.read()

    records = parse(blob)
    if not records:
        print("\nno valid records - the dump is empty or the kernel never panicked")
        return

    outdir = "."
    if len(sys.argv) > 2:
        outdir = sys.argv[2]

    for name, dtype, total, body in records:
        safe = name.replace("/", "_")
        if safe.endswith(".txt"):
            safe = safe[:-4]
        path = "%s/rec_%s.txt" % (outdir.rstrip("/"), safe)
        with open(path, "wb") as handle:
            handle.write(body)

        printable = sum(1 for c in body if 32 <= c < 127 or c in (9, 10, 13))
        ratio = 100.0 * printable / len(body) if body else 0.0
        print("  %-20s type=%-3d total=%-10d %7d bytes  %3.0f%% printable  -> %s"
              % (name, dtype, total, len(body), ratio, path))


if __name__ == "__main__":
    main()
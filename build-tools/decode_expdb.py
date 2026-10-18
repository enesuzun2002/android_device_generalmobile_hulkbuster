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

The periodic log sink (drivers/misc/mediatek/aee/klog_sink.c) also writes
snapshots of the ring buffer into this partition, past the end of the ipanic
record table, so that a *hang* - which never reaches the panic path - is still
capturable. Those are not part of the ipanic header; pass --klog to decode
them:

    adb shell "dd if=/dev/expdb of=/sdcard/expdb.bin bs=1024 count=10240"
    ./decode_expdb.py expdb.bin --klog

Each snapshot starts with a 32 byte header (little endian):

    u32 magic     'KLG1' = 0x31474C4B
    u32 kind      0 tail, 1 full, 2 panic, 3 restart
    u32 seq       increments with every write
    u32 len       payload bytes, excluding padding
    u64 uptime_ms
    u32 write_ok  set after the write returns (so it reads 0 if torn)
    u32 reserved

followed by `len` bytes of printk text, padded to a 512 byte boundary. The
most recent tail snapshot always wins, and kind 2/3 means the kernel died
during that write.
"""

import os
import struct
import sys

IPANIC_MAGIC = 0xAEE0DEAD
IPANIC_IV = 0xAABBCCDD
HEADER_FIXED = 0x28
ENTRY_SIZE = 0x40  # struct ipanic_data_header

# Offsets used by klog_sink.c, in the regions measured to be erased in the
# 0xa00000 byte expdb partition. Note 0x700000 is NOT free: it holds MTK's
# RAM console (signature "DBGC", dram_console.c), so the tail lives below it.
KLOG_MAGIC = 0x31474C4B
KLOG_HDR_SIZE = 0x20
KLOG_LOCATIONS = ((0x660000, "tail"), (0x800000, "full"))


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
    # The record array is a fixed size recorded in the header (hdrsize);
    # walking past it finds whatever follows in the partition and decodes
    # it as if it were a record.
    if size > HEADER_FIXED:
        n_sections = (size - HEADER_FIXED) // ENTRY_SIZE
    else:
        n_sections = len(blob) // ENTRY_SIZE
    while True:
        base = HEADER_FIXED + ENTRY_SIZE * index
        if base + ENTRY_SIZE > len(blob) or index >= n_sections:
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


def parse_klog(blob):
    """Decode the periodic log sink snapshots (see klog_sink.c)."""
    found = []
    for offset, label in KLOG_LOCATIONS:
        if offset + KLOG_HDR_SIZE > len(blob):
            continue
        magic, kind, seq, length, uptime_ms, write_ok, _ = struct.unpack_from(
            "<4I Q 2I", blob, offset
        )
        if magic != KLOG_MAGIC:
            continue
        if length == 0 or offset + KLOG_HDR_SIZE + length > len(blob):
            continue
        body = blob[offset + KLOG_HDR_SIZE:offset + KLOG_HDR_SIZE + length]
        found.append((label, kind, seq, uptime_ms, write_ok, body))
    return found


KIND_NAMES = {0: "tail", 1: "full", 2: "PANIC", 3: "restart"}


def main():
    args = [a for a in sys.argv[1:] if a != "--klog"]
    want_klog = "--klog" in sys.argv
    if not args:
        raise SystemExit(__doc__)

    with open(args[0], "rb") as handle:
        blob = handle.read()

    outdir = args[1] if len(args) > 1 else "."
    if not os.path.isdir(outdir):
        os.makedirs(outdir)

    records = parse(blob)
    if records:
        for name, dtype, total, body in records:
            safe = name.replace("/", "_")
            if safe.endswith(".txt"):
                safe = safe[:-4]

            printable = sum(1 for c in body if 32 <= c < 127 or c in (9, 10, 13))
            ratio = 100.0 * printable / len(body) if body else 0.0

            # Not every record is text. SYS_MINI_RDUMP is a raw register
            # dump and PROC_CUR_TSK is a packed struct, so writing them out
            # as .txt and eyeballing "0% printable" is misleading. Give
            # them a .bin extension instead.
            binary = ratio < 60.0
            ext = "bin" if binary else "txt"
            path = "%s/rec_%s.%s" % (outdir.rstrip("/"), safe, ext)
            with open(path, "wb") as handle:
                handle.write(body)

            print("  %-20s type=%-3d total=%-10d %7d bytes  %3.0f%% printable "
                  "%-6s -> %s"
                  % (name, dtype, total, len(body), ratio,
                     "(binary)" if binary else "(text)", path))
    else:
        print("\nno ipanic records - the kernel did not panic since the last erase")

    if want_klog:
        snapshots = parse_klog(blob)
        if not snapshots:
            print("\nno log sink snapshots (needs a kernel built with "
                  "CONFIG_MTK_KLOG_SINK, and the dump read to the end of the "
                  "partition, i.e. count=10240)")
        for label, kind, seq, uptime_ms, write_ok, body in snapshots:
            path = "%s/klog_%s.txt" % (outdir.rstrip("/"), label)
            with open(path, "wb") as handle:
                handle.write(body)
            print("  %-20s %-8s seq=%-5d uptime=%8.3fs len=%-8d %s -> %s"
                  % ("KLOG_" + label, KIND_NAMES.get(kind, str(kind)), seq,
                     uptime_ms / 1000.0, len(body),
                     "torn" if not write_ok else "ok", path))


if __name__ == "__main__":
    main()
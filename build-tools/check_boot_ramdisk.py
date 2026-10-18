#!/usr/bin/env python
"""Prove what is actually inside the ramdisk of the built boot.img.

The ramdisk is $(MKBOOTFS) $(TARGET_ROOT_OUT) | minigzip, i.e. a gzip stream
wrapping an uncompressed cpio. So: find the gzip member, inflate it, and search
the raw cpio bytes for the strings we care about. No cpio tool needed."""
import sys, zlib

BOOT = "/workspace/los-14.1/out/target/product/hulkbuster/boot.img"

blob = open(BOOT, "rb").read()
print("boot.img: %d bytes" % len(blob))

# Collect every gzip member we can inflate.
payload = b""
i = 0
members = 0
while True:
    i = blob.find(b"\x1f\x8b\x08", i)
    if i < 0:
        break
    d = zlib.decompressobj(31)
    try:
        out = d.decompress(blob[i:])
        out += d.flush()
    except Exception:
        out = b""
    if out:
        payload += out
        members += 1
    i += 3

print("gzip members inflated: %d   ramdisk payload: %d bytes" % (members, len(payload)))

checks = [
    (b"ro.adb.secure=0", "adb authorisation disabled (needed for adb shell)"),
    (b"ro.adb.secure=1", "adb authorisation ENABLED (would block adb shell)"),
    (b"start mtkdiag_watch", "mtkdiag watch service is started"),
    (b"sh /mtkdiag.sh watch", "mtkdiag watch service defined"),
    (b"fstab.mt6592", "fstab present"),
    # 3.10.72 Mali gralloc opens /dev/ion O_RDWR in every GPU client
    # (bootanim, SystemUI/ImageWallpaper, hwui). A restrictive mode here
    # causes "ion_open failed with Permission denied" -> gralloc register
    # -13 -> eglMakeCurrent EGL_BAD_ALLOC (no bootanim, SystemUI crash
    # loop). Reference ROM uses "chmod 0666 /dev/ion".
    (b"chmod 0666 /dev/ion", "ion world-rw (gralloc client open must not EACCES)"),
    (b"chmod 0644 /dev/ion", "ion 0644 (breaks every GPU client)"),
    (b"/dev/ion                   0666    root       root", "ion ueventd fallback entry"),
]

print()
bad = 0
for needle, desc in checks:
    n = payload.count(needle)
    print("  %-48s x%-3d  %s" % (needle.decode(), n, desc))
    if needle == b"ro.adb.secure=0" and n < 1:
        bad += 1
    if needle == b"ro.adb.secure=1" and n != 0:
        bad += 1
    if needle in (b"start mtkdiag_watch", b"sh /mtkdiag.sh watch") and n < 1:
        bad += 1
    if needle == b"chmod 0666 /dev/ion" and n < 1:
        bad += 1
        print("        FAIL: init.mt6592.rc must chmod /dev/ion to 0666")
    if needle == b"chmod 0644 /dev/ion" and n != 0:
        bad += 1
        print("        FAIL: the 0644 /dev/ion regression is back")
    if needle == b"/dev/ion                   0666    root       root" and n < 1:
        bad += 1
        print("        FAIL: ueventd.mt6592.rc must cover /dev/ion 0666")

print()
print("RESULT: %s" % ("OK" if bad == 0 else "PROBLEM (%d)" % bad))
sys.exit(1 if bad else 0)

#!/usr/bin/env python
"""End-to-end check: compare every file the OTA will flash against its source in
vendor/generalmobile/hulkbuster/proprietary/. This validates what actually gets
installed, not just the intermediate out/ tree."""
import os, sys, zipfile

ZIP = "/workspace/los-14.1/out/target/product/hulkbuster/lineage-14.1-20261018-UNOFFICIAL-hulkbuster.zip"
PROP = "/workspace/los-14.1/vendor/generalmobile/hulkbuster/proprietary"

# proprietary/<sub> -> system/<sub>, exactly as vendor.mk's find-copy-subdir-files does
subs = ["bin", "etc", "lib", "vendor", "xbin"]

z = zipfile.ZipFile(ZIP)
names = set(z.namelist())

verified = mismatched = missing = 0
problems = []
for sub in subs:
    root = os.path.join(PROP, sub)
    if not os.path.isdir(root):
        continue
    for dirpath, _, files in os.walk(root):
        for fn in files:
            src = os.path.join(dirpath, fn)
            rel = os.path.relpath(src, PROP)            # e.g. lib/hw/hwcomposer.mt6592.so
            entry = "system/" + rel.replace(os.sep, "/")
            if entry not in names:
                missing += 1
                problems.append("MISSING IN OTA   %s" % entry)
                continue
            with z.open(entry) as fh:
                data = fh.read()
            with open(src, "rb") as fh:
                want = fh.read()
            if data == want:
                verified += 1
            else:
                mismatched += 1
                problems.append("DIFFERS          %-50s ota=%-9d src=%-9d" % (entry, len(data), len(want)))

print("=== OTA vs proprietary/ (%s) ===" % os.path.basename(ZIP))
print("  identical : %d" % verified)
print("  differing : %d" % mismatched)
print("  missing   : %d" % missing)
print()
for p in problems:
    print("  " + p)

# spot-check the specific libs this change was about
print()
print("=== the libraries this change targeted ===")
targets = [
    ("system/lib/hw/hwcomposer.mt6592.so", 120228),
    ("system/lib/libperfservicenative.so", 9328),
    ("system/lib/libdpframework.so", 296296),
    ("system/lib/libbwc.so", 17568),
    ("system/lib/libgralloc_extra.so", 22064),
    ("system/lib/libm4u.so", 13420),
    ("system/lib/libion.so", 13820),
    ("system/lib/libion_mtk.so", 17916),
    ("system/lib/libMali.so", 1095140),
    ("system/lib/libxlog.so", 26264),
]
bad = 0
for entry, want in targets:
    got = z.getinfo(entry).file_size if entry in names else None
    ok = got == want
    bad += 0 if ok else 1
    print("  %-42s %-9s %s" % (entry, got, "ok" if ok else "EXPECTED %d" % want))

sys.exit(1 if (mismatched or missing or bad) else 0)

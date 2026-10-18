#!/usr/bin/env python
"""Check that the /system library closure for hwcomposer.mt6592.so is complete,
after overlaying vendor/generalmobile/hulkbuster/proprietary/lib on top of the
built out/ tree (that overlay is what PRODUCT_COPY_FILES installs)."""
import os, glob, subprocess, sys, collections

OUT = "/workspace/los-14.1/out/target/product/hulkbuster/system"
PROP = "/workspace/los-14.1/vendor/generalmobile/hulkbuster/proprietary"

# order matters: proprietary wins (copied last)
dirs = [
    (OUT + "/lib",                 "system/lib"),
    (OUT + "/lib/hw",              "system/lib/hw"),
    (OUT + "/vendor/lib",          "system/vendor/lib"),
    (OUT + "/vendor/lib/hw",       "system/vendor/lib/hw"),
    (PROP + "/lib",                "system/lib"),
    (PROP + "/lib/hw",             "system/lib/hw"),
    (PROP + "/vendor/lib",         "system/vendor/lib"),
    (PROP + "/vendor/lib/hw",      "system/vendor/lib/hw"),
]

# basename -> (realpath, install_path)
avail = {}
for d, inst in dirs:
    for f in sorted(glob.glob(d + "/*.so")):
        avail[os.path.basename(f)] = (f, inst + "/" + os.path.basename(f))

# extra non-.so names that may be dlopen'd (not needed for NEEDED closure)
def needed(path):
    out = subprocess.run(["readelf", "-d", path], stdout=subprocess.PIPE, stderr=subprocess.DEVNULL).stdout.decode("utf-8","replace")
    libs = []
    for line in out.splitlines():
        if "(NEEDED)" in line and "[" in line:
            libs.append(line.split("[")[1].split("]")[0])
    return libs

def soname(path):
    out = subprocess.run(["readelf", "-d", path], stdout=subprocess.PIPE, stderr=subprocess.DEVNULL).stdout.decode("utf-8","replace")
    for line in out.splitlines():
        if "(SONAME)" in line and "[" in line:
            return line.split("[")[1].split("]")[0]
    return None

# index by SONAME too, so DT_NEEDED that names a SONAME resolves
by_soname = {}
for name, (path, inst) in list(avail.items()):
    sn = soname(path)
    if sn and sn not in avail:
        by_soname[sn] = (path, inst)

def resolve(name):
    if name in avail:
        return avail[name]
    if name in by_soname:
        return by_soname[name]
    return None

roots = ["hwcomposer.mt6592.so"]

seen, missing, order = set(), [], []
queue = list(roots)
while queue:
    cur = queue.pop(0)
    if cur in seen:
        continue
    seen.add(cur)
    hit = resolve(cur)
    if hit is None:
        missing.append(cur)
        continue
    path, inst = hit
    order.append((inst, os.path.getsize(path)))
    for dep in needed(path):
        if dep not in seen:
            queue.append(dep)

print("=== transitive NEEDED closure of %s ===" % ", ".join(roots))
for inst, size in sorted(order):
    print("  %-45s %8d" % (inst, size))
print()
print("total libs in closure: %d" % len(order))
if missing:
    print()
    print("=== UNRESOLVED DT_NEEDED ===")
    for m in missing:
        print("  %s" % m)
    sys.exit(2)
print()
print("OK: every DT_NEEDED in the closure resolves against the installed /system.")

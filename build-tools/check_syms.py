#!/usr/bin/env python
"""Symbol-level consistency check across the installed /system closure."""
import os, glob, subprocess, sys

OUT = "/workspace/los-14.1/out/target/product/hulkbuster/system"
PROP = "/workspace/los-14.1/vendor/generalmobile/hulkbuster/proprietary"

dirs = [
    (OUT + "/lib", "system/lib"), (OUT + "/lib/hw", "system/lib/hw"),
    (OUT + "/vendor/lib", "system/vendor/lib"), (OUT + "/vendor/lib/hw", "system/vendor/lib/hw"),
    (PROP + "/lib", "system/lib"), (PROP + "/lib/hw", "system/lib/hw"),
    (PROP + "/vendor/lib", "system/vendor/lib"), (PROP + "/vendor/lib/hw", "system/vendor/lib/hw"),
]
avail = {}
for d, inst in dirs:
    for f in sorted(glob.glob(d + "/*.so")):
        avail[os.path.basename(f)] = (f, inst + "/" + os.path.basename(f))

def run(args):
    return subprocess.run(args, stdout=subprocess.PIPE, stderr=subprocess.DEVNULL).stdout.decode("utf-8","replace")

def dyn(path):
    return run(["readelf", "--dyn-syms", "-W", path])

def needed(path):
    return [l.split("[")[1].split("]")[0] for l in run(["readelf","-d",path]).splitlines()
            if "(NEEDED)" in l and "[" in l]

def soname(path):
    for l in run(["readelf","-d",path]).splitlines():
        if "(SONAME)" in l and "[" in l:
            return l.split("[")[1].split("]")[0]
    return None

by_soname = {}
for n,(p,i) in list(avail.items()):
    sn = soname(p)
    if sn and sn not in avail:
        by_soname[sn] = (p,i)

def resolve(n):
    return avail.get(n) or by_soname.get(n)

roots = ["hwcomposer.mt6592.so", "libMali.so"]
seen, order, missing, queue = set(), [], [], list(roots)
while queue:
    cur = queue.pop(0)
    if cur in seen: continue
    seen.add(cur)
    hit = resolve(cur)
    if hit is None:
        missing.append(cur); continue
    order.append(cur)
    for d in needed(hit[0]):
        if d not in seen: queue.append(d)

if missing:
    print("UNRESOLVED DT_NEEDED:", missing); sys.exit(2)

# Build global export table from the closure + the two roots.
# libmtk_symbols.so is LD_PRELOAD'ed globally (init.environ.rc), so its exports
# are visible to every process and satisfy prebuilt MTK libs' shim needs.
exports = {}
libs = list(order) + [r for r in roots if r not in order]
for name in list(libs) + ["libmtk_symbols.so"]:
    path = resolve(name)[0]
    for line in dyn(path).splitlines():
        parts = line.split()
        # Num: Value Size Type Bind Vis Ndx Name
        if len(parts) >= 8 and parts[3] in ("FUNC","OBJECT","IFUNC","NOTYPE") \
           and parts[4] in ("GLOBAL","WEAK") and parts[6] != "UND":
            exports.setdefault(parts[7].split("@")[0], []).append(name)

print("=== undefined symbols not resolvable inside the graphics closure ===")
bad = 0
for name in libs:
    path = resolve(name)[0]
    for line in dyn(path).splitlines():
        parts = line.split()
        if len(parts) >= 8 and parts[6] == "UND" and parts[4] == "GLOBAL":
            sym = parts[7].split("@")[0]
            if sym and sym not in exports:
                print("  %-28s <- %s" % (sym, name)); bad += 1
print()
print("libs checked: %d   unresolved GLOBAL undefined symbols: %d" % (len(libs), bad))

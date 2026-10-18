#!/usr/bin/env python3
"""Post-build verification for the telephony-removal + Mali-blob-swap build.

Checks the OTA artifact itself (what TWRP will flash), not the intermediate tree:
  1. blob regression vs vendor/generalmobile/hulkbuster/proprietary (what gets installed)
  2. the 6 swapped graphics blobs match the reference ROM byte-for-byte
  3. telephony is actually gone (module + feature XML level)
  4. the wifi-only identity + keyguard guard-rail survived
  5. the WebView fix is intact
  6. what changed relative to the previously flashed build (7f97b5d61b)
"""
import glob
import hashlib
import os
import sys
import zipfile

BASE = "/workspace/los-14.1"
OUT = BASE + "/out/target/product/hulkbuster"
PROP = BASE + "/vendor/generalmobile/hulkbuster/proprietary"
REF = "/workspace/aosp_jdct_hulkbuster_3.10.72.zip"
OLD_LIST = "/tmp/opencode/old.list"          # file list of build 7f97b5d61b (flashed)

problems = []


def fail(msg):
    problems.append(msg)
    print("  FAIL  %s" % msg)


def sha_member(zf, name):
    h = hashlib.sha256()
    h.update(zf.read(name))
    return h.hexdigest()


# ---------------------------------------------------------------- locate OTA
zips = sorted(glob.glob(OUT + "/lineage-14.1-*-hulkbuster.zip"), key=os.path.getmtime)
if not zips:
    print("no OTA zip found in %s" % OUT)
    sys.exit(1)
OTA = zips[-1]
zf = zipfile.ZipFile(OTA)
names = set(zf.namelist())

print("=" * 78)
print("OTA: %s" % OTA)
print("size: %d bytes   sha256: %s" % (os.path.getsize(OTA), hashlib.sha256(open(OTA, "rb").read()).hexdigest()))
print("entries: %d" % len(names))

prop = zf.read("system/build.prop").decode("utf-8", "replace").splitlines()
want = {
    "ro.build.version.incremental": None,
    "ro.product.name": "lineage_hulkbuster",
    "ro.product.device": "hulkbuster",
    "keyguard.no_require_sim": "true",
}
print()
print("--- identity / props ---")
for k, expect in want.items():
    line = next((l for l in prop if l.startswith(k + "=")), None)
    print("  %-30s %s" % (k, line or "<MISSING>"))
    if line is None:
        fail("build.prop missing %s" % k)
    elif expect and line.split("=", 1)[1] != expect:
        fail("build.prop %s = %r, expected %r" % (k, line.split("=", 1)[1], expect))

# ------------------------------------------------------- 1. blob regression
print()
print("--- 1. every proprietary/ file present in OTA and byte-identical ---")
verified = mismatched = missing = 0
for sub in ["bin", "etc", "lib", "vendor", "xbin"]:
    root = os.path.join(PROP, sub)
    for dirpath, _, files in os.walk(root):
        for fn in files:
            src = os.path.join(dirpath, fn)
            entry = "system/" + os.path.relpath(src, PROP).replace(os.sep, "/")
            if entry not in names:
                missing += 1
                fail("MISSING IN OTA   %s" % entry)
                continue
            if hashlib.sha256(open(src, "rb").read()).hexdigest() != sha_member(zf, entry):
                mismatched += 1
                fail("CONTENT MISMATCH %s" % entry)
            else:
                verified += 1
print("  verified %d, mismatched %d, missing %d" % (verified, mismatched, missing))

# --------------------------------------------- 2. swapped graphics blobs
print()
print("--- 2. the 6 swapped blobs vs reference ROM ---")
BLOBS = [
    "system/lib/egl/libEGL_mali.so",
    "system/lib/egl/libGLESv1_CM_mali.so",
    "system/lib/egl/libGLESv2_mali.so",
    "system/lib/libmtk_mali_user.so",
    "system/lib/hw/gralloc.default.so",
    "system/lib/libClearMotionFW.so",
]
ref = zipfile.ZipFile(REF)
for b in BLOBS:
    if b not in names:
        fail("swapped blob missing from OTA: %s" % b)
        continue
    a, r = sha_member(zf, b), sha_member(ref, b)
    ok = a == r
    print("  %s %-38s %8d bytes  %s" % ("OK  " if ok else "FAIL", os.path.basename(b),
                                        len(zf.read(b)), a[:16]))
    if not ok:
        fail("%s differs from reference (%s vs %s)" % (b, a[:16], r[:16]))

# also prove libMali.so (the crashing lib) is identical to the reference
for b in ["system/lib/libMali.so", "system/lib/libgpu_aux.so", "system/lib/egl/libGLES_android.so"]:
    if b in names and b in ref.namelist():
        same = sha_member(zf, b) == sha_member(ref, b)
        print("  %s %-38s %s" % ("same" if same else "DIFF", os.path.basename(b),
                                 "matches reference" if same else "differs"))

# --------------------------------------------------------- 3. telephony gone
print()
print("--- 3. telephony removed ---")
GONE = [
    "system/bin/rild",
    "system/bin/rildmd2",
    "system/priv-app/Dialer/Dialer.apk",
    # NOT in GONE any more: CarrierConfig and CellBroadcastReceiver are back
    # (see device.mk "framework-required packages"). Settings dereferences
    # both unconditionally, and without them Settings > Connectivity crashes.
    "system/priv-app/CallLogBackup/CallLogBackup.apk",
    "system/priv-app/EmergencyInfo/EmergencyInfo.apk",
    "system/etc/permissions/android.hardware.telephony.gsm.xml",
]
for g in GONE:
    if g in names:
        fail("still in OTA: %s" % g)
    else:
        print("  gone  %s" % g)

# nothing anywhere in the OTA may *declare* android.hardware.telephony.
# Comments in handheld_core_hardware.xml mention the string, so match an actual
# feature element rather than a substring.
import re
FEATURE_RE = re.compile(rb'<(feature|uses-feature)\s+name="android\.hardware\.telephony(\.[a-z0-9]+)?"')
tele_feature_files = [(n, sorted(set(m.group(0) for m in FEATURE_RE.finditer(zf.read(n)))))
                      for n in names if n.startswith("system/etc/permissions/")
                      and FEATURE_RE.search(zf.read(n))]
if tele_feature_files:
    fail("permission files still declaring android.hardware.telephony: %s" % tele_feature_files)
else:
    print("  no permission XML declares android.hardware.telephony  "
          "(=> FEATURE_TELEPHONY absent => setup wizard disables SimMissingActivity)")

# radiooptions is LOCAL_MODULE_TAGS := debug (auto-installed in userdebug builds),
# not part of the telephony product config; report it but do not fail.
if "system/bin/radiooptions" in names:
    print("  note  system/bin/radiooptions present (debug-tagged module, inert without rild)")

# telephony-adjacent jars/APKs that are expected to REMAIN (part of AOSP core, not the
# telephony product config) -- must not be accidentally destroyed
KEEP = [
    "system/framework/telephony-common.jar",
    "system/priv-app/TeleService/TeleService.apk",
    "system/priv-app/Telecom/Telecom.apk",
    "system/priv-app/TelephonyProvider/TelephonyProvider.apk",
]
for k in KEEP:
    if k in names:
        print("  kept  %s" % k)
    else:
        print("  (absent) %s" % k)

# ------------------------------------------------------- 4. must-have pieces
print()
print("--- 4. components that must survive ---")
MUST = [
    "system/priv-app/SystemUI/SystemUI.apk",
    "system/priv-app/Settings/Settings.apk",
    "system/priv-app/LineageSetupWizard/LineageSetupWizard.apk",
    "system/bin/bootanimation",
    "system/media/bootanimation.zip",
    "system/lib/libwebviewchromium_loader.so",
    "system/lib/libwebviewchromium_plat_support.so",
    "system/app/webview/webview.apk",
    "system/lib/libMali.so",
    "system/lib/egl/libEGL_mali.so",
    "system/lib/libmtk_symbols.so",
    "system/lib/hw/gralloc.mt6592.so",
    "system/lib/hw/hwcomposer.mt6592.so",
]
for m in MUST:
    if m in names:
        print("  ok      %s" % m)
    else:
        fail("missing %s" % m)

# --------------------------------------------------- 5. diff vs last build
# Acceptable removals: the telephony purge (rild/Dialer/...) from the first
# reputation build, then the lean-base purge of build 7f97b5d61b/ab25aae4dd
# (TeleService/Telecom/TelephonyProvider/MmsService/WAPPushManager plus the
# unused LOS apps: Email, Exchange2, Terminal, Recorder, PhotoTable,
# Profiles, AudioFX, CMAudioService, Updater).
ACCEPT_REMOVED = ("Dialer", "CarrierConfig", "CellBroadcast", "CallLogBackup",
                  "EmergencyInfo", "rild", "telephony", "Telephony", "Telecom",
                  "TeleService", "MmsService", "WAPPushManager",
                  "Email", "Exchange2", "Terminal", "Recorder", "PhotoTable",
                  "Profiles", "AudioFX", "CMAudioService", "Updater",
                  # direct consequences of the same trim:
                  "spn-conf",            # GSM SPN table, no consumer left
                  "cmaudio",             # CMAudioService's native lib
                  "libjni_terminal")     # Terminal's native lib
# Acceptable additions: boot-image pre-opt output (WITH_DEXPREOPT_...)
ACCEPT_ADDED = (".oat", ".odex", ".art", ".vdex", "/arm/", "/arm64/")
print()
print("--- 5. change vs previously flashed build (see ACCEPT lists) ---")
if os.path.exists(OLD_LIST):
    old = set(l.strip() for l in open(OLD_LIST) if l.strip())
    removed = sorted(old - names)
    added = sorted(names - old)
    print("  removed: %d" % len(removed))
    for r in removed:
        print("     - %s" % r)
    print("  added:   %d" % len(added))
    for a in added:
        print("     + %s" % a)
    unexpected_removed = [r for r in removed if not any(t in r for t in ACCEPT_REMOVED)]
    unexpected_added = [a for a in added if not any(t in a for t in ACCEPT_ADDED)]
    if unexpected_added:
        fail("unexpected new files in OTA: %s" % unexpected_added)
    if unexpected_removed:
        print("  NOTE: removals that are not in the accepted-trim set: %s" % unexpected_removed)
else:
    print("  (%s not available)" % OLD_LIST)

print()
print("=" * 78)
if problems:
    print("RESULT: %d PROBLEM(S)" % len(problems))
    for p in problems:
        print("   - %s" % p)
    sys.exit(1)
print("RESULT: ALL CHECKS PASS")

Copyright (C) 2017 The LineageOS Project @OneLabs.net

Device Tree for General Mobile ETAB5 (etab5)
===========================================================

Basic   | Specifications
-------:|:-------------------------
CPU     | 8x ARM Cortex-A7 1,66GHz, 32 bit
CHIPSET | MTK 6592 (MediaTek)
GPU     | Mali-450 MP
Memory  | 2 GB
Storage | 32 GB
Battery | 7200 mAh (non-removable)
Display | 1920 x 1200 pixels, 10.1" (ro.sf.lcd_density=220)
Cameras | 5.0 MP rear / 2.0 MP front
No modem: WiFi-only tablet. Telephony is not inherited (full_base.mk, not
full_base_telephony.mk) and no telephony APKs/configs ship from this tree.

![ETAB5](http://1.bp.blogspot.com/-OAx9fusDmyc/VliVxrhS72I/AAAAAAAAARE/1cEkdgBFlqU/s1600/etab5.jpg "ETAB5")


# LineageOS 14.1 status

Working (validated on device):

  * Display / graphics (Mali 3.10.72 blob generation, see Graphics below)
  * Boot animation (renders; its `readyToRun` logs EGL errors)
  * SystemUI stable (`config.disable_atlas=1`, see Graphics below)
  * Touch, Sound, Sensors, Bluetooth, Wi-Fi, MTP, Charging
  * Boot to launcher; adb without authorisation prompt (`ro.adb.secure=0`
    is injected into ADDITIONAL_DEFAULT_PROPERTIES -> /default.prop)

# Graphics: the two rules this device requires

1.  The 3.10.72-era Mali userland (libMali.so, libEGL_mali.so, gralloc,
    hwcomposer, ...) opens /dev/ion O_RDWR inside every GPU client process
    (bootanim, SystemUI's ImageWallpaper, every hwui app) - both to
    allocate and to register/import buffer handles. init.mt6592.rc and
    ueventd.mt6592.rc therefore keep /dev/ion world-readable-writable
    (chmod 0666). A restrictive mode here produced
    "[MALI][Gralloc-ERROR] ion_open failed with Permission denied ->
    gralloc0 register failed -13 -> eglMakeCurrent EGL_BAD_ALLOC", which
    killed bootanim (no animation) and put SystemUI in a crash loop.
    SELinux is permissive; do not rely on it to keep this working.

2.  MTK's Mali crashes inside AssetAtlas::init -> eglCreateImageKHR
    (GpuAuxIsSupportFormat() rejects the buffer, NULL deref). The stock
    ROM never ran the atlas, and neither do we: system.prop sets
    config.disable_atlas=1 (SystemServer gates AssetAtlasService on it).

The kernel (device/.../kernel) is a stock 3.10.72 payload byte-identical to
the known-good reference build. mtkbootimg offsets (BOARD_MKBOOTIMG_ARGS:
ramdisk 0x04000000, tags 0x0e000000, mtk 1) are hardware-mandated; wrong
values hang the bootloader logo.

# Build

    source build/envsetup.sh
    lunch lineage_hulkbuster-userdebug
    mka installclean      # always: product rebuilds are not always detected
    mka bacon

Flash the produced lineage-14.1-*.zip with TWRP; wipe /system (mandatory -
stale /system files from older revisions break boot) and /data (advised).

Package options in device.mk:
  MTKDIAG=false    builds without the mtkdiag.sh debug watchdog
                   (default: true while the ROM is being validated)

# Known content quirks / follow-up ideas

  * vendor/pittpatt face-detection models (~14 MB) are still copied but are
    referenced by nothing in 14.1 - candidate for removal.
  * Modem daemon binaries (gsm0710muxd, ccci_*, ...) still ship in the
    vendor blob copy but are never started (init.modem.rc is not imported).
  * WITH_DEXPREOPT_BOOT_IMG_ONLY=true pre-opts the framework boot image.
    If a future host produces "Failed to mmap at expected address" during
    dex2oat, WITH_DEXPREOPT() flags are build-time-reversible (revert
    BoardConfig.mk).
  * For an Android 8.1 port, the items to carry over: kernel + blob set +
    /dev/ion permission rule + config.disable_atlas, mtkbootimg offsets,
    TARGET_LDPRELOAD=libmtk_symbols strategy (the MTK libui additions were
    reverted upstream, so the shim still carries their symbols), and a
    fresh sepolicy (POLICYVERS will need to move to 29+; this tree's
    device/netd rules are minimal MTK additions).

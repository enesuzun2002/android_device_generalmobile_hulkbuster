Copyright (C) 2026 enesuzun2002 (emurat.uzun@gmail.com)

Based on the original device tree, Copyright (C) 2017 The LineageOS Project @OneLabs.net

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


# LineageOS 16.0 (Android 9) status

Working (validated on device):

  * Boot, setup wizard, launcher; /data on f2fs; adb and MTP
  * Display 1920x1200 at ~60 Hz (59.95 Hz, see Display below), Mali-450
    GPU, touch, backlight, screen off/on
  * Wi-Fi, Bluetooth (stack enables; pairing not re-tested on this build)
  * Audio playback, microphone and camcorder audio
  * Rear and front camera: photo and video recording
  * Hardware H.264 decoding up to 1080p60 (OMX.hulk.h264.decoder)
  * Hardware H.264 encoding up to 1920x1088 (OMX.hulk.h264.encoder): camera
    video, screenrecord (falls back to 720p for 1920x1200), MediaCodec apps
  * Sensors, charging, lights, thermal, power HAL

Known issues:

  * SELinux is permissive (androidboot.selinux=permissive).
  * GPS is not verified; mtk_agpsd fails to link (missing ICU symbol), so
    there is no AGPS.
  * Camera video runs at about 25 fps, and 1080p is upscaled from the
    sensor's 1280x960 mode.
  * Only H.264 has a hardware codec; VP9/HEVC decode in software.
  * The advertised refresh rate can stay at LK's 55.71 Hz when the
    kernel's post-boot measurement misses; the panel itself still runs at
    59.95 Hz (dumpsys SurfaceFlinger --dispsync shows the real period).
  * Small flicker at the bottom of the timezone dropdown; closing recents
    can stutter.
  * On the first boot after a reset, the "Allow USB debugging" dialog may
    ignore taps (obscured-touch filter); it works after a restart.

# What is in this tree

  hwc/        Composer HAL: a device copy of AOSP's HWC2On1Adapter with
              the fixes the MTK HWC1 blob needs (framebuffer-target fence
              ownership, release fence merged into the retire fence, GPU
              composition for 30 frames after scene changes and while a dim
              layer is shown). Never keep HWC1 vsync always on: with the
              screen off it spins at RT priority and the display never wakes.
  media/      libstagefrighthw OMX plugin with our own H.264 decoder and
              encoder on top of MTK's libvcodecdrv (the MTK OMX blobs are
              not used). Decoder output goes through the MDP (DpBlitStream)
              into gralloc YV12 buffers. docs/mtk-venc-spec.txt documents
              the encoder API.
  camera/     HIDL camera provider and camera.device@1.0 wrapper for the
              MTK HAL1 blobs.
  audio/      Wrapper around the MTK audio blob (input-stream struct fix,
              speaker digital boost off by default).
  seccomp/    mediacodec policy for the hardware codecs.
  libmtk_symbols/  Shims for symbols the MTK blobs expect.
  sensors/, lights/, power/, thermal/, health/, memtrack/, wifi/,
  bluetooth/  Device HALs and configs.
  tools/      Test tools: vdectest, venctest, mdptest, mctest (codecs),
              fbclk (DSI clock), aarec (capture pacing), gpustress.
  build-tools/patches/los-16.0/  Patches to upstream LineageOS projects
              (see its README.md); los-14.1/ holds the older port's.
  docs/       Encoder API notes and DVFS research.

# Display

LK programs the DSI PLL and the kernel never powers the panel down, so the
panel used to run at LK's 55.7 Hz. The kernel (mtkfb) reprograms the DSI
clock to 476 MHz about 2 s after boot and re-measures vsync (59.95 Hz).

Graphics rules this device still needs:

1.  The MTK Mali userland opens /dev/ion O_RDWR in every GPU client, so
    init.mt6592.rc and ueventd.mt6592.rc keep /dev/ion 0666.
2.  The gralloc0 blob cannot share GRALLOC_USAGE_HW_FB buffers across
    processes: ro.sf.no_hw_fb_usage=true (frameworks-native patch).
3.  The screen on/off ColorFade crashes libMali inside system_server:
    ro.config.disable_color_fade=true (frameworks-base patch).

# Kernel

kernel/mediatek/mt6592 (hulkbuster_defconfig), MT6592 3.10.72. Notable
changes: 60 Hz panel clock, wake backlight delay, HZ-independent audio IRQ
timeouts (the HZ=250 kernel otherwise padded every 50 ms capture period
with stale frames), quieter logging, a mild DVFS undervolt with an 8-core 1768 MHz
turbo (see docs/dvfs-research.md). mtkbootimg offsets (BOARD_MKBOOTIMG_ARGS: ramdisk 0x04000000, tags
0x0e000000) are hardware-mandated; wrong values hang at the logo.

# Build

Needs this device tree, vendor/generalmobile/hulkbuster and
kernel/mediatek/mt6592 in a LineageOS 16.0 checkout.

    repo sync
    device/generalmobile/hulkbuster/build-tools/patches/los-16.0/apply.sh
    source build/envsetup.sh
    lunch lineage_hulkbuster-userdebug
    mka installclean
    mka bacon

The build preopts the boot image and all system apps and compiles
SystemUI, Trebuchet and Settings with the speed filter.

Package options in device.mk:
  MTKDIAG=true     installs and starts the mtkdiag.sh bring-up logging
                   (default: false)

# Install

There is no fastboot on this device. Flash the zip from TWRP 3.7.0 with f2fs
support (twrp-3.7.0_9-0-hulkbuster-f2fs-recovery.img). Coming from another
ROM or from 14.1, format /data as f2fs and wipe /system first.

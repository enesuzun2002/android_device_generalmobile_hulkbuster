LineageOS 16.0 (Android 9) patches for hulkbuster
================================================

Changes to LineageOS source projects that this port needs. The device,
vendor and kernel repositories carry their own changes; everything that
touches an upstream LineageOS project lives here. The parent directory
holds the older cm-14.1 patches (see ../README.md and ../PORTING.md).

Apply after `repo sync` (idempotent):

    device/generalmobile/hulkbuster/build-tools/patches/lineage-16.0/apply.sh

or by hand with `git am` in each project. Directory names map to project
paths with the first '-' turned into '/' (frameworks-native ->
frameworks/native, vendor-lineage -> vendor/lineage).

external-selinux/0001-libsepol-drop-xperm-rules-for-pre-xperm-kernel-policies.patch
    The 3.10 kernel loads a policy version without ioctl extended
    permissions. Writing the kernel policy failed on allowxperm rules;
    drop them (plain ioctl rules still apply).

libcore/0001-X500Name-build-asX500Principal-without-reflection.patch
    Pie port of the 14.1 fix: the reflective X500Principal constructor
    crashes ART on this device and takes system_server down during
    NetworkMonitor's HTTPS probe.

system-bt/0001-controller-let-a-device-skip-the-LE-vendor-capabilities-query.patch
    Pie port of the 14.1 fix: the MT6592 combo firmware never completes
    HCI 0xFD53, so Bluetooth enable times out. Gated on
    BTM_BLE_SKIP_OFFLOAD_FEATURES_QUERY (bluetooth/bdroid_buildcfg).

system-bt/0002-hci-stop-at-the-last-extended-features-page-the-controller-serves.patch
    The MT6592 controller reports more classic feature pages than it
    serves and rejects page 2 with status 0x30; Pie's parser CHECK()ed the
    response and aborted the Bluetooth stack (old Bluedroid ignored it).
    Stop at the last page the controller accepts.

vendor-lineage/0001-kernel-only-match-CONFIG-m-lines-when-detecting-modules.patch
    The kernel build's module detection grepped for '=m' anywhere in the
    config and wrongly enabled the modules step for this all-built-in
    kernel; match only CONFIG_*=m lines.

frameworks-native/0001-sf-allow-dropping-GRALLOC-USAGE-HW-FB-from-the-client-target.patch
    gralloc.mt6592 returns HW_FB buffers whose native_handle has no valid
    fd and refuses to register them, so on Pie (separate allocator
    process) the first FramebufferSurface allocation fails, EGL reports
    EGL_BAD_ALLOC and nothing is ever composited ("RenderEngine is not
    current"; device stuck on the boot logo). With
    ro.sf.no_hw_fb_usage=true (system.prop) the client target is a
    normal ion buffer that hwcomposer.mt6592 posts itself.

frameworks-base/0001-display-allow-disabling-the-screen-on-off-ColorFade.patch
    The screen on/off ColorFade draws with GLES inside system_server, and
    libMali crashes in eglMakeCurrent there (PowerManagerService thread),
    so every screen-off killed the framework and left the button lights on.
    With ro.config.disable_color_fade=true (system.prop) DisplayPowerController
    takes the low-RAM path and skips the animation.

art/0001-image_space-skip-the-zygote-image-verify-without-a-s.patch
    Without a boot image in /system, Pie verified the zygote image on every
    boot and pruned the dalvik-cache, so each boot recompiled the boot
    classpath (5-10 minute boots). Skip the verify in that case.

build-make/0001-main-add-a-PRODUCT_PACKAGES_FILTER-hook-for-device-t.patch
    Lets a device tree drop packages that inherited product makefiles add
    (here TrebuchetQuickStep, replaced by plain Trebuchet).

frameworks-av/0001-cameraservice-cache-hal1-legacy-parameters-at-startu.patch
    Opens each HAL1 camera once at cameraserver start to cache its legacy
    parameters, so the first app open does not race the shim's own open
    and get evicted.

frameworks-av/0002-stagefright-add-OMX.hulk.h264.decoder-for-MT6592-har.patch
frameworks-av/0003-stagefright-use-software-renderer-path-and-finish-OM.patch
frameworks-av/0004-stagefright-advertise-up-to-level-4.2-on-OMX.hulk.h2.patch
    OMX.hulk.h264.decoder: our own OMX component (codecs/hulkavcdec) that
    drives the MT6592 VDEC hardware through libvcodecdrv.so directly. MTK's
    libMtkOmxVdec is unusable on Pie: its bundled libClearMotionFW (MJC)
    was built for a different decoder layout and corrupts it. Output is
    de-tiled (MTK block format -> I420) with NEON and rendered like the
    Google decoders, so OMX.hulk. joins OMX.google. in ACodec/MediaCodec's
    software-renderer checks. Advertises up to 1080p60, High@4.2, which
    YouTube needs to pick it. Device side: configs/media_codecs.xml,
    seccomp/mediacodec.policy, PRODUCT_PACKAGES in device.mk.

frameworks-av/0005-stagefright-pipeline-hulkavcdec-de-tile-queue-for-10.patch
    1080p60 for OMX.hulk.h264.decoder: queue de-tile jobs (decode no longer
    waits on the worker), flush only the written bitstream bytes, write the
    de-tiled output in 64-byte bursts, 16 output buffers. 1080p60 went from
    ~14 to ~60 fps; HW decode is ~14 ms per 1080p frame (ceiling ~68 fps).

frameworks-av/0006-stagefright-convert-hulkavcdec-frames-into-gralloc-b.patch
frameworks-av/0007-stagefright-render-OMX.hulk.-decoder-output-through-.patch
    OMX.hulk.h264.decoder now takes native-window (YV12 gralloc) buffers and
    converts the MTK block frames into them with the MDP (libdpframework
    DpBlitStream, dest = the gralloc ION fd), instead of a CPU de-tile into
    a ByteBuffer plus SoftwareRenderer copy; OMX.hulk. leaves ACodec/
    MediaCodec's software-renderer checks. 1080p60: media.codec 48-66% ->
    16-22%, mediaserver 45-53% -> 13-16%, 0 drops. The CPU de-tile path stays
    for ByteBuffer users and as per-frame fallback.

hardware-interfaces/0001-camera-device-support-pre-O-HAL1-clients.patch
    MediaTek's N-era HAL1 calls get_memory with a NULL cookie and takes
    references on preview buffers as ANativeWindowBuffers; handle both.

hardware-interfaces/0002-audio-allow-skipping-the-legacy-device-dump.patch
    The MTK audio.primary blob crashes in its legacy dump(); skip it when
    ro.audio.skip_legacy_dump=true (system.prop).

hardware-interfaces/0003-camera-device-translate-mtk-hal1-shutter-and-jpeg-ca.patch
    MTK delivers the shutter and the JPEG as private messages; map them to
    CAMERA_MSG_SHUTTER and CAMERA_MSG_COMPRESSED_IMAGE so photos save.

hardware-interfaces/0004-camera-device-cap-advertised-picture-sizes-per-camer.patch
    Hides picture sizes above ro.vendor.camera<id>.max_picture_size (the
    main camera's borrowed feature table lists upscaled sizes).

packages-apps-Backgrounds/0001-Backgrounds-load-downscaled-previews-for-the-wallpap.patch
    The Wallpapers app decoded every built-in wallpaper at full size (the
    1440p set, 2560x1440) for its small grid cells. On this tablet it used
    ~380 MB and half the frames took 250-950 ms (texture uploads to the
    Mali-450). Grid previews are now decoded downscaled: 81 MB, 2.4% janky.

hardware-interfaces/0006-camera-device-hide-flash-modes-on-cameras-without-a-.patch
    The front camera got the rear camera's flash modes from a shared HAL1
    feature table; ro.vendor.camera1.no_flash=true limits it to "off".

frameworks-base/0002-SystemUI-allow-disabling-the-notification-launch-ani.patch
    persist.sys.notif_launch_anim=false (system.prop) replaces SystemUI's
    per-frame notification-to-app remote animation, which stuttered here,
    with the normal window transition.

frameworks-base/0003-SystemUI-grow-the-notification-launch-animation-from.patch
    The launch animation centred the expanding card horizontally, so on
    this tablet's left-side notification panel it jumped sideways first.
    It now grows from the notification's real position.

Retired: hardware-interfaces 0005 and 0007 (HWC2On1Adapter fence/tearing
fixes) now live in the device tree (hwc/: libhwc2on1adapter_hulkbuster and
android.hardware.graphics.composer@2.1-impl-hulkbuster).

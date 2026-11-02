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

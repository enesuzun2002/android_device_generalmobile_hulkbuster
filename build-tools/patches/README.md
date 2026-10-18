Workspace patches that live outside the device/vendor trees.

A fresh checkout of this product needs, beyond this device tree and the
vendor blob tree:

  build/                       <- from LineageOS android_build cm-14.1
  frameworks/base              <- from TestMT6572 cm-14.1 (its "Fix Graphics"
                                  commits must be present to allow the
                                  reverts below to apply)
  frameworks/native            <- from TestMT6572 cm-14.1 (contains
                                  "MediaTek: Fix Graphics")

Apply:

    cd build          && git am build-tools/patches/build/*.patch
    cd frameworks/base && git am build-tools/patches/frameworks-base/*.patch
    cd frameworks/native && git am build-tools/patches/frameworks-native/*.patch

  0001-main.mk-PRODUCT_PACKAGES_FILTER-hook.patch
      Lets a device tree remove packages from the inherited product
      union via PRODUCT_PACKAGES_FILTER (see device.mk - the classic
      PRODUCT_PACKAGES := $(filter-out ...) idiom is ineffective under
      the @inherit: product model).

  0001-Revert-Fix-Graphics.patch (frameworks/base)
  0001-Revert-MediaTek-Fix-Graphics.patch (frameworks/native)
      The "Fix Graphics" commits stored copy-side workaround that broke
      this device (the working mechanism is the /dev/ion permission in
      the ramdisk + system.prop config.disable_atlas=1).

  0002-bootanimation-init-EGL-before-surface.patch
      Bootanimation: EGL init before the SF surface, correct visual,
      error logs. This turned the old silent black-screen failure into
      diagnosable logs (the ION EACCES class of failure).

Workspace patches that live outside the device/vendor trees.

A fresh checkout of this product needs, beyond this device tree and the
vendor blob tree:

  build/                       <- from LineageOS android_build cm-14.1
  frameworks/base              <- from TestMT6572 cm-14.1 (its "Fix Graphics"
                                  commits must be present to allow the
                                  reverts below to apply)
  frameworks/native            <- from TestMT6572 cm-14.1 (contains
                                  "MediaTek: Fix Graphics")
  packages/apps/Settings       <- from LineageOS cm-14.1
  system/bt                    <- from LineageOS cm-14.1

Apply:

    cd build          && git am build-tools/patches/build/*.patch
    cd frameworks/base && git am build-tools/patches/frameworks-base/*.patch
    cd frameworks/native && git am build-tools/patches/frameworks-native/*.patch
    cd packages/apps/Settings && git am build-tools/patches/packages-apps-Settings/*.patch
    cd system/bt && git am build-tools/patches/system-bt/*.patch

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

  0003-SettingsLib-TetherUtil-null-safe.patch (frameworks/base)
  0001-Settings-null-safe-telephony-pages.patch (packages/apps/Settings)
      Wifi-only robustness: CarrierConfigManager.getConfig() returns null
      without a SIM/carrier config. TetherUtil dereferenced it, so opening
      Settings > Connectivity crashed with an NPE; ApnSettings and
      SecuritySettings did the same. WirelessSettings also queried
      getApplicationEnabledSetting() for CellBroadcastReceiver inside a
      catch (IllegalArgumentException) - the missing-package case throws
      NameNotFoundException, which was not caught.

  0001-controller-let-a-device-skip-the-ble-offload-feature.patch (system/bt)
      Bluetooth enable timed out: the stack sends the vendor "LE Get Vendor
      Capabilities" command (0xFD53) unconditionally, the MT6592 combo
      firmware answers Command Status 0x01 (Unknown HCI Command) and never
      a Command Complete, and AWAIT_COMMAND blocks forever (captured with
      the btsnoop network server). bdroid_buildcfg.h defines
      BTM_BLE_SKIP_OFFLOAD_FEATURES_QUERY to skip it.

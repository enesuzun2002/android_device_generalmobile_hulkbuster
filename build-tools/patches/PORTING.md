Carrying the hulkbuster fixes to a newer Android (e.g. 9 / lineage-16.0)
=====================================================================

Every out-of-tree change this device needs is kept here, so a new port
starts from a checklist instead of a re-investigation. For each item: the
symptom, how it was proven, what the patch does, and what to check on the
new release (code moves; the symptom is what you verify against).

Patches in this directory (apply with `git am`, see README.md)
--------------------------------------------------------------

system-bt/0001-controller-let-a-device-skip-the-ble-offload-feature.patch
    Symptom   Bluetooth toggle times out after ~11 s ("Error enabling
              Bluetooth (enable timeout)").
    Proof     HCI trace via the btsnoop network server (no root needed:
              settings put secure bluetooth_hci_log 1, adb forward
              tcp:8872 tcp:8872, connect while toggling BT). The stack
              sends 0xFD53 (LE Get Vendor Capabilities); the MT6592 combo
              firmware replies Command Status 0x01 (Unknown HCI Command)
              and never a Command Complete, so AWAIT_COMMAND blocks.
    Patch     controller.c start_up() skips the query when the device
              defines BTM_BLE_SKIP_OFFLOAD_FEATURES_QUERY
              (bluetooth/bdroid_buildcfg/bdroid_buildcfg.h).
    On 9      system/bt is C++ (device/src/controller.cc). Look in
              start_up() for the "ble offload features"/vendor capabilities
              read (HCI_BLE_VENDOR_CAP_OCF / 0xFD53) and gate it the same
              way. Also check btm_ble_vendor_capability_init() in
              stack/btm, which sends the same opcode later. Verify with a
              btsnoop trace: no 0xFD53 should be sent.

libcore/0001-X500Name-build-asX500Principal-without-reflection.patch
    Symptom   system_server soft-reboots a few seconds after Wi-Fi
              connects (NetworkMonitor captive-portal HTTPS probe).
    Proof     Tombstone: SIGSEGV fault addr 0x78 in art::InvokeMethod <-
              Constructor_newInstance0 <- Constructor.newInstance <-
              sun.security.x509.X500Name.asX500Principal() (mapped with
              oatdump on the device's boot-core-oj.oat). Reproduces on the
              stock and the open kernel and with a wiped dalvik-cache.
              Root cause inside ART not identified; this is a workaround.
    Patch     Build the X500Principal with the public
              X500Principal(byte[]) constructor from getEncoded() instead
              of the reflective package-private constructor.
    On 9      ojluni/src/main/java/sun/security/x509/X500Name.java still
              has the reflective asX500Principal(); the patch should apply
              as is. If the crash is gone on the new ART, it can be
              dropped - test by connecting to Wi-Fi without it.

build/, frameworks-base/, frameworks-native/, packages-apps-Settings/
    See README.md. Graphics reverts and the PRODUCT_PACKAGES_FILTER hook
    are 14.1-specific; re-check each against the new tree.

Device-tree pieces that go with them
------------------------------------

bluetooth/bdroid_buildcfg/bdroid_buildcfg.h
    BTM_BLE_SKIP_OFFLOAD_FEATURES_QUERY (pairs with the system-bt patch).
    BTM_DEF_LOCAL_NAME is still the P55 one ("P55 Novo by ...") - set the
    E-Tab5 name when porting.

sensors/ (sensors.hulkbuster.so) + ro.hardware.sensors=hulkbuster
    The MTK sensors.mt6592.so blob (also the one in the other hulkbuster
    AOSP build) lists no gyroscope; the wrapper adds the BMG160 through
    hwmsen's /dev/hwmsensor ioctls (ID 3, rad/s = values/value_divide).
    On 9: android.hardware.sensors@1.0-impl (passthrough) loads legacy
    HALs with hw_get_module, which still honours ro.hardware.sensors, so
    the wrapper keeps working behind HIDL. It exposes the blob's device
    API version; HIDL needs >= 1_3 - check what the blob reports
    (logcat -s SensorsWrapper prints it) and raise the wrapper's version
    with the missing calls (batch/flush/inject) if HIDL refuses it.
    There is no barometer on this tablet.

Kernel (separate repo: android_kernel_mediatek_mt6592, branch hulkbuster-3.10.108)
----------------------------------------------------------------------------------
All values below were recovered from the stock kernel
(device/generalmobile/hulkbuster/kernel) by disassembly; the tree's DCT
and board files came from the Panasonic P55 Novo and were wrong.

    - DCT headers are only used when the full DrvGen set is present in
      arch/arm/mach-mt6592/hulkbuster/dct/dct/ (else they are regenerated
      from the P55 codegen.dws and edits are silently ignored).
    - MT6333 EINT 8 (was 0 -> interrupt storm -> WDT reset); touch
      GPIO13/14 EINT14; ALS GPIO18 EINT18; accdet GPIO168 EINT168 HIGH;
      SD card detect EINT9; camera RST/PDN 80/83 (main), 82/81 (sub).
    - No CCCI modem driver (WiFi-only): with it, ccci_mdinit deadlocks
      clock_lock in md_power_on() and every clock user hangs.
    - CONFIG_MTK_RAM_CONSOLE_DRAM_ADDR must be 0x83F00000 (inside DRAM).
    - Panel nt71397: burst video mode, PLL 441, porches 2/18/15 and
      16/32/16, GPIO119/118 enables, "Turn On Peripheral" packet.
    - Touch: raw FT5x06 coordinates are already 1920x1200 landscape (no
      rotation/calibration); keys at x=2099, y 97/62/0 = Back/Home/Recents.
    - Charger BQ24196 on i2c-2 @0x6b, CE = GPIO19; MT6333 only for VCORE.
    - Sensors on i2c-2: BMA255 @0x10 dir 5, BMG160 @0x68 dir 5, BMM050
      @0x12 dir 5 (old hwmsen drivers), CM36283 @0x60 with the stock
      als_level/als_value tables.
    - Keys: matrix [0] = VOLUMEUP only; Volume Down is the PMIC FCHR key
      (KPD_PMIC_RSTKEY_MAP). Its unreliability is hardware (stock too).
    - Key backlight = PMIC NLED ISINK0/1/2 via button-backlight.
    - Speaker amp GPIO20, headset switch GPIO121, LED table as stock.
    - Branch hulkbuster-3.10.108 = hulkbuster-3.10.72 + linux-stable
      3.10.73..3.10.108, one commit per release (MTK changes kept on
      conflicts); bisect by release if a regression shows up.
    Android 9 officially wants a newer kernel; booting it on 3.10 needs
    the usual backports (binder, seccomp/filter, cgroup and SELinux bits),
    which are not done here.

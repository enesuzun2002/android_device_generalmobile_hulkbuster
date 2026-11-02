# This tablet has no modem/telephony hardware, so do not inherit the telephony
# base (which pulls in Dialer/CarrierConfig/rild/CellBroadcastReceiver and makes
# the framework advertise android.hardware.telephony). Lineage already treats it
# as a wifi-only tablet in lineage.mk.
$(call inherit-product, $(SRC_TARGET_DIR)/product/full_base.mk)

# Keep the keyguard from demanding a SIM now that telephony is gone.
PRODUCT_PROPERTY_OVERRIDES += keyguard.no_require_sim=true

# The gps config appropriate for this device
$(call inherit-product, device/common/gps/gps_us_supl.mk)

$(call inherit-product-if-exists, $(VENDOR_PATH)/vendor.mk)

PRODUCT_CHARACTERISTICS := tablet,nosdcard

PRODUCT_TAGS += dalvik.gc.type-precise

DEVICE_PACKAGE_OVERLAYS += $(DEVICE_PATH)/overlay

# The kernel is built from kernel/mediatek/mt6592 (TARGET_KERNEL_SOURCE); the
# stock 3.10.72 payload in this directory is kept only as a reference and must
# not be copied to $(PRODUCT_OUT)/kernel, where the kernel build installs it.

# Symbols
PRODUCT_PACKAGES += libmtk_symbols libmtk_atomic libmtk_xlog libmtk_gui

# Sensors: native ISensors@1.0 service (sensors/) talking to the kernel hwmsen
# framework directly; replaces the closed sensors.mt6592.so blob (HAL 1.1),
# which Pie's sensors passthrough rejects (needs >= 1.3).
PRODUCT_PACKAGES += android.hardware.sensors@1.0-service.hulkbuster

# WebView: Pie's core_minimal.mk pulls in webview (external/chromium-webview)
# and its loader/plat_support libraries; nothing to add here.

# HIDL HALs (Pie). The Lollipop-era legacy blobs are used through the
# passthrough implementations; services are started by init from the rc files
# shipped with each -service package. See manifest.xml.

# Graphics: gralloc.mt6592 / hwcomposer.mt6592 (HWC1) behind the HIDL HALs.
PRODUCT_PACKAGES += \
    android.hardware.graphics.allocator@2.0-impl \
    android.hardware.graphics.allocator@2.0-service \
    android.hardware.graphics.mapper@2.0-impl \
    android.hardware.graphics.composer@2.1-impl \
    android.hardware.graphics.composer@2.1-service \
    android.hardware.configstore@1.0-service \
    libhwc2on1adapter \
    libhwc2onfbadapter

# Lights, power, health, thermal, memtrack: native services (lights/, power/,
# health/, thermal/, memtrack/). memtrack reads the kernel's /proc/ion_pids and
# /proc/mali/pid_memory; the USB HAL uses the backported dual_role_usb class.
PRODUCT_PACKAGES += \
    android.hardware.light@2.0-service.hulkbuster \
    android.hardware.power@1.0-service.hulkbuster \
    android.hardware.health@2.0-service.hulkbuster \
    android.hardware.thermal@1.0-service.hulkbuster \
    android.hardware.usb@1.0-service \
    android.hardware.memtrack@1.0-impl \
    android.hardware.memtrack@1.0-service \
    memtrack.mt6592 \
    android.hardware.keymaster@4.0-service \
    android.hardware.gnss@1.0-impl \
    android.hardware.gnss@1.0-service \
    android.hardware.camera.provider@2.4-impl \
    android.hardware.camera.provider@2.4-service \
    android.hardware.drm@1.0-impl \
    android.hardware.drm@1.0-service \
    android.hardware.wifi@1.0-service \
    android.hardware.bluetooth@1.0-impl \
    android.hardware.bluetooth@1.0-service

# Audio: legacy audio.primary (via the hw module) behind the audio HAL 2.0
# passthrough. The blob only speaks audio_policy.conf, hence
# USE_XML_AUDIO_POLICY_CONF := 0.
PRODUCT_PACKAGES += \
    android.hardware.audio@2.0-impl \
    android.hardware.audio@2.0-service \
    android.hardware.audio.effect@2.0-impl \
    audio.primary.mt6592 \
    audio_policy.default \
    audio.a2dp.default \
    audio.usb.default \
    audio.r_submix.default \
    libaudio-resampler \
    tinymix

PRODUCT_COPY_FILES += \
    $(DEVICE_PATH)/configs/audio_policy.conf:system/etc/audio_policy.conf

# Bluetooth
PRODUCT_PACKAGES += libbt-vendor

# Wifi
PRODUCT_PACKAGES += \
    lib_driver_cmd_mt66xx \
    libwifi-hal-mt66xx \
    libwpa_client \
    hostapd \
    wpa_supplicant \
    wpa_supplicant.conf

PRODUCT_COPY_FILES += \
    $(DEVICE_PATH)/configs/hostapd/hostapd_default.conf:system/etc/hostapd/hostapd_default.conf \
    $(DEVICE_PATH)/configs/hostapd/hostapd.accept:system/etc/hostapd/hostapd.accept \
    $(DEVICE_PATH)/configs/hostapd/hostapd.deny:system/etc/hostapd/hostapd.deny

# GPS
PRODUCT_COPY_FILES += \
    $(DEVICE_PATH)/configs/agps_profiles_conf2.xml:system/etc/agps_profiles_conf2.xml

# Rootdir
# No root/ueventd.rc here on purpose: Pie's system/core/rootdir/ueventd.rc
# provides /dev/hwbinder and /dev/vndbinder permissions; the device-specific
# rules live in ueventd.mt6592.rc (which also covers /dev/ion).
PRODUCT_COPY_FILES += \
    $(DEVICE_PATH)/rootdir/fstab.mt6592:root/fstab.mt6592 \
    $(DEVICE_PATH)/rootdir/init.mt6592.rc:root/init.mt6592.rc \
    $(DEVICE_PATH)/rootdir/init.mt6592.usb.rc:root/init.mt6592.usb.rc \
    $(DEVICE_PATH)/rootdir/ueventd.mt6592.rc:root/ueventd.mt6592.rc \
    $(DEVICE_PATH)/rootdir/sbin/poweroff.sh:recovery/root/sbin/poweroff.sh \
    $(DEVICE_PATH)/rootdir/sbin/rebootrecovery.sh:recovery/root/sbin/rebootrecovery.sh \
    $(DEVICE_PATH)/rootdir/sbin/rebootsystem.sh:recovery/root/sbin/rebootsystem.sh

# On-device diagnostics (mtkdiag.sh + its watch service). Debug-only: it
# produces a rotating log tree under /data/mtkdiag. Kept enabled (the
# default) while this ROM is being validated; set MTKDIAG=false for a
# clean-base build.
MTKDIAG ?= true
ifeq ($(strip $(MTKDIAG)),true)
PRODUCT_COPY_FILES += \
    $(DEVICE_PATH)/rootdir/mtkdiag.sh:root/mtkdiag.sh
endif

# Bluetooth address helper (see rootdir/bt_addr.sh)
PRODUCT_COPY_FILES += \
    $(DEVICE_PATH)/rootdir/bt_addr.sh:root/bt_addr.sh

# Permissions
PRODUCT_COPY_FILES += \
    frameworks/native/data/etc/android.hardware.bluetooth_le.xml:system/etc/permissions/android.hardware.bluetooth_le.xml \
    frameworks/native/data/etc/android.hardware.bluetooth.xml:system/etc/permissions/android.hardware.bluetooth.xml \
    frameworks/native/data/etc/android.hardware.camera.autofocus.xml:system/etc/permissions/android.hardware.camera.autofocus.xml \
    frameworks/native/data/etc/android.hardware.camera.flash-autofocus.xml:system/etc/permissions/android.hardware.camera.flash-autofocus.xml \
    frameworks/native/data/etc/android.hardware.camera.front.xml:system/etc/permissions/android.hardware.camera.front.xml \
    frameworks/native/data/etc/android.hardware.faketouch.xml:system/etc/permissions/android.hardware.faketouch.xml \
    frameworks/native/data/etc/android.hardware.location.gps.xml:system/etc/permissions/android.hardware.location.gps.xml \
    frameworks/native/data/etc/android.hardware.sensor.accelerometer.xml:system/etc/permissions/android.hardware.sensor.accelerometer.xml \
    frameworks/native/data/etc/android.hardware.sensor.compass.xml:system/etc/permissions/android.hardware.sensor.compass.xml \
    frameworks/native/data/etc/android.hardware.sensor.gyroscope.xml:system/etc/permissions/android.hardware.sensor.gyroscope.xml \
    frameworks/native/data/etc/android.hardware.sensor.light.xml:system/etc/permissions/android.hardware.sensor.light.xml \
    frameworks/native/data/etc/android.hardware.sensor.proximity.xml:system/etc/permissions/android.hardware.sensor.proximity.xml \
    frameworks/native/data/etc/android.hardware.touchscreen.multitouch.distinct.xml:system/etc/permissions/android.hardware.touchscreen.multitouch.distinct.xml \
    frameworks/native/data/etc/android.hardware.touchscreen.multitouch.jazzhand.xml:system/etc/permissions/android.hardware.touchscreen.multitouch.jazzhand.xml \
    frameworks/native/data/etc/android.hardware.touchscreen.multitouch.xml:system/etc/permissions/android.hardware.touchscreen.multitouch.xml \
    frameworks/native/data/etc/android.hardware.touchscreen.xml:system/etc/permissions/android.hardware.touchscreen.xml \
    frameworks/native/data/etc/android.hardware.usb.accessory.xml:system/etc/permissions/android.hardware.usb.accessory.xml \
    frameworks/native/data/etc/android.hardware.wifi.direct.xml:system/etc/permissions/android.hardware.wifi.direct.xml \
    frameworks/native/data/etc/android.hardware.wifi.xml:system/etc/permissions/android.hardware.wifi.xml \
    frameworks/native/data/etc/android.software.midi.xml:system/etc/permissions/android.software.midi.xml \
    frameworks/native/data/etc/android.software.sip.voip.xml:system/etc/permissions/android.software.sip.voip.xml \
    frameworks/native/data/etc/handheld_core_hardware.xml:system/etc/permissions/handheld_core_hardware.xml \
    $(DEVICE_PATH)/configs/android.hardware.camera.xml:system/etc/permissions/android.hardware.camera.xml \
    $(DEVICE_PATH)/configs/android.hardware.microphone.xml:system/etc/permissions/android.hardware.microphone.xml

# Keylayout
PRODUCT_COPY_FILES += \
    $(DEVICE_PATH)/configs/mtk-kpd.kl:system/usr/keylayout/mtk-kpd.kl

# FM Radio app: not built on Pie. packages/apps/FMRadio's libfmjni fails with
# -Werror (unused parameters/variables) and cannot be fixed from the device tree.

# Dalvik heap. The jdct values (48m growth limit, 2m max free) made GC run
# constantly at 1920x1200, where one full-screen ARGB bitmap is ~9 MB.
# Between those and tablet-10in-xhdpi-2048 (192m/512m): enough headroom for
# the launcher/wallpaper without letting one app hog the 2 GB.
PRODUCT_PROPERTY_OVERRIDES += \
    dalvik.vm.heapstartsize=8m \
    dalvik.vm.heapgrowthlimit=128m \
    dalvik.vm.heapsize=256m \
    dalvik.vm.heaptargetutilization=0.75 \
    dalvik.vm.heapminfree=512k \
    dalvik.vm.heapmaxfree=8m
    
# Media codecs
PRODUCT_COPY_FILES += \
    $(DEVICE_PATH)/configs/media_codecs.xml:system/etc/media_codecs.xml \
    frameworks/av/media/libstagefright/data/media_codecs_google_audio.xml:system/etc/media_codecs_google_audio.xml \
    frameworks/av/media/libstagefright/data/media_codecs_google_telephony.xml:system/etc/media_codecs_google_telephony.xml \
    frameworks/av/media/libstagefright/data/media_codecs_google_video_le.xml:system/etc/media_codecs_google_video.xml \
    $(DEVICE_PATH)/configs/media_profiles.xml:system/etc/media_profiles.xml	

# USB
PRODUCT_DEFAULT_PROPERTY_OVERRIDES += persist.sys.usb.config=mtp,adb

# Make adb usable without a working screen.
#
# adbd will not authorise a host unless ro.adb.secure=0, and the prompt that
# normally grants that authorisation is drawn by the UI - which is exactly what
# we do not have while the boot is broken. adbd therefore comes up and then
# waits forever for an authorisation that can never arrive, which presents
# identically to "adbd never started".
#
# On Pie ADDITIONAL_DEFAULT_PROPERTIES is gone; the equivalent is
# PRODUCT_SYSTEM_DEFAULT_PROPERTIES (-> /system/etc/prop.default). ro.* is
# write-once, vendor/lineage/config/common.mk appends ro.adb.secure=1 before
# this file is inherited, and the build keeps only the first entry for a given
# key, so prepending here makes ours win.
PRODUCT_SYSTEM_DEFAULT_PROPERTIES := ro.adb.secure=0 $(PRODUCT_SYSTEM_DEFAULT_PROPERTIES)

# AAPT
PRODUCT_AAPT_CONFIG := normal
PRODUCT_AAPT_PREF_CONFIG := xhdpi

PRODUCT_PACKAGES += \
    librs_jni \
    com.android.future.usb.accessory

$(call inherit-product, frameworks/native/build/tablet-10in-xhdpi-2048-dalvik-heap.mk)

# Packages that the framework still expects to exist, restored on top of
# whatever full_base.mk / vendor/cm/config pull in.
#
# These were removed during the de-phone pass, but parts of the framework
# dereference them unconditionally:
#   CarrierConfig            - Settings' TetherUtil.isEntitlementCheckRequired()
#                              calls CarrierConfigManager.getConfig().getBoolean()
#                              with no null check; with the app gone getConfig()
#                              returns null and opening the Connectivity (wireless)
#                              settings page crashes Settings with an NPE.
#   CellBroadcastReceiver    - WirelessSettings.onCreate() queries
#                              PackageManager.getApplicationEnabledSetting() for this
#                              package; if the package does not exist that throws
#                              NameNotFoundException, which the surrounding
#                              catch (IllegalArgumentException) does not cover.
#   Provision                - NOT restored on purpose: cm's SetupWizard declares
#                              LOCAL_OVERRIDES_PACKAGES := Provision, so AOSP's
#                              build system filters Provision out of the install
#                              list by design (main.mk: "Filter out (do not
#                              install) any overridden packages"). Adding it to
#                              PRODUCT_PACKAGES has no effect. The one-time
#                              SetupWizard crash in the bugreport ("No Activity
#                              found to handle com.android.wizard.LOAD") is a
#                              wizard-side issue on this tablet, not a missing
#                              package.
# The tablet has no modem, so CarrierConfig and CellBroadcastReceiver are inert
# at runtime - but Settings dereferences them unconditionally.
PRODUCT_PACKAGES += \
    CarrierConfig \
    CellBroadcastReceiver

# Bluetooth: Pie's bluetooth@1.0 HAL needs a BD address up front; bt_addr.sh
# writes the NVRAM one here at post-fs-data.
PRODUCT_DEFAULT_PROPERTY_OVERRIDES += ro.bt.bdaddr_path=/data/misc/bluetooth/bdaddr

# ART: no Baker read barriers on these 32-bit Cortex-A7s. The runtime then
# uses the CMS collector instead of concurrent copying, which removes the
# read-barrier overhead from every managed field load.
PRODUCT_ART_USE_READ_BARRIER := false

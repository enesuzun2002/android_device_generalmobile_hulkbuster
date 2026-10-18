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

ifeq ($(TARGET_PREBUILT_KERNEL),)
    LOCAL_KERNEL := $(LOCAL_PATH)/kernel
else
    LOCAL_KERNEL := $(TARGET_PREBUILT_KERNEL)
endif

# Symbols
PRODUCT_PACKAGES += libmtk_symbols

# Memtrack
PRODUCT_PACKAGES += memtrack.mt6592

# WebView support libraries.
#
# external/chromium-webview was removed from the manifest (it is delivered over
# git-lfs from review.lineageos.org and the prebuilt_* projects no longer carry
# a lineage-14.1 branch), so nothing adds these to PRODUCT_PACKAGES any more --
# frameworks/webview/chromium/chromium.mk, which normally does, is absent from
# this tree.  But system/core/rootdir/etc/public.libraries.android.txt still
# lists libwebviewchromium_plat_support.so, and the linker dlopen()s every
# soname named in /system/etc/public.libraries.txt
# (system/core/libnativeloader/native_loader.cpp, init_public_namespace()).
# With the library missing, android_init_namespaces() fails, zygote dies while
# creating system_server's classloader and system_server never starts.
#
# Both modules are built from source by frameworks/webview/chromium/Android.mk
# and link no chromium code (see the comment at the top of that file).
PRODUCT_PACKAGES += \
    libwebviewchromium_loader \
    libwebviewchromium_plat_support

# Audio
PRODUCT_PACKAGES += \
    audio.primary.mt6592 \
    audio_policy.default \
    audio.a2dp.default \
    audio.usb.default \
    audio.r_submix.default \
    libaudio-resampler \
    tinymix

PRODUCT_COPY_FILES += \
    $(DEVICE_PATH)/configs/audio_policy.conf:system/etc/audio_policy.conf

# GSM
PRODUCT_COPY_FILES += \
     $(DEVICE_PATH)/configs/spn-conf.xml:system/etc/spn-conf.xml

# Bluetooth
PRODUCT_PACKAGES += libbt-vendor

# Wifi
PRODUCT_PACKAGES += \
    lib_driver_cmd_mt66xx \
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
PRODUCT_COPY_FILES += \
    $(DEVICE_PATH)/rootdir/enableswap.sh:root/enableswap.sh \
    $(DEVICE_PATH)/rootdir/fstab.mt6592:root/fstab.mt6592 \
    $(DEVICE_PATH)/rootdir/init.modem.rc:root/init.modem.rc \
    $(DEVICE_PATH)/rootdir/init.mt6592.rc:root/init.mt6592.rc \
    $(DEVICE_PATH)/rootdir/init.mt6592.usb.rc:root/init.mt6592.usb.rc \
    $(DEVICE_PATH)/rootdir/ueventd.mt6592.rc:root/ueventd.mt6592.rc \
	$(DEVICE_PATH)/rootdir/ueventd.rc:root/ueventd.rc \
    $(DEVICE_PATH)/rootdir/twrp.fstab:recovery/root/etc/twrp.fstab \
    $(DEVICE_PATH)/rootdir/sbin/poweroff.sh:recovery/root/sbin/poweroff.sh \
    $(DEVICE_PATH)/rootdir/sbin/rebootrecovery.sh:recovery/root/sbin/rebootrecovery.sh \
    $(DEVICE_PATH)/rootdir/sbin/rebootsystem.sh:recovery/root/sbin/rebootsystem.sh \
    $(LOCAL_KERNEL):kernel

# On-device diagnostics (mtkdiag.sh + its watch service). Debug-only: it
# produces a rotating log tree under /data/mtkdiag. Kept enabled (the
# default) while this ROM is being validated; set MTKDIAG=false for a
# clean-base build.
MTKDIAG ?= true
ifeq ($(strip $(MTKDIAG)),true)
PRODUCT_COPY_FILES += \
    $(DEVICE_PATH)/rootdir/mtkdiag.sh:root/mtkdiag.sh
endif

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

# FM Radio app
PRODUCT_PACKAGES += \
    FMRadio \
    libfmjni

# Heap sizes aligned with the known-good 3.10.72 jdct build
# (blobs/3.10_system_props.txt). This device has 2GB RAM, so the stock
# tablet-10in-xhdpi values (512m heapsize / 192m growthlimit) are far
# too aggressive and risk OOM-killing the zygote early in boot.
PRODUCT_PROPERTY_OVERRIDES += \
    dalvik.vm.heapstartsize=5m \
    dalvik.vm.heapgrowthlimit=48m \
    dalvik.vm.heapsize=128m \
    dalvik.vm.heaptargetutilization=0.75 \
    dalvik.vm.heapminfree=512k \
    dalvik.vm.heapmaxfree=2m
    
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
# This has to go through ADDITIONAL_DEFAULT_PROPERTIES (-> /default.prop) rather
# than PRODUCT_PROPERTY_OVERRIDES (-> /system/build.prop): ro.* properties are
# write-once, init reads /default.prop before /system/build.prop, and the later
# write is dropped (the log shows `property_set("ro.secure", "0") failed`).
#
# vendor/cm/config/common.mk adds ro.adb.secure=1 during product config, and
# lineage.mk does that before inheriting this file, so prepending here puts ours
# first - and build/core/Makefile keeps only the first entry for a given key
# (uniq-pairs-by-first-component), so ours is the one that survives.
ADDITIONAL_DEFAULT_PROPERTIES := ro.adb.secure=0 $(ADDITIONAL_DEFAULT_PROPERTIES)

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
#   Provision                - handles the com.android.wizard.LOAD intent that
#                              SetupWizard fires when it finishes; without it the
#                              wizard dies with ActivityNotFoundException.
# The tablet has no modem, so these are inert at runtime - but they must be
# installed for Settings and SetupWizard to survive.
PRODUCT_PACKAGES += \
    CarrierConfig \
    CellBroadcastReceiver \
    Provision

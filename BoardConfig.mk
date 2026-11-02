DEVICE_PATH := device/generalmobile/hulkbuster
VENDOR_PATH := vendor/generalmobile/hulkbuster

# inherit from the proprietary version
-include $(VENDOR_PATH)/BoardConfigVendor.mk

# Platform
TARGET_BOARD_PLATFORM := mt6592
TARGET_NO_BOOTLOADER := true

# Architecture
TARGET_ARCH := arm
TARGET_ARCH_VARIANT := armv7-a-neon
TARGET_CPU_ABI := armeabi-v7a
TARGET_CPU_ABI2 := armeabi
TARGET_CPU_SMP := true
TARGET_CPU_VARIANT := cortex-a7

# Binder: the kernel and every blob speak the 32-bit binder protocol. Pie
# still supports it for devices that did not launch with Pie.
TARGET_USES_64_BIT_BINDER := false

# Kernel (lineage-16.0 branch: treble binder + SELinux xperms backports)
TARGET_KERNEL_SOURCE := kernel/mediatek/mt6592
TARGET_KERNEL_CONFIG := hulkbuster_defconfig
TARGET_KERNEL_ARCH := arm
TARGET_KERNEL_HEADER_ARCH := arm
BOARD_KERNEL_IMAGE_NAME := zImage
# SELinux is permissive during bring-up; the kernel already defaults to
# permissive-capable (SECURITY_SELINUX_DEVELOP).
BOARD_KERNEL_CMDLINE := androidboot.hardware=mt6592 androidboot.selinux=permissive
BOARD_KERNEL_BASE := 0x80000000
BOARD_KERNEL_PAGESIZE := 2048

# Partitions (dec, must fit the real partition map in blobs/dumchar_info.txt):
# boot 0xa00000, recovery 0xa00000, android 0x40000000.
BOARD_BOOTIMAGE_PARTITION_SIZE := 10485760
BOARD_RECOVERYIMAGE_PARTITION_SIZE := 10485760
BOARD_SYSTEMIMAGE_PARTITION_SIZE := 1073741824
BOARD_USERDATAIMAGE_PARTITION_SIZE := 1237319680
BOARD_CACHEIMAGE_PARTITION_SIZE := 134217728
BOARD_CACHEIMAGE_FILE_SYSTEM_TYPE := ext4
BOARD_FLASH_BLOCK_SIZE := 131072
TARGET_USERIMAGES_USE_EXT4 := true

# Kernel + ramdisk do not fit the 10 MB recovery partition with gzip.
LZMA_RAMDISK_TARGETS := recovery

# MTK boot image: 512-byte MTK headers in front of kernel and ramdisk.
# The offsets are dictated by the bootloader (ramdisk 0x04000000, tags
# 0x0e000000); mtkbootimg's compiled-in defaults leave the device hanging on
# the logo. BOARD_RAMDISK_OFFSET / BOARD_KERNEL_TAGS_OFFSET only reach the
# OTA zip, so they are passed here as well.
BOARD_CUSTOM_MKBOOTIMG := mtkbootimg
BOARD_CUSTOM_BOOTIMG_MK := $(DEVICE_PATH)/bootimg.mk
BOARD_CUSTOM_BOOTIMG := true
BOARD_RAMDISK_OFFSET := 0x04000000
BOARD_KERNEL_TAGS_OFFSET := 0x0e000000
BOARD_MKBOOTIMG_ARGS := --mtk 1 \
    --ramdisk_offset 0x04000000 \
    --tags_offset 0x0e000000 \
    --second_offset 0x00f00000

# Assert
TARGET_OTA_ASSERT_DEVICE := etab5,ETAB5,E-TAB5,E-Tab5,hulkbuster

# Shims for the Lollipop-era MTK blobs (symbols that moved or were removed
# from the platform libraries). Format: <blob path>|<shim lib>. A blob may be
# listed more than once. libmtk_atomic provides android_atomic_*, which Pie's
# libcutils no longer exports; libmtk_xlog provides __xlog_buf_printf;
# libmtk_gui the L-era libgui symbols used by libgui_ext.
TARGET_LD_SHIM_LIBS := \
    /system/lib/libui_ext.so|libmtk_symbols.so \
    /system/lib/libgui_ext.so|libmtk_symbols.so \
    /system/lib/hw/hwcomposer.mt6592.so|libmtk_symbols.so \
    /system/lib/libMtkOmxCore.so|libmtk_symbols.so \
    /system/lib/libmtkplayer.so|libmtk_symbols.so \
    /system/lib/hw/gps.mt6592.so|libmtk_symbols.so \
    /system/lib/libmnl.so|libmtk_symbols.so \
    /system/lib/libaudioflinger.so|libmtk_symbols.so \
    /system/lib/libaudio.primary.default.so|libmtk_symbols.so \
    /system/lib/hw/camera.default.so|libmtk_symbols.so \
    /system/lib/egl/libGLES_android.so|libmtk_atomic.so \
    /system/lib/hw/hwcomposer.mt6592.so|libmtk_atomic.so \
    /system/lib/libClearMotionFW.so|libmtk_atomic.so \
    /system/lib/libJpgDecPipe.so|libmtk_atomic.so \
    /system/lib/libGdmaScalerPipe.so|libmtk_atomic.so \
    /system/lib/libMali.so|libmtk_atomic.so \
    /system/lib/libMtkOmxVdec.so|libmtk_atomic.so \
    /system/lib/libMtkOmxVenc.so|libmtk_atomic.so \
    /system/lib/liba3m.so|libmtk_atomic.so \
    /system/lib/libSwJpgCodec.so|libmtk_atomic.so \
    /system/lib/libcam.device1.so|libmtk_atomic.so \
    /system/lib/libcam.camadapter.so|libmtk_atomic.so \
    /system/lib/libcam.utils.sensorlistener.so|libmtk_atomic.so \
    /system/lib/libcam.campipe.so|libmtk_atomic.so \
    /system/lib/libcam.camshot.so|libmtk_atomic.so \
    /system/lib/libcam.client.so|libmtk_atomic.so \
    /system/lib/libcam_utils.so|libmtk_atomic.so \
    /system/lib/libcamdrv.so|libmtk_atomic.so \
    /system/lib/libcorkscrew.so|libmtk_atomic.so \
    /system/lib/libfeatureio.so|libmtk_atomic.so \
    /system/lib/libgpu_aux.so|libmtk_atomic.so \
    /system/lib/libgui_ext.so|libmtk_atomic.so \
    /system/lib/libimageio.so|libmtk_atomic.so \
    /system/lib/libja3m.so|libmtk_atomic.so \
    /system/lib/libimageio_plat_drv.so|libmtk_atomic.so \
    /system/lib/libmtk_mmutils.so|libmtk_atomic.so \
    /system/lib/libvcodecdrv.so|libmtk_atomic.so \
    /system/lib/libMali.so|libutilscallstack.so \
    /system/lib/libgui_ext.so|libmtk_gui.so \
    /system/bin/dualmdlogger|libmtk_xlog.so \
    /system/bin/emcsmdlogger|libmtk_xlog.so \
    /system/bin/factory|libmtk_xlog.so \
    /system/bin/ipod|libmtk_xlog.so \
    /system/bin/mdlogger|libmtk_xlog.so \
    /system/bin/thermal|libmtk_xlog.so \
    /system/bin/thermald|libmtk_xlog.so \
    /system/lib/lib3a.so|libmtk_xlog.so \
    /system/lib/libAGifEncoder.so|libmtk_xlog.so \
    /system/lib/libBnMtkCodec.so|libmtk_xlog.so \
    /system/lib/libClearMotionFW.so|libmtk_xlog.so \
    /system/lib/libGdmaScalerPipe.so|libmtk_xlog.so \
    /system/lib/libJpgDecPipe.so|libmtk_xlog.so \
    /system/lib/libJpgEncPipe.so|libmtk_xlog.so \
    /system/lib/libMJCjni.so|libmtk_xlog.so \
    /system/lib/libMtkOmxAMRNBDec.so|libmtk_xlog.so \
    /system/lib/libMtkOmxAMRWBDec.so|libmtk_xlog.so \
    /system/lib/libMtkOmxAacDec.so|libmtk_xlog.so \
    /system/lib/libMtkOmxAacEnc.so|libmtk_xlog.so \
    /system/lib/libMtkOmxAdpcmDec.so|libmtk_xlog.so \
    /system/lib/libMtkOmxAdpcmEnc.so|libmtk_xlog.so \
    /system/lib/libMtkOmxAmrEnc.so|libmtk_xlog.so \
    /system/lib/libMtkOmxApeDec.so|libmtk_xlog.so \
    /system/lib/libMtkOmxAwbEnc.so|libmtk_xlog.so \
    /system/lib/libMtkOmxFlacDec.so|libmtk_xlog.so \
    /system/lib/libMtkOmxG711Dec.so|libmtk_xlog.so \
    /system/lib/libMtkOmxGsmDec.so|libmtk_xlog.so \
    /system/lib/libMtkOmxMp3Dec.so|libmtk_xlog.so \
    /system/lib/libMtkOmxRawDec.so|libmtk_xlog.so \
    /system/lib/libMtkOmxVorbisDec.so|libmtk_xlog.so \
    /system/lib/libMtkOmxVorbisEnc.so|libmtk_xlog.so \
    /system/lib/libSwJpgCodec.so|libmtk_xlog.so \
    /system/lib/libcam.camadapter.so|libmtk_xlog.so \
    /system/lib/libcam.campipe.so|libmtk_xlog.so \
    /system/lib/libcam.client.so|libmtk_xlog.so \
    /system/lib/libcam.device1.so|libmtk_xlog.so \
    /system/lib/libcam.paramsmgr.so|libmtk_xlog.so \
    /system/lib/libcam.utils.sensorlistener.so|libmtk_xlog.so \
    /system/lib/libcam.utils.so|libmtk_xlog.so \
    /system/lib/libcam_platform.so|libmtk_xlog.so \
    /system/lib/libcamalgo.so|libmtk_xlog.so \
    /system/lib/libcamdrv.so|libmtk_xlog.so \
    /system/lib/libcameracustom.so|libmtk_xlog.so \
    /system/lib/libdpframework.so|libmtk_xlog.so \
    /system/lib/libdrmmtkutil.so|libmtk_xlog.so \
    /system/lib/libfeatureio.so|libmtk_xlog.so \
    /system/lib/libgifEncoder_jni.so|libmtk_xlog.so \
    /system/lib/libimageio.so|libmtk_xlog.so \
    /system/lib/libimageio_plat_drv.so|libmtk_xlog.so \
    /system/lib/libmdloggerrecycle.so|libmtk_xlog.so \
    /system/lib/libmjc.so|libmtk_xlog.so \
    /system/lib/libmp4enc_sa.ca7.so|libmtk_xlog.so \
    /system/lib/libmpo.so|libmtk_xlog.so \
    /system/lib/libmpodecoder.so|libmtk_xlog.so \
    /system/lib/libmpoencoder.so|libmtk_xlog.so \
    /system/lib/libmpojni.so|libmtk_xlog.so \
    /system/lib/libshowlogo.so|libmtk_xlog.so \
    /system/lib/libvc1dec_sa.ca7.so|libmtk_xlog.so \
    /system/lib/libvcodec_utility.so|libmtk_xlog.so \
    /system/lib/libvcodecdrv.so|libmtk_xlog.so \
    /system/lib/libvp8dec_sa.ca7.so|libmtk_xlog.so \
    /system/xbin/BGW|libmtk_xlog.so \
    /system/xbin/mnld|libmtk_xlog.so

# MTK hardware
BOARD_HAS_MTK_HARDWARE := true
MTK_HARDWARE := true
BOARD_GLOBAL_CFLAGS += -DMTK_HARDWARE
BOARD_GLOBAL_CPPFLAGS += -DMTK_HARDWARE

# Graphics: legacy gralloc0/HWC1 blobs behind the passthrough HIDL HALs.
USE_OPENGL_RENDERER := true
NUM_FRAMEBUFFER_SURFACE_BUFFERS := 3
TARGET_RUNNING_WITHOUT_SYNC_FRAMEWORK := true
TARGET_HAS_HH_VSYNC_ISSUE := true
BOARD_EGL_CFG := $(DEVICE_PATH)/configs/egl.cfg

# Display
TARGET_SCREEN_DENSITY := 220

# Audio
BOARD_GLOBAL_CFLAGS += -DMTK_AUDIO_BLOB
USE_XML_AUDIO_POLICY_CONF := 0

# Bluetooth
BOARD_HAVE_BLUETOOTH := true
BOARD_BLUETOOTH_BDROID_BUILDCFG_INCLUDE_DIR := $(DEVICE_PATH)/bluetooth/bdroid_buildcfg

# Wifi (MT6592 combo through /dev/wmtWifi)
BOARD_CONNECTIVITY_VENDOR := MediaTek
BOARD_CONNECTIVITY_MODULE := conn_soc
BOARD_WLAN_DEVICE := MediaTek
WPA_SUPPLICANT_VERSION := VER_0_8_X
BOARD_HOSTAPD_DRIVER := NL80211
BOARD_HOSTAPD_PRIVATE_LIB := lib_driver_cmd_mt66xx
BOARD_WPA_SUPPLICANT_DRIVER := NL80211
BOARD_WPA_SUPPLICANT_PRIVATE_LIB := lib_driver_cmd_mt66xx
WIFI_DRIVER_FW_PATH_PARAM := "/dev/wmtWifi"
WIFI_DRIVER_FW_PATH_STA := STA
WIFI_DRIVER_FW_PATH_AP := AP
WIFI_DRIVER_FW_PATH_P2P := P2P
WIFI_DRIVER_STATE_CTRL_PARAM := "/dev/wmtWifi"
WIFI_DRIVER_STATE_ON := 1
WIFI_DRIVER_STATE_OFF := 0

# Offline charging
BOARD_CHARGING_MODE_BOOTING_LPM := /sys/class/BOOT/BOOT/boot/boot_mode
BACKLIGHT_PATH := /sys/class/leds/lcd-backlight/brightness
BOARD_CHARGER_ENABLE_SUSPEND := true
BOARD_CHARGER_SHOW_PERCENTAGE := true

TARGET_SYSTEM_PROP := $(DEVICE_PATH)/system.prop

# Sensors
TARGET_NO_SENSOR_PERMISSION_CHECK := true

# HIDL
DEVICE_MANIFEST_FILE := $(DEVICE_PATH)/manifest.xml

# Dex preopt stays off: the 14.1 experiment never had ART use the prebuilt
# boot image on this device (see git history), so it only cost build time.
WITH_DEXPREOPT := false

# Recovery
TARGET_RECOVERY_FSTAB := $(DEVICE_PATH)/rootdir/recovery.fstab
BOARD_HAS_NO_SELECT_BUTTON := true
TARGET_RECOVERY_PIXEL_FORMAT := "RGBA_8888"

# Use custom lun file path
TARGET_USE_CUSTOM_LUN_FILE_PATH := "/sys/devices/virtual/android_usb/android0/f_mass_storage/lun%d/file"

# SELinux
BOARD_SEPOLICY_DIRS += $(DEVICE_PATH)/sepolicy


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

# Kernel 
TARGET_PREBUILT_KERNEL := $(DEVICE_PATH)/kernel
# Matches the known-good 3.10.72 jdct build (see /workspace/unpacked_boot).
# bootopt/selinux are carried over verbatim from that build for consistency;
# note the kernel itself contains neither string, so they are inert.
# console= is added so kernel/init output is available over UART for debugging.
BOARD_KERNEL_CMDLINE := console=ttyMT0,115200n8 androidboot.hardware=mt6592
BOARD_KERNEL_BASE := 0x80000000
BOARD_KERNEL_PAGESIZE := 2048

# Also emitted into the ZIP so flash tooling records the same offsets.
BOARD_RAMDISK_OFFSET := 0x04000000
BOARD_KERNEL_TAGS_OFFSET := 0x0e000000

# Sizes are dec format and must fit the real partition map
# (blobs/dumchar_info.txt): boot 0xa00000, recovery 0xa00000, android 0x40000000.
BOARD_BOOTIMAGE_PARTITION_SIZE := 10485760
BOARD_RECOVERYIMAGE_PARTITION_SIZE := 10485760
BOARD_SYSTEMIMAGE_PARTITION_SIZE := 1073741824
BOARD_USERDATAIMAGE_PARTITION_SIZE := 1237319680
BOARD_CACHEIMAGE_PARTITION_SIZE := 134217728
BOARD_FLASH_BLOCK_SIZE := 131072
TARGET_USERIMAGES_USE_EXT4 := true
TARGET_USERIMAGES_SPARSE_EXT_DISABLED := false

# The 3.10.72 kernel is larger than the old 3.4.67 one, so kernel+ramdisk no
# longer fits the real 10MB recovery partition at gzip. Compress the recovery
# ramdisk with LZMA (requires an `lzma` binary on the build host).
LZMA_RAMDISK_TARGETS := recovery

# Enable kmodules
TARGET_KMODULES := true

# Old kernel support
OLD_KERNEL := true

# Custom bootimg
BOARD_CUSTOM_MKBOOTIMG := mtkbootimg
BOARD_CUSTOM_BOOTIMG_MK := $(DEVICE_PATH)/bootimg.mk
BOARD_CUSTOM_BOOTIMG := true

# mkbootimg ARGS
# The offsets below are dictated by the hardware and match the known-good
# 3.10.72 image (/workspace/unpacked_boot/boot.img-{ramdisk,tags}_offset).
# mtkbootimg.c's compiled-in defaults (ramdisk 0x01000000, tags 0x00000100)
# are wrong for this board and leave the device hanging on the bootloader logo.
# BOARD_RAMDISK_OFFSET / BOARD_KERNEL_TAGS_OFFSET are NOT passed to mkbootimg
# by the build system (build/core/Makefile only writes them into the ZIP), so
# the flags must go here.
BOARD_MKBOOTIMG_ARGS := --mtk 1 \
    --ramdisk_offset 0x04000000 \
    --tags_offset 0x0e000000 \
    --second_offset 0x00f00000

# Dex preoptimization
# OFF again. The boot-image pre-opt experiment (WITH_DEXPREOPT +
# WITH_DEXPREOPT_BOOT_IMG_ONLY) shipped in build 88e67bfc28 together with the
# app trim, and the device showed two problems in that combination:
#   1. system_server crashed with a SIGSEGV inside a JNI reflection call
#      (art::Constructor_newInstance0 -> a return address that is not code at
#      all: an OatMethodOffsets slot in core-oj.oat), so ART executed a bad
#      method pointer. (Tracked as the "wifi connect -> soft restart" bug.)
#   2. ART never used the prebuilt oat: the crash maps
#      /data/dalvik-cache/arm/system@framework@boot-core-oj.oat, i.e. the
#      framework was compiled on device at first boot instead.
# Since (2) means the experiment buys nothing on this device while (1) is an
# unresolved crash in the only build that had it enabled, pre-opt stays off.
# To retry it later, build with both flags true and check on device that
# /system/framework/arm/boot.art is actually mapped before believing any
# speedup.
WITH_DEXPREOPT := false
DONT_DEXPREOPT_PREBUILTS := true

# Link libmtk_symbols
TARGET_LDPRELOAD := libmtk_symbols.so

# Assert
TARGET_OTA_ASSERT_DEVICE := etab5, ETAB5, E-TAB5, E-Tab5, hulkbuster

# MTK hardware
BOARD_HAS_MTK_HARDWARE := true
MTK_HARDWARE := true

BOARD_GLOBAL_CFLAGS += -DMTK_HARDWARE
BOARD_GLOBAL_CPPFLAGS += -DMTK_HARDWARE

TARGET_GLOBAL_CFLAGS   += -mfpu=neon -mfloat-abi=softfp
TARGET_GLOBAL_CPPFLAGS += -mfpu=neon -mfloat-abi=softfp

# Disable hw id match check
BOARD_GLOBAL_CFLAGS += -DDISABLE_HW_ID_MATCH_CHECK

# Disable sync framework
TARGET_RUNNING_WITHOUT_SYNC_FRAMEWORK := true

# Enable Legacy MTK AV blobs
BOARD_USES_LEGACY_MTK_AV_BLOB := true

# Disable ashmem tracking
BOARD_GLOBAL_CFLAGS += -DDISABLE_ASHMEM_TRACKING

# Fix fake NVRAM
NVRAM_WARNING := true

# Disable block based OTA
BLOCK_BASED_OTA := false

# Offline charging
BOARD_CHARGING_MODE_BOOTING_LPM := /sys/class/BOOT/BOOT/boot/boot_mode
BACKLIGHT_PATH := /sys/class/leds/lcd-backlight/brightness
BOARD_CHARGER_ENABLE_SUSPEND := true
BOARD_CHARGER_SHOW_PERCENTAGE := true

# Sensors
TARGET_NO_SENSOR_PERMISSION_CHECK := true

# EGL settings
BOARD_EGL_CFG := $(DEVICE_PATH)/configs/egl.cfg
USE_OPENGL_RENDERER := true
BOARD_EGL_WORKAROUND_BUG_10194508 := true
BOARD_EGL_NEEDS_HANDLE_VALUE := true
BOARD_EGL_NEEDS_FNW := true
TARGET_REQUIRES_SYNCHRONOUS_SETSURFACE := true

# Display
TARGET_BOOTANIMATION_MULTITHREAD_DECODE := true

# Audio
BOARD_GLOBAL_CFLAGS += -DMTK_AUDIO_BLOB

# Connectivity
BOARD_CONNECTIVITY_VENDOR := MediaTek
BOARD_CONNECTIVITY_MODULE := conn_soc

# Bluetooth
BOARD_BLUETOOTH_BDROID_BUILDCFG_INCLUDE_DIR := $(DEVICE_PATH)/bluetooth/bdroid_buildcfg

# WIFI
WPA_SUPPLICANT_VERSION := VER_0_8_X
BOARD_HOSTAPD_DRIVER := NL80211
BOARD_HOSTAPD_PRIVATE_LIB := lib_driver_cmd_mt66xx
BOARD_WPA_SUPPLICANT_DRIVER := NL80211
BOARD_WPA_SUPPLICANT_PRIVATE_LIB := lib_driver_cmd_mt66xx
WIFI_DRIVER_FW_PATH_PARAM:="/dev/wmtWifi"
WIFI_DRIVER_FW_PATH_STA:=STA
WIFI_DRIVER_FW_PATH_AP:=AP
WIFI_DRIVER_FW_PATH_P2P:=P2P

# TWRP
BOARD_HAS_NO_SELECT_BUTTON := true
RECOVERY_SDCARD_ON_DATA := true
RECOVERY_GRAPHICS_USE_LINELENGTH := true
TARGET_RECOVERY_FSTAB := $(DEVICE_PATH)/rootdir/recovery.fstab
TARGET_RECOVERY_DEVICE_DIRS += $(LOCAL_PATH_DEVICE)
TARGET_NO_TWO_STEP_RECOVERY := true
TW_USE_MODEL_HARDWARE_ID_FOR_DEVICE_ID := true
TW_NO_REBOOT_BOOTLOADER := true
TW_THEME := portrait_xxhdpi
TW_BRIGHTNESS_PATH := /sys/devices/platform/leds-mt65xx/leds/lcd-backlight/brightness
TW_MAX_BRIGHTNESS := 255
TW_DEFAULT_EXTERNAL_STORAGE := true
TW_CRYPTO_FS_TYPE := "ext4"
TW_CRYPTO_REAL_BLKDEV := "/dev/block/mmcblk0p7"
TW_CRYPTO_MNT_POINT := "/data"
TW_CRYPTO_FS_OPTIONS := "nosuid,nodev,noatime,discard,noauto_da_alloc,data=ordered"
TW_NO_USB_STORAGE := true
TW_EXCLUDE_SUPERSU := true
TW_INCLUDE_FB2PNG := true
TW_CUSTOM_CPU_TEMP_PATH := /sys/class/thermal/thermal_zone1/temp
TW_EXTRA_LANGUAGES := true

# Use custom lun file path
TARGET_USE_CUSTOM_LUN_FILE_PATH := "/sys/devices/virtual/android_usb/android0/f_mass_storage/lun%d/file"

# Enable Minikin text layout engine
USE_MINIKIN := true

# SELinux
BOARD_SEPOLICY_DIRS += $(DEVICE_PATH)/sepolicy

# Sepolicy hack for 3.4.67 kernel
POLICYVERS := 26

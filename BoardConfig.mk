DEVICE_PATH := device/generalmobile/hulkbuster

# Platform
TARGET_BOARD_PLATFORM := mt6592
TARGET_BOOTLOADER_BOARD_NAME := mt6592
TARGET_NO_BOOTLOADER := true
TARGET_NO_RADIOIMAGE := true

# Architecture
TARGET_ARCH := arm
TARGET_ARCH_VARIANT := armv7-a-neon
TARGET_CPU_ABI := armeabi-v7a
TARGET_CPU_ABI2 := armeabi
TARGET_CPU_VARIANT := cortex-a7
TARGET_CPU_SMP := true

# prebuilt/zImage is the LOS kernel Image re-wrapped with an LZMA decompressor
# stub (tools/mk_lzma_zimage.sh); the original gzip zImage is zImage.gzip.orig.
# Needed: gzip zImage + TWRP ramdisk exceed the 10 MB recovery partition.
# Kernel: prebuilt 3.10 zImage (CONFIG_QUOTA) from the LineageOS tree. Its
# built-in CONFIG_CMDLINE already carries vmalloc=496M slub_max_order=0.
TARGET_PREBUILT_KERNEL := $(DEVICE_PATH)/prebuilt/zImage
BOARD_KERNEL_CMDLINE := androidboot.hardware=mt6592 androidboot.selinux=permissive
BOARD_KERNEL_BASE := 0x80000000
BOARD_KERNEL_PAGESIZE := 2048
BOARD_RAMDISK_OFFSET := 0x04000000
BOARD_KERNEL_TAGS_OFFSET := 0x0e000000

# MTK boot image: 512-byte MTK headers in front of kernel and ramdisk.
BOARD_CUSTOM_MKBOOTIMG := mtkbootimg
BOARD_CUSTOM_BOOTIMG_MK := $(DEVICE_PATH)/bootimg.mk
BOARD_CUSTOM_BOOTIMG := true
BOARD_MKBOOTIMG_ARGS := --mtk 1 \
    --ramdisk_offset 0x04000000 \
    --tags_offset 0x0e000000 \
    --second_offset 0x00f00000

# Partitions (hard limit: recovery partition is 0xa00000)
BOARD_BOOTIMAGE_PARTITION_SIZE := 10485760
BOARD_RECOVERYIMAGE_PARTITION_SIZE := 10485760
BOARD_SYSTEMIMAGE_PARTITION_SIZE := 1073741824
BOARD_USERDATAIMAGE_PARTITION_SIZE := 1237319680
BOARD_CACHEIMAGE_PARTITION_SIZE := 134217728
BOARD_CACHEIMAGE_FILE_SYSTEM_TYPE := ext4
BOARD_FLASH_BLOCK_SIZE := 131072
TARGET_USERIMAGES_USE_EXT4 := true
TARGET_RECOVERY_FSTAB := $(DEVICE_PATH)/recovery.fstab

# Ramdisk compression (kernel has CONFIG_RD_GZIP/LZMA/XZ)
LZMA_RAMDISK_TARGETS := recovery

# TWRP 9.0 / build overrides
PLATFORM_VERSION := 16.1.0
PLATFORM_SECURITY_PATCH := 2127-12-31
TARGET_OTA_ASSERT_DEVICE := etab5,ETAB5,E-TAB5,E-Tab5,hulkbuster

# Recovery / TWRP
RECOVERY_SDCARD_ON_DATA := true
BOARD_HAS_NO_SELECT_BUTTON := true
BOARD_SUPPRESS_SECURE_ERASE := true
TARGET_RECOVERY_PIXEL_FORMAT := "RGBA_8888"
RECOVERY_GRAPHICS_USE_LINELENGTH := true
TW_THEME := landscape_hdpi
TW_BRIGHTNESS_PATH := /sys/class/leds/lcd-backlight/brightness
TW_MAX_BRIGHTNESS := 255
TW_DEFAULT_BRIGHTNESS := 160
TW_CUSTOM_CPU_TEMP_PATH := /sys/class/thermal/thermal_zone1/temp
TW_DEFAULT_EXTERNAL_STORAGE := false
TW_INCLUDE_CRYPTO := false
TW_NO_USB_STORAGE := true
TW_USE_TOOLBOX := true
TW_EXCLUDE_SUPERSU := true
TW_EXCLUDE_TWRPAPP := true
TW_NO_REBOOT_BOOTLOADER := true
TW_EXTRA_LANGUAGES := false
TW_INCLUDE_NTFS_3G := false

# Size trimming: kernel (5.6 MB) + ramdisk must fit in 10 MB
TW_EXCLUDE_NANO := true
TW_EXCLUDE_BASH := true
TW_EXCLUDE_PYTHON := true
TW_EXCLUDE_TZDATA := true
TW_EXCLUDE_ENCRYPTED_BACKUPS := true
TW_NO_EXFAT_FUSE := true

# f2fs: the kernel has the f2fs-stable 3.10 backport; include mkfs.f2fs/fsck.f2fs
# so /data can be formatted f2fs (no -O features: the backport predates them).
TARGET_USERIMAGES_USE_F2FS := true

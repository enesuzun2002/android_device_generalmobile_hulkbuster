# Device path
DEVICE_PATH := device/generalmobile/hulkbuster

# Vendor path
VENDOR_PATH := vendor/generalmobile/hulkbuster

# Release name
PRODUCT_RELEASE_NAME := hulkbuster

# Inherit some common LineageOS stuff.
$(call inherit-product, vendor/lineage/config/common_full_tablet_wifionly.mk)

# Inherit device configuration
$(call inherit-product, $(DEVICE_PATH)/device.mk)

# Boot animation
TARGET_SCREEN_HEIGHT := 1920
TARGET_SCREEN_WIDTH := 1200

# Device identifier. This must come after all inclusions
PRODUCT_DEVICE := hulkbuster
PRODUCT_NAME := lineage_hulkbuster
PRODUCT_BRAND := GeneralMobile
PRODUCT_MODEL := hulkbuster
PRODUCT_MANUFACTURER := GeneralMobile

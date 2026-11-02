# TWRP 9.0 device tree for the General Mobile E-Tab5 (hulkbuster, MT6592).

DEVICE_PATH := device/generalmobile/hulkbuster

$(call inherit-product, $(SRC_TARGET_DIR)/product/embedded.mk)
$(call inherit-product, $(SRC_TARGET_DIR)/product/full_base.mk)
$(call inherit-product, vendor/omni/config/common.mk)

PRODUCT_DEVICE := hulkbuster
PRODUCT_NAME := omni_hulkbuster
PRODUCT_BRAND := GeneralMobile
PRODUCT_MODEL := hulkbuster
PRODUCT_MANUFACTURER := GeneralMobile
PRODUCT_RELEASE_NAME := E-Tab5

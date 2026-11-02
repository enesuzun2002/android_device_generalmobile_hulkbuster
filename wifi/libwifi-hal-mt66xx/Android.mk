LOCAL_PATH := $(call my-dir)

# Pie's frameworks/opt/net/wifi/libwifi_hal picks libwifi-hal-mt66xx when
# BOARD_WLAN_DEVICE is MediaTek, but nothing in the tree provides it. The
# MT6592 combo has no vendor wifi HAL (the driver only speaks nl80211 to the
# supplicant). A NOT_SUPPORTED stub is not enough on Pie: the HIDL service
# then refuses to start. wifi_hal_mt66xx.cpp implements just the lifecycle
# (initialize / event loop / cleanup / interface list / feature set).
include $(CLEAR_VARS)
LOCAL_MODULE := libwifi-hal-mt66xx
LOCAL_VENDOR_MODULE := true
LOCAL_SRC_FILES := wifi_hal_mt66xx.cpp
LOCAL_HEADER_LIBRARIES := libhardware_legacy_headers
include $(BUILD_STATIC_LIBRARY)

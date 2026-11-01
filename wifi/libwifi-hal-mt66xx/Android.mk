LOCAL_PATH := $(call my-dir)

# Pie's frameworks/opt/net/wifi/libwifi_hal picks libwifi-hal-mt66xx when
# BOARD_WLAN_DEVICE is MediaTek, but nothing in the tree provides it. The
# MT6592 combo has no vendor wifi HAL (the legacy blob only exposes the nl80211
# supplicant interface), so this is the same "not supported" stub as the
# framework's fallback: it makes android.hardware.wifi@1.0 start/stop the
# driver (via WIFI_DRIVER_STATE_CTRL_PARAM) without any chip feature set.
include $(CLEAR_VARS)
LOCAL_MODULE := libwifi-hal-mt66xx
LOCAL_VENDOR_MODULE := true
LOCAL_SRC_FILES := wifi_hal_mt66xx.cpp
LOCAL_HEADER_LIBRARIES := libhardware_legacy_headers
include $(BUILD_STATIC_LIBRARY)

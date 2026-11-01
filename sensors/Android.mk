LOCAL_PATH := $(call my-dir)

# Wraps the MTK sensors.mt6592.so blob and adds the BMG160 gyroscope.
# Selected with ro.hardware.sensors=hulkbuster (system.prop).
include $(CLEAR_VARS)
LOCAL_MODULE := sensors.hulkbuster
LOCAL_MODULE_RELATIVE_PATH := hw
LOCAL_MODULE_TAGS := optional
LOCAL_SRC_FILES := sensors_wrapper.c
LOCAL_SHARED_LIBRARIES := liblog libcutils libhardware libdl
LOCAL_CFLAGS := -Wall -Werror
include $(BUILD_SHARED_LIBRARY)

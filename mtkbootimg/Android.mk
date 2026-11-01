
ifeq ($(BOARD_CUSTOM_BOOTIMG),true)
LOCAL_PATH := $(call my-dir)

include $(CLEAR_VARS)

LOCAL_SRC_FILES := mkbootimg.c
# Pie dropped libmincrypt; SHA-1 comes from BoringSSL.
LOCAL_STATIC_LIBRARIES := libcrypto_static
LOCAL_CFLAGS := -Wno-error

LOCAL_MODULE := mtkbootimg

LOCAL_C_INCLUDES := $(LOCAL_PATH)
include $(BUILD_HOST_EXECUTABLE)

$(call dist-for-goals,dist_files,$(LOCAL_BUILT_MODULE))

endif

LOCAL_PATH := $(call my-dir)

include $(CLEAR_VARS)

LOCAL_SRC_FILES := \
             ril.cpp \
             xlog.cpp \
             wvm.cpp \
             gps.cpp \
             egl.cpp \
             audio.cpp \
             ui.cpp \
             crypto.c \
             ssl.cpp \
             mtkplayer.cpp

LOCAL_SHARED_LIBRARIES := libbinder libcutils libutils libutilscallstack liblog libicuuc libui libcrypto
LOCAL_MODULE := libmtk_symbols
LOCAL_MODULE_TAGS := optional

include $(BUILD_SHARED_LIBRARY)

# android_atomic_* for the old MTK blobs. Kept separate from libmtk_symbols
# so it only pulls in libc (it is shimmed into libMali and friends).
include $(CLEAR_VARS)

LOCAL_SRC_FILES := atomic.c
LOCAL_MODULE := libmtk_atomic
LOCAL_MODULE_TAGS := optional

include $(BUILD_SHARED_LIBRARY)

# __xlog_buf_printf only (needs just liblog), for the many xlog users.
include $(CLEAR_VARS)

LOCAL_SRC_FILES := xlog_light.c
LOCAL_SHARED_LIBRARIES := liblog
LOCAL_MODULE := libmtk_xlog
LOCAL_MODULE_TAGS := optional

include $(BUILD_SHARED_LIBRARY)

# Old libgui ABI for libgui_ext.so.
include $(CLEAR_VARS)

LOCAL_SRC_FILES := gui.cpp
LOCAL_SHARED_LIBRARIES := libgui libui libutils libcutils liblog
LOCAL_MODULE := libmtk_gui
LOCAL_MODULE_TAGS := optional

include $(BUILD_SHARED_LIBRARY)

# Lollipop-era MTK camera HAL ABI (GraphicBuffer ctors, GraphicBufferMapper
# int-usage overloads, MtkCameraParameters::APP_MODE_NAME_MTK_S3D, 2-arg
# CamExif::appendDebugExif). Shimmed into the camera blobs only.
include $(CLEAR_VARS)

LOCAL_SRC_FILES := camera.cpp
LOCAL_SHARED_LIBRARIES := libui libutils liblog libsensor
LOCAL_MODULE := libmtk_camera
LOCAL_MODULE_TAGS := optional

include $(BUILD_SHARED_LIBRARY)

# Empty libskia.so: the MTK camera blobs list it as DT_NEEDED but import no
# Skia symbol from it. Pie has no system libskia.so (Skia is linked into
# libhwui; the Soong "libskia" module is a static library only), so the
# dependency just has to be satisfiable. An Android.mk library cannot set a
# module stem and Soong has no `stem` here, hence the module name must be the
# file name.
include $(CLEAR_VARS)

LOCAL_SRC_FILES := skia_stub.c
LOCAL_MODULE := libskia
LOCAL_MODULE_TAGS := optional

include $(BUILD_SHARED_LIBRARY)

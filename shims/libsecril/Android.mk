LOCAL_PATH := $(call my-dir)

include $(CLEAR_VARS)
LOCAL_SRC_FILES := libsecril_shim.cpp
LOCAL_MODULE := libsecril_shim
LOCAL_MODULE_TAGS := optional
LOCAL_PROPRIETARY_MODULE := true
LOCAL_MULTILIB := 32
LOCAL_CFLAGS += -fvisibility=default
LOCAL_LDFLAGS += -Wl,-z,global
include $(BUILD_SHARED_LIBRARY)


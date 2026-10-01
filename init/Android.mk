LOCAL_PATH := $(call my-dir)

include $(CLEAR_VARS)

LOCAL_MODULE := libinit_universal7870
LOCAL_MODULE_TAGS := optional
LOCAL_SRC_FILES := init_universal7870.cpp
LOCAL_STATIC_LIBRARIES := libbase
LOCAL_C_INCLUDES := \
    system/libbase/include \
    system/core/init

include $(BUILD_STATIC_LIBRARY)

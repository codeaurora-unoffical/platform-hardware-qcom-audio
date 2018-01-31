LOCAL_PATH:= $(call my-dir)

# audio preprocessing wrapper
include $(CLEAR_VARS)

LOCAL_CFLAGS += -Wno-unused-variable -Wno-gnu-designator -Wno-unused-value -Wno-unused-function

LOCAL_MODULE:= libqcomvoiceprocessing
LOCAL_MODULE_TAGS := optional
LOCAL_MODULE_RELATIVE_PATH := soundfx
LOCAL_VENDOR_MODULE := true

LOCAL_SRC_FILES:= \
    voice_processing.c

LOCAL_C_INCLUDES += \
    $(call include-path-for, audio-effects)

LOCAL_SHARED_LIBRARIES := \
    liblog \
    libcutils

LOCAL_SHARED_LIBRARIES += libdl

LOCAL_CFLAGS += -fvisibility=hidden

include $(BUILD_SHARED_LIBRARY)

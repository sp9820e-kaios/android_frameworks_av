LOCAL_PATH:= $(call my-dir)
include $(CLEAR_VARS)

LOCAL_SRC_FILES := \
        SoftRaw.cpp

LOCAL_C_INCLUDES := \
        frameworks/av/media/libstagefright/include \
        frameworks/native/include/media/openmax

ifeq ($(AUDIO_24BITS_OUTPUT), 1)
    LOCAL_CFLAGS += -DAUDIO_24BIT_PLAYBACK_SUPPORT
endif

LOCAL_CFLAGS += -Werror

LOCAL_SHARED_LIBRARIES := \
        libstagefright_omx libstagefright_foundation libutils liblog

LOCAL_MODULE := libstagefright_soft_rawdec
LOCAL_MODULE_TAGS := optional

include $(BUILD_SHARED_LIBRARY)

LOCAL_PATH:= $(call my-dir)

ifneq ($(BOARD_USE_CUSTOM_MEDIASERVEREXTENSIONS),true)
include $(CLEAR_VARS)
LOCAL_SRC_FILES := register.cpp
LOCAL_MODULE := libregistermsext
LOCAL_MODULE_TAGS := optional
include $(BUILD_STATIC_LIBRARY)
endif

include $(CLEAR_VARS)

LOCAL_SRC_FILES:= \
	main_mediaserver.cpp

LOCAL_SHARED_LIBRARIES := \
	libaudioflinger \
	libaudiopolicyservice \
	libcamera_metadata\
	libcameraservice \
	libicuuc \
	libmedialogservice \
	libresourcemanagerservice \
	libcutils \
	libnbaio \
	libmedia \
	libmediaplayerservice \
	libutils \
	liblog \
	libbinder \
	libsoundtriggerservice \
	libradioservice

LOCAL_STATIC_LIBRARIES := \
        libicuandroid_utils \
        libregistermsext

LOCAL_C_INCLUDES := \
    frameworks/av/media/libmediaplayerservice \
    frameworks/av/media/libmedia \
    frameworks/av/services/medialog \
    frameworks/av/services/audioflinger \
    frameworks/av/services/audiopolicy \
    frameworks/av/services/audiopolicy/common/managerdefinitions/include \
    frameworks/av/services/audiopolicy/common/include \
    frameworks/av/services/audiopolicy/engine/interface \
    frameworks/av/services/camera/libcameraservice \
    frameworks/av/services/mediaresourcemanager \
    $(call include-path-for, audio-utils) \
    frameworks/av/services/soundtrigger \
    frameworks/av/services/radio \
    external/sonic

ifeq ($(strip $(VOLTE_SERVICE_ENABLE)), true)
LOCAL_SHARED_LIBRARIES += libVideoCallEngineService
LOCAL_C_INCLUDES += frameworks/av/volte
LOCAL_CFLAGS += -DMEDIA_VOLTE_ENABLE
endif

LOCAL_MODULE:= mediaserver
LOCAL_32_BIT_ONLY := true

include $(BUILD_EXECUTABLE)

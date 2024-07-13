LOCAL_PATH:= $(call my-dir)

################################################################################

ifneq ($(TARGET_VOLTE_VIDEO_2_0_LIB_DIR),)
include $(CLEAR_VARS)

LOCAL_MODULE := libvideo_2_0
LOCAL_MODULE_SUFFIX := .so
LOCAL_MODULE_CLASS := SHARED_LIBRARIES
LOCAL_SRC_FILES := ../../../$(TARGET_VOLTE_VIDEO_2_0_LIB_DIR)/libvideo_2_0.so

LOCAL_32_BIT_ONLY := true
include $(BUILD_PREBUILT)

include $(CLEAR_VARS)

LOCAL_MODULE := libvcecamera
LOCAL_MODULE_SUFFIX := .so
LOCAL_MODULE_CLASS := SHARED_LIBRARIES
LOCAL_SRC_FILES := ../../../$(TARGET_VOLTE_VIDEO_2_0_LIB_DIR)/libvcecamera.so

LOCAL_32_BIT_ONLY := true
include $(BUILD_PREBUILT)

endif

################################################################################
include $(CLEAR_VARS)

LOCAL_SRC_FILES:= \
    VideoCallEngineClient.cpp \
    VideoCallEngineAvcUtils.cpp \
    VideoCallEngineSoftwareRenderer.cpp

LOCAL_SHARED_LIBRARIES := \
    libcamera_client libvideo_2_0 \
    libstagefright liblog libutils libbinder libstagefright_foundation \
    libmedia libgui libcutils libui libc

LOCAL_SHARED_LIBRARIES += libvcecamera

LOCAL_C_INCLUDES:= \
    frameworks/av/media/libstagefright \
    frameworks/av/services/camera/libcameraservice \
    frameworks/av/services/camera/libcameraservice/api1 \
    system/media/camera/include \
    $(TOP)/frameworks/native/include/media/openmax

#add for vce camera
LOCAL_C_INCLUDES += \
        hardware/libhardware/include/hardware \
        frameworks/av/include/camera \
        frameworks/av/services/camera/libcameraservice \
        frameworks/av/services/camera/libcameraservice/device1

ifeq ($(strip $(BOARD_KAIOS_CONFIG)), true)
LOCAL_C_INCLUDES += $(LOCAL_PATH)/include_ac702
else
ifneq ($(TARGET_VOLTE_VIDEO_2_0_C_INCLUDE_SOURCE),)
LOCAL_C_INCLUDES += $(TARGET_VOLTE_VIDEO_2_0_C_INCLUDE_SOURCE)/vendor/sprd/proprietories-source/volte_engine/include_ac702
else
LOCAL_C_INCLUDES += vendor/sprd/proprietories-source/volte_engine/include_ac702
endif
endif

LOCAL_CFLAGS += -Wno-multichar

LOCAL_MODULE_TAGS := optional

LOCAL_32_BIT_ONLY := true
LOCAL_MODULE:= libVideoCallEngineClient

include $(BUILD_SHARED_LIBRARY)

################################################################################

include $(CLEAR_VARS)

LOCAL_SRC_FILES:= \
    VideoCallEngineService.cpp \

LOCAL_SHARED_LIBRARIES := \
    libVideoCallEngineClient \
    libcamera_client \
    liblog libutils libbinder \
    libgui

LOCAL_C_INCLUDES:= \
    frameworks/av/media/libstagefright \
    system/media/camera/include \
    $(TOP)/frameworks/native/include/media/openmax

LOCAL_CFLAGS += -Wno-multichar

LOCAL_MODULE_TAGS := optional

LOCAL_32_BIT_ONLY := true
LOCAL_MODULE:= libVideoCallEngineService

include $(BUILD_SHARED_LIBRARY)

################################################################################

include $(CLEAR_VARS)

LOCAL_SRC_FILES:= \
    VideoCallEngineServer.cpp \

LOCAL_SHARED_LIBRARIES := \
    libVideoCallEngineService \
    libstagefright liblog libutils libbinder libstagefright_foundation \
    libmedia libgui libcutils libui libc

LOCAL_C_INCLUDES:= \
    frameworks/av/media/libstagefright \
    $(TOP)/frameworks/native/include/media/openmax

LOCAL_CFLAGS += -Wno-multichar

LOCAL_MODULE_TAGS := optional

LOCAL_32_BIT_ONLY := true
LOCAL_MODULE:= VideoCallEngineServer

include $(BUILD_EXECUTABLE)

################################################################################

include $(CLEAR_VARS)

LOCAL_SRC_FILES:=              \
        VideoCallEngineUI.cpp        \
        VideoCallEngine.cpp            \
        VideoCallEngineProxy.cpp

LOCAL_SHARED_LIBRARIES := \
    libstagefright liblog libutils libbinder libstagefright_foundation \
    libmedia libgui libcutils libui libc libcamera_client

LOCAL_C_INCLUDES:= \
    frameworks/av/media/libstagefright \
    system/media/camera/include \
    $(TOP)/frameworks/native/include/media/openmax \

LOCAL_CFLAGS += -Wno-multichar

LOCAL_MODULE_TAGS := optional

LOCAL_MODULE:= libVideoCallEngine

include $(BUILD_SHARED_LIBRARY)

################################################################################
#
#include $(CLEAR_VARS)
#
#LOCAL_SRC_FILES:= \
#    com_sprd_phone_videophone_VideoCallEngine.cpp \
#
#LOCAL_SHARED_LIBRARIES := \
#    libVideoCallEngine \
#    libnativehelper \
#    libandroid_runtime \
#    libmedia_jni \
#    libcamera_client \
#    libstagefright liblog libutils libbinder libstagefright_foundation \
#    libmedia libgui libcutils libui libc
#
#LOCAL_REQUIRED_MODULES := \
#    libexif_jni
#
#LOCAL_C_INCLUDES:= \
#    frameworks/base/core/jni \
#    frameworks/av/media/libstagefright \
#    $(LOCAL_PATH) \
#    system/media/camera/include \
#    $(JNI_H_INCLUDE) \
#    $(TOP)/frameworks/native/include/media/openmax
#
#LOCAL_CFLAGS += -Wno-multichar
#
#LOCAL_MODULE_TAGS := optional
#
#LOCAL_MODULE:= libvideo_call_engine_jni
#
#include $(BUILD_SHARED_LIBRARY)

LOCAL_PATH:= $(call my-dir)

#########################################################
include $(CLEAR_VARS)

LOCAL_SRC_FILES:= \
    ../com_sprd_phone_videophone_VideoCallEngine.cpp \

LOCAL_SHARED_LIBRARIES := \
    libVideoCallEngine \
    libnativehelper \
    libandroid_runtime \
    libcamera_client \
    libstagefright liblog libutils libbinder libstagefright_foundation \
    libmedia libgui libcutils libui libc

LOCAL_REQUIRED_MODULES := \
    libexif_jni

LOCAL_C_INCLUDES:= \
    frameworks/base/core/jni \
    frameworks/av/include/camera/android/hardware \
    frameworks/av/media/libstagefright \
    $(TOP)/frameworks/av/volte \
    system/media/camera/include \
    $(JNI_H_INCLUDE) \
    $(TOP)/frameworks/native/include/media/openmax

LOCAL_CFLAGS += -Wno-multichar

LOCAL_CFLAGS += -DVCE_TEST

LOCAL_MODULE_TAGS := optional

LOCAL_MODULE:= libvcetest_jni

include $(BUILD_SHARED_LIBRARY)

#########################################################
include $(CLEAR_VARS)

res_dirs := res
src_dirs := src
LOCAL_SRC_FILES := $(call all-subdir-java-files)
LOCAL_RESOURCE_DIR := $(addprefix $(LOCAL_PATH)/, $(res_dirs))

LOCAL_STATIC_JAVA_LIBRARIES := \
      android-support-v4 \
      android-support-v7-appcompat \
      android-support-v7-cardview \
      android-support-v13

LOCAL_MODULE_TAGS := optional
LOCAL_PACKAGE_NAME := VceTest

LOCAL_CERTIFICATE := platform

LOCAL_PROGUARD_ENABLED := disabled
LOCAL_PROTOC_OPTIMIZE_TYPE := micro

include $(BUILD_PACKAGE)
include $(call all-makefiles-under,$(LOCAL_PATH))

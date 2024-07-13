LOCAL_PATH:= $(call my-dir)
include $(CLEAR_VARS)

LOCAL_SRC_FILES:= \
    TestDrm.cpp \
    ../src/DrmOmaPlugIn.cpp \
    ../src/DcfParser.cpp \
    ../src/RightsParser.cpp \
    ../src/DmParser.cpp \
    ../src/DcfCreator.cpp \
    ../src/RightsManager.cpp \
    ../src/RightsConsumer.cpp \
    ../src/WbXmlConverter.cpp \
    ../src/UUID.cpp

LOCAL_MODULE := test_drm

LOCAL_STATIC_LIBRARIES := libdrmframeworkcommon
LOCAL_STATIC_LIBRARIES += libgtest \
					      liblog
		
LOCAL_SHARED_LIBRARIES := \
    libutils \
    libdl \
    libtinyxml \
    libsqlite \
    libcrypto 

LOCAL_C_INCLUDES += \
    $(TOP)/frameworks/av/drm/libdrmframework/include \
    $(TOP)/frameworks/av/drm/libdrmframework/plugins/oma/include \
    $(TOP)/frameworks/av/drm/libdrmframework/plugins/common/include \
    $(TOP)/frameworks/av/drm/libdrmframework/plugins/common/util/include \
    $(TOP)/frameworks/av/include \
    $(TOP)/external/tinyxml \
    $(TOP)/external/sqlite/dist \
    $(TOP)/external/openssl/include \
    $(TOP)/external/gtest/include 

# Set the following flag to enable the decryption oma flow
#LOCAL_CFLAGS += -DENABLE_OMA_DECRYPTION
LOCAL_MODULE_TAGS := debug
LOCAL_C_INCLUDES += external/stlport/stlport bionic/ bionic/libstdc++/include
LOCAL_SHARED_LIBRARIES += libstlport

#include $(BUILD_EXECUTABLE)

include $(CLEAR_VARS)
LOCAL_MODULE := test.dm
LOCAL_MODULE_TAGS := debug
LOCAL_MODULE_CLASS := drm
LOCAL_SRC_FILES := files/test.dm
LOCAL_MODULE_PATH := $(TARGET_OUT_DATA)/drm/test
#include $(BUILD_PREBUILT)

include $(CLEAR_VARS)
LOCAL_MODULE := test.drc
LOCAL_MODULE_TAGS := debug
LOCAL_MODULE_CLASS := drm
LOCAL_SRC_FILES := files/test.drc
LOCAL_MODULE_PATH := $(TARGET_OUT_DATA)/drm/test
#include $(BUILD_PREBUILT)

include $(CLEAR_VARS)
LOCAL_MODULE := test.dr
LOCAL_MODULE_TAGS := debug
LOCAL_MODULE_CLASS := drm
LOCAL_SRC_FILES := files/test.dr
LOCAL_MODULE_PATH := $(TARGET_OUT_DATA)/drm/test
#include $(BUILD_PREBUILT)

include $(CLEAR_VARS)
LOCAL_MODULE := test.dcf
LOCAL_MODULE_TAGS := debug
LOCAL_MODULE_CLASS := drm
LOCAL_SRC_FILES := files/test.dcf
LOCAL_MODULE_PATH := $(TARGET_OUT_DATA)/drm/test
#include $(BUILD_PREBUILT)

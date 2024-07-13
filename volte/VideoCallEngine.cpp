 #define LOG_NDEBUG 0
#define LOG_TAG "VideoCallEngine"

#include <stdio.h>
#include <utils/Log.h>
#include <media/stagefright/foundation/ADebug.h>
#include <ui/DisplayInfo.h>

#include <utils/RefBase.h>
#include <binder/IPCThreadState.h>
#include <binder/ProcessState.h>
#include <binder/IServiceManager.h>
#include <cutils/properties.h>
#include <cutils/memory.h>

#include <stdlib.h>
#include <unistd.h>
#include <fcntl.h>
#include <time.h>
#include <semaphore.h>
#include <pthread.h>

#include <sys/types.h>
#include <sys/stat.h>
#include <sys/wait.h>

#include "VideoCallEngineProxy.h"
#include "VideoCallEngine.h"


namespace android
{
    VideoCallEngine::VideoCallEngine()
        : mIsRemoteSurfaceSet(false),
        mIsLocalSurfaceSet(false),
        mIsCameraSet(false),
        mIsSetup(false),
        mIsStartUplink(false),
        mIsStartDownlink(false),
        mVideoCallEngineProxy(NULL)
    {
        ALOGI("new VideoCallEngine");
        mVideoCallEngineProxy = new VideoCallEngineProxy();
        mVideoCallEngineProxy->CreateClient();
    }

    void VideoCallEngine::SetupVideoCall(int loopback)
    {
        if(!mIsSetup)
        {
            ALOGI("SetupVideoCall %d", loopback);
            CHECK(mVideoCallEngineProxy != NULL);
            mVideoCallEngineProxy->Setup(loopback);
            mIsSetup = true;
        }
    }

    void VideoCallEngine::ReleaseVideoCall()
    {
        if(mIsSetup)
        {
            ALOGI("ReleaseVideoCall");
            CHECK(mVideoCallEngineProxy != NULL);
            mVideoCallEngineProxy->Release();
            mIsSetup = false;
        }
    }

    VideoCallEngine::~VideoCallEngine()
    {
        delete mVideoCallEngineProxy;
    };

    void VideoCallEngine::setUplinkQos(int qos){
        ALOGI("setUplinkQos");
        CHECK(mVideoCallEngineProxy != NULL);
        mVideoCallEngineProxy->setUplinkQos(qos);
    }

    void VideoCallEngine::setRemoteSurface(const sp<IGraphicBufferProducer>& bufferProducer)
    {
        if(!mIsRemoteSurfaceSet && bufferProducer != NULL)
        {
            ALOGI("setRemoteSurface");
            CHECK(mVideoCallEngineProxy != NULL);
            mIsRemoteSurfaceSet = true;
            mVideoCallEngineProxy->setRemoteSurface(bufferProducer);
        }
        else if(bufferProducer == NULL)
        {
            mIsRemoteSurfaceSet = false;
            mVideoCallEngineProxy->setRemoteSurface(bufferProducer);
        }
    }

    void VideoCallEngine::setLocalSurface(const sp<IGraphicBufferProducer>& bufferProducer)
    {
        if(!mIsLocalSurfaceSet && bufferProducer != NULL)
        {
            ALOGI("setLocalSurface");
            CHECK(mVideoCallEngineProxy != NULL);
            mVideoCallEngineProxy->setLocalSurface(bufferProducer);
            mIsLocalSurfaceSet = true;
        }
        else if(bufferProducer == NULL)
        {
            mIsLocalSurfaceSet = false;
            mVideoCallEngineProxy->setLocalSurface(bufferProducer);
        }
    }

    void VideoCallEngine::setListener(const sp<VideoCallEngineListener>& listner)
    {
        ALOGI("setListener");
        CHECK(mVideoCallEngineProxy != NULL);
        mVideoCallEngineProxy->setListener(listner);
    }

    void VideoCallEngine::setCamera(const sp<Camera>& camera, int camera_size)
    {
        if(camera == NULL)
        {
            mIsCameraSet = false;
            mVideoCallEngineProxy->setCamera(NULL, camera_size);
        }
        else if(!mIsCameraSet)
        {
            ALOGI("setCamera");
            mIsCameraSet = true;
            CHECK(mVideoCallEngineProxy != NULL);
            mVideoCallEngineProxy->setCamera(camera, camera_size);
        }
    }

    int VideoCallEngine::setCameraId(int cameraId){
        ALOGI("setCameraId E, cameraId = %d", cameraId);
        return mVideoCallEngineProxy->setCameraId(cameraId);
    }

    void VideoCallEngine::setCameraPreviewSize(int cameraSize)
    {
        ALOGI("setCameraPreviewSize E, cameraSize = %d", cameraSize);
        mVideoCallEngineProxy->setCameraPreviewSize(cameraSize);
    }

    void VideoCallEngine::setPreviewDisplayOrientation(int previewOrientation)
    {
        ALOGI("setPreviewDisplayOrientation E, %d", previewOrientation);
        mVideoCallEngineProxy->setPreviewDisplayOrientation(previewOrientation);
    }

    void VideoCallEngine::setPreviewSurface(const sp<IGraphicBufferProducer>& bufferProducer)
    {
        //setPreviewSurface using for startPreview
        if(bufferProducer != NULL)
        {
            ALOGI("setPreviewSurface");
            CHECK(mVideoCallEngineProxy != NULL);
            mVideoCallEngineProxy->setPreviewSurface(bufferProducer);
            mIsLocalSurfaceSet = true;
        }
        else if(bufferProducer == NULL)
        {
            mIsLocalSurfaceSet = false;
            mVideoCallEngineProxy->setPreviewSurface(bufferProducer);
        }
    }

    void VideoCallEngine::startPreview()
    {
        ALOGI("startPreview");
        mVideoCallEngineProxy->startPreview();
    }

    void VideoCallEngine::stopPreview()
    {
        ALOGI("stopPreview");
        mVideoCallEngineProxy->stopPreview();
    }

    void VideoCallEngine::stopCamera()
    {
        mVideoCallEngineProxy->stopCamera();
    }

    void VideoCallEngine::startCamera()
    {
        ALOGI("VideoCallEngine startCamera");
        mVideoCallEngineProxy->startCamera();
    }

    void VideoCallEngine::startUplink()
    {
        if(mIsCameraSet && mIsLocalSurfaceSet && !mIsStartUplink)
        {
            ALOGI("startUplink");
            CHECK(mVideoCallEngineProxy != NULL);
            mVideoCallEngineProxy->startUplink();
            mIsStartUplink = true;
        }
    }

    void VideoCallEngine::startDownlink()
    {
        if(mIsRemoteSurfaceSet && !mIsStartDownlink)
        {
            ALOGI("startDownlink");
            CHECK(mVideoCallEngineProxy != NULL);
            mVideoCallEngineProxy->startDownlink();
            mIsStartDownlink = true;
        }
    }

    void VideoCallEngine::stopUplink(bool uplink_started_for_ready)
    {
        if(mIsStartUplink)
        {
            ALOGI("stopUplink");
            CHECK(mVideoCallEngineProxy != NULL);
            mVideoCallEngineProxy->stopUplink();
            mIsStartUplink = false;
        }
        else if(uplink_started_for_ready)
        {
            ALOGI("stop Uplink running for ready");
            CHECK(mVideoCallEngineProxy != NULL);
            mVideoCallEngineProxy->stopUplink();
        }
    }

    void VideoCallEngine::stopDownlink()
    {
        if(mIsStartDownlink)
        {
            ALOGI("stopDownlink");
            CHECK(mVideoCallEngineProxy != NULL);
            mVideoCallEngineProxy->stopDownlink();
            mIsStartDownlink = false;
        }
    }

    //using to send pre-selected pictures to replace loacal camera output image
    void VideoCallEngine::hideLocalImage(bool enable, const char* path){
        ALOGI("hideLocalImage");
        if (mVideoCallEngineProxy != NULL){
            mVideoCallEngineProxy->hideLocalImage(enable, path);
        }
    }

    void VideoCallEngine::setVideoCodecType(int type){
        ALOGI("setVideoCodecType");
        if (mVideoCallEngineProxy != NULL){
            mVideoCallEngineProxy->setVideoCodecType(type);
        }
    }

}


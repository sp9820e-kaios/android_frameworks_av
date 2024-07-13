#ifndef ANDROID_VCEENGINE_H
#define ANDROID_VCEENGINE_H

#include <gui/ISurfaceComposer.h>
#include <gui/SurfaceComposerClient.h>
#include <gui/Surface.h>
#include <camera/Camera.h>
#include <camera/ICamera.h>
#include <camera/CameraParameters.h>

namespace android
{
    class VideoCallEngineListener;
    class VideoCallEngineProxy;
    class VideoCallEngine
    {
    public:
        void SetupVideoCall(int loopback);
        void ReleaseVideoCall();

        void setUplinkQos(int qos);

        void setRemoteSurface(const sp<IGraphicBufferProducer>& bufferProducer);
        void setLocalSurface(const sp<IGraphicBufferProducer>& bufferProducer);
        void setCamera(const sp<Camera>& camera, int camera_size);

        //add camera process apis
        int setCameraId(int cameraId);
        void setCameraPreviewSize(int cameraSize);
        void setPreviewDisplayOrientation(int previewOrientation);
        void setPreviewSurface(const sp<IGraphicBufferProducer>& bufferProducer);
        void startPreview();
        void stopPreview();

        void stopCamera();
        void startCamera();
        void startUplink();
        void startDownlink();
        void stopUplink(bool uplink_started_for_ready = false);
        void stopDownlink();

        //using to send pre-selected pictures to replace loacal camera output image
        void hideLocalImage(bool enable, const char* path);

        void setVideoCodecType(int type);

        void setListener(const sp<VideoCallEngineListener>& listner);
        VideoCallEngine();
        ~VideoCallEngine();

    private:
        bool                        mIsRemoteSurfaceSet;
        bool                        mIsLocalSurfaceSet;
        bool                        mIsCameraSet;
        bool                        mIsSetup;
        bool                        mIsStartUplink;
        bool                        mIsStartDownlink;
        VideoCallEngineProxy*       mVideoCallEngineProxy;
    };
}

#endif

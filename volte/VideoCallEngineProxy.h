#ifndef ANDROID_VCESERVICE_H
#define ANDROID_VCESERVICE_H

#include <utils/Vector.h>
#include <utils/RefBase.h>
#include <binder/IInterface.h>
#include <binder/Parcel.h>
#include <binder/BinderService.h>
#include <gui/ISurfaceComposer.h>
#include <gui/SurfaceComposerClient.h>
#include <gui/Surface.h>
#include <camera/Camera.h>
#include <camera/ICamera.h>
#include <camera/CameraParameters.h>
#include <utils/threads.h>

namespace android
{
    struct ALooper;

    class DeathNotifier: public IBinder::DeathRecipient
    {
    public:
         DeathNotifier() {}
         ~DeathNotifier() {}
         virtual void binderDied(const wp<IBinder>& who);
    };

    class VideoCallEngineListener: virtual public RefBase
    {
    public:
        virtual void notify(int msg, int ext1, int ext2, const Parcel *obj) = 0;
    };

    class IVideoCallEngineCallback : public IInterface
    {
    public:
        DECLARE_META_INTERFACE(VideoCallEngineCallback);
        virtual int notifyCallback(int event) = 0;
        virtual int notifyCallback(int event, int ext1, int ext2, int ext3) = 0;
    };

    class BnVideoCallEngineCallback : public BnInterface<IVideoCallEngineCallback>
    {
    public:
        virtual status_t onTransact( uint32_t code, const Parcel& data, Parcel* reply, uint32_t flags = 0);
    };

    class BpVideoCallEngineCallback : public BpInterface<IVideoCallEngineCallback>
    {
    public:
        BpVideoCallEngineCallback(const sp<IBinder>& impl) : BpInterface<IVideoCallEngineCallback>(impl){}
        virtual int notifyCallback(int event);
        virtual int notifyCallback(int event, int ext1, int ext2, int ext3);
    };

    class VideoCallEngineCallback: public BnVideoCallEngineCallback
    {
        friend class BinderService<VideoCallEngineCallback>;
    public:
        virtual int notifyCallback(int event);
        virtual int notifyCallback(int event, int ext1, int ext2, int ext3);
        virtual status_t onTransact(uint32_t code, const Parcel& data, Parcel* reply, uint32_t flags);
        void setListener(const sp<VideoCallEngineListener>& listner);

    private:
        sp<VideoCallEngineListener> mListener;
    };

    enum VCE_ACTION_TYPE{
        VCE_ACTION_INIT,
        VCE_ACTION_SET_SURFACE,
        VCE_ACTION_SET_CAMERA,
        VCE_ACTION_SET_CALLBACK,
        VCE_ACTION_SETUP,
        VCE_ACTION_STARTLINK,
        VCE_ACTION_STOPLINK,
        VCE_ACTION_CLEAN_SURFACE,
        VCE_ACTION_CLEAN_CAMERA,
        VCE_ACTION_RELEASE,
        VCE_ACTION_STOP_CAMERA,
        VCE_ACTION_START_CAMERA,
        VCE_ACTION_SET_CAMERAID,
        VCE_ACTION_SET_CAMERASIZE,
        VCE_ACTION_DISPLAY_ORIENTATION,
        VCE_ACTION_PREVIEW_SURFACE,
        VCE_ACTION_START_PREVIEW,
        VCE_ACTION_STOP_PREVIEW,
        VCE_ACTION_HIDE_LOCAL_IMAGE,
        VCE_ACTION_SET_UPLINK_QOS,
        VCE_ACTION_SET_CODEC_TYPE,
    };

    enum VCE_CALLBACK_TYPE{
        VCE_ACTION_NOTIFY_CALLBACK,
        VCE_ACTION_NOTIFY_CALLBACK_WITH_DATA,
    };

    enum VCE_SERVICE_TYPE{
        VCE_SERVICE_DOWNLINK,
        VCE_SERVICE_UPLINK
    };

    class VideoCallEngineProxy : public BinderService<VideoCallEngineProxy>,
                                       public BBinder
    {
    public:
        void CreateClient();
        int Setup(int loopback);
        int Release();

        void setUplinkQos(int qos);

        void setLocalSurface(const sp<IGraphicBufferProducer>& surface);
        void setRemoteSurface(const sp<IGraphicBufferProducer>& surface);
        void setCamera(const sp<Camera>& camera, int camera_size);

        //add camera process apis
        int setCameraId(int cameraId);
        void setCameraPreviewSize(int cameraSize);
        void setPreviewDisplayOrientation(int previewOrientation);
        void setPreviewSurface(const sp<IGraphicBufferProducer>& surface);
        void startPreview();
        void stopPreview();

        void setListener(const sp<VideoCallEngineListener>& listner);
        void stopCamera();
        void startCamera();
        int startUplink();
        int stopUplink();
        int startDownlink();
        int stopDownlink();

        void hideLocalImage(bool enable, const char* path);

        void setVideoCodecType(int type);

        VideoCallEngineProxy();

    protected:
        virtual status_t onTransact(uint32_t, const Parcel&, Parcel*, uint32_t);

    private:
        void getVideoCallEngineService();
        sp<VideoCallEngineCallback> mVideoCallEngineCallback;
    };
}

#endif


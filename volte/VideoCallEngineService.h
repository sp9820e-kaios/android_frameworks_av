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
    struct VideoCallEngineClient;
    struct ALooper;
    class VideoCallEngineListener;
    class VideoCallEngineCallback;

    class VideoCallEngineService : public BinderService<VideoCallEngineService>,
                                         public BBinder
    {
        friend class BinderService<VideoCallEngineService>;
    public:
        static int instantiate();
        VideoCallEngineService();
        static char const* getServiceName() {
            return "media.VideoCallEngineService";
        }

    protected:
        virtual ~VideoCallEngineService();
        virtual status_t onTransact(uint32_t, const Parcel&, Parcel*, uint32_t);
        void release();

        VideoCallEngineClient* mVideoCallEngineClient;
    };
}

#endif


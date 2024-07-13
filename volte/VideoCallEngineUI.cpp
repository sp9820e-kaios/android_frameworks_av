#define LOG_NDEBUG 0
#define LOG_TAG "VideoCallEngineUI"

#include "VideoCallEngine.h"
#include <utils/Log.h>
#include "VideoCallEngineProxy.h"

#include "VideoCallEngineUI.h"

#include <gui/IGraphicBufferProducer.h>


using namespace android;

typedef enum {
    VC_EVENT_NONE                   = 0,
    VC_EVENT_INIT_COMPLETE          = 1,
    VC_EVENT_START_ENC              = 2,
    VC_EVENT_START_DEC              = 3,
    VC_EVENT_STOP_ENC               = 4,
    VC_EVENT_STOP_DEC               = 5,
    VC_EVENT_SHUTDOWN               = 6,
    VC_EVENT_REMOTE_RECV_BW_KBPS    = 7,
    VC_EVENT_SEND_KEY_FRAME         = 8,
    VC_EVENT_RESOLUTION_CHANGED     = 11,   //notify AP to change remote surface
    VC_EVENT_CODEC_TYPE_CHENGED     = 12,
} VC_Event;

Mutex                                   mVCELock;

static VideoCallEngine* vce = NULL;
static bool is_setup = false;
static bool uplink_started_for_ready = false;
static bool CameraPrepared=false;
static bool LocalSurfacePrepared=false;
static bool RemoteSurfacePrepared=false;
static bool Event_ENCPrepared=false;
static bool Event_DECPrepared=false;

#ifdef __cplusplus
extern "C" {
#endif

static void stopUplink()
{
    ALOGI("VideoCallEngine_stopUplink");
    if(vce == NULL)
    {
        ALOGI("VideoCallEngine not init");
        return;
    }
    vce->stopUplink();
}

static void startUplink()
{
    ALOGI("VideoCallEngine_startUplink");
    if(vce == NULL)
    {
        ALOGI("VideoCallEngine not init");
        return;
    }
    if(Event_ENCPrepared && CameraPrepared && LocalSurfacePrepared)
    {
        vce->startUplink();
        uplink_started_for_ready = false;
    }
}
static void startDownlink()
{
    ALOGI("VideoCallEngine_startDownlink");
    if(vce == NULL)
    {
        ALOGI("VideoCallEngine not init");
        return;
    }
    if(Event_DECPrepared && RemoteSurfacePrepared)
    {
        vce->startDownlink();
    }
}

static void stopDownlink()
{
    ALOGI("VideoCallEngine_stopDownlink");
    if(vce == NULL)
    {
        ALOGI("VideoCallEngine not init");
        return;
    }
    vce->stopDownlink();
}
void setVideoCodecType(int type){
    mVCELock.lock();
    ALOGI("setVideoCodecType");
    if(vce == NULL)
    {
        ALOGI("VideoCallEngine not init");
        mVCELock.unlock();
        return;
    }
    vce->setVideoCodecType(type);
    ALOGI("setVideoCodecType X");
    mVCELock.unlock();
}


#ifdef __cplusplus
}
#endif


// ref-counted object for callbacks
class VideoCallEngineListenerUI: public VideoCallEngineListener
{
public:
    VideoCallEngineListenerUI(VCECALLBACK callback);
    ~VideoCallEngineListenerUI();
    virtual void notify(int msg, int ext1, int ext2, const Parcel *obj = NULL);
private:
    VCECALLBACK mCallback;
};


VideoCallEngineListenerUI::VideoCallEngineListenerUI(VCECALLBACK callback)
{
    mCallback = callback;
}

VideoCallEngineListenerUI::~VideoCallEngineListenerUI()
{
}

void VideoCallEngineListenerUI::notify(int msg, int ext1, int ext2, const Parcel *obj)
{
    //ALOGI("notify msg %d", msg);

    if(msg == 1000)  //only for set uplink_started_for_ready flag, no need pass to application
    {
        ALOGI("notify msg %d, encode thread is ready, waiting for network ready", msg);
        uplink_started_for_ready = true;
        return;
    }
    switch(msg)
    {
    case VC_EVENT_START_ENC:
           ALOGI("client start enc");
           Event_ENCPrepared=true;
           startUplink();
           return;

    case VC_EVENT_START_DEC:
           ALOGI("client start dec");
           Event_DECPrepared=true;
           startDownlink();
            return;
/*
    case VC_EVENT_STOP_ENC:
           ALOGI("client stop enc");
           stopUplink();
           break;

    case VC_EVENT_STOP_DEC:
           ALOGI("client stop dec");
           stopDownlink();
           break;
*/

    case VC_EVENT_CODEC_TYPE_CHENGED:
        {
           ALOGI("update codec type %d", ext1);
           int codec_type = ext1;
           setVideoCodecType(codec_type);
        }
           return;

    case VC_EVENT_RESOLUTION_CHANGED:
           ALOGI("notify APP to change remote display");
           break;

    default:
           return;
    }

    mCallback(msg, ext1, ext2, (Parcel *)obj);
}

#ifdef __cplusplus
extern "C" {
#endif

/*
 * Method:    init
 * Signature: ()V
 */
void VideoCallEngine_init()
{
    mVCELock.lock();
    ALOGI("VideoCallEngine_init");
    if(vce == NULL)
    {
        vce = new VideoCallEngine();
    }
    ALOGI("VideoCallEngine_init X");
    mVCELock.unlock();
}

/*
 * Method:    setup
 * Signature: (Ljava/lang/Object;)V
 */
void VideoCallEngine_setup(VCECALLBACK callback)
{
    mVCELock.lock();
    ALOGI("VideoCallEngine_setup");
    if(vce == NULL)
    {
        ALOGI("VideoCallEngine not init");
        mVCELock.unlock();
        return;
    }
    if(is_setup)
    {
        ALOGI("VideoCallEngine has been setup");
        mVCELock.unlock();
        return;
    }
    VideoCallEngineListenerUI *listener = new VideoCallEngineListenerUI(callback);
    vce->setListener(listener);
#ifdef VCE_TEST
        int loopback = 1;
#else
        int loopback = 0;
#endif

    vce->SetupVideoCall(loopback);
    is_setup = true;
    uplink_started_for_ready = false;
    CameraPrepared=false;
    LocalSurfacePrepared=false;
    RemoteSurfacePrepared=false;
    Event_ENCPrepared=false;
    Event_DECPrepared=false;
    ALOGI("VideoCallEngine_setup X");
    mVCELock.unlock();
}

/*
 * Method:    reset
 * Signature: ()V
 */
void VideoCallEngine_reset()
{
}

/*
 * Method:    release
 * Signature: ()V
 */
void VideoCallEngine_release()
{
    mVCELock.lock();
    ALOGI("VideoCallEngine_release");
    if(vce == NULL)
    {
        ALOGI("VideoCallEngine not init");
        mVCELock.unlock();
        return;
    }
    if(!is_setup)
    {
        ALOGI("VideoCallEngine not setup");
        mVCELock.unlock();
        return;
    }
    vce->setListener(0);
    vce->ReleaseVideoCall();
    delete vce;
    vce = NULL;
    is_setup = false;
    ALOGI("VideoCallEngine_release X");
    mVCELock.unlock();

}


/*
 * Method:    setRemoteSurface
 * Signature: (Ljava/lang/Object;)V
 */
void VideoCallEngine_setRemoteSurface(sp<IGraphicBufferProducer>& aProducer)
{
    mVCELock.lock();
    ALOGI("setRemoteSurface");
    if(vce == NULL)
    {
        ALOGI("VideoCallEngine not init");
        mVCELock.unlock();
        return;
    }

    if (aProducer == NULL)
    {
        ALOGI("stopdownlink");
        stopDownlink();
        RemoteSurfacePrepared=false;
        vce->setRemoteSurface(NULL);
        mVCELock.unlock();
        return;
    }
    else
    {
        RemoteSurfacePrepared=true;
        ALOGI("RemoteSurface Prepared");
    }
    ALOGI("setRemoteSurface: new_st=%p", aProducer.get());
    vce->setRemoteSurface(aProducer);
    ALOGI("set remote surface end");
    mVCELock.unlock();
}

int VideoCallEngine_setCameraId(int cameraId)
{
    mVCELock.lock();
    ALOGI("setCameraId E, cameraId = %d", cameraId);
    int ret = 0;
    if(vce == NULL)
    {
        ALOGI("setCameraId X, VCE is null");
        mVCELock.unlock();
        return ret;
    }
    ret = vce->setCameraId(cameraId);
    ALOGI("setCameraId X, ret = %d", ret);
    mVCELock.unlock();
    return ret;
}

void VideoCallEngine_setCameraPreviewSize(int cameraSize)
{
    mVCELock.lock();
    ALOGI("setCameraPreviewSize E, cameraSize = %d", cameraSize);
    if(vce == NULL)
    {
        ALOGI("setCameraPreviewSize X, VCE is null");
        mVCELock.unlock();
        return;
    }
    vce->setCameraPreviewSize(cameraSize);
    ALOGI("setCameraPreviewSize X");
    mVCELock.unlock();
}

//here previewOrientation means device orientation
void VideoCallEngine_setPreviewDisplayOrientation(int previewOrientation)
{
    mVCELock.lock();
    ALOGI("setPreviewDisplayOrientation, previewOrientation = %d",
            previewOrientation);
    if(vce == NULL)
    {
        ALOGI("setPreviewDisplayOrientation X, VCE is null");
        mVCELock.unlock();
        return;
    }
    vce->setPreviewDisplayOrientation(previewOrientation);
    ALOGI("setPreviewDisplayOrientation X");
    mVCELock.unlock();
}

void VideoCallEngine_setLocalSurface(sp<IGraphicBufferProducer>& aProducer)
{
    mVCELock.lock();
    ALOGI("setLocalSurface");
    if(vce == NULL)
    {
        ALOGI("VideoCallEngine not init");
        mVCELock.unlock();
        return;
    }

    if (aProducer== NULL)
    {
        vce->setPreviewSurface(NULL);
        mVCELock.unlock();
        return;
    }

    ALOGI("setPreviewSurface: IGraphicBufferProducer =%p", aProducer.get());
    vce->setPreviewSurface(aProducer);
    ALOGI("setPreviewSurface X");
    mVCELock.unlock();
}

void VideoCallEngine_startPreview()
{
    mVCELock.lock();
    ALOGI("startPreview E");
    if(vce == NULL)
    {
        ALOGI("startPreview X, VCE is null");
        mVCELock.unlock();
        return;
    }
    vce->startPreview();
    ALOGI("startPreview X");
    mVCELock.unlock();
}

void VideoCallEngine_stopPreview()
{
    ALOGI("stopPreview E");
    mVCELock.lock();
    if(vce == NULL)
    {
        ALOGI("stopPreview X, VCE is null");
        mVCELock.unlock();
        return;
    }
    vce->stopPreview();
    ALOGI("stopPreview X");
    mVCELock.unlock();
}

// update qos during establishing video call
void VideoCallEngine_setUplinkQos(int qos){
    mVCELock.lock();
    ALOGI("setUplinkQos, max uplink bitrate %d", qos);
    if(vce == NULL)
    {
        ALOGI("setUplinkQos X, VCE is null");
        mVCELock.unlock();
        return;
    }
    // qos range is 0x40 ~ 0xFE, but here qos is the value of bitrate
    if (qos < 64 || qos > 8640){
        ALOGI("setUplinkQos X, qos is invalid");
        mVCELock.unlock();
        return;
    }
    vce->setUplinkQos(qos);
    mVCELock.unlock();
}

void VideoCallEngine_hideLocalImage(bool enable, char* path)
{
    mVCELock.lock();
    ALOGI("hideLocalImage");
    const char* str = path;
    bool value = enable;
    if(str == NULL && value) {
        ALOGI("hideLocalImage X, path is null");
        mVCELock.unlock();
        return;
    }
    if(vce == NULL)
    {
        ALOGI("hideLocalImage X, VCE is null");
        mVCELock.unlock();
        return;
    }
    vce->hideLocalImage(value, str);
    mVCELock.unlock();
}


#ifdef __cplusplus
}
#endif



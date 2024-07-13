#define LOG_NDEBUG 0
#ifdef VCE_TEST
#define LOG_TAG "VceTest-JNI"
#else
#define LOG_TAG "VideoCallEngine-JNI"
#endif

#include <utils/Log.h>
#include <jni.h>
#include "JNIHelp.h"
#include "VideoCallEngineProxy.h"
#include "VideoCallEngine.h"
#include <binder/ProcessState.h>
#include <binder/IServiceManager.h>
#include <binder/Parcel.h>
#include <gui/IGraphicBufferProducer.h>
#include <gui/Surface.h>

#include <android_runtime/android_view_Surface.h>
#include <android_runtime/android_graphics_SurfaceTexture.h>
#include "android_runtime/AndroidRuntime.h"
#include "android_runtime/android_view_Surface.h"
#include "android_runtime/Log.h"
#include "android_os_Parcel.h"

#include <media/ICrypto.h>
#include <media/stagefright/MediaBuffer.h>
#include <media/stagefright/MediaSource.h>
#include <media/stagefright/MediaCodec.h>
#include <media/stagefright/NuMediaExtractor.h>
#include <media/stagefright/foundation/ADebug.h>
#include <media/stagefright/foundation/ALooper.h>
#include <media/stagefright/foundation/AString.h>
#include <media/stagefright/DataSource.h>
#include <media/stagefright/foundation/ABuffer.h>
#include <media/stagefright/CameraSource.h>
#include <media/AudioTrack.h>
#include <binder/ProcessState.h>
#include <media/stagefright/MediaDefs.h>
#include <media/stagefright/MediaErrors.h>
#include <media/stagefright/MetaData.h>
#include <OMX_Video.h>
#include <media/stagefright/foundation/AMessage.h>
#include <media/stagefright/foundation/AHandler.h>
//#include <media/stagefright/NativeWindowWrapper.h>

#include <system/camera.h>
#include <camera/Camera.h>
#include <camera/ICamera.h>
#include <camera/CameraParameters.h>
#include <camera/ICameraService.h>


using namespace android;

struct fields_t
{
    jmethodID   post_event;
};

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

Mutex                                   mVCEJniLock;

static fields_t fields;

static VideoCallEngine* vce = NULL;
static bool is_setup = false;
static bool uplink_started_for_ready = false;
static bool CameraPrepared=false;
static bool LocalSurfacePrepared=false;
static bool RemoteSurfacePrepared=false;
static bool Event_ENCPrepared=false;
static bool Event_DECPrepared=false;

//static jclass      objectClass;     // Reference to MediaPlayer class
//static jobject     globalObject;    // Weak ref to MediaPlayer Java object to call on




// ----------------------------------------------------------------------------
// ref-counted object for callbacks
class JNIVideoCallEngineListener: public VideoCallEngineListener
{
public:
    JNIVideoCallEngineListener(JNIEnv* env, jobject thiz, jobject weak_thiz);
    ~JNIVideoCallEngineListener();
    virtual void notify(int msg, int ext1, int ext2, const Parcel *obj = NULL);
private:
    JNIVideoCallEngineListener();
    jclass      mClass;     // Reference to MediaPlayer class
    jobject     mObject;    // Weak ref to MediaPlayer Java object to call on
};

JNIVideoCallEngineListener::JNIVideoCallEngineListener(JNIEnv* env, jobject thiz, jobject weak_thiz)
{
    // Hold onto the MediaPlayer class for use in calling the static method
    // that posts events to the application thread.
#ifdef VCE_TEST
    const char* className = "com/spreadtrum/vcetest/VideoCallEngine";
#else
    const char* className = "com/spreadtrum/ims/vt/VideoCallEngine";
#endif
    jclass clazz = env->FindClass(className);
    if (clazz == NULL)
    {
        ALOGI("Can't find com/spreadtrum/ims/vt/VideoCallEngine");
        jniThrowException(env, "java/lang/Exception", NULL);
        return;
    }
    mClass = (jclass)env->NewGlobalRef(clazz);

    // We use a weak reference so the MediaPlayer object can be garbage collected.
    // The reference is only used as a proxy for callbacks.
    mObject  = env->NewGlobalRef(weak_thiz);
}

JNIVideoCallEngineListener::~JNIVideoCallEngineListener()
{
    // remove global references
    JNIEnv *env = AndroidRuntime::getJNIEnv();
    env->DeleteGlobalRef(mObject);
    env->DeleteGlobalRef(mClass);
}

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
    mVCEJniLock.lock();
    ALOGI("setVideoCodecType, %d", type);
    if(vce == NULL)
    {
        ALOGI("VideoCallEngine not init");
        mVCEJniLock.unlock();
        return;
    }
    vce->setVideoCodecType(type);
    ALOGI("setVideoCodecType X");
    mVCEJniLock.unlock();
}

static void VideoCallEngine_setVideoCodecType(
        JNIEnv* env __unused, jobject thiz __unused, jint type)
{
    setVideoCodecType(type);
}


void JNIVideoCallEngineListener::notify(int msg, int ext1, int ext2, const Parcel *obj)
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

    JNIEnv *env = AndroidRuntime::getJNIEnv();
    if (obj && obj->dataSize() > 0)
    {
        ALOGI("obj && obj->dataSize() > 0");
        jobject jParcel = createJavaParcelObject(env);
        if (jParcel != NULL)
        {
            Parcel* nativeParcel = parcelForJavaObject(env, jParcel);
            nativeParcel->setData(obj->data(), obj->dataSize());
            env->CallStaticVoidMethod(mClass, fields.post_event, mObject, msg, ext1, ext2, jParcel);
            env->DeleteLocalRef(jParcel);
        }
    }
    else
    {
        env->CallStaticVoidMethod(mClass, fields.post_event, mObject, msg, ext1, ext2, NULL);
    }
    if (env->ExceptionCheck())
    {
        ALOGW("An exception occurred while notifying an event.");
        LOGW_EX(env);
        env->ExceptionClear();
    }
}

/*
 * Class:     com_sprd_phone_videophone_VideoCallEngine
 * Method:    init
 * Signature: ()V
 */
static void VideoCallEngine_init(JNIEnv* env __unused, jobject thiz __unused)
{
    mVCEJniLock.lock();
    ALOGI("VideoCallEngine_init");
    if(vce == NULL)
    {
        vce = new VideoCallEngine();
    }
    ALOGI("VideoCallEngine_init X");
    mVCEJniLock.unlock();
}

/*
 * Class:     com_sprd_phone_videophone_VideoCallEngine
 * Method:    setup
 * Signature: (Ljava/lang/Object;)V
 */
static void VideoCallEngine_setup(JNIEnv* env, jobject thiz, jobject weak_this)
{
    mVCEJniLock.lock();
    ALOGI("VideoCallEngine_setup");
    if(vce == NULL)
    {
        ALOGI("VideoCallEngine not init");
        mVCEJniLock.unlock();
        return;
    }
    if(is_setup)
    {
        ALOGI("VideoCallEngine has been setup");
        mVCEJniLock.unlock();
        return;
    }
    sp<JNIVideoCallEngineListener> listener = new JNIVideoCallEngineListener(env, thiz, weak_this);
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
    mVCEJniLock.unlock();
}

/*
 * Class:     com_sprd_phone_videophone_VideoCallEngine
 * Method:    reset
 * Signature: ()V
 */
static void VideoCallEngine_reset(JNIEnv* env __unused, jobject thiz __unused)
{
}

/*
 * Class:     com_sprd_phone_videophone_VideoCallEngine
 * Method:    release
 * Signature: ()V
 */
static void VideoCallEngine_release(JNIEnv* env __unused, jobject thiz __unused)
{
    mVCEJniLock.lock();
    ALOGI("VideoCallEngine_release");
    if(vce == NULL)
    {
        ALOGI("VideoCallEngine not init");
        mVCEJniLock.unlock();
        return;
    }
    if(!is_setup)
    {
        ALOGI("VideoCallEngine not setup");
        mVCEJniLock.unlock();
        return;
    }
    vce->setListener(0);
    vce->ReleaseVideoCall();
    delete vce;
    vce = NULL;
    is_setup = false;
    ALOGI("VideoCallEngine_release X");
    mVCEJniLock.unlock();

}

/*
 * Class:     com_sprd_phone_videophone_VideoCallEngine
 * Method:    jfinalize
 * Signature: ()V
 */
static void VideoCallEngine_jfinalize(JNIEnv* env __unused, jobject thiz __unused)
{
}

/*
 * Class:     com_sprd_phone_videophone_VideoCallEngine
 * Method:    setRemoteSurface
 * Signature: (Ljava/lang/Object;)V
 */
static void VideoCallEngine_setRemoteSurface(JNIEnv* env, jobject thiz __unused, jobject jsurface)
{
    mVCEJniLock.lock();
    ALOGI("setRemoteSurface");
    if(vce == NULL)
    {
        ALOGI("VideoCallEngine not init");
        mVCEJniLock.unlock();
        return;
    }
    sp<IGraphicBufferProducer> new_st;

    if (jsurface == NULL)
    {
        ALOGI("JNI stopdownlink");
        stopDownlink();
        RemoteSurfacePrepared=false;
        vce->setRemoteSurface(NULL);
        mVCEJniLock.unlock();
        return;
    }
    else
    {
        sp<Surface> surface(android_view_Surface_getSurface(env, jsurface));
        if (surface != NULL)
        {
            RemoteSurfacePrepared=true;
            ALOGI("RemoteSurface Prepared");
            new_st = surface->getIGraphicBufferProducer();
            if (new_st == NULL)
            {
                jniThrowException(env, "java/lang/IllegalArgumentException",
                    "The surface does not have a binding SurfaceTexture!");
                mVCEJniLock.unlock();
                return;
            }
        }
        else
        {
            jniThrowException(env, "java/lang/IllegalArgumentException",
                    "The surface has been released");
            mVCEJniLock.unlock();
            return;
        }
    }
    ALOGI("setRemoteSurface: new_st=%p", new_st.get());
    vce->setRemoteSurface(new_st);
    ALOGI("set remote surface end");
    mVCEJniLock.unlock();
}

static jint VideoCallEngine_setCameraId(JNIEnv* env __unused, jobject thiz __unused, jint cameraId)
{
    mVCEJniLock.lock();
    ALOGI("setCameraId E, cameraId = %d", cameraId);
    int ret = 0;
    if(vce == NULL)
    {
        ALOGI("setCameraId X, VCE is null");
        mVCEJniLock.unlock();
        return ret;
    }
    ret = vce->setCameraId(cameraId);
    ALOGI("setCameraId X, ret = %d", ret);
    mVCEJniLock.unlock();
    return (jint)ret;
}

static void VideoCallEngine_setCameraPreviewSize(
        JNIEnv* env __unused, jobject thiz __unused, jint cameraSize)
{
    mVCEJniLock.lock();
    ALOGI("setCameraPreviewSize E, cameraSize = %d", cameraSize);
    if(vce == NULL)
    {
        ALOGI("setCameraPreviewSize X, VCE is null");
        mVCEJniLock.unlock();
        return;
    }
    vce->setCameraPreviewSize(cameraSize);
    ALOGI("setCameraPreviewSize X");
    mVCEJniLock.unlock();
}

//here previewOrientation means device orientation
static void VideoCallEngine_setPreviewDisplayOrientation(
        JNIEnv* env __unused, jobject thiz __unused, jint previewOrientation)
{
    mVCEJniLock.lock();
    ALOGI("setPreviewDisplayOrientation, previewOrientation = %d",
            previewOrientation);
    if(vce == NULL)
    {
        ALOGI("setPreviewDisplayOrientation X, VCE is null");
        mVCEJniLock.unlock();
        return;
    }
    vce->setPreviewDisplayOrientation(previewOrientation);
    ALOGI("setPreviewDisplayOrientation X");
    mVCEJniLock.unlock();
}

static void VideoCallEngine_setPreviewSurface(JNIEnv* env, jobject thiz __unused, jobject jsurface)
{
    mVCEJniLock.lock();
    ALOGI("setPreviewSurface");
    if(vce == NULL)
    {
        ALOGI("VideoCallEngine not init");
        mVCEJniLock.unlock();
        return;
    }

     if (jsurface == NULL)
     {
         vce->setPreviewSurface(NULL);
         mVCEJniLock.unlock();
         return;
     }

    sp<IGraphicBufferProducer> local_gbl;
    if (jsurface)
    {
        sp<Surface> surface(android_view_Surface_getSurface(env, jsurface));
        if (surface != NULL)
        {
            ALOGI("PreviewSurface Prepared");
            local_gbl = surface->getIGraphicBufferProducer();
            if (local_gbl == NULL)
            {
                jniThrowException(env, "java/lang/IllegalArgumentException",
                    "The surface does not have a binding SurfaceTexture!");
                mVCEJniLock.unlock();
                return;
            }
        } else {
            jniThrowException(env, "java/lang/IllegalArgumentException",
            "The surface has been released");
            mVCEJniLock.unlock();
            return;
        }
    }
    ALOGI("setPreviewSurface: local_gbl =%p", local_gbl.get());
    vce->setPreviewSurface(local_gbl);
    ALOGI("setPreviewSurface X");
    mVCEJniLock.unlock();
}

static void VideoCallEngine_startPreview(JNIEnv* env __unused, jobject thiz __unused)
{
    mVCEJniLock.lock();
    ALOGI("startPreview E");
    if(vce == NULL)
    {
        ALOGI("startPreview X, VCE is null");
        mVCEJniLock.unlock();
        return;
    }
    vce->startPreview();
    ALOGI("startPreview X");
    mVCEJniLock.unlock();
}

static void VideoCallEngine_stopPreview(JNIEnv* env __unused, jobject thiz __unused)
{
    ALOGI("stopPreview E");
    mVCEJniLock.lock();
    if(vce == NULL)
    {
        ALOGI("stopPreview X, VCE is null");
        mVCEJniLock.unlock();
        return;
    }
    vce->stopPreview();
    ALOGI("stopPreview X");
    mVCEJniLock.unlock();
}

/*
 * Class:     com_sprd_phone_videophone_VideoCallEngine
 * Method:    setLocalSurface
 * Signature: (Ljava/lang/Object;)V
 */
static void VideoCallEngine_setLocalSurface(JNIEnv* env, jobject thiz __unused, jobject jsurface)
{
    mVCEJniLock.lock();
    ALOGI("setLocalSurface");
    if(vce == NULL)
    {
        ALOGI("VideoCallEngine not init");
        mVCEJniLock.unlock();
        return;
    }

     if (jsurface == NULL)
     {
         LocalSurfacePrepared=false;
         stopUplink();
         vce->setLocalSurface(NULL);
         mVCEJniLock.unlock();
         return;
     }

    sp<IGraphicBufferProducer> new_st;
    if (jsurface)
    {
        sp<Surface> surface(android_view_Surface_getSurface(env, jsurface));
        if (surface != NULL)
        {
            LocalSurfacePrepared=true;
            startUplink();
            ALOGI("LocalSurface Prepared");
            new_st = surface->getIGraphicBufferProducer();
            if (new_st == NULL)
            {
                jniThrowException(env, "java/lang/IllegalArgumentException",
                    "The surface does not have a binding SurfaceTexture!");
                mVCEJniLock.unlock();
                return;
            }
        } else {
            jniThrowException(env, "java/lang/IllegalArgumentException",
            "The surface has been released");
            mVCEJniLock.unlock();
            return;
        }
    }
    ALOGI("setLocalSurface: new_st=%p", new_st.get());
    vce->setLocalSurface(new_st);
    ALOGI("set local surface end");
    mVCEJniLock.unlock();
}

// update qos during establishing video call
static void VideoCallEngine_setUplinkQos(JNIEnv* env __unused, jobject thiz __unused, jint qos){
    mVCEJniLock.lock();
    ALOGI("setUplinkQos, max uplink bitrate %d", qos);
    if(vce == NULL)
    {
        ALOGI("setUplinkQos X, VCE is null");
        mVCEJniLock.unlock();
        return;
    }
    // qos range is 0x40 ~ 0xFE, but here qos is the value of bitrate
    if (qos < 64 || qos > 8640){
        ALOGI("setUplinkQos X, qos is invalid");
        mVCEJniLock.unlock();
        return;
    }
    vce->setUplinkQos(qos);
    mVCEJniLock.unlock();
}

extern sp<Camera> get_native_camera(JNIEnv *env __unused, jobject thiz __unused, struct JNICameraContext** context);

/*
 * Class:     com_sprd_phone_videophone_VideoCallEngine
 * Method:    setCamera
 * Signature: (Ljava/lang/Object;I)V
 */
static void VideoCallEngine_setCamera(JNIEnv* env, jobject thiz __unused, jobject camera, jint camera_size)
{
    mVCEJniLock.lock();
    ALOGI("setCamera");
    if(vce == NULL)
    {
        ALOGI("VideoCallEngine not init");
        mVCEJniLock.unlock();
        return;
    }
    if (camera == NULL)
    {
        ALOGI("camera is null");
        CameraPrepared=false;
        stopUplink();
        vce->setCamera(NULL, camera_size);
    }
    else
    {
        sp<Camera> c = get_native_camera(env, camera, NULL);
        vce->setCamera(c, camera_size);
        ALOGI("camera prepared");
        CameraPrepared=true;
        startUplink();
    }
    ALOGI("camera prepared end");
    mVCEJniLock.unlock();
}

/*
 * Class:     com_sprd_phone_videophone_VideoCallEngine
 * Method:    prepare
 * Signature: ()V
 */
static void VideoCallEngine_prepare(JNIEnv* env __unused, jobject thiz __unused)
{
}

/*
 * Class:     com_sprd_phone_videophone_VideoCallEngine
 * Method:    startUplink
 * Signature: ()V
 */

static void VideoCallEngine_startUplink(JNIEnv* env __unused, jobject thiz __unused)
{
   /*
    if(vce == NULL)
    {
        ALOGI("VideoCallEngine not init");
        return;
    }
    ALOGI("VideoCallEngine_startUplink");
    vce->startUplink();
    uplink_started_for_ready = false;
    */
}

/*
 * Class:     com_sprd_phone_videophone_VideoCallEngine
 * Method:    stopUplink
 * Signature: ()V
 */
static void VideoCallEngine_stopUplink(JNIEnv* env __unused, jobject thiz __unused)
{
/*
    if(vce == NULL)
    {
        ALOGI("VideoCallEngine not init");
        return;
    }
    ALOGI("VideoCallEngine_stopUplink");
    vce->stopUplink(uplink_started_for_ready);
    if(uplink_started_for_ready)
    {
        uplink_started_for_ready = false;
    }
    */
}

/*
 * Class:     com_sprd_phone_videophone_VideoCallEngine
 * Method:    startDownlink
 * Signature: ()V
 */
static void VideoCallEngine_startDownlink(JNIEnv* env __unused, jobject thiz __unused)
{
/*
    if(vce == NULL)
    {
        ALOGI("VideoCallEngine not init");
        return;
    }
    ALOGI("VideoCallEngine_startDownlink");
    vce->startDownlink();
    */
}

/*
 * Class:     com_sprd_phone_videophone_VideoCallEngine
 * Method:    stopDownlink
 * Signature: ()V
 */
static void VideoCallEngine_stopDownlink(JNIEnv* env __unused, jobject thiz __unused)
{
/*
    if(vce == NULL)
    {
        ALOGI("VideoCallEngine not init");
        return;
    }
    ALOGI("VideoCallEngine_stopDownlink");
    vce->stopDownlink();
    */
}

/*
 * Class:     com_sprd_phone_videophone_VideoCallEngine
 * Method:    setUplinkImageFileFD
 * Signature: (Ljava/lang/Object;JJ)V
 */
static void VideoCallEngine_setUplinkImageFileFD(JNIEnv* env __unused, jobject thiz __unused, jobject object __unused, jlong offset __unused, jlong length __unused)
{
}

/*
 * Class:     com_sprd_phone_videophone_VideoCallEngine
 * Method:    selectRecordSource
 * Signature: (I)V
 */
static void VideoCallEngine_selectRecordSource(JNIEnv* env __unused, jobject thiz __unused, jint source __unused)
{
}

/*
 * Class:     com_sprd_phone_videophone_VideoCallEngine
 * Method:    selectRecordFileFormat
 * Signature: (I)V
 */
static void VideoCallEngine_selectRecordFileFormat(JNIEnv* env __unused, jobject thiz __unused, jint format __unused)
{
}

/*
 * Class:     com_sprd_phone_videophone_VideoCallEngine
 * Method:    startRecord
 * Signature: ()V
 */
static void VideoCallEngine_startRecord(JNIEnv* env __unused, jobject thiz __unused)
{
}

/*
 * Class:     com_sprd_phone_videophone_VideoCallEngine
 * Method:    stopRecord
 * Signature: ()V
 */
static void VideoCallEngine_stopRecord(JNIEnv* env __unused, jobject thiz __unused)
{
}

/*
 * Class:     com_sprd_phone_videophone_VideoCallEngine
 * Method:    setRecordFileFD
 * Signature: (Ljava/lang/Object;JJ)V
 */
static void VideoCallEngine_setRecordFileFD(JNIEnv* env __unused, jobject thiz __unused, jobject object __unused, jlong offset __unused, jlong length __unused)
{
}

static void VideoCallEngine_hideLocalImage(JNIEnv* env, jobject thiz __unused, jboolean enable, jstring path)
{
    mVCEJniLock.lock();
    ALOGI("hideLocalImage");
    const char* str = env->GetStringUTFChars(path, NULL);
    bool value = (enable == JNI_TRUE)? true : false;
    if(str == NULL && value) {
        ALOGI("hideLocalImage X, path is null");
        mVCEJniLock.unlock();
        return;
    }
    if(vce == NULL)
    {
        ALOGI("hideLocalImage X, VCE is null");
        mVCEJniLock.unlock();
        return;
    }
    vce->hideLocalImage(value, str);
    //path is an new c/c++ string converted from java string in JNI
    //it should be released after being used
    env->ReleaseStringUTFChars(path, str);
    mVCEJniLock.unlock();
}

/*
 * Class:     com_sprd_phone_videophone_VideoCallEngine
 * Method:    setRecordMaxFileSize
 * Signature: (J)V
 */
static void VideoCallEngine_setRecordMaxFileSize(JNIEnv* env __unused, jobject thiz __unused, jlong size __unused)
{
}

static JNINativeMethod gMethods[] =
{
    {"init",                        "()V",                              (void *)VideoCallEngine_init},
    //{"reset",                       "()V",                              (void *)VideoCallEngine_reset},
    {"setup",                       "(Ljava/lang/Object;)V",            (void *)VideoCallEngine_setup},
    {"release",                     "()V",                              (void *)VideoCallEngine_release},
    //{"jfinalize",                   "()V",                              (void *)VideoCallEngine_jfinalize},
    {"setRemoteSurface",            "(Ljava/lang/Object;)V",            (void *)VideoCallEngine_setRemoteSurface},
    {"setLocalSurface",             "(Ljava/lang/Object;)V",            (void *)VideoCallEngine_setPreviewSurface},
    {"setCamera",                   "(Ljava/lang/Object;I)V",            (void *)VideoCallEngine_setCamera},

    {"setCameraId",                 "(I)I",            (void *)VideoCallEngine_setCameraId},
    {"setCameraPreviewSize",        "(I)V",            (void *)VideoCallEngine_setCameraPreviewSize},
    {"setPreviewDisplayOrientation","(I)V",            (void *)VideoCallEngine_setPreviewDisplayOrientation},
    //{"setPreviewSurface",           "(Ljava/lang/Object;)V",            (void *)VideoCallEngine_setPreviewSurface},
    {"startPreview",                "()V",            (void *)VideoCallEngine_startPreview},
    {"stopPreview",                 "()V",            (void *)VideoCallEngine_stopPreview},

    //{"prepare",                     "()V",                              (void *)VideoCallEngine_prepare},

    {"setUplinkQos",                "(I)V",                             (void *)VideoCallEngine_setUplinkQos},

    {"startUplink",                 "()V",                              (void *)VideoCallEngine_startUplink},
    {"stopUplink",                  "()V",                              (void *)VideoCallEngine_stopUplink},
    {"startDownlink",               "()V",                              (void *)VideoCallEngine_startDownlink},
    {"stopDownlink",                "()V",                              (void *)VideoCallEngine_stopDownlink},

#ifdef VCE_TEST
    {"hideLocalImage",                 "(ZLjava/lang/String;)V",            (void *)VideoCallEngine_hideLocalImage},
    {"setVideoCodecType",                 "(I)V",            (void *)VideoCallEngine_setVideoCodecType},
#endif

    //{"setUplinkImageFileFD",        "(Ljava/io/FileDescriptor;JJ)V",    (void *)VideoCallEngine_setUplinkImageFileFD},
    //{"selectRecordSource",          "(I)V",                             (void *)VideoCallEngine_selectRecordSource},
    //{"selectRecordFileFormat",      "(I)V",                             (void *)VideoCallEngine_selectRecordFileFormat},
    //{"startRecord",                 "()V",                              (void *)VideoCallEngine_startRecord},
    //{"stopRecord",                  "()V",                              (void *)VideoCallEngine_stopRecord},
    //{"setRecordFileFD",             "(Ljava/lang/Object;JJ)V",          (void *)VideoCallEngine_setRecordFileFD},
    //{"setRecordMaxFileSize",        "(Ljava/lang/Object;J)V",           (void *)VideoCallEngine_setRecordMaxFileSize}
};

jint JNI_OnLoad(JavaVM* vm, void* reserved __unused)
{
    JNIEnv* env = NULL;
    jint result = -1;
    jclass clazz = NULL;

#ifdef VCE_TEST
    const char* className = "com/spreadtrum/vcetest/VideoCallEngine";
#else
    const char* className = "com/spreadtrum/ims/vt/VideoCallEngine";
#endif

    ALOGI("JNI_OnLoad E");
    if (vm->GetEnv((void**) &env, JNI_VERSION_1_4) != JNI_OK)
    {
        ALOGE("GetEnv failed\n");
        goto bail;
    }
    assert(env != NULL);

    clazz = env->FindClass(className);
    if (clazz == NULL)
    {
        ALOGE("Can't find %s", className);
        goto bail;
    }

    if (env->RegisterNatives(clazz, gMethods, NELEM(gMethods)) != JNI_OK)
    {
        ALOGE("RegisterNatives failed");
        goto bail;
    }

    fields.post_event = env->GetStaticMethodID(clazz, "postEventFromNative", "(Ljava/lang/Object;IIILjava/lang/Object;)V");
    if (fields.post_event == NULL)
    {
        ALOGE("postEventFromNative failed");
        return result;
    }
    result = JNI_VERSION_1_4;

bail:
    env->DeleteLocalRef(clazz);
    return result;
}


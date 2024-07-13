//#define LOG_NDEBUG 0
#define LOG_TAG "VideoCallEngineClient"

#if 0
#define DLOG ALOGI
#else
#define DLOG(...)
#endif

#include "cutils/properties.h"
#include <utils/Log.h>
#include <binder/IPCThreadState.h>
#include <media/ICrypto.h>
#include <media/stagefright/MediaBuffer.h>
#include <media/stagefright/MediaSource.h>
#include <media/stagefright/MediaCodec.h>
#include <media/stagefright/NuMediaExtractor.h>
#include <media/stagefright/MediaMuxer.h>
#include <media/stagefright/foundation/ADebug.h>
#include <media/stagefright/foundation/ALooper.h>
#include <media/stagefright/foundation/AString.h>
#include <media/stagefright/foundation/hexdump.h>
#include <media/stagefright/DataSource.h>
#include <media/stagefright/foundation/ABuffer.h>
#include <media/stagefright/CameraSource.h>
#include <media/AudioTrack.h>
#include <binder/ProcessState.h>
#include <media/stagefright/MediaDefs.h>
#include <media/stagefright/MediaErrors.h>
#include <media/stagefright/MetaData.h>
#include <OMX_Video.h>

#include <ui/Rect.h>
#include <ui/GraphicBufferMapper.h>


#include <system/camera.h>
#include "../services/camera/libcameraservice/api1/Camera2Client.h"
#include <camera/Camera.h>
#include <camera/ICamera.h>
#include <camera/CameraParameters.h>

#include "VideoCallEngineAvcUtils.h"
#include "VideoCallEngineClient.h"
#include "VideoCallEngineSoftwareRenderer.h"

#include <VideoCallEngineCameraInterface.h>

/* video encode sync mode, means single input single putput */
#define ENC_SYNC_MODE 0

#define ROTATE_ENABLE  1
#define RECORD_TEST 0


#ifdef __cplusplus
extern "C" {
#endif

#define OSAL_PTHREADS
#define INCLUDE_VPR
#define INCLUDE_VIER

#include <vci.h>
#include <vier.h>

#ifdef __cplusplus
}
#endif

#if 0
#define VCI_EVENT_DESC_STRING_SZ (127)

typedef enum {
    VC_EVENT_NONE               = 0,
    VC_EVENT_INIT_COMPLETE      = 1,
    VC_EVENT_START_ENC          = 2,
    VC_EVENT_START_DEC          = 3,
    VC_EVENT_STOP_ENC           = 4,
    VC_EVENT_STOP_DEC           = 5,
    VC_EVENT_SHUTDOWN           = 6
} VC_Event;

typedef struct {
    uint8        *data_ptr;          /* data buffer */
    vint          length;            /* length of data */
    uint64        tsMs;              /* timestamp in milli-second */
    vint          flags;             /* SPS/PPS flags */
    uint8         rcsRtpExtnPayload; /* VCO - payload */
} VC_EncodedFrame;

vint VCI_init(void)
{
    return 0;
}

void VCI_shutdown(   void)
 {
 }

vint VCI_getEvent(
    VC_Event *event_ptr,
    char     *eventDesc_ptr,
    vint     *codecType_ptr,
    vint      timeout)
{
    return 0;
}

vint VCI_sendEncodedFrame(    VC_EncodedFrame *frame_ptr)
{
    return 0;
}

vint VCI_getEncodedFrame(    VC_EncodedFrame *frame_ptr)
{
    return 0;
}
OSAL_Status VIER_init(  void)
{
    return OSAL_SUCCESS;
}

OSAL_Status VIER_shutdown(    void)
{
    return OSAL_SUCCESS;
}
#endif

#define MAX_INPUT_BUFFERS  50

#if RECORD_TEST
static int counter = 0;
#endif

static const int64_t kTimeout = 500ll;
#if ROTATE_ENABLE
static const uint8 rotate_0 = 0;
static const uint8 rotate_270 = 1;
static const uint8 rotate_180 = 2;
static const uint8 rotate_90 = 3;
static const uint8 flip_h = 4;
#endif

//#define VQ_TEST_NV21
//#define ENABLE_STRIDE

// For video encoded buffer timeStamp
static int64_t startSysTime = 0;

//For bug 473707, remove later
static int64_t pushFrame = 0;
static int64_t dropFrame = 0;
static int64_t cameraRelease = 0;
static int64_t encodeRelease = 0;

//VideoCallEngineClient instance
static VideoCallEngineClient* VCEClient = NULL;

namespace android
{
    char value_dump[PROPERTY_VALUE_MAX];
    char *default_value = (char*)"false";
    bool video_dump_enable = false;

    enum VCE_ACTION_TYPE{
       VCE_ACTION_NOTIFY_CALLBACK,
       VCE_ACTION_NOTIFY_CALLBACK_WITH_DATA
    };

    struct BufferInfo {
        size_t mIndex;
        size_t mOffset;
        size_t mSize;
        int64_t mPresentationTimeUs;
        uint32_t mFlags;
    };

    struct CodecState {
        sp<MediaCodec> mCodec;
        Vector<sp<ABuffer> > mInBuffers;
        Vector<sp<ABuffer> > mOutBuffers;
    };

    bool using_local_ps = false;
    uint8 sps_pps_saved = 0;
    uint8 *frame_sps = NULL;
    uint8 *frame_pps = NULL;
    unsigned int frame_sps_length = 0;
    unsigned int frame_pps_length = 0;

    int64_t camera_output_num = 0;

    int64_t mCalculateCameraFpsStartTs = -1;
    int32_t mActFrameRate = 0;

    VideoEncBitrateAdaption::VideoEncBitrateAdaption()
            :curBitrate(0),
             maxBitrate(0),
             minBitrate(0),
             codecBitrate(0),
             isUpdated(false),
             frameDropEnabled(false),
             frameDropCnt(0),
             frameSentCnt(0),
             changeTime(0),
             lengthPerSec(0){}

    VideoEncBitrateAdaption::~VideoEncBitrateAdaption() {
        for (int i=0; i < frameQueue.size(); i++) {
            frameQueue.pop();
        }
    }

    void VideoEncBitrateAdaption::setConfig(const Config &cfg) {
        curBitrate = cfg.curBr;
        maxBitrate = cfg.maxBr;
        minBitrate = cfg.minBr;
        codecBitrate = curBitrate > minBitrate ? curBitrate : minBitrate;
    }

    void VideoEncBitrateAdaption::enableFrameDrop() {
        frameDropCnt = 0;
        frameSentCnt = 0;
        lengthPerSec = 0;
        /* clean the frameQueue before, it stats running*/\
        cleanFrames();
        frameDropEnabled = true;
    }

    void VideoEncBitrateAdaption::disableFrameDrop() {
        frameDropCnt = 0;
        frameSentCnt = 0;
        cleanFrames();
        frameDropEnabled = false;
    }

    void VideoEncBitrateAdaption::cleanFrames() {
        Mutex::Autolock autoLock(lock);
        lengthPerSec = 0;
        while(!frameQueue.empty()) {
            frameQueue.pop();
        }

    }

    bool VideoEncBitrateAdaption::isEncodable() {
        if(!frameDropEnabled) {
            return true;
        }

        int64_t now = systemTime();
        sp<frameInfo> inf = NULL;

        Mutex::Autolock autoLock(lock);
        //drop the stale infos
        while (!frameQueue.empty()) {
            inf = frameQueue.front();
            ALOGI("VideoEncBitrateAdaption: now %lld, inf %lld", now, inf->getTs());
            if ((now - inf->getTs()) >= 1000000000LL) {
                lengthPerSec -= inf->getSize();
                frameQueue.pop();
            } else {
                break;
            }
        }
        if (lengthPerSec < 0) {
            ALOGE("VideoEncBitrateAdaption: ERROR lengthPerSec %d !!!", lengthPerSec);
            lengthPerSec = 0;
        }
        if (lengthPerSec > (curBitrate >> 3)) {
            frameDropCnt ++;
            ALOGI("VideoEncBitrateAdaption: frameDropCnt %d, frameSentCnt %d, "
                    "lengthPerSec %d, curBitrate/8 %d",
                   frameDropCnt, frameSentCnt, lengthPerSec, (curBitrate >> 3));
            return false;
        }

        return true;
    }

    void VideoEncBitrateAdaption::pushFrameInfo(int length) {
        if (!frameDropEnabled || length <= 0) {
            return;
        }
        int64_t now = systemTime();
        Mutex::Autolock autoLock(lock);
        if (frameQueue.size() > kFrameQueueLimit) {
            //frameQueue.pop();
            ALOGE("VideoEncBitrateAdaption: OOPS, frame queue overflows !!!");
        }

        sp<frameInfo> inf = new frameInfo(now, length);
        frameQueue.push(inf);
        frameSentCnt++;
        lengthPerSec += length;
        if (!frameQueue.empty()) {
            ALOGE("VideoEncBitrateAdaption: front.ts %lld, front.size %d, "
                  "back.ts %lld, back.size %d, lengthPerSec %d",
                   frameQueue.front()->getTs(), frameQueue.front()->getSize(),
                   frameQueue.back()->getTs(), frameQueue.back()->getSize(),
                   lengthPerSec);
        }
    }

    void VideoEncBitrateAdaption::setTargetBitrate(int tBr) {
        curBitrate = tBr > maxBitrate ? maxBitrate : tBr;
        ALOGI("VideoEncBitrateAdaption: setTargetBitrate tmmbr %d bps, "
              "curBitrate %d bps, enable %d",
               tBr, curBitrate, frameDropEnabled == true);
    }

    void VideoEncBitrateAdaption::updateMaxBitrate(int Br) {
        maxBitrate = Br;
    }

    int VideoEncBitrateAdaption::getTargetBitrate() {
        int newBr = curBitrate > minBitrate ? curBitrate : minBitrate;

        isUpdated = (codecBitrate != newBr);
        if (isUpdated) {
            codecBitrate = newBr;
            ALOGI("VideoEncBitrateAdaption: getTargetBitrate curBitrate %d Kbps, "
                  "newBr %d Kbps, updated %d, enable %d",
                  curBitrate, newBr, isUpdated == true, frameDropEnabled == true);
        }
        return newBr;
    }

    bool VideoEncBitrateAdaption::isReadyToUpdateCodec(){
        int64_t now = systemTime();
        if (isUpdated && (changeTime == 0 || (now - changeTime) > 1000000000LL)) {
            changeTime = now;
            isUpdated = false;
            return true;
        }
        return false;
    }

    //add for new camera processing
    void camera_data_callback_timestamp(nsecs_t timestamp,
            int32_t msgType,
            const sp<IMemory> &dataPtr,
            void* user){
        //ALOGE("engine_camera_data_callback_timestamp timestamp = %lld, camera_output_num = %lld",
        //        timestamp/1000, camera_output_num);
        //if (timestamp == 0){
        //    ALOGI("camera_data_callback_timestamp drop, timestamp %d", timestamp);
        //    return;
        //}
        sp<IMemory> frame = dataPtr.get();
        MediaBuffer *buffer = new MediaBuffer(frame->pointer(), frame->size());
        buffer->add_ref();
        buffer->meta_data()->setInt64(kKeyTime, timestamp/1000);
        VideoCallEngineClient::incomingCameraCallBackFrame(&buffer);
    }

    void camera_data_callback(int32_t msgType, const sp<IMemory> &dataPtr,
                         camera_frame_metadata_t *metadata, void* user){
        ALOGI("custom_params_callback msgType = %d", msgType);
        //char *fptr = (char *) dataPtr->pointer();
        //ptr->pushFramestoCircularBuffer(fptr, (size_t)dataPtr->size());
    }

    void custom_params_callback(void* params) {
        ALOGI("custom_params_callback");
    }

    void camera_notify_callback(int32_t msgType, int32_t ext1, int32_t ext2,void* user) {
         ALOGI("camera_notify_callback msgType = %d", msgType);
    }

    VideoCallEngineClient* VideoCallEngineClient::getInstance() {
        //ALOGI("getInstance, VCEClient value = %p", VCEClient);
        if (VCEClient == NULL) {
            ALOGI("Create new Instance");
            VCEClient = new VideoCallEngineClient();
        }
        return VCEClient;
    }

    void VideoCallEngineClient::incomingCameraCallBackFrame(MediaBuffer **mediaBuffer){
        //MediaBuffer new
        VideoCallEngineClient* client = getInstance();
        if (client != NULL && VCEClient->mVQTest < 0 && !VCEClient->mHideLocalImage){
            camera_output_num ++;

            //calculate the actual frame rate from camera
            if (camera_output_num == 1){
                mCalculateCameraFpsStartTs = systemTime()/1000;
                ALOGI("mCalculateCameraFpsStartTs = %lld", mCalculateCameraFpsStartTs);
            }
            int64_t mCalculateCameraFpsCurTs = systemTime()/1000;
            if ((mCalculateCameraFpsStartTs != -1)
                    && (mCalculateCameraFpsCurTs - mCalculateCameraFpsStartTs >= 3000000)){
                mActFrameRate = camera_output_num * 1000000 /
                    (mCalculateCameraFpsCurTs - mCalculateCameraFpsStartTs);
                mCalculateCameraFpsStartTs = -1;
                ALOGI("mActFrameRate = %d", mActFrameRate);
            }

            // check whether the frame is encodable
            if (VCEClient->mEncBrAdapt->isEncodable()) {
                client->mCameraOutputBufferLock.lock();
                sp<ABuffer> aBuffer = ABuffer::CreateAsCopy((*mediaBuffer)->data(),
                        (*mediaBuffer)->size());
                //save tsMs in the Int32Data
                int64_t timeUs;
                (*mediaBuffer)->meta_data()->findInt64(kKeyTime, &timeUs);
                aBuffer->setInt32Data(timeUs/1000);
                client->mCameraOutputBuffers.push_back(aBuffer);
                client->mCameraOutputBufferLock.unlock();
                client->mCameraOutputBufferCond.signal();
                pushFrame ++;
                DLOG("incomingCameraCallBackFrame, pushFrame %lld", pushFrame);
            } else {
                dropFrame ++;
                DLOG("incomingCameraCallBackFrame, dropFrame %lld", dropFrame);
            }
        }
    }

    void* VideoCallEngineClient::CameraThreadWrapper(void* me)
    {
        return (void*)static_cast<VideoCallEngineClient*>(me)->CameraThreadFunc();
    }

    void* VideoCallEngineClient::VideoInputWrapper(void* me)
    {
        return (void*)static_cast<VideoCallEngineClient*>(me)->VideoInputThreadFunc();
    }

    status_t VideoCallEngineClient::VideoInputThreadFunc()
    {
        ALOGI("local input thread start");
        int setname_ret = pthread_setname_np(pthread_self(), "vce_input");
        if (0 != setname_ret){
            ALOGE("local input thread, set name failed, ret = %d", setname_ret);
        }

        Size currentPreviewSize;
        int32_t framerate = mVCEVideoParameter->mFps;
        currentPreviewSize.width = mVCEVideoParameter->mWidth;
        currentPreviewSize.height= mVCEVideoParameter->mHeight;
        int32_t frame_length = (currentPreviewSize.width)*(currentPreviewSize.height)*3/2;
        ALOGI("local input thread, video input size %d X %d, fps = %d", currentPreviewSize.width,
                currentPreviewSize.height, framerate);

        if(mVQTest >= 0){
            mReplacedYUV = "/data/misc/media/vqTest.yuv";
        } else if(mHideLocalImage){
            mReplacedYUV = "/data/misc/media/replaceImage.yuv";
        }
        ALOGI("local input thread, mReplacedYUV: %s", mReplacedYUV);

        mFp_local_input = NULL;
        mFp_local_input = fopen(mReplacedYUV, "r");
        if (mFp_local_input == NULL){
            ALOGE("local input thread end, mFp_local_input open failed");
            return 0;
        }

        mCameraStartTimeUs = systemTime()/1000;
        while (!mStopVideoInput && mFp_local_input != NULL)
        {
            if (mUpNetworkReady){
                MediaBuffer *mediaBuffer = new MediaBuffer(frame_length);
                //read video file
                if (mFp_local_input != NULL){
                     if (feof(mFp_local_input)){
                         ALOGE("local input thread, end of file, reopen");
                         fclose(mFp_local_input);
                         mFp_local_input = fopen(mReplacedYUV, "r");
                         if (mFp_local_input == NULL){
                             ALOGE("local input thread end, mFp_local_input reopen failed");
                             mediaBuffer->release();
                             break;
                         }
                     }
                     int64_t length= fread(mediaBuffer->data(), 1, frame_length, mFp_local_input);
                     if (length == frame_length){
                         ALOGI("local input thread, got one frame");
                     } else {
                         ALOGE("local input thread, read failed, length = %lld", length);
                         mediaBuffer->release();
                         continue;
                     }
                }
                //set TS(us) for one frame
                int64_t mCurrnetTimeUs = systemTime()/1000;
                mediaBuffer->meta_data()->setInt64(kKeyTime, (mCurrnetTimeUs-mCameraStartTimeUs));
                mCameraOutputBufferLock.lock();
                sp<ABuffer> aBuffer = ABuffer::CreateAsCopy(mediaBuffer->data(),
                        mediaBuffer->size());
                //save tsMs in the Int32Data
                int64_t timeUs;
                mediaBuffer->meta_data()->findInt64(kKeyTime, &timeUs);
                aBuffer->setInt32Data(timeUs/1000);
                mCameraOutputBuffers.push_back(aBuffer);
                mediaBuffer->release();
                mCameraOutputBufferLock.unlock();
                pushFrame ++;
                mCameraOutputBufferCond.signal();
                usleep((1000/framerate)*1000);
            }else {
                usleep(20*1000);
            }
        }
        ALOGI("local input thread, stop read");
        while(!isCameraOutputBufferEmpty())
        {
            ALOGI("local input thread, clean mCameraOutputBuffers");
            mCameraOutputBufferLock.lock();
            //MediaBuffer* mediaBuffer = *mCameraOutputBuffers.begin();
            //mediaBuffer->release();
            cameraRelease ++;
            mCameraOutputBuffers.erase(mCameraOutputBuffers.begin());
            mCameraOutputBufferLock.unlock();
        }
        ALOGE("local input thread end, pushFrame=%lld,cameraRelease=%lld,encodeRelease=%lld",
                pushFrame,cameraRelease,encodeRelease);
        mCameraExitedCond.signal();
        if (mFp_local_input != NULL){
            fclose(mFp_local_input);
            mFp_local_input = NULL;
        }
        return 0;
    }

    status_t VideoCallEngineClient::CameraThreadFunc()
    {
        ALOGI("camera thread start");
        int setname_ret = pthread_setname_np(pthread_self(), "vce_cam");
        if (0 != setname_ret){
            ALOGE("camera thread, set name failed, ret = %d", setname_ret);
        }
        char value[PROPERTY_VALUE_MAX];
        property_get("volte.incall.camera.enable", value, "false");
        if(strcmp(value, "true")){
            ALOGI("camera thread, camera prop enable");
            property_set("volte.incall.camera.enable", "true");
        }
        const char *rawClientName = "ASDFGH";
        int rawClientNameLen = 7;
        int ret;
        String16 clientName(rawClientName, rawClientNameLen);
        char fr_str[8] = {0};
        unsigned int frame_num = 0;
#if 1
        Size currentPreviewSize;
        int32_t framerate = mVCEVideoParameter->mFps;
        currentPreviewSize.width = mVCEVideoParameter->mWidth;
        currentPreviewSize.height= mVCEVideoParameter->mHeight;
        ALOGI("camera thread, output size %d X %d, fps = %d", currentPreviewSize.width,
                currentPreviewSize.height, framerate);

        sprintf(fr_str, "%d", framerate);
        ret = property_set("persist.volte.cmr.fps", fr_str);
        ALOGI("camera thread, property_set ret %d", ret);
        if ((mLocalSurface == NULL) || (mCamera== NULL) || (mCameraProxy== NULL)){
            ALOGI("camera thread end, mCamera %d, Proxy %d, mLocalSurface %d",
                    mCamera== NULL, mCameraProxy== NULL, mLocalSurface == NULL);
            mStopCamera = true;
            mCameraExitedCond.signal();
            return 0;
        }

        sp<CameraSource> source = CameraSource::CreateFromCamera(
            mCamera, mCameraProxy, -1, clientName, -1, currentPreviewSize, -1, NULL, true);
        status_t init_err = source->initCheck();
        ALOGI("camera thread, initCheck() = %d", init_err);
        if (init_err != OK){
            ALOGE("camera thread end, initCheck failed");
            mStopCamera = true;
            mCameraExitedCond.signal();
            return 0;
        }

        ALOGI("camera thread, source = %p camera = %p proxy = %p", source.get(), mCamera.get(), mCameraProxy.get());
        mCamera->sendCommand(CAMERA_CMD_ENABLE_SHUTTER_SOUND,0,0);
        //mCamera.clear();
        //mCameraProxy.clear();
#else
        Size currentPreviewSize;
        currentPreviewSize.height = 480;
        currentPreviewSize.width = 640;
        sp<ICamera> camera = NULL;
        sp<ICameraRecordingProxy> proxy = NULL;

        sp<CameraSource> source = CameraSource::CreateFromCamera(
            camera, proxy, 1, clientName, -1, currentPreviewSize, 30, mLocalSurface, true);

        //camera.clear();
        //proxy.clear();
#endif
        mCameraStartTimeUs = systemTime()/1000;
        MetaData* meta = new MetaData();
        meta->setInt64(kKeyTime, mCameraStartTimeUs);
        ALOGI("camera thread, source->start enter, startTimeUs=%llu", mCameraStartTimeUs);
        status_t err_start = source->start(meta);
        ALOGI("camera thread, source->start return, err = %d", err_start);
        if (err_start != (status_t)OK){
            ALOGE("camera thread end, source->start(), err = %d", err_start);
            mStopCamera = true;
        } else {
            status_t err_read = OK;
            while (err_read == OK && !mStopCamera)
            {
                MediaBuffer *mediaBuffer;
                err_read = source->read(&mediaBuffer);
                if (err_read!=OK)
                {
                    ALOGE("camera thread, source->read, err_read = %d", err_read);
                    break;
                }
                frame_num ++;
                if (getCameraOutputBufferSize() > 6){
                    /* there are 8 buffers only for camera out put,
                     * if size of mCameraOutputBuffers is nearly full,
                     * drop the current frmae
                     */
                    mediaBuffer->release();
                    dropFrame ++;
                    ALOGE("camera thread, drop one frame, dropFrame = %lld", dropFrame);
                }
                mCameraOutputBufferCond.signal();
            }
            ALOGI("camera thread, stop source->read");
            while(!isCameraOutputBufferEmpty())
            {
                ALOGI("camera thread, clean mCameraOutputBuffers");
                //MediaBuffer* mediaBuffer = *mCameraOutputBuffers.begin();
                //mediaBuffer->release();
                cameraRelease ++;
                mCameraOutputBuffers.erase(mCameraOutputBuffers.begin());
            }
            ALOGI("camera thread, stop mCameraOutputBuffers clean");
            property_get("volte.incall.camera.enable", value, "false");
            if(strcmp(value, "false")){
                ALOGI("camera thread, camera prop disable");
                property_set("volte.incall.camera.enable", "false");
            }
            if (source != NULL){
                ALOGE("pushFrame=%lld,dropFrame=%lld,cameraRelease=%lld,encodeRelease=%lld",
                        pushFrame,dropFrame,cameraRelease,encodeRelease);
                status_t err_stop = source->stop();
                ALOGI("camera thread, source->stop() = %d", err_stop);
                mStopCamera = true;
            }
        }
        ALOGI("camera thread end");
        mCameraExitedCond.signal();
        return 0;
    }

    int VideoCallEngineClient::getCameraOutputBufferSize(){
        mCameraOutputBufferLock.lock();
        int size = mCameraOutputBuffers.size();
        mCameraOutputBufferLock.unlock();
        return size;
    }

    bool VideoCallEngineClient::isCameraOutputBufferEmpty(){
        mCameraOutputBufferLock.lock();
        bool isEmpty = mCameraOutputBuffers.empty();
        mCameraOutputBufferLock.unlock();
        return isEmpty;
    }

    void VideoCallEngineClient::cleanCameraOutputBuffer(){
        ALOGI("cleanCameraOutputBuffer ");
        int i = 0;
        while(!isCameraOutputBufferEmpty())
        {
            mCameraOutputBufferLock.lock();
            if(mVQTest >= 0 || mHideLocalImage){
                ALOGI("cleanCameraOutputBuffer, mediabuffer release");
                sp<ABuffer> m_buffer = *mCameraOutputBuffers.begin();
                m_buffer = NULL;
            }
            mCameraOutputBuffers.erase(mCameraOutputBuffers.begin());
            mCameraOutputBufferLock.unlock();
            i++;
        }
        ALOGI("cleanCameraOutputBuffer num = %d", i);
        return;
    }

    // Start Audio Source(Downlink&Uplink) capturing
    void VideoCallEngineClient::startAudioCapture()
    {
        ALOGI("[Alan] startAudioCapture...");

        pthread_attr_t attr;
        pthread_attr_init(&attr);
        pthread_attr_setdetachstate(&attr, PTHREAD_CREATE_DETACHED);
        pthread_create(&mAudioCaptureThread, &attr, AudioCaptureThreadWrapper, this);
        pthread_attr_destroy(&attr);
    }

    void* VideoCallEngineClient::AudioCaptureThreadWrapper(void* me)
    {
        return (void*)static_cast<VideoCallEngineClient*>(me)->AudioCaptureThreadFunc();
    }

    status_t VideoCallEngineClient::AudioCaptureThreadFunc()
    {
        // Obtain Audio PCM data in this thread and encode to AMR format in RecordThread thread
        ALOGI("[Alan] AudioSourceThread...");
        status_t err = OK;
        sp<AudioSource> audioSource = new AudioSource(AUDIO_SOURCE_MIC,
                                                      String16(), /*changed for AndroidM */
                                                      8000 /* Sample Rate */,
                                                      1    /* Channel count */);

        err = audioSource->initCheck();
        err = audioSource->start();
        CHECK_EQ(err, status_t(OK));

        while (1) {

            MediaBuffer *mediaBuffer = NULL;
            err = audioSource->read(&mediaBuffer);
            CHECK_EQ(err, status_t(OK));

            ALOGI("[Alan] Audio mediaBuffer = %p, and mediaBuffer size = %zd", mediaBuffer, mediaBuffer->size());
            {
                Mutex::Autolock autoLock(mRecordedAudioBufferLock);
                mRecordedPCMBuffers.push_back(mediaBuffer);
                mRecordedAudioBufferCond.signal();
            }

        }

        // The recorded buffers will be released in RecordThread
        return audioSource->stop();

        return 0;
    }

    void* VideoCallEngineClient::EncodeThreadWrapper(void* me)
    {
        return (void*)static_cast<VideoCallEngineClient*>(me)->EncodeThreadFunc();
    }

    status_t VideoCallEngineClient::EncodeThreadFunc()
    {
        ALOGI("encode thread start");
        int setname_ret = pthread_setname_np(pthread_self(), "vce_enc");
        if (0 != setname_ret){
            ALOGE("set name for encode thread failed, ret = %d", setname_ret);
        }
#if DUMP_VIDEO_BS
        if (video_dump_enable && (!mFp_local_dump)) {
            ALOGI("video dump local start");
            mFp_local_dump = fopen("/data/misc/media/video_local.mp4","wb");
            if (!mFp_local_dump){
                ALOGE("create video_local.mp4 failed");
            }
        }
#endif
#if DUMP_VIDEO_YUV
        if (video_dump_enable && (!mFp_local_yuv)) {
            mFp_local_yuv = fopen("/data/misc/media/video_local.yuv","wb");
            if (!mFp_local_yuv){
                ALOGE("create video_local.yuv failed");
            }
        }
#endif
        ProcessState::self()->startThreadPool();
        status_t err = OK;
        CodecState state;

        uint64_t sendingStartTsMs = 0; // mark the systemtime when first frame be sent
        uint64_t firstFrameTsMs = 0; // mark the tsMS of frist incoming frame be sent
        // tsMs(sending ts) = sendingStartTsMs + (tsMs(incoming frame ts) - firstFrameTsMs)

        int qos_bitrate = 0;

        int64_t frameSend = 0;
        sendingFirstFrame = true;
        bool sps_pps_frame = true;
        bool need_send_sps_pps = true;
        int after_idr_frame_count = 30;
        int target_bitrate = 0;
        int max_bitrate = 0;
        int first_frame = 1;
        unsigned int bitrate_act = 0;
        uint64_t enc_tol_size = 0;
        uint64_t enc_interval_size = 0;
        uint32_t enc_frame_tol_num = 0;
        uint32_t enc_frame_interval_num = 0;

        //char br_str[PROPERTY_VALUE_MAX] = {0};
        int64_t stop_tm;
        int64_t start_tm;

        VC_EncodedFrame paraSetFrame = {NULL, 0, 0, 0, 0};

        sp<AMessage> format = new AMessage;

        ALOGI("encode thread, output size %d X %d, fps = %d, bitrate = %d",
                mVCEVideoParameter->mWidth, mVCEVideoParameter->mHeight,
                mVCEVideoParameter->mFps, mVCEVideoParameter->mCurBitrate);
        format->setInt32("width", mVCEVideoParameter->mWidth);
        format->setInt32("height", mVCEVideoParameter->mHeight);
        format->setFloat("frame-rate", mVCEVideoParameter->mFps);
        format->setInt32("bitrate", mVCEVideoParameter->mCurBitrate * 8/10);

#ifdef ENABLE_STRIDE
        format->setInt32("stride", mVCEVideoParameter->mWidthStride);
#endif

        VideoEncBitrateAdaption::Config cfg;
        cfg.curBr = mVCEVideoParameter->mCurBitrate;
        cfg.maxBr = mVCEVideoParameter->mMaxBitrate;
        cfg.minBr = mVCEVideoParameter->mMinBitrate;
        mEncBrAdapt->setConfig(cfg);
        mEncBrAdapt->enableFrameDrop();

        ALOGI("encode thread, init mEncBrAdapt, max %d, min %d, init %d",
                mVCEVideoParameter->mMaxBitrate, mVCEVideoParameter->mMinBitrate,
                mVCEVideoParameter->mCurBitrate);
        max_bitrate = mVCEVideoParameter->mMaxBitrate;

        if(mVCEVideoParameter->codecType == VCE_CODEC_VIDEO_H265){
            format->setString("mime", "video/hevc");
        } else {
            format->setString("mime", "video/avc");
        }
        if(mVQTest >= 0 || mHideLocalImage){
#ifdef VQ_TEST_NV21
            format->setInt32("color-format", OMX_SPRD_COLOR_FormatYVU420SemiPlanar); //0x7FD00001
#else
            format->setInt32("color-format", OMX_COLOR_FormatYUV420Planar);
#endif
            format->setInt32("store-metadata-in-buffers", 0);
        } else {
            format->setInt32("color-format", OMX_COLOR_FormatYUV420SemiPlanar);
            format->setInt32("store-metadata-in-buffers", 1);
        }
        format->setInt32("i-frame-interval", 1);
        format->setInt32("prepend-sps-pps-to-idr-frames", 0);

        sp<ALooper> looper = new ALooper;
        looper->start();

        if (mVCEVideoParameter->codecType == VCE_CODEC_VIDEO_H265){
            state.mCodec = MediaCodec::CreateByType(looper, "video/hevc", true);
        } else {
            state.mCodec = MediaCodec::CreateByType(looper, "video/avc", true);
        }
        if(state.mCodec == NULL){
            ALOGE("encode thread end, state.mCodec == NULL");
            return CodecThreadEndProcessing(NULL, looper, &mStopEncode);
        }

        sp<MediaCodec> codec = state.mCodec;

        if ((status_t)OK !=
            codec->configure(format, NULL, NULL, MediaCodec::CONFIGURE_FLAG_ENCODE)){
            ALOGE("encode thread end, codec->configure failed");
            return CodecThreadEndProcessing(codec, looper, &mStopEncode);
        }
        if ((status_t)OK != codec->start()){
            ALOGE("encode thread end, codec->start failed");
            return CodecThreadEndProcessing(codec, looper, &mStopEncode);
        }
        if ((status_t)OK != codec->getInputBuffers(&state.mInBuffers)){
            ALOGE("encode thread end, codec->getInputBuffers failed");
            codec->stop();
            return CodecThreadEndProcessing(codec, looper, &mStopEncode);
        }
        if ((status_t)OK != codec->getOutputBuffers(&state.mOutBuffers)){
            ALOGE("encode thread end, codec->getOutputBuffers failed");
            codec->stop();
            return CodecThreadEndProcessing(codec, looper, &mStopEncode);
        }

        ALOGI("encode got %d input and %d output buffers", state.mInBuffers.size(), state.mOutBuffers.size());

        if (mVCEVideoParameter->codecType != VCE_CODEC_VIDEO_H265) {
            AString SceneMode = "Volte";
            sp<AMessage> format2 = new AMessage;
            format2->setString("scene-mode",SceneMode);
            ALOGI("encode thread, set scene-mode: Volte");
            status_t set_error = codec ->setParameters(format2);
            if ((status_t)OK != set_error){
                ALOGE("encode thread, codec->setParameters scene-mode failed, err = %d", set_error);
            }
        }
        while (!mStopEncode)
        {
            size_t index;
            err = codec->dequeueInputBuffer(&index);
            if(err == OK)
            {
                sp<ABuffer> a_buffer = NULL;

                while(isCameraOutputBufferEmpty() && !mStopEncode)
                {
                    uint64 timeout = 500000000; //nano seconds, 500ms
                    int wait_err = mCameraOutputBufferCond.waitRelative(mCameraEncodeLock, timeout);
                    if (wait_err == -ETIMEDOUT){
                        ALOGI("encode thread, waiting for incoming frame");
                    }
                }

                if((mRequestIDRFrame && after_idr_frame_count >= 30)
                        || mRequestIDRFrameNow)
                {
                    ALOGI("encode thread, request idr frame by remote");
                    after_idr_frame_count = 0;
                    codec->requestIDRFrame();
                    sendingFirstFrame = true;
                    mRequestIDRFrame = false;
                    mRequestIDRFrameNow = false;
                }

                if(!mStopEncode)
                {
                    mCameraOutputBufferLock.lock();
                    a_buffer = *mCameraOutputBuffers.begin();
                    mCameraOutputBuffers.erase(mCameraOutputBuffers.begin());
                    mCameraOutputBufferLock.unlock();
                }
                else
                {
                    ALOGI("encode thread exit 1");
                    //mStopCamera = true;
                    break;
                }
                const sp<ABuffer> &buffer = state.mInBuffers.itemAt(index);

                memcpy(buffer->data(), a_buffer->data(), a_buffer->size());
                DLOG("buffer %p, size %d; mediaBuffer->size() %d",
                        buffer->data(), buffer->size(), a_buffer->size());

                int64_t timeUs;
                timeUs = ((int64_t)a_buffer->int32Data()) * 1000;
                DLOG("encode thread, got one frame, timeUs = %lld", timeUs);
                encodeRelease ++;
                if (mStopEncode){
                    ALOGI("encode thread exit 2");
                    break;
                }

                /* update paras when qos change happens after encode thread is started
                 * the max_bitrate and prev_target_bitrate update for VideoEncBitrateAdaption
                 * bitrate for encoder should also be updated */
                if(qos_bitrate != mUplinkGBR){
                    ALOGI("encode thread, qos is changed %d -> %d", qos_bitrate, mUplinkGBR);
                    qos_bitrate = mUplinkGBR;

                    /* if qos is changed during the video call,set qos targed bitrate
                       (mUplinkGBR * 90%) to maxbitrate */
                    mEncBrAdapt->updateMaxBitrate((max_bitrate < mUplinkGBR * 9/10)
                            ? max_bitrate : mUplinkGBR * 9/10);

                    ALOGI("encode thread, MaxBitRate is changed %d -> %d", max_bitrate,
                            mUplinkGBR * 9/10);
                    max_bitrate = mUplinkGBR * 9/10;

                    /* if target bitrate has not been calculated with tmmbr
                     * and qos bitrate is below the current bitrate,
                     * set qos targed bitrate (mUplinkGBR * 90%) to mEncBrAdapt */
                    if (target_bitrate <= 0 &&
                            (mVCEVideoParameter->mCurBitrate > mUplinkGBR * 9/10)){
                        mEncBrAdapt->setTargetBitrate(mUplinkGBR * 9/10);
                    }
                }

                target_bitrate = mEncBrAdapt->getTargetBitrate() * 8/10;
                if (target_bitrate && mEncBrAdapt->isReadyToUpdateCodec()) {
                    format->setInt32("video-bitrate", target_bitrate);
                    codec ->setParameters(format);
                    mEncBrAdapt->cleanFrames();
                }

                err = codec->queueInputBuffer(index, 0, buffer->size(), timeUs, 0);
                if(err != (status_t)OK){
                    ALOGE("encode thread exit, queueInputBuffer failed, err = %d", err);
                    break;
                } else {
                    DLOG("encode thread, queueInputBuffer success, index %d", index);
#if DUMP_VIDEO_YUV
                    /* using to dump yuv data send to encoder*/
                    if(video_dump_enable && mFp_local_yuv)
                    {
                        unsigned int* inData = (unsigned int*)(buffer->data() + buffer->offset());
                        unsigned int type = *inData++;
                        buffer_handle_t buf = *((buffer_handle_t *)(buffer->data() + 4));
                        GraphicBufferMapper &mapper = GraphicBufferMapper::get();
                        Rect bounds((mVCEVideoParameter->mWidth+15)&(~15),
                                (mVCEVideoParameter->mHeight+15)&(~15));
                        void* vaddr = NULL;
                        if (mapper.lock(buf, GRALLOC_USAGE_SW_READ_OFTEN
                                | GRALLOC_USAGE_SW_WRITE_NEVER, bounds, &vaddr)) {
                            ALOGE("dump one frame, mapper.lock failed", __LINE__);
                        }
                        if (vaddr != NULL){
                            if (type == 1) { //kMetadataBufferTypeGrallocSource
                                ALOGI("dump one frame, index %d, buffer %p, size %d, type %d",
                                        index, vaddr, buffer->size(), type);
                                fwrite(vaddr, 1, buffer->size(), mFp_local_yuv);
                            } else {
                                ALOGI("dump one frame, skip, wrong type");
                            }
                        }
                    }
#endif
                }
            }
            else
            {
               if (err != -EAGAIN){
                   ALOGE("encode thread exit, dequeueInputBuffer failed, err = %d", err);
                   break;
               } else {
                   DLOG("encode thread, dequeueInputBuffer EAGAIN %d", err);
                   // dequeneInputBuffer failed with EAGAIN, do this again
                   continue;
               }
            }

#if ENC_SYNC_MODE
            // sleep 10 ms for encoder to complete this frame
            usleep(10*1000);
#endif

            /* get output data from encoder,
             * should be executed only after queueInputbuffer success
             * do this untile dequeue output buffer failed with EAGAIN */
            while(!mStopEncode)
            {
                BufferInfo info; // for encode output data
                if (mStopEncode){
                    ALOGI("encode thread exit 3");
                    break;
                }
                status_t err = codec->dequeueOutputBuffer(&info.mIndex, &info.mOffset, &info.mSize, &info.mPresentationTimeUs, &info.mFlags);
                if (err == OK)
                {
                    DLOG("encode thread, dequeueOutputBuffer, info.mIndex = %d, info.mPresentationTimeUs = %lld",
                            info.mIndex, info.mPresentationTimeUs);
                    const sp<ABuffer> &buffer = state.mOutBuffers.itemAt(info.mIndex);
                    uint32_t tsMs = (uint32_t)((mCameraStartTimeUs+info.mPresentationTimeUs) / 1000);

                    /**
                     * <b> According to RCS 5.1 Spec Section 2.7.1.2. </b>
                     * <br>Last Packet of Key (I-Frame) will have 8 bytes of RTP Header Extension.
                     * <br>The 1-byte payload will indicate Camera and frame orientation as transmitted on wire.
                     * <br>
                     * <br> 7 6 5 4 3 2 1 0
                     * <br>+-+-+-+-+-+-+-+-+
                     * <br>|U|U|U|U|C| ROT |
                     * <br>+-+-+-+-+-+-+-+-+
                     * <br>
                     * <br>MSB Bits (7-4) are reserved for Future use.
                     * <br>
                     * <br>The Camera bit (bit 3) value that goes in to the RTP Header Extension is:
                     * <br>0 - Front Camera
                     * <br>1 - Back Camera
                     * <br>
                     * <br>The MSB (bit 2) of ROT indicates if we need to flip horizontally or not.
                     * <br>The last 2 bits (bit 1 and 0) of ROT indicates the rotation 0, 270, 180 or 90.
                     * <br>
                     * <br>This payload byte goes into the RTP Header Extension.
                     */

                    if(sps_pps_frame) //first frame is always sps pps frame
                    {
                        ALOGI("get sps/pps, tsMs = %d, size = %d", tsMs, buffer->size());
                        uint8_t* data = (uint8_t*)malloc(buffer->size());
                        memcpy(data, buffer->data(), buffer->size());
                        paraSetFrame.data_ptr = data;
                        paraSetFrame.tsMs = tsMs;
                        paraSetFrame.length = buffer->size();
                        paraSetFrame.flags = info.mFlags;
                        paraSetFrame.rcsRtpExtnPayload = 0;
                        sps_pps_frame = false;
                        /* as tsMs of sps/pps frame is 0, do not send it
                         * send it with I frame and share the same tsMs of I frame */
                        int err_release = codec->releaseOutputBuffer(info.mIndex);
                        if(err_release != (status_t)OK){
                            ALOGE("encode thread exit, releaseOutputBuffer failed, err = %d", err_release);
                            mStopEncode = true;
                        }
                        /* got sps/pps
                         * continue loop untill get one frame*/
                        continue;
                    }
                    if(mUpNetworkReady)
                    {
                        if (first_frame) {
                            start_tm = systemTime();
                            stop_tm = start_tm;
                            enc_tol_size = 0;
                            enc_interval_size = 0;
                            enc_frame_tol_num = 0;
                            enc_frame_interval_num = 0;
                            first_frame = 0;
                        } else{
                            int64_t now = systemTime();
                            if ((now - stop_tm) > 3000000000LL) {
                                unsigned int fr = (enc_frame_tol_num - enc_frame_interval_num) * 1000 / ((now - stop_tm)/1000000);
                                bitrate_act = (unsigned int)((enc_tol_size - enc_interval_size) * 8 * 1000 / ((now - stop_tm) / 1000000));
                                ALOGI("bitrate_act %u bps, frame_rate %u fps, start_tm %llu, stop_tm %llu, enc_tol_size %llu, frame_tol_num %u",
                                        bitrate_act, fr ,start_tm, stop_tm, enc_tol_size, enc_frame_tol_num);
                                //sprintf(br_str, "%u", bitrate_act);
                                //property_set("persist.volte.enc.bitrate_act", br_str);
                                stop_tm = now;
                                enc_interval_size = enc_tol_size;
                                enc_frame_interval_num = enc_frame_tol_num;
                            }
                        }

                        if(!sendingFirstFrame)
                        {
                            tsMs = sendingStartTsMs + (tsMs - firstFrameTsMs);

                            // send sps/pps before sending I frame
                            if((info.mFlags == 1) && need_send_sps_pps)
                            {
                                //using current tsMs of I frame as the tsMs of sps/pps
                                paraSetFrame.tsMs = tsMs;
                                sendSpsPps(&paraSetFrame);
                                enc_tol_size += paraSetFrame.length;
                                enc_frame_tol_num ++;
                                after_idr_frame_count ++;
                            }

                            // send current frame
                            VC_EncodedFrame frame = {NULL, 0, 0, 0, 0};
                            sendFrame(&frame, buffer, tsMs, info.mFlags, &frameSend);
                            enc_tol_size += frame.length;
                            enc_frame_tol_num ++;
                            free(frame.data_ptr);
                            after_idr_frame_count ++;
                        }
                        else if(info.mFlags == 1)
                        {
                            // update sendingStartTsMs;
                            sendingStartTsMs = systemTime()/1000000;
                            // backup the ts of incoming frame being sent at first
                            firstFrameTsMs = tsMs;
                            ALOGI("encode thread sendingStartTsMs = %lld, firstFrameTsMs = %lld",
                                    sendingStartTsMs, firstFrameTsMs);

                            tsMs = sendingStartTsMs + (tsMs - firstFrameTsMs);

                            // send sps/pps before sending I frame
                            if(need_send_sps_pps)
                            {
                                //using current tsMs of I frame as the tsMs of sps/pps
                                paraSetFrame.tsMs = tsMs;
                                sendSpsPps(&paraSetFrame);
                                enc_tol_size += paraSetFrame.length;
                                enc_frame_tol_num ++;
                                after_idr_frame_count ++;
                            }

                            //send first I frame
                            VC_EncodedFrame frame = {NULL, 0, 0, 0, 0};
                            sendFrame(&frame, buffer, tsMs, info.mFlags, &frameSend);
                            enc_tol_size += frame.length;
                            enc_frame_tol_num ++;
                            free(frame.data_ptr);
                            after_idr_frame_count ++;
                            sendingFirstFrame = false;
                        }
                        else
                        {
                            ALOGI("waiting I frame");
                        }

                        if(after_idr_frame_count > 10000)
                        {
                            after_idr_frame_count = 30;
                        }
                    }
                    else
                    {
                        ALOGI("encode thread, waiting uplink network ready");
                        //keep requesting idr frame for send idir
                        //frame out at once when get start encode event
                        //mRequestIDRFrame = true;
                        //after_idr_frame_count = 30;
                        //ALOGI("waiting send frame event tsMs %lld, mFlags %d", info.mPresentationTimeUs, info.mFlags);
                    }
                    err = codec->releaseOutputBuffer(info.mIndex);
                    if(err != (status_t)OK){
                        ALOGE("encode thread exit, releaseOutputBuffer failed, err = %d", err);
                        mStopEncode = true;
                    }

#if ENC_SYNC_MODE
                    /* only one frame has been queue input to encoder
                     * so, no more output buffer, should break output loop
                     * and run input loop */
                    break;
#endif

                }
                else
                {
                    if (err == INFO_FORMAT_CHANGED)
                    {
                        ALOGI("encode thread, dequeueOutputBuffer, INFO_FORMAT_CHANGED");
                        continue;
                    }
                    else if (err == INFO_OUTPUT_BUFFERS_CHANGED)
                    {
                        ALOGI("encode thread, dequeueOutputBuffer, INFO_OUTPUT_BUFFERS_CHANGED");
                        CHECK_EQ((status_t)OK, codec->getOutputBuffers(&state.mOutBuffers));
                        continue;
                    }
                    if (err == -EAGAIN)
                    {
                        DLOG("encode thread, dequeueOutputBuffer, -EAGAIN %d", err);
                        err = OK;
#if ENC_SYNC_MODE
                        continue;
#else
                        break;
#endif
                    } else {
                        ALOGE("encode thread, dequeueOutputBuffer failed, err = %d", err);
                        break;
                    }
                }
            }
        }
        cleanCameraOutputBuffer();
        ALOGI("stopping encode, release codec");
        ALOGI("pushFrame=%lld,dropFrame=%lld,cameraRelease=%lld,encodeRelease=%lld",
                pushFrame,dropFrame,cameraRelease,encodeRelease);
        codec->stop();
        codec->release();
        codec.clear();
        ALOGI("stopping encode, release looper");
        looper->stop();
        looper.clear();

        if(need_send_sps_pps) //if not send sps pps, the frame data shuold be free
        {
            ALOGI("free sps pps frame");
            free(paraSetFrame.data_ptr);
        }
        ALOGI("encode thread end, enc_frame_tol_num = %d", enc_frame_tol_num);
        mEncodeExitedCond.signal();
        return 0;
       }

       status_t VideoCallEngineClient::CodecThreadEndProcessing(
               sp<MediaCodec> codec, sp<ALooper> looper, bool* mStopCodec)
       {
           ALOGI("codec thread end, CodecThreadEndProcessing E");
           cleanCameraOutputBuffer();
           if (codec != NULL){
               ALOGI("codec thread end, release codec");
               codec->release();
               codec.clear();
           }
           if (looper != NULL){
               ALOGI("codec thread end, release looper");
               looper->stop();
               looper.clear();
           }
           *mStopCodec = true;
           ALOGI("codec thread end, CodecThreadEndProcessing X");
           return 0;
       }

       unsigned int VideoCallEngineClient::SaveRemoteVideoParamSet(
               sp<ABuffer> paramSetWithPrefix, unsigned int* frame_length, unsigned char **frame){
           ALOGI("SaveRemoteVideoParamSet");
           unsigned int length = *frame_length;
           if (paramSetWithPrefix == NULL){
               return 0;
           }
           length = paramSetWithPrefix->size();
           if (*frame != NULL){
               ALOGI("SaveRemoteVideoParamSet, paramSet was saved , update");
               free(*frame);
               *frame = NULL;
           }
           *frame = (unsigned char*)malloc(length);
           if(*frame == NULL){
               ALOGI("SaveRemoteVideoParamSet, failed");
               return 0;
           }
           memcpy(*frame, paramSetWithPrefix->data(), length);
           *frame_length = length;
           ALOGI("SaveRemoteVideoParamSet, length = %d", length);
           return length;
       }

       unsigned int VideoCallEngineClient::SendLocalParamSetToDecoder(
               unsigned int ps_length, unsigned char *ps_data,
               vint* frame_length, unsigned char** frame_data){
           ALOGI("SendLocalParamSetToDecoder, ps_length = %d", ps_length);
           if (ps_data == NULL || (ps_length == 0)){
               ALOGI("SendLocalParamSetToDecoder, ps is null");
               return 0;
           }
           uint8* tmp_frame_data = (uint8*)malloc(*frame_length);
           if(tmp_frame_data == NULL){
               ALOGE("SendLocalParamSetToDecoder, failed");
               return 0;
           }
           memcpy(tmp_frame_data, *frame_data, *frame_length);
           free(*frame_data);
           *frame_data = (uint8*)malloc(*frame_length + ps_length);
           if(*frame_data == NULL){
               ALOGE("SendLocalParamSetToDecoder, failed 2");
               return 0;
           }
           memcpy(*frame_data, ps_data, ps_length);
           memcpy(*frame_data + ps_length, tmp_frame_data, *frame_length);
           *frame_length += ps_length;
           free(tmp_frame_data);
           return ps_length;
       }

       void* VideoCallEngineClient::DecodeThreadWrapper(void* me)
       {
           return (void*)static_cast<VideoCallEngineClient*>(me)->DecodeThreadFunc();
       }

       status_t VideoCallEngineClient::DecodeThreadFunc()
       {
        ALOGI("decode thread start");
        // set width/height for codec as negotiation
        int dec_width = mVCEVideoParameter->mWidth;
        int dec_height = mVCEVideoParameter->mHeight;
        int64_t frameGot = 0;
        int64_t frame2Decoder = 0;
        int64_t frameRender = 0;
        using_local_ps = true;
        uint8 rotate_value = 0;
        int64_t cvo_change_ts_us = 0;
        int setname_ret = pthread_setname_np(pthread_self(), "vce_dec");
        if (0 != setname_ret){
            ALOGE("set name for decode thread failed, ret = %d", setname_ret);
        }
#if DUMP_VIDEO_BS
        if (video_dump_enable && (!mFp_video)){
            ALOGI("video dump remote start");
            mFp_video = fopen("/data/misc/media/video_remote.mp4", "wb");
            if (!mFp_video){
                ALOGI("create video_remote.mp4 failed");
            }
        }
#endif
        status_t err;
        ProcessState::self()->startThreadPool();

        // remote render buffer transform
        uint32_t transform = 0;
        bool isFirstPs = true;

        sp<ALooper> looper = new ALooper;
        looper->start();

        sp<AMessage> format = new AMessage;

        format->setInt32("width", dec_width);
        format->setInt32("height", dec_height);
        if (mVCEVideoParameter->codecType == VCE_CODEC_VIDEO_H265){
            format->setString("mime", "video/hevc");
        } else {
            format->setString("mime", "video/avc");
        }
        format->setInt32("bitrate", 1000000);
        format->setFloat("frame-rate", 30);
//        format->setInt32("color-format", 0x7FD00001);
        format->setInt32("i-frame-interval", 10);
        format->setInt32("store-metadata-in-buffers", 1);
        format->setInt32("transform", transform);
        format->setInt32("color-format", OMX_COLOR_FormatYUV420SemiPlanar);

        int64_t idr_time = systemTime();

        CodecState state;
        if (mVCEVideoParameter->codecType == VCE_CODEC_VIDEO_H265){
            state.mCodec = MediaCodec::CreateByType(looper, "video/hevc", false);
        } else {
            state.mCodec = MediaCodec::CreateByType(looper, "video/avc", false);
        }
        if(state.mCodec == NULL){
            ALOGE("decode thread end, state.mCodec == NULL");
            return CodecThreadEndProcessing(NULL, looper, &mStopDecode);
        }
        sp<MediaCodec> codec = state.mCodec;

        if(mRemoteSurface == NULL){
            ALOGE("decode thread end, mRemoteSurface == NULL");
            return CodecThreadEndProcessing(codec, looper, &mStopDecode);
        }
        sp<Surface> surface = new Surface(mRemoteSurface);

        ALOGI("decode thread, state.mCodec->configure");
        err = state.mCodec->configure(format, surface, NULL, 0);
        if (err != (status_t)OK){
            int retry_times = 5;
            while (err!=(status_t)OK && retry_times--){
                usleep(400*1000);
                ALOGI("decode thread, state.mCodec->configure retry %d", 5-retry_times);
                err = state.mCodec->configure(format, surface, NULL, 0);
                if (err == (status_t)OK){
                    break;
                }
            }
            if (err != (status_t)OK) {
                ALOGE("decode thread end, state.mCodec->configure failed");
                return CodecThreadEndProcessing(codec, looper, &mStopDecode);
            }
        }

        if ((status_t)OK != codec->start()){
            ALOGE("decode thread end, codec->start failed");
            return CodecThreadEndProcessing(codec, looper, &mStopDecode);
        }
        if ((status_t)OK != codec->getInputBuffers(&state.mInBuffers)){
            ALOGE("decode thread end, codec->getInputBuffers failed");
            codec->stop();
            return CodecThreadEndProcessing(codec, looper, &mStopDecode);
        }
        if ((status_t)OK != codec->getOutputBuffers(&state.mOutBuffers)){
            ALOGE("decode thread end, codec->getOutputBuffers failed");
            codec->stop();
            return CodecThreadEndProcessing(codec, looper, &mStopDecode);
        }

        ALOGI("decode got %d input and %d output buffers \n", state.mInBuffers.size(), state.mOutBuffers.size());
        VCESoftwareRenderer* render = new VCESoftwareRenderer(surface);

        while(!mStopDecode)
        {
            //dequeue input buffer from encoder, and send frame to encoder
            size_t index;
            err = codec->dequeueInputBuffer(&index, kTimeout);

            if (err == OK)
            {
                const sp<ABuffer> &buffer = state.mInBuffers.itemAt(index);

                //----------get frame from jbv-----------
                VC_EncodedFrame frame = {NULL, 0, 0, 0, 0};
                int retry = 0;
                {
                    ALOGI("VCI_getEncodedFrame begin");
                    getFrame(&frame);
                    while(frame.length <= 0 && !mStopDecode)
                    {
                        usleep(20 * 1000);
                        getFrame(&frame);
                        retry ++;
                    }
                    if(mStopDecode)
                    {
                        ALOGI("decode thread exit 1");
                        if(frame.length > 0){
                            free(frame.data_ptr);
                        }
                        break;
                    }
                }
                if(frame.flags != 2){
                    frameGot ++;
                }
                ALOGI("get length %d, tsMs %lld, mFlags %d, cvo_info %d, retry %d, frameGot %lld",
                  frame.length, frame.tsMs/90, frame.flags, frame.rcsRtpExtnPayload, retry, frameGot);
                //-----------------------------------------

#if ROTATE_ENABLE
                /* update remote display according to cvo info and local device oritation angle */
                if ((((frame.flags & MediaCodec::BUFFER_FLAG_SYNCFRAME) == 1)
                            && (rotate_value != frame.rcsRtpExtnPayload))
                            || (mDeviceOrientation != mPreviousDeviceOrientation && mUpdateDisplayOrientation)) {
                    ALOGI("decode thread, remote display update, %d X %d, flags = %d, cvo %d -> %d, device %d -> %d",
                        dec_width, dec_height, frame.flags, rotate_value, frame.rcsRtpExtnPayload,
                        mPreviousDeviceOrientation, mDeviceOrientation);

                    if (rotate_value != frame.rcsRtpExtnPayload){
                        cvo_change_ts_us = ((int64_t)frame.tsMs) * 1000 / 90;
                    }

                    // calutate rotate fram cvo info
                    rotate_value = frame.rcsRtpExtnPayload;
                    uint8 rotate = frame.rcsRtpExtnPayload & 3;
                    uint16 cvo_rotate = 0;
                    switch (rotate) {
                        case rotate_0: cvo_rotate = 0; break;
                        case rotate_90: cvo_rotate = 270; break;
                        case rotate_180: cvo_rotate = 180; break;
                        case rotate_270: cvo_rotate = 90; break;
                        default:
                            ALOGE("decode thread, invalid cvo info");
                            break;
                    }

                    /* local device rotated from A to B
                     * if B is 180, screen will not rotate and remote image is shown in wrong angle
                     * fix the remote display with local_rotate_fix*/
                    uint16 local_rotate_fix = 0;
                    if (mPreviousDeviceOrientation != mDeviceOrientation &&
                            mDeviceOrientation == 180){
                        local_rotate_fix = (360 + mPreviousDeviceOrientation - mDeviceOrientation)%360;
                    }
                    ALOGI("decode thread, remote display update, cvo_rotate = %d, local_rotate_fix = %d",
                            cvo_rotate, local_rotate_fix);

                    /* update transform according to cvo_rotate + local_rotate_fix
                     * notify APP the display size*/
                    transform = 0;
                    uint32_t display_width = dec_width;
                    uint32_t display_height = dec_height;
                    switch ((cvo_rotate + local_rotate_fix)%360) {
                        case 0:
                            break;
                        case 90:
                            transform = HAL_TRANSFORM_ROT_90;
                            display_width = dec_height;
                            display_height = dec_width;
                            break;
                        case 180:
                            transform = HAL_TRANSFORM_ROT_180;
                            break;
                        case 270:
                            transform = HAL_TRANSFORM_ROT_270;
                            display_width = dec_height;
                            display_height = dec_width;
                            break;
                        default:
                            break;
                    }
                    notifyCallback(VC_EVENT_RESOLUTION_CHANGED,
                            display_width, display_height, 0);
                    if(frame.rcsRtpExtnPayload & flip_h) {
                        transform = transform | HAL_TRANSFORM_FLIP_H;
                    }
                    ALOGI("cvo, transform = %d", transform);

                    /* local device orientation change, update remote surface immediately.
                     * for cvo change, update display when reander related frame */
                    if (mUpdateDisplayOrientation){
                        sp<NativeWindowWrapper> wrapper = new NativeWindowWrapper(surface);
                        if (wrapper != NULL){
                            ALOGI("local device orientation change, mCodec->configure");
                            native_window_set_buffers_transform(wrapper->getNativeWindow().get(), transform);
                            //status_t err_reconfig = state.mCodec->configure(format, surface, NULL, 0);
                        }
                        mUpdateDisplayOrientation = false;
                    }
                }

                //update the transform data
                format->setInt32("transform", transform);
#endif

                /* ---------------------------------------------------------
                 * if the frist received frame is not sps/pps, which should never happen,
                 * add local saved sps/pps to the frist received frame(not sps/pps)
                 */
                if (using_local_ps){
                    using_local_ps = false;
                    if (2 != (frame.flags & MediaCodec::BUFFER_FLAG_CODECCONFIG)){
                        SendLocalParamSetToDecoder(
                                frame_pps_length, frame_pps, &frame.length, &frame.data_ptr);
                        SendLocalParamSetToDecoder(
                                frame_sps_length, frame_sps, &frame.length, &frame.data_ptr);
                        ALOGI("decode thread, send local sps(%d) pps(%d) to decoder, frame_len(%d)",
                            frame_sps_length, frame_pps_length, frame.length);
                    } else {
                        ALOGI("decode thread, no need to send local sps(%d) pps(%d) to decoder",
                                frame_sps_length, frame_pps_length);
                    }
                }
                //----------------------------------------------------------

                buffer->meta()->setInt64("timeUs", frame.tsMs * 1000 / 90);
                buffer->setRange(0, frame.length);
                memcpy(buffer->data(), frame.data_ptr, frame.length);

                if (2 == (frame.flags & MediaCodec::BUFFER_FLAG_CODECCONFIG))
                {
                    sp<ABuffer> seqParamSet;
                    sp<ABuffer> picParamSet;
                    sp<ABuffer> videoParamSet;
                    if (mVCEVideoParameter->codecType == VCE_CODEC_VIDEO_H264){
                        ALOGI("decode thread, avc codec params");
                        MakeAVCCodecSpecificData(buffer, seqParamSet, picParamSet);
                    } else if (mVCEVideoParameter->codecType == VCE_CODEC_VIDEO_H265) {
                        ALOGI("decode thread, hevc codec params");
                        MakeHEVCCodecSpecificData(buffer, videoParamSet, seqParamSet, picParamSet);
                    } else {
                        ALOGI("decode thread, unknow codec params");
                    }
                    if(videoParamSet != NULL){
                        sp<ABuffer> videoParamSetWithPrefix = new ABuffer(videoParamSet->size()+4);
                        memcpy(videoParamSetWithPrefix->data(), "\x00\x00\x00\x01", 4);
                        memcpy(videoParamSetWithPrefix->data() + 4, videoParamSet->data(),
                                videoParamSet->size());
#if 1
                        hexdump(videoParamSetWithPrefix->data(), videoParamSetWithPrefix->size());
#endif
                    }
                    if(seqParamSet != NULL)
                    {
                        sp<ABuffer> seqParamSetWithPrefix = new ABuffer(seqParamSet->size() + 4);
                        memcpy(seqParamSetWithPrefix->data(), "\x00\x00\x00\x01", 4);
                        memcpy(seqParamSetWithPrefix->data() + 4, seqParamSet->data(), seqParamSet->size());
#if 1
                        hexdump(seqParamSetWithPrefix->data(), seqParamSetWithPrefix->size());
#endif
                        //here using to notify AP resolution change, for example: 480x640 --> 640x480
                        int32_t tmp_width = dec_width;
                        int32_t tmp_height = dec_height;
                        if(mVCEVideoParameter->codecType == VCE_CODEC_VIDEO_H264){
                            FindAVCDimensions(seqParamSet, &tmp_width, &tmp_height);
                        } else if(mVCEVideoParameter->codecType == VCE_CODEC_VIDEO_H265){
                            FindHEVCDimensions(seqParamSet, &tmp_width, &tmp_height);
                        }
                        DLOG("decode thread, resolution from sps, %d X %d", tmp_width, tmp_height);
                        if(isFirstPs || (tmp_width != dec_width) || (tmp_height != dec_height)){
                            ALOGI("decode thread, resolution changed, %d X %d, rotate_value = %d, firstPs = %d",
                                    tmp_width, tmp_height, rotate_value, isFirstPs);
                            isFirstPs = false;
                            //update current UI width/height
                            dec_width = tmp_width;
                            dec_height = tmp_height;
                            if ((1 & rotate_value) == 0){
                                notifyCallback(VC_EVENT_RESOLUTION_CHANGED,
                                        dec_width, dec_height, rotate_value);
                            }
                            if ((1 & rotate_value) == 1){
                                notifyCallback(VC_EVENT_RESOLUTION_CHANGED,
                                        dec_height, dec_width, rotate_value);
                            }
                            //save the width/height data
                            format->setInt32("width", dec_width);
                            format->setInt32("height", dec_height);
                        }

                        //ALOGI("[Alan] set csd-0");
                        //mVideoFormat->setBuffer("csd-0", seqParamSetWithPrefix);
                        if(0 == (sps_pps_saved & 1)){
                            SaveRemoteVideoParamSet(
                                    seqParamSetWithPrefix, &frame_sps_length, &frame_sps);
                            sps_pps_saved |= 1;
                            ALOGI("decode thread, save sps(%d), sps_pps_saved(%x)",
                                    frame_sps_length, sps_pps_saved);
                        }
                    }
                    if(picParamSet != NULL)
                    {
                        sp<ABuffer> picParamSetWithPrefix = new ABuffer(picParamSet->size() + 4);
                        memcpy(picParamSetWithPrefix->data(), "\x00\x00\x00\x01", 4);
                        memcpy(picParamSetWithPrefix->data() + 4, picParamSet->data(), picParamSet->size());
#if 1
                        hexdump(picParamSetWithPrefix->data(), picParamSetWithPrefix->size());
#endif
                        //ALOGI("[Alan] set csd-1");
                        //mVideoFormat->setBuffer("csd-1", picParamSetWithPrefix);
                        if(0 == (sps_pps_saved & 2)){
                            SaveRemoteVideoParamSet(
                                    picParamSetWithPrefix, &frame_pps_length, &frame_pps);
                            sps_pps_saved |= 2;
                            ALOGI("decode thread, save pps(%d), sps_pps_saved(%x)",
                                    frame_pps_length, sps_pps_saved);
                        }
                    }
                }

                if (1 == (frame.flags & MediaCodec::BUFFER_FLAG_SYNCFRAME))
                {
                    idr_time = systemTime();
                }
                //check if I frame is received on time, and send fir if needed
                if(idr_time > 0)
                {
                    int64_t now = systemTime();
                    unsigned int dur = (unsigned int)((now - idr_time) / 1000000L);
                    if(dur >= 2000 && !mStopDecode)
                    {
                        ALOGI("VCI_sendFIR as dur = %d", dur);
                        VCI_sendFIR();
                        idr_time = systemTime();
                    }
                }
#if DUMP_VIDEO_BS
                // Dump input video buffer(bs)
                if(video_dump_enable && mFp_video)
                {
                    fwrite(buffer->data(), 1, frame.length, mFp_video);
                }
#endif

#if RECORD_TEST
                // It's recoding so store encoded frames
                if (!mStopRecord) {
                    int64_t timeStamp = 0;

                    if (startSysTime == 0) {
                        startSysTime = systemTime();
                    } else {
                        timeStamp = systemTime() - startSysTime;
                    }

                    err = mMuxer->writeSampleData(buffer, mVideoTrack, timeStamp / 1000, frame.flags);
                    CHECK_EQ(err, status_t(OK));
                }

                if (counter++ >= MAX_INPUT_BUFFERS && !mStopRecord) {
                    ALOGI("[Alan] Stop MediaMux...");
                    mMuxer->stop();
                    mStopRecord = true;
                }
#endif
                if (mStopDecode){
                    ALOGI("decode thread exit 2");
                    if(frame.length > 0){
                        free(frame.data_ptr);
                    }
                    break;
                }
                err = codec->queueInputBuffer(index, buffer->offset(), buffer->size(), ((int64_t)frame.tsMs) * 1000 / 90, frame.flags);
                if (err != OK)
                {
                    ALOGE("decode thread exit, queueInputBuffer failed, err = %d", err);
                    mStopDecode = true;
                } else {
                    if (frame.flags != 2){
                        frame2Decoder++;
                        DLOG("decode thread, queueInputBuffer success, index %d, ts %lld", index, ((int64_t)frame.tsMs) * 1000 / 90);
                    }
                }
                free(frame.data_ptr);
            }
            else
            {
               if(err != -EAGAIN)
               {
                   ALOGE("decode thread exit, dequeueInputBuffer failed, error = %d", err);
                   mStopDecode = true;
               } else {
                   DLOG("decode thread, dequeueInputBuffer, EAGAIN %d", err);
               }
            }
            BufferInfo info;
            if (mStopDecode){
                ALOGI("decode thread exit 3");
                break;
            }

            // get decoded frame from decoder
            err = codec->dequeueOutputBuffer(&info.mIndex, &info.mOffset, &info.mSize, &info.mPresentationTimeUs, &info.mFlags);
            if (err == OK)
            {
                frameRender ++;
                DLOG("decode thread, dequeueOutputBuffer, success, info.mIndex %d, mPresentationTimeUs %lld", info.mIndex, info.mPresentationTimeUs);
                if (mRemoteSurface!=NULL && mStartRender)
                {
                    if (cvo_change_ts_us == info.mPresentationTimeUs){
                        sp<NativeWindowWrapper> wrapper = new NativeWindowWrapper(surface);
                        if (wrapper != NULL){
                            ALOGI("cvo, mCodec->configure");
                            native_window_set_buffers_transform(wrapper->getNativeWindow().get(), transform);
                        }
                        cvo_change_ts_us = 0;
                    }
                    // kaios only, get yuv data from GraphicBuffer and render
                    sp<GraphicBuffer> aGraphicBuffer = state.mCodec->getOutputGraphicBufferFromIndex(info.mIndex);
                    DLOG("decode thread, getOutputGraphicBufferFromIndex info.mIndex:%d, aGraphicBuffer:%p, getWidth:%d"
                           " getPixelFormat:%d", info.mIndex, aGraphicBuffer.get(), aGraphicBuffer->getWidth(),
                           aGraphicBuffer->getPixelFormat());
                    const void * bufferndata;
                    aGraphicBuffer->lock(GraphicBuffer::USAGE_SOFTWARE_MASK, (void **)&bufferndata);
                    render->render((const void *)bufferndata, info.mSize, info.mPresentationTimeUs*1000, 0, format);
                    aGraphicBuffer->unlock();
                    // render complete, release the outpurt buffer
                    int err_release = codec->releaseOutputBuffer(info.mIndex);
                    if (err_release != OK){
                        ALOGE("decode thread, releaseOutputBuffer after render failed, err_release = %d", err_release);
                    }
#if 0
                    int err_render = codec->renderOutputBufferAndRelease(info.mIndex);
                    if (err_render != OK){
                        ALOGE("decode thread, renderOutputBufferAndRelease failed, err_render = %d", err_render);
                    }
#endif
                }
                else
                {
                    DLOG("decode thread, release buffer without render, mStartRender = %d", mStartRender);
                    int err_release = codec->releaseOutputBuffer(info.mIndex);
                    if (err_release != OK){
                        ALOGE("decode thread, releaseOutputBuffer failed, err_release = %d", err_release);
                    }
                }
            }
            else if (err == INFO_OUTPUT_BUFFERS_CHANGED)
            {
                ALOGI("decode thread, dequeueOutputBuffer, INFO_OUTPUT_BUFFERS_CHANGED");
                int err_get = state.mCodec->getOutputBuffers(&state.mOutBuffers);
                if((status_t)OK != err_get){
                    ALOGE("decode thread exit, getOutputBuffers failed, err = %d", err_get);
                    mStopDecode = true;
                }
            }
            else if (err == INFO_FORMAT_CHANGED)
            {
                ALOGI("decode thread, dequeueOutputBuffer, INFO_FORMAT_CHANGED");
                sp<NativeWindowWrapper> wrapper = new NativeWindowWrapper(surface);
                if (wrapper != NULL){
                    native_window_set_buffers_transform(wrapper->getNativeWindow().get(), transform);
                    ALOGI("decode thread, reset transform = %d", transform);
                    status_t err_reconfig = state.mCodec->configure(format, surface, NULL, 0);
                }
            }
            else
            {
                if(err != -EAGAIN)
                {
                    ALOGE("decode thread exit, dequeueOutputBuffer failed, error = %d", err);
                    mStopDecode = true;
                } else {
                    DLOG("decode thread, dequeueOutputBuffer, EAGAIN %d", -EAGAIN);
                }
            }
        }
        ALOGI("stopping decode, delete render");
        delete render;
        ALOGI("stopping decode, release codec");
        codec->stop();
        codec->release();
        codec.clear();
        ALOGI("stopping decode, release looper");
        looper->stop();
        looper.clear();
        ALOGI("decode thread end, frameGot %lld, frame2Decoder %lld frameRender %d",
                frameGot, frame2Decoder, frameRender);
        mDecodeExitedCond.signal();
        return 0;
       }

       void* VideoCallEngineClient::Switch2ThreadWrapper(void* me)
       {
           return (void*)static_cast<VideoCallEngineClient*>(me)->Switch2ThreadFunc();
       }

       status_t VideoCallEngineClient::Switch2ThreadFunc()
       {
           ALOGI("startSwitch");
           int setname_ret = pthread_setname_np(pthread_self(), "vce_back");
           if (0 != setname_ret){
               ALOGE("set name for Switch2ThreadFunc thread failed, ret = %d", setname_ret);
           }
           status_t err;
           ProcessState::self()->startThreadPool();
           while (!mStopSwitch)
           {
               VC_EncodedFrame frame = {NULL, 0, 0, 0, 0};
               int retry = 0;
               ALOGI("VCI_getEncodedFrame begin");
               getFrame(&frame);
               while(frame.length <= 0 && !mStopSwitch)
               {
                   usleep(20 * 1000);
                   getFrame(&frame);
                   retry ++;
               }
               free(frame.data_ptr);
           }
           mSwitchExitedCond.signal();
           ALOGI("Switch2ThreadFunc end");
           return 0;
           }
        void VideoCallEngineClient::startCamera()
        {
            if(mStopCamera &&  mCamera != NULL)
            {
                ALOGI("start camera");
                mStopCamera = false;
                pthread_attr_t attr;
                pthread_attr_init(&attr);
                pthread_attr_setdetachstate(&attr, PTHREAD_CREATE_DETACHED);
                pthread_create(&mCameraThread, &attr, CameraThreadWrapper, this);
                pthread_attr_destroy(&attr);
            }
            ALOGI("start camera thread end");
        }

        void VideoCallEngineClient::startEncode()
        {
            if(mStopCamera && mLocalSurface != NULL && mCamera != NULL)
            {
                startCamera();
                ALOGI("start encode");
                mStopEncode = false;
                mUplinkReady = true;
                pthread_attr_t attr;
                pthread_attr_init(&attr);
                pthread_attr_setdetachstate(&attr, PTHREAD_CREATE_DETACHED);
                pthread_create(&mEncodeThread, &attr, EncodeThreadWrapper, this);
                pthread_attr_destroy(&attr);

                if(mUpNetworkReady)
               {
                   notifyCallback(VC_EVENT_START_ENC);
                   ALOGI("notifyCallback VC_EVENT_START_ENC");
               }
               else
               {
                   notifyCallback(1000);
               }
           }
           else
           {
               ALOGI("mStopCamera = %d, mLocalSurface = %d, mCamera = %d",
                   mStopCamera, mLocalSurface != NULL, mCamera != NULL);
           }
    }
    void VideoCallEngineClient::setLocalSurface(sp<IGraphicBufferProducer> bufferProducer)
    {
        ALOGI("setSurface VCE_SERVICE_CAMERA");
        mLocalSurface = bufferProducer;
        if(bufferProducer != NULL)
        {
            startEncode();
        } else {
            stopUplink();
        }
    }

    void VideoCallEngineClient::setRemoteSurface(sp<IGraphicBufferProducer> bufferProducer)
    {
        ALOGI("setSurface VCE_SERVICE_DOWNLINK");
        mRemoteSurface = bufferProducer;
        mDownlinkReady=true;
        if (mDownNetworkReady && mRemoteSurface != NULL )
        {
            notifyCallback(VC_EVENT_START_DEC);
            ALOGI("notifyCallback VC_EVENT_START_DEC");
        } else{
            ALOGI("startUplink, mDownNetworkReady = %d, (mRemoteSurface != NULL)? %d",
                    mDownNetworkReady, (mRemoteSurface != NULL));
        }
    }

    void VideoCallEngineClient::setCamera(const sp<ICamera>& camera, const sp<ICameraRecordingProxy>& proxy, VCE_CAMERA_SIZE camera_size)
    {
        ALOGI("setCamera, camera == NULL? %d", (camera == NULL));
        if (camera == NULL){
            stopUplink();
            mCamera = camera;
            mCameraProxy = proxy;
            mCameraSize = camera_size;
            mVCEVideoParameter->updateVideoParameter(mCameraSize);
        } else {
            mCamera = camera;
            mCameraProxy = proxy;
            mCameraSize = camera_size;
            mVCEVideoParameter->updateVideoParameter(mCameraSize);
            startEncode();
        }
    }

    void VideoCallEngineClient::startVideoInput()
    {
        if(mVQTest < 0 && !mHideLocalImage){
            ALOGI("startVideoInput, should not be here");
            return;
        }
        if(!mStopVideoInput){
            ALOGI("startVideoInput failed as input thread has been started");
            return;
        }
        ALOGI("startVideoInput E");
        mStopVideoInput = false;
        pthread_attr_t attr;
        pthread_attr_init(&attr);
        pthread_attr_setdetachstate(&attr, PTHREAD_CREATE_DETACHED);
        pthread_create(&mVideoInputThread, &attr, VideoInputWrapper, this);
        pthread_attr_destroy(&attr);
        ALOGI("startVideoInput X");
    }

    void VideoCallEngineClient::stopVideoInput()
    {
        if(!mStopVideoInput)
        {
            ALOGI("mStopVideoInput E");
            mStopVideoInput = true;
            int64_t timeout = 1000000000; //nano seconds, 1s
            status_t wait_err = mCameraExitedCond.waitRelative(mCameraExitedLock,timeout);
            ALOGI("mStopVideoInput X, wait_err = %d", wait_err);
            if (wait_err == -ETIMEDOUT){
                ALOGE("mStopVideoInput, time__out");
            }
            ALOGI("mStopVideoInput X");
        } else {
            ALOGI("mStopVideoInput, no need");
        }
    }

    void VideoCallEngineClient::startEncodeThread()
    {
        if(!mStopEncode){
            ALOGI("startEncodeThread, return as encode thread has been startted");
        }
        ALOGI("start encode thread");
        mStopEncode = false;
        mUplinkReady = true;
        pthread_attr_t attr;
        pthread_attr_init(&attr);
        pthread_attr_setdetachstate(&attr, PTHREAD_CREATE_DETACHED);
        pthread_create(&mEncodeThread, &attr, EncodeThreadWrapper, this);
        pthread_attr_destroy(&attr);

        if(mUpNetworkReady)
       {
           notifyCallback(VC_EVENT_START_ENC);
           ALOGI("notifyCallback VC_EVENT_START_ENC");
       }
       else
       {
           notifyCallback(1000);
       }
    }

    void VideoCallEngineClient::stopEncodeThread()
    {
       if(!mStopEncode)
        {
            ALOGI("stop encode thread");
            mStopEncode = true;
            ALOGI("mCameraOutputBufferCond.signal");
            mCameraOutputBufferCond.signal();
            uint64 time_out = 1500000000; //nano seconds, 1500ms
            int wait_err = mEncodeExitedCond.waitRelative(mEncodeExitedLock, time_out);
            if (wait_err == -ETIMEDOUT){
                ALOGE("stop encode thread time__out");
            }
            ALOGI("encode thread stopped");
        } else {
            ALOGI("encode thread has already been stopped");
        }
    }

    //open/close camera
    int VideoCallEngineClient::setCameraId(int cameraId){
        ALOGI("setCameraId E, cameraId = %d", cameraId);
        int ret = 0;
        if (cameraId == mCameraId){
            ALOGI("setCameraId, ignore");
            return ret;
        }
        if(cameraId == -1){
            ALOGI("setCameraId, close camera");
            releaseCamera();
            mCameraId = cameraId;
            ALOGI("setCameraId X");
            return ret;
        }
        if(mCameraOpen){
            ALOGI("setCameraId, switch camera");
            switchCamera(cameraId);
        } else {
            ALOGI("setCameraId, open camera");
            ret = initCamera(cameraId);
        }
        mCameraId = cameraId;
        ALOGI("setCameraId X, ret = %d", ret);
        return ret;
    }

    void VideoCallEngineClient::setCameraPreviewSize(VCE_CAMERA_SIZE cameraSize){
        ALOGI("setCameraPreviewSize E");

        /* APP may startPreview(with default size) before set preview size!!!
         * stopPreview, set preview size and startPreview again */
        int need_restart_preview = false;
        if (mCameraPreviewStarted && cameraSize != mCameraSize){
            ALOGI("setCameraPreviewSize, stopPreview");
            stopPreview();
            need_restart_preview = true;
        }

        if ((cameraSize != mCameraSize) && !mStopEncode && (mVQTest< 0)){
            ALOGI("setCameraPreviewSize, camera size changed as negotiated");
            stopEncodeThread();
        }
        mCameraSize = cameraSize;
        mVCEVideoParameter->updateVideoParameter(mCameraSize, mUplinkGBR);

        cameraSetPreviewSize(mVCEVideoParameter->mWidth, mVCEVideoParameter->mHeight);
        cameraSetFps(mVCEVideoParameter->mFps);
        ALOGI("setCameraPreviewSize, %d X %d, fps = %d", mVCEVideoParameter->mWidth,
                mVCEVideoParameter->mHeight, mVCEVideoParameter->mFps);

        if(mVQTest >= 0){
            //this para should match the size of yuv file using for vq test
            ALOGI("setCameraPreviewSize, vq test enabled, set video size according to prop");
            mCameraSize = (VCE_CAMERA_SIZE)mVQTest;
            mVCEVideoParameter->updateVideoParameter(mCameraSize, mUplinkGBR);
            ALOGI("setCameraPreviewSize mVQTest, %d X %d, fps = %d", mVCEVideoParameter->mWidth,
                    mVCEVideoParameter->mHeight, mVCEVideoParameter->mFps);
        }

        if (need_restart_preview){
            ALOGI("setCameraPreviewSize, startPreview");
            startPreview();
            need_restart_preview = false;
        }

        if (mStopEncode){
            ALOGI("setCameraPreviewSize, startEncodeThread");
            startEncodeThread();
        }
    }

    void VideoCallEngineClient::setPreviewOrientation(int deviceOrientation){
        ALOGI("setPreviewOrientation %d -> %d", mDeviceOrientation, deviceOrientation);
        int previewOrientation = deviceOrientation;
        if (!mCameraPreviewStarted){
            ALOGI("setPreviewOrientation, init");
            if (deviceOrientation == 180){
                previewOrientation = mDeviceOrientation;
            }
            if (0 == imsCamerasetPreviewDisplayOrientation(previewOrientation)){
                setCVOInfo(deviceOrientation);
                mPreviousDeviceOrientation = mDeviceOrientation;
                mDeviceOrientation = deviceOrientation;
                mUpdateDisplayOrientation = true;
            }
        } else if (mDeviceOrientation != deviceOrientation){
            ALOGI("setPreviewOrientation, update");
            if (deviceOrientation == 180){
                previewOrientation = mDeviceOrientation;
            }
            if (0 == imsCamerasetPreviewDisplayOrientation(previewOrientation)){
                setCVOInfo(deviceOrientation);
                mPreviousDeviceOrientation = mDeviceOrientation;
                mDeviceOrientation = deviceOrientation;
                mUpdateDisplayOrientation = true;
            }
        } else {
            ALOGI("setPreviewOrientation, no change, ignore");
        }
        ALOGI("setPreviewOrientation X");
    }


    /*
        // flip source image horizontally (around the vertical axis)
        HAL_TRANSFORM_FLIP_H    = 0x01,
        // flip source image vertically (around the horizontal axis)
        HAL_TRANSFORM_FLIP_V    = 0x02,
        // rotate source image 90 degrees clockwise
        HAL_TRANSFORM_ROT_90    = 0x04,
        // rotate source image 180 degrees
        HAL_TRANSFORM_ROT_180   = 0x03,
        // rotate source image 270 degrees clockwise
        HAL_TRANSFORM_ROT_270   = 0x07,
        // don't use. see system/window.h
        HAL_TRANSFORM_RESERVED  = 0x08,
    */
    void  VideoCallEngineClient::setCVOInfo(int cvo_rotate)
    {
        ALOGI("setCVOInfo, rotate = %d", cvo_rotate);
        int _rotate = 0;
        int _flip_h = 0;
        if (mCameraId == 1){ //facing camera
            switch (cvo_rotate)
            {
                case 90:  _rotate = 3; break;
                case 180: _rotate = 2; break;
                case 270: _rotate = 1; break;
                default:  _rotate = 0; break;
            }
        } else { //back camera
            switch (cvo_rotate)
            {
                case 90:  _rotate = 1; break;
                case 180: _rotate = 2; break;
                case 270: _rotate = 3; break;
                default:  _rotate = 0; break;
            }
        }
        if (mVQTest >= 0 || mHideLocalImage){
            ALOGI("setCVOInfo for local input");
            _rotate = 0;
            _flip_h = 0;
        }
        if (_rotate != mRotate || _flip_h != mFlip_h){
            ALOGI("setCVOInfo, cvo changed, request an I frame with new cvo info");
            mRequestIDRFrame = true;
            mRequestIDRFrameNow = true;
            mRotate = _rotate;
            mFlip_h = _flip_h;
        } else {
            ALOGI("setCVOInfo, cvo info is not changed, return");
        }
    }

    void VideoCallEngineClient::setPreviewSurface(sp<IGraphicBufferProducer> bufferProducer)
    {
        ALOGI("setPreviewSurface");
        mLocalSurface = bufferProducer;
        if(bufferProducer != NULL)
        {
            //set preview surface
            if (mCameraPreviewStarted){
                ALOGI("setPreviewSurface, preview is on going");
                return;
            }
            if (!mCameraOpen) {
                ALOGI("setPreviewSurface, camera is not open");
                return;
            }
            imsCameraSetPreviewSurface(&mLocalSurface);
            mPreviewSurfaceSet = true;
        } else {
            ALOGI("setPreviewSurface, surface is null");
            if (mCameraPreviewStarted){
                stopPreview();
            }
        }
    }

    int VideoCallEngineClient::initCamera(int cameraId)
    {
        ALOGI("initCamera E cameraId =%d", cameraId);
        //--set camera in volte mode to avoid distortion issue
        char value[PROPERTY_VALUE_MAX];
        property_get("volte.incall.camera.enable", value, "false");
        if(strcmp(value, "true")){
            ALOGI("initCamera, camera prop enable");
            property_set("volte.incall.camera.enable", "true");
        }
        //--
        registerCallbacks(camera_notify_callback,
                camera_data_callback,
                camera_data_callback_timestamp,
                custom_params_callback,
                NULL);
        int ret = (int)imsCameraOpen(cameraId);
        if (ret == 0){
            mCameraOpen = true;
        }
        ALOGI("initCamera X, ret = %d", ret);
        return ret;
    }

    void VideoCallEngineClient::switchCamera(int cameraId){
        ALOGI("switchCamera E");
        ALOGI("switchCamera X");
    }

    void VideoCallEngineClient::releaseCamera()
    {
        ALOGI("releaseCamera E");
        if(!mCameraOpen){
            ALOGI("releaseCamera X, camera is closed");
            return;
        }
        //stop preview before prop setting to avoid preview distortion
        stopPreview();
        char value[PROPERTY_VALUE_MAX];
        property_get("volte.incall.camera.enable", value, "false");
        if(strcmp(value, "false")){
            ALOGI("releaseCamera, camera prop disable");
            property_set("volte.incall.camera.enable", "false");
        }
        imsCameraRelease();
        mCameraOpen = false;
        mCameraPreviewStarted = false;
        mPreviewSurfaceSet = false;
        mCameraId = -1;
        cleanCameraOutputBuffer();
        ALOGI("releaseCamera X");
    }

    void VideoCallEngineClient::startPreview()
    {
        if(mLocalSurface != NULL && !mCameraPreviewStarted && mCameraOpen){
            if (!mPreviewSurfaceSet) {
                // confirm preview surface is set before start preview
                ALOGI("startPreview, set preview surface");
                imsCameraSetPreviewSurface(&mLocalSurface);
                mPreviewSurfaceSet = true;
            }
            ALOGI("startPreview");
            if (0 != imsCameraStartPreview()){
                ALOGI("startPreview failed");
                return;
            }
            mCameraPreviewStarted = true;
            if(mVQTest >= 0 || mHideLocalImage){
                ALOGI("startPreview, start local video input thread");
                if (mHideLocalImage) {
                    mVCEVideoParameter->updateVideoParameter(
                            CAMERA_SIZE_QVGA_REVERSED_15, mUplinkGBR);
                }
                startVideoInput();
            } else {
                ALOGI("startPreview, start recording");
                camera_output_num = 0;
                mActFrameRate = 0;
                imsCameraStartRecording();
            }
            return;
        } else if(mLocalSurface == NULL){
            ALOGI("startPreview, waiting local surface ready");
        } else if(!mCameraOpen){
            ALOGI("startPreview, camera is not open");
        } else {
            ALOGI("startPreview, preview is ongoing");
        }
    }

    void VideoCallEngineClient::stopPreview()
    {
        if(mCameraPreviewStarted){
            ALOGI("stopPreview");
            if(mVQTest >= 0 || mHideLocalImage){
                ALOGI("stopPreview, stop local video input thread");
                stopVideoInput();
            } else {
                ALOGI("stopPreview, stop recording");
                imsCameraStopRecording();
            }
            imsCameraStopPreview();
            mCameraPreviewStarted = false;
        } else {
            ALOGI("stopPreview, no need");
        }
    }

    short VideoCallEngineClient::cameraSetPreviewSize(int width, int height) {
        ALOGI("cameraSetPreviewSize");
        Resolution res;
        res.width = width;
        res.height = height;
        CameraParams cp;
        cp.cameraResolution = res;
        CameraParamContainer cpc;
        cpc.type = SET_RESOLUTION;
        cpc.params = cp;
        return imsCameraSetParameter(cpc);
    }

    short VideoCallEngineClient::cameraSetFps(int fps) {
        ALOGI("cameraSetFps %d", fps);

        CameraParams cp;
        cp.fps = fps;
        CameraParamContainer cpc;
        cpc.type = SET_FPS;
        cpc.params = cp;
        imsCameraSetParameter(cpc);

        //currently, we using prop to set fps
        char fr_str[8] = {0};
        sprintf(fr_str, "%d", mVCEVideoParameter->mFps);
        short ret = property_set("persist.volte.cmr.fps", fr_str);
        ALOGI("cameraSetFps, property_set ret %d", ret);
        return ret;
    }

    void VideoCallEngineClient::init()
    {
        ALOGI("init");

        mCameraId = -1;
        mCameraPreviewStarted = false;
        mCameraOpen = false;
        mPreviewSurfaceSet = false;
        mDeviceOrientation = 0;
        mPreviousDeviceOrientation = 0;
        mUpdateDisplayOrientation = false;
        mRotate = 0;
        mFlip_h = 0;
        mRemoteRotate = 0;

        //add prop for video quality test
        mVQTest = -1;
        char vq_prop[PROPERTY_VALUE_MAX];
        int ret = property_get("volte.vq.test.enable", vq_prop, "false");
        if ((ret > 0) && strcmp(vq_prop, "false")){
            mVQTest = atoi(vq_prop);
        }
        ALOGI("init, ret = %d, vq_prop = %s, mVQTest = %d", ret, vq_prop, mVQTest);

        mHideLocalImage = false;
        mReplacedImage = NULL;
        mFp_local_input = NULL;
        mStopVideoInput = true;

        camera_output_num = 0;
        mCameraStartTimeUs = 0;

        //uplink qos/bitrate paras
        mUplinkGBR = 0;

        mRemoteSurface = NULL;
        mLocalSurface = NULL;
        mCamera = NULL;
        mCameraProxy = NULL;
        mStopSwitch = true;
        mStopDecode = true;
        mStopEncode = true;
        mStopCamera = true;
        mStopRecord = true;
        mStartUplink = false;
        mCameraSize = CAMERA_SIZE_VGA_REVERSED_30;
        if (mVQTest >= 0){
            //this para should match the size of yuv file using for vq test
            ALOGI("init, vq test enabled, set video size according to prop");
            mCameraSize = (VCE_CAMERA_SIZE)mVQTest;
        }
        mVCEVideoParameter->updateVideoParameter(mCameraSize, mUplinkGBR);

        if (mEnableLoopBack){
            //for video loop test
            ALOGI("init vce loopback");
            mVideoLoop = new VCEVideoLoop(true);
            mVCEVideoParameter->updateVideoParameter(VCE_CODEC_VIDEO_H264);
        }

        mCallEnd = false;
        pushFrame = 0;
        dropFrame = 0;
        cameraRelease = 0;
        encodeRelease = 0;
        sps_pps_saved = 0;
    }

    VideoCallEngineClient::VideoCallEngineClient()
        : mRemoteSurface(NULL),
        mStartRender(false),
        mLocalSurface(NULL),
        mCamera(NULL),
        mCameraProxy(NULL),
        mCameraStartTimeUs(0),
        sendingFirstFrame(true),
        mCameraId(-1),
        mCameraPreviewStarted(false),
        mCameraOpen(false),
        mPreviewSurfaceSet(false),
        mPreviousDeviceOrientation(0),
        mDeviceOrientation(0),
        mUpdateDisplayOrientation(false),
        mRotate(0),
        mFlip_h(0),
        mRemoteRotate(0),
        mPrevSampleTimeUs(0ll),
        mStopSwitch(true),
        mStopDecode(true),
        mStopEncode(true),
        mStopCamera(true),
        mHideLocalImage(false),
        mReplacedYUV(NULL),
        mReplacedImage(NULL),
        mVQTest(-1),
        mUplinkGBR(0),
        mStopRecord(true),
        mStopProcessEvent(true),
        mStartUplink(false),
        mCameraSize(CAMERA_SIZE_VGA_REVERSED_30),
        mRequestIDRFrame(false),
        mRequestIDRFrameNow(false),
        mCallEnd(false),
        mUpNetworkReady(false),
        mUplinkReady(false),
        mDownNetworkReady(false),
        mDownlinkReady(false),
        mEnableLoopBack(false),
        mVCInitCompleted(false)
    {
        ALOGI("Constructor in VideoCallEngineClient...");
        // Audio PCM data dump file
#if DUMP_AUDIO_AMR
        mFp_audio = fopen("/data/misc/media/audio.bs","wb");
#endif

#if DUMP_VIDEO_BS
        mFp_video = NULL;
        mFp_local_dump = NULL;
#endif
#if DUMP_VIDEO_YUV
        mFp_local_yuv = NULL;
#endif

#if DUMP_AUDIO_PCM
        mFp_pcm   = fopen("/data/misc/media/audio.pcm", "wb");
#endif

        mFp_local_input = NULL;

        mVideoLoop = NULL;

        mVideoFormat = new AMessage();
        mAudioFormat = new AMessage();
        mEncBrAdapt = new VideoEncBitrateAdaption();
        mVCEVideoParameter = new VCEVideoParameter();
    }

    VideoCallEngineClient::~VideoCallEngineClient()
    {
#if DUMP_AUDIO_AMR
        if (mFp_audio) {
            fclose(mFp_audio);
            mFp_audio = NULL;
        }
#endif

#if DUMP_AUDIO_PCM
        if (mFp_pcm) {
            fclose(mFp_pcm);
            mFp_pcm = NULL;
        }
#endif

#if DUMP_VIDEO_BS
        if (mFp_video)
        {
            fclose(mFp_video);
            mFp_video = NULL;
        }
        if (mFp_local_dump)
        {
            fclose(mFp_local_dump);
            mFp_local_dump = NULL;
        }
#endif
#if DUMP_VIDEO_YUV
        if (mFp_local_yuv)
        {
            fclose(mFp_local_yuv);
            mFp_local_yuv = NULL;
        }
#endif
        if (mVCEVideoParameter != NULL) {
            delete mVCEVideoParameter;
            mVCEVideoParameter = NULL;
        }
        //Release mAudioSource
        //delete mAudioSource;

        mNotifyCallback = NULL;
    }

    void VideoCallEngineClient::setUplinkQos(int qos){
        if (mUplinkGBR == qos){
            ALOGI("setUplinkQos, same as previous value %d", mUplinkGBR);
            return;
        }
        ALOGI("setUplinkQos, %d kbitps", qos);
        mVCEVideoParameter->updateVideoParameter(mCameraSize, qos * 1000);
        mUplinkGBR = qos * 1000;
        return;
    }

    void VideoCallEngineClient::startUplink()
    {
        ALOGI("startUplink");
        if(!mStartUplink)
        {
            mStartUplink = true;
        }
    }

    void VideoCallEngineClient::startDownlink()
    {
        mCallEnd=false;
        stopswitch2Background();
        mStartRender = true;
        if(mStopDecode)
        {
            ALOGI("startDownlink");
            mStopDecode = false;
            mDownNetworkReady = true;
            pthread_attr_t attr;
            pthread_attr_init(&attr);
            pthread_attr_setdetachstate(&attr, PTHREAD_CREATE_DETACHED);
            pthread_create(&mDecodeThread, &attr, DecodeThreadWrapper, this);
            pthread_attr_destroy(&attr);
        }
    }

    void VideoCallEngineClient::switch2Background()
    {
        if(mStopSwitch)
        {
            ALOGI("switch2Background");
            mStopSwitch=false;
            pthread_attr_t attr;
            pthread_attr_init(&attr);
            pthread_attr_setdetachstate(&attr, PTHREAD_CREATE_DETACHED);
            pthread_create(&mSwitch2Thread, &attr, Switch2ThreadWrapper, this);
            pthread_attr_destroy(&attr);
        }
    }

    void VideoCallEngineClient::stopUplink()
    {
       if(!mStopEncode)
        {
            ALOGI("stop encode thread");
            mStopEncode = true;
            ALOGI("mCameraOutputBufferCond.signal");
            mCameraOutputBufferCond.signal();
            uint64 time_out = 500000000; //nano seconds, 500ms
            int wait_err = mEncodeExitedCond.waitRelative(mEncodeExitedLock, time_out);
            if (wait_err == -ETIMEDOUT){
                ALOGE("stop encode thread time__out");
            }
            ALOGI("encode thread stopped");
        } else {
            ALOGI("encode thread has already been stopped");
        }
        stopCamera();
        mStartUplink = false;
        mUplinkReady = false;
    }

    void VideoCallEngineClient::stopCamera()
    {
        if(!mStopCamera)
        {
            ALOGI("stop camera thread");
            mStopCamera = true;
            int64_t timeout = 1000000000; //nano seconds, 1s
            status_t wait_err = mCameraExitedCond.waitRelative(mCameraExitedLock,timeout);
            ALOGI("stop camera thread wait_err = %d", wait_err);
            if (wait_err == -ETIMEDOUT){
                ALOGE("stop camera thread time__out");
            }
            ALOGI("stop camera thread end");
        } else {
            ALOGI("camera thread has already been stopped");
        }
    }

    void VideoCallEngineClient::stopDownlink()
    {
        ALOGI("stop downlink");
        if(!mStopDecode)
        {
            ALOGI("stop decode thread");
            mStopDecode = true;
            int64_t timeout = 1500000000; //nano seconds, 1500ms
            status_t wait_err = mDecodeExitedCond.waitRelative(mDecodeExitedLock, timeout);
            ALOGI("stop decode thread wait_err = %d", wait_err);
            if (wait_err == -ETIMEDOUT){
                ALOGE("stop decode thread time__out");
            }
            ALOGI("decode thread stopped");
        } else {
            ALOGI("decode thread has already been stopped");
        }
        mDownlinkReady = false;
        if (!mCallEnd)
        {
            switch2Background();
        }
    }

    void VideoCallEngineClient::stopswitch2Background()
    {
        ALOGI("mStopSwitch=%d",mStopSwitch);
        if(!mStopSwitch)
        {
            ALOGI("stop switch2Background");
            mStopSwitch=true;
            mSwitchExitedCond.wait(mSwitchExitedLock);
            if (!mCallEnd){
                ALOGI("swith to foreground, and ask for an I-frame with SPS/PPS");
                VCI_sendFIR();
            }
            ALOGI("switch2Background stopped");
        }
    }

    // Record remote video and Downlink&Uplink voice
    void VideoCallEngineClient::startRecord()
    {
        ALOGI("[Alan] startRecord...");
        status_t err = OK;
        startSysTime = 0;
        /*
         * O_CREAT: creat new file if file is not existing
         * O_TRUNC: clear the file content if file is existing
         * O_RDWR: read and write
         */
        int fd = open("/data/test.mp4",
                O_CREAT | O_LARGEFILE | O_TRUNC | O_RDWR, S_IRUSR | S_IWUSR);
        if (fd < 0) {
            ALOGI("ERROR: couldn't open file\n");
        }
        mMuxer = new MediaMuxer(fd, MediaMuxer::OUTPUT_FORMAT_MPEG_4);

        // Hard code csd-0 & csd-1
        // TODO: This is only for test purpose
        // We should extract sps/pps from codec-config-buffer
        sp<ABuffer> csd_0;
        sp<ABuffer> csd_1;

        mVideoFormat = new AMessage;
        mVideoFormat->setInt32("width", 720);
        mVideoFormat->setInt32("height", 480);
        mVideoFormat->setString("mime", "video/avc");
        mVideoFormat->setInt32("bitrate", 4000000);

        mVideoFormat->setFloat("frame-rate", 30);
        mVideoFormat->setInt32("color-format", 0x7FD00001);
        mVideoFormat->setInt32("i-frame-interval", 10);
        mVideoFormat->setInt32("store-metadata-in-buffers", 1);

        while ((mVideoFormat->findBuffer("csd-0", &csd_0) == false)
                || (mVideoFormat->findBuffer("csd-1", &csd_1) == false)) {
            ALOGI("[Alan] waitting for csd-0 & csd-1...");
            sleep(1);
        }

        ALOGI("[Alan] Got csd-0/csd-1...");
        hexdump(csd_0->data(), csd_0->size());
        hexdump(csd_1->data(), csd_1->size());

        csd_0 = new ABuffer(13);
        memcpy(csd_0->data(), "\x00\x00\x00\x01\x67\x42\xc0\x33\xe9\x01\x68\x7a\x20", 13);

        csd_1 = new ABuffer(8);
        memcpy(csd_1->data(), "\x00\x00\x00\x01\x68\xce\x3c\x80", 8);

        mVideoFormat->setBuffer("csd-0", csd_0);
        mVideoFormat->setBuffer("csd-1", csd_1);

        mAudioFormat = new AMessage;
        mAudioFormat->setString("mime", "audio/3gpp");
        mAudioFormat->setInt32("bitrate", 12200);
        mAudioFormat->setInt32("channel-count", 2);
        mAudioFormat->setInt32("sample-rate", 8000);
        mAudioFormat->setInt32("max-input-size", 8 * 1024);

        mVideoTrack = mMuxer->addTrack(mVideoFormat);
        mAudioTrack = mMuxer->addTrack(mAudioFormat);

        CHECK_GT(mVideoTrack, -1);
        CHECK_GT(mAudioTrack, -1);

        err = mMuxer->start();
        CHECK_EQ(err, status_t(OK));

        mStopRecord = false;

        pthread_attr_t attr;
        pthread_attr_init(&attr);
        pthread_attr_setdetachstate(&attr, PTHREAD_CREATE_DETACHED);
        pthread_create(&mRecordThread, &attr, AudioRecordThreadWrapper, this);
        pthread_attr_destroy(&attr);
    }

    void* VideoCallEngineClient::AudioRecordThreadWrapper(void* me)
    {
        return (void*)static_cast<VideoCallEngineClient*>(me)->AudioRecordThreadFunc();
    }

    // In this thread, we encode audio pcm data to AMR format then mux with video data
    status_t VideoCallEngineClient::AudioRecordThreadFunc()
    {
        ALOGI("[Alan] In RecordThreadFunc...");
        status_t err = OK;

        List<size_t> availPCMInputIndices;
        ProcessState::self()->startThreadPool();

        CodecState state;

        sp<ALooper> looper = new ALooper;
        looper->start();

        state.mCodec = MediaCodec::CreateByType(looper, "audio/3gpp", true);
        CHECK(state.mCodec != NULL);

        sp<MediaCodec> codec = state.mCodec;

        int sampleRate = 8000;

        sp<AMessage> audioFormat = new AMessage;
        audioFormat->setString("mime", "audio/3gpp");
        audioFormat->setInt32("bitrate", 12200);
        audioFormat->setInt32("channel-count", 2);
        audioFormat->setInt32("sample-rate", sampleRate);
        audioFormat->setInt32("max-input-size", 8 * 1024);

        err = codec->configure(audioFormat, NULL, NULL, MediaCodec::CONFIGURE_FLAG_ENCODE);
        CHECK_EQ(err, (status_t)OK);

        CHECK_EQ((status_t)OK, codec->start());
        CHECK_EQ((status_t)OK, codec->getInputBuffers(&state.mInBuffers));
        CHECK_EQ((status_t)OK, codec->getOutputBuffers(&state.mOutBuffers));

        ALOGI("[Alan] Encode got %d input and %d output buffers", state.mInBuffers.size(), state.mOutBuffers.size());

        while(!availPCMInputIndices.empty()) {
            availPCMInputIndices.erase(availPCMInputIndices.begin());
        }

        while (!mStopRecord) {

            while(!mStopRecord) {

                size_t bufferIndex;
                err = codec->dequeueInputBuffer(&bufferIndex);

                if (err != OK) break;

                availPCMInputIndices.push_back(bufferIndex);
            }

            while (!availPCMInputIndices.empty() && !mStopRecord) {

                size_t index = *availPCMInputIndices.begin();
                availPCMInputIndices.erase(availPCMInputIndices.begin());

                MediaBuffer* mediaBuffer = NULL;
                {
                    Mutex::Autolock autoLock(mRecordedAudioBufferLock);
                    while(mRecordedPCMBuffers.empty() && !mStopEncode)
                    {
                        mRecordedAudioBufferCond.wait(mRecordedAudioBufferLock);
                    }
                    mediaBuffer = *mRecordedPCMBuffers.begin();
                    mRecordedPCMBuffers.erase(mRecordedPCMBuffers.begin());
                }

                const sp<ABuffer> &buffer = state.mInBuffers.itemAt(index);
                ALOGI("[Alan] mediaBuffer->data() = %p, size = %zd", mediaBuffer->data(), mediaBuffer->size());
                memcpy(buffer->data(), mediaBuffer->data(), mediaBuffer->size());

#if DUMP_AUDIO_PCM
                // Dump Audio Data(PCM)
                fwrite(buffer->data(), 1, mediaBuffer->size(), mFp_pcm);
#endif
                int64_t size = mediaBuffer->size();
                uint32_t bufferFlags = 0;
                int64_t timestampUs = mPrevSampleTimeUs + ((1000000 * size / 2) + (sampleRate >> 1)) / sampleRate;
                err = codec->queueInputBuffer(index, 0, size, mPrevSampleTimeUs, bufferFlags);
                mPrevSampleTimeUs = timestampUs;

                if (err != OK) break;

            }

            while(!mStopRecord) {

                // dequeueOutputBuffer(Audio AMR) and mux it
                BufferInfo info;
                status_t err = codec->dequeueOutputBuffer(&info.mIndex, &info.mOffset, &info.mSize, &info.mPresentationTimeUs, &info.mFlags);

                if (err == OK) {
                    const sp<ABuffer> &buffer = state.mOutBuffers.itemAt(info.mIndex);

#if DUMP_AUDIO_AMR
                    ALOGI("[Alan] dump ecoded audio size = %zd", info.mSize);
                    // Dump Encoded Audio buffer(AMR)
                    fwrite(buffer->data(), 1, info.mSize, mFp_audio);
#endif

                    err = mMuxer->writeSampleData(buffer, mAudioTrack, info.mPresentationTimeUs, info.mFlags);

                    err = codec->releaseOutputBuffer(info.mIndex);
                    CHECK_EQ(err, (status_t)OK);

                } else {
                    if (err == INFO_FORMAT_CHANGED) {
                        ALOGI("[Alan] Got format INFO_FORMAT_CHANGED");
                        //codec->getOutputFormat(&mAudioFormat);
                        continue;
                    } else if (err == INFO_OUTPUT_BUFFERS_CHANGED) {
                        CHECK_EQ((status_t)OK, codec->getOutputBuffers(&state.mOutBuffers));
                        continue;
                    }
                    if (err == -EAGAIN) {
                        err = OK;
                    }
                    break;
                }
            }

        }

        codec->stop();
        codec->release();
        looper->stop();

        // Record stopped release mediabuffers
        while(!mRecordedPCMBuffers.empty() && mStopRecord) {
            ALOGI("[Alan] clean mRecordedBuffers...");
            MediaBuffer* mediaBuffer = *mRecordedPCMBuffers.begin();
            mediaBuffer->release();
            mRecordedPCMBuffers.erase(mRecordedPCMBuffers.begin());

            ALOGI("[Alan] muxer Stopped!");
            //mMuxer->stop();
        }

        return OK;
    }

    void VideoCallEngineClient::setNotifyCallback(const sp<IVideoCallEngineCallback>& callback)
    {
        mNotifyCallback = callback;
    }

    void VideoCallEngineClient::startProcessEvent(int loopback)
    {
        mEnableLoopBack = (1 == loopback? true:false);
        property_get("persist.volte.video.dump", value_dump, default_value);
        video_dump_enable = !strcmp(value_dump, "true");
        ALOGI("startProcessEvent, video_dump_enable = %d", video_dump_enable);
        init();
        if(mStopProcessEvent)
        {
            ALOGI("startProcessEvent");
            mStopProcessEvent = false;
            pthread_attr_t attr;
            pthread_attr_init(&attr);
            pthread_attr_setdetachstate(&attr, PTHREAD_CREATE_DETACHED);
            pthread_create(&mProcessEventThread, &attr, ProcessEventThreadWrapper, this);
            pthread_attr_destroy(&attr);
            mInitCompleteCond.wait(mInitCompleteCondLock);
            ALOGI("startProcessEvent completed");
        }
        if(mEnableLoopBack){
            mRequestIDRFrame = true;
            mRequestIDRFrameNow = true;
            mUpNetworkReady = true;
            mDownNetworkReady = true;
        }

    }

    void VideoCallEngineClient::stopProcessEvent()
    {
        if(!mStopProcessEvent)
        {
            mCallEnd=true;
            ALOGI("stopProcessEvent");

            /* stop render to avoid codec being blocked
             * when remote surface is unavailable */
            mStartRender = false;

            mNotifyCallback = 0;

            releaseCamera();
            if(mVQTest >= 0 || mHideLocalImage){
                stopVideoInput();
            }
            stopEncodeThread();

            //reset uplink qos/bitrate paras
            mUplinkGBR = 0;

            stopUplink();
            stopDownlink();
            stopswitch2Background();
            mCamera = NULL;
            mCameraProxy = NULL;
            mLocalSurface = NULL;
            mRemoteSurface = NULL;
            mDownNetworkReady = false;
            mUpNetworkReady = false;
            video_dump_enable = false;
#if DUMP_VIDEO_BS
            if (mFp_video)
            {
                ALOGI("video dump remote end");
                fclose(mFp_video);
                mFp_video = NULL;
            }
            if (mFp_local_dump)
            {
                ALOGI("video dump local end");
                fclose(mFp_local_dump);
                mFp_local_dump = NULL;
            }
#endif
#if DUMP_VIDEO_YUV
            if (mFp_local_yuv)
            {
                ALOGI("video dump local yuv end");
                fclose(mFp_local_yuv);
                mFp_local_yuv = NULL;
            }
#endif
            //clear mark to save sps/pps, keep sps/pps
            sps_pps_saved = 0;

            //clear hide local imsge setting
            mHideLocalImage = false;
            if(mReplacedImage != NULL){
                delete []mReplacedImage;
                mReplacedImage = NULL;
            }

            if (mEnableLoopBack && mVideoLoop != NULL){
                delete mVideoLoop;
                mVideoLoop = NULL;
                mEnableLoopBack = false;
            }

            ALOGI("ProcessEvent stopped");
        }
    }

    void* VideoCallEngineClient::ProcessEventThreadWrapper(void* me)
    {
        return (void*)static_cast<VideoCallEngineClient*>(me)->ProcessEventThreadFunc();
    }

    status_t VideoCallEngineClient::ProcessEventThreadFunc()
    {
        int setname_ret = pthread_setname_np(pthread_self(), "vce_evt");
        if (0 != setname_ret){
            ALOGE("set name for event process thread failed, ret = %d", setname_ret);
        }
        vint        ret;
        VC_Event    event;
        char        eventDesc[VCI_EVENT_DESC_STRING_SZ];
        vint        codecType = -1;

        VIER_init();
        VCI_init();

        /* Currently, we only support H264 in Android.
         * "codecType" is not used.
         */
        while(!mStopProcessEvent)
        {
            ret = VCI_getEvent(&event, eventDesc, &codecType, -1);
            if(ret == 1 && !mStopProcessEvent)
            {
                switch(event)
                {
                    case VC_EVENT_NONE:
                        //ALOGI("VC_EVENT_NONE");
                        break;

                    case VC_EVENT_INIT_COMPLETE  :
                        ALOGI("VC_EVENT_INIT_COMPLETE");
                        mInitCompleteCond.signal();
                        mVCInitCompleted = true;
                        // Start Recording threads, for test purpose only
#if RECORD_TEST
                        startAudioCapture();
                        startRecord();
#endif
                        break;

                    case VC_EVENT_START_ENC   :
                        ALOGI("VC_EVENT_START_ENC");
                        mRequestIDRFrame = true;
                        mRequestIDRFrameNow = true;
                        mUpNetworkReady = true;
                        break;

                    case VC_EVENT_START_DEC   :
                        ALOGI("VC_EVENT_START_DEC");
                        mDownNetworkReady = true;
                        break;

                    case VC_EVENT_STOP_ENC  :
                        mUpNetworkReady = false;
                        sendingFirstFrame = true;
                        cleanCameraOutputBuffer();
                        ALOGI("VC_EVENT_STOP_ENC");
                        break;

                    case VC_EVENT_STOP_DEC   :
                        mDownNetworkReady = false;
                        ALOGI("VC_EVENT_STOP_DEC");
                        break;

                    case VC_EVENT_SHUTDOWN   :
                        ALOGI("VC_EVENT_SHUTDOWN");
                        //stopUplink();
                        //stopDownlink();
                        //mStopProcessEvent = true;
                        break;

                    case VC_EVENT_SEND_KEY_FRAME:
                        ALOGI("VC_EVENT_SEND_KEY_FRAME");
                        mRequestIDRFrameNow = true;
                        break;

                    case VC_EVENT_REMOTE_RECV_BW_KBPS:
                        mEncBrAdapt->setTargetBitrate(atoi(eventDesc) * 1100);
                        ALOGI("VC_EVENT_REMOTE_RECV_BW_KBPS, mTargetBitRate=%d->%d bps",
                                atoi(eventDesc) * 1000, mEncBrAdapt->getTargetBitrate());
                        break;
                    case VC_EVENT_NO_RTP:
                        ALOGI("VC_EVENT_NO_RTP, for %s us", eventDesc);
                        break;
                    case VC_EVENT_PKT_LOSS_RATE:
                        ALOGI("VC_EVENT_PKT_LOSS_RATE, %s\%", eventDesc);
                        break;
                    default:
                        ALOGE("Receive a invalid EVENT");
                        break;
                }
                if(!mUplinkReady && VC_EVENT_START_ENC == event)
                {
                    ALOGI("should be waiting uplink ready");
                }
                else if (!mDownlinkReady && VC_EVENT_START_DEC == event)
                {
                    ALOGI("should be waiting downlink ready");
                }
                else if (event != VC_EVENT_NONE)
                {
                    notifyCallback(event);
                }

#if ENABLE_VIDEO_LOOP
                //set code type for loop back mode
                codecType = 0;

                VCE_CODEC_TYPE type = (VCE_CODEC_TYPE)codecType;
                if (codecType != -1 && type != mVCEVideoParameter->codecType
                            && (VC_EVENT_NONE != event)){
                    ALOGI("codecType changed %d -> %d, event %d", mVCEVideoParameter->codecType,
                            type, event);
                    switch(type){
                        case VCE_CODEC_VIDEO_H265:
                        case VCE_CODEC_VIDEO_H264:
                        case VCE_CODEC_VIDEO_H263:
                            notifyCallback(VC_EVENT_CODEC_TYPE_CHENGED,
                                    codecType, 0, 0);
                            break;
                        default:
                            ALOGE("unknown codecType %d", codecType);
                            break;
                    }
                }
#endif
            }
            else
            {
                ALOGI("invalide VC_EVENT");
            }
        }
        VCI_shutdown();
        VIER_shutdown();
        ALOGI("stopping process event");
        return OK;
    }

    void VideoCallEngineClient::hideLocalImage(bool enable, const char* path){
        ALOGI("hideLocalImage, enable = %d, path = %s", enable, path);
        if (mHideLocalImage && enable){
            ALOGI("hideLocalImage, local inmage is hiden, return");
            return;
        }
        //clear previous path
        if (mReplacedImage != NULL) {
            delete []mReplacedImage;
            mReplacedImage = NULL;
        }
        if (enable) {
            {//---stop camera callback and encode thread
                imsCameraStopRecording();
                if(!mStopCamera){
                    //stop camera thread is needed
                    stopCamera();
                }
                /*
                 * currently we need to reset encode thread;
                 * if "mediabuffer" need to be released is different between
                 * camera callback frames and locainput frames
                */
                stopEncodeThread();
            }
            {//---update paras, start local video input and encode thread
                /* set this mark be true, using to change encode thread config:
                 *     -- mediabuffer.release is needed after frame being encoded
                 *     -- color format should be OMX_COLOR_FormatYUV420Planar
                 *     -- metadata is not stored in frame buffer
                 */
                mHideLocalImage = true;

                /* currently, only QVGA_15 is supported for replaced picture
                 * update para to config input frame size to encode correctly
                 */
                mVCEVideoParameter->updateVideoParameter(CAMERA_SIZE_QVGA_REVERSED_15, mUplinkGBR);

                //get the path of raw image
                mReplacedImage = new char[strlen(path)];
                strcpy(mReplacedImage, path);
                ALOGI("hideLocalImage, show replaced image, mReplacedImage = %s", mReplacedImage);

                //convert rgb to yuv
                mReplacedYUV = "data/misc/media/replaceImage.yuv";
                ProcessRawImage(mReplacedImage, mVCEVideoParameter->mWidth,
                        mVCEVideoParameter->mHeight, mReplacedYUV);

                //start reading and encoding the replaced yuv image
                startVideoInput();
                setCVOInfo(mDeviceOrientation);
                startEncodeThread();
            }
        } else {
            ALOGI("hideLocalImage, stop loacal video input");
            {//stop local video input thread
                stopVideoInput();
                stopEncodeThread();
                /* mHideLocalImage should be set to false after encode thread stopped
                 * as we are using this flag in encode thread to determine
                 * if media buffer should be released in encode thread
                 */
                mHideLocalImage = false;
            }
            if (!mCallEnd) {//start recording if call is active
                ALOGI("hideLocalImage, show local image");
                //recover the previous setting for camera size
                mVCEVideoParameter->updateVideoParameter(mCameraSize, mUplinkGBR);
                short ret = imsCameraStartRecording();
                if (ret != 0){
                    ALOGE("hideLocalImage, start recording failed, try camera thread");
                    startCamera();
                }
                setCVOInfo(mDeviceOrientation);
                startEncodeThread();
            }
        }
    }

    void VideoCallEngineClient::sendSpsPps(void *frame_to_send){
        VC_EncodedFrame *frame = (VC_EncodedFrame *)frame_to_send;
        ALOGI("send length%s sps pps %d, tsMs %lld, mFlags %d",
                (sendingFirstFrame? " first":""),
                frame->length, frame->tsMs, frame->flags);
#if DUMP_VIDEO_BS
        if(video_dump_enable && mFp_local_dump)
        {
            fwrite(frame->data_ptr, 1, frame->length, mFp_local_dump);
        }
#endif
        mEncBrAdapt->pushFrameInfo(frame->length);
        if (mEnableLoopBack) {
            mVideoLoop->queueLoopFrame((EncodedFrame*)frame);
        } else {
            VCI_sendEncodedFrame(frame);
        }
    }

    void VideoCallEngineClient::sendFrame(void *frame_to_send,
            sp<ABuffer> buffer, uint32_t ts, uint32_t flag, int64_t *frame_send){
        VC_EncodedFrame *frame = (VC_EncodedFrame *)frame_to_send;
        uint8_t* data = (uint8_t*)malloc(buffer->size());
        memcpy(data, buffer->data(), buffer->size());
        frame->data_ptr = data;
        frame->tsMs = ts;
        frame->length = buffer->size();
        frame->flags = flag;
        frame->rcsRtpExtnPayload = (frame->flags == 1)? (mRotate | mFlip_h):0;
        ALOGI("send length%s %d, tsMs %lld, mFlags %d, cvo %d, frameSend %lld",
                (sendingFirstFrame? " first frame":""), frame->length,
                frame->tsMs, frame->flags, frame->rcsRtpExtnPayload, ++(*frame_send));
#if DUMP_VIDEO_BS
        if(video_dump_enable && mFp_local_dump)
        {
            fwrite(frame->data_ptr, 1, frame->length, mFp_local_dump);
        }
#endif
        mEncBrAdapt->pushFrameInfo(frame->length);
        if (mEnableLoopBack) {
            mVideoLoop->queueLoopFrame((EncodedFrame*)frame);
        } else {
            VCI_sendEncodedFrame(frame);
        }
    }

    void VideoCallEngineClient::getFrame(void* frame){
        if (mEnableLoopBack){
            mVideoLoop->dequeueLoopFrame((EncodedFrame *)frame);
        } else {
            VCI_getEncodedFrame((VC_EncodedFrame *)frame);
        }
    }

    void VideoCallEngineClient::setVideoCodecType(VCE_CODEC_TYPE type){
        ALOGI("setVideoCodecType, %d", type);
        if (type == mVCEVideoParameter->codecType){
            ALOGI("setVideoCodecType X, no change");
            return;
        }

        bool needRestartEncode = !mStopEncode;
        bool needRestartDecode = !mStopDecode;

        if (needRestartEncode){
            ALOGI("setVideoCodecType, stopEncode");
            stopEncodeThread();
        }
        if (needRestartDecode) {
            ALOGI("setVideoCodecType, stopDecode");
            stopDownlink();
        }

        /* update with new codec type */
        mVCEVideoParameter->updateVideoParameter(type);

        /* restart encode/decode thread if needed*/
        if (needRestartEncode){
            ALOGI("setVideoCodecType, startEncode");
            startEncodeThread();
        }
        if (needRestartDecode){
            ALOGI("setVideoCodecType, startDecode");
            startDownlink();
        }
    }

    int VideoCallEngineClient::notifyCallback(int event){
        if (mNotifyCallback == NULL){
            ALOGI("notifyCallback null");
            return -1;
        }
        if (!mVCInitCompleted){
            ALOGI("mVCInitCompleted = %d", mVCInitCompleted);
            return -1;
        }
        return mNotifyCallback->notifyCallback(event);
    }
    int VideoCallEngineClient::notifyCallback(int event,
            int ext1, int ext2, int ext3){
        if (mNotifyCallback == NULL){
            ALOGI("notifyCallback null");
            return -1;
        }
        if (!mVCInitCompleted){
            ALOGI("mVCInitCompleted = %d", mVCInitCompleted);
            return -1;
        }
        return mNotifyCallback->notifyCallback(event,
                ext1, ext2, ext3);
    }

    int BpVideoCallEngineCallback::notifyCallback(int event)
    {
        ALOGI("BpVideoCallEngineCallback notifyCallback, %d", event);
        Parcel data,reply;
        data.writeInt32(event);
        remote()->transact(VCE_ACTION_NOTIFY_CALLBACK, data, &reply);
        return reply.readInt32();
    }

    int BpVideoCallEngineCallback::notifyCallback(int event, int ext1, int ext2,  int ext3)
    {
        ALOGI("BpVideoCallEngineCallback notifyCallback with data, %d, %d, %d, %d",
                event, ext1, ext2, ext3);
        Parcel data,reply;
        data.writeInt32(event);
        data.writeInt32(ext1);
        data.writeInt32(ext2);
        data.writeInt32(ext3);
        remote()->transact(VCE_ACTION_NOTIFY_CALLBACK_WITH_DATA, data, &reply);
        return reply.readInt32();
    }

    /* unused in video call engine client*/
    status_t BnVideoCallEngineCallback::onTransact(uint32_t code, const Parcel& data, Parcel* reply, uint32_t flags)
    {
        switch (code)
        {
            case VCE_ACTION_NOTIFY_CALLBACK:
                {
                    ALOGI("BnVideoCallEngineCallback onTransact notifyCallback");
                    reply->writeInt32(notifyCallback((int)data.readInt32()));
                    return NO_ERROR;
                }
                break;

            case VCE_ACTION_NOTIFY_CALLBACK_WITH_DATA:
                {
                    ALOGI("BnVideoCallEngineCallback onTransact notifyCallback");
                    reply->writeInt32(notifyCallback( (int)data.readInt32(), (int)data.readInt32(),
                        (int)data.readInt32(), (int)data.readInt32()));
                    return NO_ERROR;
                }
                break;

            default:
                return BBinder::onTransact(code, data, reply, flags);
        }
    }

    /* unused in video call engine client*/
    int VideoCallEngineCallback::notifyCallback(int event)
    {
        ALOGI("VideoCallEngineCallback notifyCallback");
        if(mListener != NULL)
        {
            mListener->notify(event, 0, 0, NULL);
        }
        else
        {
            ALOGI("listener has not been setup");
        }
        return 0;
    }

    /* unused in video call engine client*/
    int VideoCallEngineCallback::notifyCallback(int event, int ext1, int ext2,  int ext3)
    {
        ALOGI("VideoCallEngineCallback notifyCallback with data");
        if(mListener != NULL) {
            Parcel data;
            data.writeInt32(ext1);
            data.writeInt32(ext2);
            data.writeInt32(ext3);
            mListener->notify(event, 0, 0, &data);
        } else {
            ALOGI("notifyCallback with data, listener has not been setup");
        }
        return 0;
    }

    /* unused in video call engine client*/
    status_t VideoCallEngineCallback::onTransact(uint32_t code, const Parcel& data, Parcel* reply, uint32_t flags)
    {
        ALOGI("VideoCallEngineCallback onTransact notifyCallback");
        return BnVideoCallEngineCallback::onTransact(code, data, reply, flags);
    }

    /* unused in video call engine client*/
    void VideoCallEngineCallback::setListener(const sp<VideoCallEngineListener>& listner)
    {
        ALOGI("VideoCallEngineCallback setListener");
        mListener = listner;
    }
}

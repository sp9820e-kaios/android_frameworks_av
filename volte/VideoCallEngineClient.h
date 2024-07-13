#ifndef ANDROID_VCECLIENT_H
#define ANDROID_VCECLIENT_H

#include <gui/ISurfaceComposer.h>
#include <gui/SurfaceComposerClient.h>
#include <gui/Surface.h>

#include <media/stagefright/foundation/AMessage.h>
#include <media/stagefright/foundation/AHandler.h>
//#include <media/stagefright/NativeWindowWrapper.h>
#include <media/stagefright/MediaMuxer.h>
#include <media/stagefright/AudioSource.h>
#include <media/stagefright/MediaCodec.h>

#include <camera/Camera.h>
#include <camera/ICamera.h>
#include <camera/CameraParameters.h>

#include <utils/Vector.h>
#include <utils/RefBase.h>

#include <binder/IInterface.h>
#include <binder/Parcel.h>
#include <binder/BinderService.h>

#include <system/audio.h>
#include <queue>

#define DUMP_VIDEO_BS 1
#define DUMP_VIDEO_YUV 0
#define DUMP_AUDIO_PCM 0
#define DUMP_AUDIO_AMR 0



namespace android
{
    // Surface derives from ANativeWindow which derives from multiple
    // base classes, in order to carry it in AMessages, we'll temporarily wrap it
    // into a NativeWindowWrapper.
    struct NativeWindowWrapper : RefBase {
        NativeWindowWrapper(
                const sp<Surface> &surfaceTextureClient) :
            mSurfaceTextureClient(surfaceTextureClient) { }

        sp<ANativeWindow> getNativeWindow() const {
            return mSurfaceTextureClient;
        }

        sp<Surface> getSurfaceTextureClient() const {
            return mSurfaceTextureClient;
        }

    private:
        const sp<Surface> mSurfaceTextureClient;

        DISALLOW_EVIL_CONSTRUCTORS(NativeWindowWrapper);
    };

    struct CodecState;
    struct BufferInfo;
    struct ABuffer;
    class MediaBuffer;

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

    IMPLEMENT_META_INTERFACE(VideoCallEngineCallback, "android.videocallengine.callback");

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

    enum VCE_SERVICE_TYPE{
        VCE_SERVICE_DOWNLINK,
        VCE_SERVICE_UPLINK
    };

    enum VCE_CAMERA_SIZE{
        CAMERA_SIZE_720P = 0,                //1280*720 Frame rate:30
        CAMERA_SIZE_VGA_REVERSED_15 = 1,     //480*640 Frame rate:15
        CAMERA_SIZE_VGA_REVERSED_30 = 2,     //480*640 Frame rate:30
        CAMERA_SIZE_QVGA_REVERSED_15 = 3,    //240*320 Frame rate:15
        CAMERA_SIZE_QVGA_REVERSED_30 = 4,    //240*320 Frame rate:30
        CAMERA_SIZE_CIF = 5,                 //352*288 Frame rate:30
        CAMERA_SIZE_QCIF = 6,                //176*144 Frame rate:30
        CAMERA_SIZE_VGA_15 = 7,              //640*480 Frame rate:15
        CAMERA_SIZE_VGA_30 = 8,              //640*480 Frame rate:30
        CAMERA_SIZE_QVGA_15 = 9,             //320*240 Frame rate:15
        CAMERA_SIZE_QVGA_30 = 10,            //320*240 Frame rate:30
    };

    enum VCE_CODEC_TYPE{
        VCE_CODEC_VIDEO_H264 = 0,
        VCE_CODEC_VIDEO_H263 = 1,
        VCE_CODEC_VIDEO_H265 = 2,
    };

    typedef struct {
        uint8_t        *data_ptr;          /* data buffer */
        int          length;            /* length of data */
        int64_t        tsMs;              /* timestamp in milli-second */
        int          flags;             /* SPS/PPS flags */
        int8_t         rcsRtpExtnPayload; /* VCO - payload */
    } EncodedFrame;


    class VCEVideoLoop{
    public:
        VCEVideoLoop(bool enable){
            ALOGI("VCEVideoLoop %d", enable);
            _enabled = enable;
            _frame_in_list = 0;
            _frame_processed = 0;
        }
        ~VCEVideoLoop(){
            _enabled = false;
            if(!isEmpty()){
                cleanFrameList();
            }
            if (_frame_in_list != 0){
                ALOGE("VCEVideoLoop frame list is not clean!");
                _frame_in_list = 0;
                _frame_processed = 0;
            }
        }
        bool isLoopBackEnabled(){
            return _enabled;
        }

        bool queueLoopFrame(EncodedFrame *frame_ptr){
            _lock.lock();
            EncodedFrame new_frame = {NULL, 0, 0, 0, 0};
            deepCopyFrame(&new_frame, frame_ptr);
            mLoopFrames.push_back(new_frame);
            _frame_in_list++;
            _frame_processed++;
            ALOGI("VCEVideoLoop, queue in_list %d, processed %d, length %d, data_ptr %p",
                    _frame_in_list, _frame_processed, new_frame.length, new_frame.data_ptr);
            _lock.unlock();
            return true;
        }
        int dequeueLoopFrame(EncodedFrame *frame_ptr){
            if (!isEmpty()) {
                _lock.lock();
                EncodedFrame frame = *mLoopFrames.begin();
                deepCopyFrame(frame_ptr, &frame);
                frame_ptr->tsMs = (frame_ptr->tsMs) * 90;
                free(frame.data_ptr);
                mLoopFrames.erase(mLoopFrames.begin());
                _frame_in_list--;
                _lock.unlock();
                ALOGI("VCEVideoLoop, dequeue in_list %d, processed %d, length %d, data_ptr %p",
                        _frame_in_list, _frame_processed, frame_ptr->length, frame_ptr->data_ptr);
                return 1;
            }
            return -1;
        }

    private:
        bool deepCopyFrame(EncodedFrame *dst, EncodedFrame *src){
            int len = src->length;
            (*dst).length = src->length;
            dst->data_ptr = (uint8_t*)malloc(len);
            memcpy(dst->data_ptr, src->data_ptr, len);
            dst->tsMs = src->tsMs;
            dst->flags = src->flags;
            dst->rcsRtpExtnPayload = src->rcsRtpExtnPayload;
            return true;
        }
        bool cleanFrameList(){
            while (!isEmpty()){
                _lock.lock();
                EncodedFrame frame = *mLoopFrames.begin();
                free(frame.data_ptr);
                mLoopFrames.erase(mLoopFrames.begin());
                _lock.unlock();
            }
            return true;
        }
        bool isEmpty(){
            _lock.lock();
            bool result = mLoopFrames.empty();
            _lock.unlock();
            return result;
        }
        bool _enabled = false;
        List<EncodedFrame>  mLoopFrames;
        Mutex _lock;
        int _frame_in_list = 0;
        int _frame_processed = 0;
    };

    class VCEVideoParameter{
    public:
        VCEVideoParameter( ){
            mWidth = 480;
            mHeight = 640;
            mWidthStride = mWidth;
            mFps = 30;
            mCurBitrate = 600000;
            mMaxBitrate = 980000;
            mMinBitrate = 480000;
            codecType = VCE_CODEC_VIDEO_H264;
        };

        void updateVideoParameter(VCE_CAMERA_SIZE camera_size){
            ALOGI("updateVideoParameter, camera_size =%d", camera_size);
            int index = (int32_t)camera_size;
            mWidth = videoParaMap[index][0];
            mWidthStride = mWidth;
            mHeight = videoParaMap[index][1];
            mFps = videoParaMap[index][2];
            mCurBitrate = bitRateMap[index][0];
            mMaxBitrate = bitRateMap[index][1];
            mMinBitrate = bitRateMap[index][2];
            ALOGI("updateVideoParameter, size %d x %d, fps = %d, mCurBitrate = %d, mMaxBitrate = %d, mMinBitrate = %d",
                    mWidth, mHeight, mFps, mCurBitrate, mMaxBitrate, mMinBitrate);
        }

        void updateVideoParameter(VCE_CAMERA_SIZE camera_size, int32_t qos_bitrate){
            ALOGI("updateVideoParameter, camera_size =%d, qos_bitrate = %d", camera_size, qos_bitrate);
            int index = int32_t(camera_size);
            mWidth = videoParaMap[index][0];
            mWidthStride = mWidth;
            mHeight = videoParaMap[index][1];
            mFps = videoParaMap[index][2];
            if (qos_bitrate > 0){
                /* current bitrate should be the bitrate of sending stream (target bitrate)
                 * no need set to VideoEncBitrateAdaption, which
                 * will calucate the proper bitrate of encoder and fps of encoder input video */
                mCurBitrate = bitRateMap[index][0];
                /* no need set to VideoEncBitrateAdaption */
                mMaxBitrate = bitRateMap[index][1];
                /* for pre-set video resolution and fps the min bitrate is determined */
                mMinBitrate = bitRateMap[index][2];
            } else {
                mCurBitrate = bitRateMap[index][0];
                mMaxBitrate = bitRateMap[index][1];
                mMinBitrate = bitRateMap[index][2];
            }
            ALOGI("updateVideoParameter, size %d x %d, fps = %d, mCurBitrate = %d, mMaxBitrate = %d, mMinBitrate = %d",
                    mWidth, mHeight, mFps, mCurBitrate, mMaxBitrate, mMinBitrate);
        }

        void updateVideoParameter(VCE_CODEC_TYPE type){
            ALOGI("updateVideoParameter, codecType =%d", type);
            codecType = type;
        }

        int32_t mCurBitrate;
        int32_t mMaxBitrate;
        int32_t mMinBitrate;
        int32_t mWidth;
        int32_t mHeight;
        int32_t mWidthStride;
        int32_t mFps;

        VCE_CODEC_TYPE codecType = VCE_CODEC_VIDEO_H264;

    private:
        /* bitrate para : current max min bitrate (bitps)*/
        int32_t bitRateMap[11][3] = {{3000000, 4000000, 1200000},   //1280*720 Frame rate:30
                                 {400000, 660000, 400000},    //480*640 Frame rate:15
                                 {600000, 980000, 480000},    //480*640 Frame rate:30
                                 {256000, 372000, 128000},    //240*320 Frame rate:15
                                 {384000, 512000, 200000},    //240*320 Frame rate:30
                                 {400000, 500000, 200000},    //352*288 Frame rate:30
                                 {100000, 300000, 100000},    //176*144 Frame rate:30
                                 {400000, 660000, 400000},    //640*480 Frame rate:15
                                 {600000, 980000, 480000},    //640*480 Frame rate:30
                                 {256000, 372000, 128000},    //320*240 Frame rate:15
                                 {384000, 512000, 200000}     //320*240 Frame rate:30
                                };
        /* video paras : width, height, fps */
        int32_t videoParaMap[11][3] = {{1280, 720, 30},       //1280*720 Frame rate:30
                                   {480, 640, 15},      //480*640 Frame rate:15
                                   {480, 640, 30},      //480*640 Frame rate:30
                                   {240, 320, 15},      //240*320 Frame rate:15
                                   {240, 320, 30},      //240*320 Frame rate:30
                                   {352, 288, 30},      //352*288 Frame rate:30
                                   {176, 144, 30},      //176*144 Frame rate:30
                                   {640, 480, 15},      //640*480 Frame rate:15
                                   {640, 480, 30},      //640*480 Frame rate:30
                                   {320, 240, 15},      //320*240 Frame rate:15
                                   {320, 240, 30}      //320*240 Frame rate:30
                                  };
    };

    class VideoEncBitrateAdaption : public RefBase{
    public:
        struct Config {
            int curBr;
            int maxBr;
            int minBr;
        };

        VideoEncBitrateAdaption();
       ~VideoEncBitrateAdaption();
        void setConfig(const Config &cfg);
        void enableFrameDrop();
        void disableFrameDrop();
        void cleanFrames();
        bool isEncodable();
        void pushFrameInfo(int length);
        void setTargetBitrate(int tBr);
        void updateMaxBitrate(int mBr);
        int getTargetBitrate();
        bool isReadyToUpdateCodec();
    private:
        enum {kFrameQueueLimit = 512};
        class frameInfo: public RefBase {
        public:
            frameInfo(int64_t time=0, int length=0)
            : ts(time),
            size(length){};
            int64_t getTs() { return ts; };
            int getSize() { return size; };
            ~frameInfo() { ALOGI("frameInfo Decons");};
        private:
            int64_t ts;
            int size;
        };

        std::queue<sp<frameInfo>> frameQueue;
        int lengthPerSec;
        int codecBitrate;
        int curBitrate;
        int maxBitrate;
        int minBitrate;
        int frameDropCnt;
        int frameSentCnt;
        int64_t changeTime;
        bool isUpdated;
        bool frameDropEnabled;
        Mutex lock;
    };

    struct VideoCallEngineClient
    {
        VideoCallEngineClient();
        virtual ~VideoCallEngineClient();
        void init();
        void startUplink();
        void stopUplink();
        void switch2Background();
        void stopswitch2Background();
        void startDownlink();
        void stopDownlink();
        void startProcessEvent(int loopback);
        void stopProcessEvent();
        void startRecord();
        void stopRecord();
        void startAudioCapture();
        void stopCamera();
        void startCamera();

        void setLocalSurface(sp<IGraphicBufferProducer> bufferProducer);
        void setRemoteSurface(sp<IGraphicBufferProducer> bufferProducer);
        void setCamera(const sp<ICamera>& camera, const sp<ICameraRecordingProxy>& proxy, VCE_CAMERA_SIZE camera_size);
        void setNotifyCallback(const sp<IVideoCallEngineCallback>& callback);

        //new apis for start/stop encode thread
        void startEncodeThread();
        void stopEncodeThread();

        //start/stop local input thread
        void startVideoInput();
        void stopVideoInput();

        //camera process apis for AP
        int setCameraId(int cameraId);
        void setCameraPreviewSize(VCE_CAMERA_SIZE cameraSize);
        void setPreviewOrientation(int previewOrientation);
        void setPreviewSurface(sp<IGraphicBufferProducer> bufferProducer);
        void startPreview();
        void stopPreview();

        //set cvo info to encoded frame
        void setCVOInfo(int rotate);

        //replace sending datas with pre-setting picture
        void hideLocalImage(bool enable, const char* path);

        //used for input camera callback frames
        static void incomingCameraCallBackFrame(MediaBuffer **mediaBuffer);
        static VideoCallEngineClient* getInstance();

        void setUplinkQos(int qos);

        /* set code type and reconfigure the encode/decode thread if needed */
        void setVideoCodecType(VCE_CODEC_TYPE type);

    private:
        static void* CameraThreadWrapper(void *);
        status_t CameraThreadFunc();

        //thread func to read frame from local video/picture file;
        static void* VideoInputWrapper(void *);
        status_t VideoInputThreadFunc();

        //process for camera output buffers
        int getCameraOutputBufferSize();
        bool isCameraOutputBufferEmpty();
        //clean camera output buffers list, no mediabuffer release included
        void cleanCameraOutputBuffer();

        status_t CodecThreadEndProcessing(sp<MediaCodec> codec, sp<ALooper> looper,
                bool* mStopCodec);
        static void* EncodeThreadWrapper(void *);
        status_t EncodeThreadFunc();
        void sendSpsPps(void *frame);
        void sendFrame(void *frame, sp<ABuffer> buffer, uint32_t ts,
                uint32_t flag, int64_t *frame_send);
        void getFrame(void* frame);

        unsigned int SaveRemoteVideoParamSet(
                sp<ABuffer> paramSetWithPrefix, unsigned int* frame_length,
                unsigned char **frame_data);
        unsigned int SendLocalParamSetToDecoder(
                unsigned int ps_length, unsigned char *ps_frame,
                int* frame_length, unsigned char **frame);
        static void* DecodeThreadWrapper(void *);
        status_t DecodeThreadFunc();
        static void* ProcessEventThreadWrapper(void *);
        status_t ProcessEventThreadFunc();
        static void* AudioRecordThreadWrapper(void *);
        status_t AudioRecordThreadFunc();
        static void* AudioCaptureThreadWrapper(void *);
        status_t AudioCaptureThreadFunc();
        static void* Switch2ThreadWrapper(void* );
        status_t Switch2ThreadFunc();
        void startEncode();

        //camera process using imscamera apis
        int initCamera(int cameraId);
        void releaseCamera();
        void switchCamera(int cameraId);
        short cameraSetPreviewSize(int width, int height);
        short cameraSetFps(int fps);

        int notifyCallback(int event);
        int notifyCallback(int event, int ext1, int ext2, int ext3);

        /* notify AP to change the remote picture display
         * according the remote video stream
         */
        void notifyRemoteDisplayOrientation(
                int dec_width, int dec_height, int cvo_info);

        //uplink qos paras
        int mUplinkGBR;

        //config the init paras for VCE
        VCEVideoParameter *mVCEVideoParameter;

        sp<IGraphicBufferProducer>              mRemoteSurface;
        sp<IGraphicBufferProducer>              mLocalSurface;
        sp<ICamera>                             mCamera;
        sp<ICameraRecordingProxy>               mCameraProxy;
        VCE_CAMERA_SIZE                         mCameraSize;
        int64_t                                 mCameraStartTimeUs;

        int32_t                                 mCameraId;
        bool                                    mCameraPreviewStarted;
        bool                                    mCameraOpen;
        bool                                    mPreviewSurfaceSet;
        int                                     mPreviousDeviceOrientation;
        int                                     mDeviceOrientation;
        bool                                    mUpdateDisplayOrientation;
        int                                     mVQTest;
        FILE                                    *mFp_local_input;
        bool                                    mHideLocalImage;
        char*                                   mReplacedImage;
        const char*                             mReplacedYUV;

        int64_t                                 mRotate;
        int64_t                                 mFlip_h;
        int64_t                                 mRemoteRotate;

        pthread_t                               mCameraThread;
        pthread_t                               mVideoInputThread;
        pthread_t                               mEncodeThread;
        pthread_t                               mDecodeThread;
        pthread_t                               mSwitch2Thread;
        pthread_t                               mProcessEventThread;
        pthread_t                               mRecordThread;
        pthread_t                               mAudioCaptureThread;

        List<sp<ABuffer> >                      mEncodedBuffers;
        List<sp<ABuffer>>                       mCameraOutputBuffers;
        /* List for for both Audio and Video buffers need to be recorded */
        List<MediaBuffer*>                      mRecordedPCMBuffers;

        Mutex                                   mCameraEncodeLock;
        Mutex                                   mEncodeDecodeLock;
        Mutex                                   mRecordedAudioBufferLock;
        Mutex                                   mCameraExitedLock;
        Mutex                                   mDecodeExitedLock;
        Mutex                                   mEncodeExitedLock;
        Mutex                                   mInitCompleteCondLock;
        Mutex                                   mSwitchExitedLock;
        Mutex                                   mCameraOutputBufferLock;

        Condition                               mCameraOutputBufferCond;
        Condition                               mEncodedBufferCond;
        Condition                               mRecordedAudioBufferCond;
        Condition                               mCameraExitedCond;
        Condition                               mDecodeExitedCond;
        Condition                               mEncodeExitedCond;
        Condition                               mInitCompleteCond;
        Condition                               mSwitchExitedCond;

        /* Audio Source for both Downlink and Uplink PCM voice */
        AudioSource                             *mAudioSource;

#if DUMP_AUDIO_AMR
        FILE                                    *mFp_audio;
#endif

#if DUMP_AUDIO_PCM
        FILE                                    *mFp_pcm;
#endif

#if DUMP_VIDEO_BS
        FILE                                    *mFp_video;
        FILE                                    *mFp_local_dump;
        FILE                                    *mFp_local_yuv;
#endif

        sp<MediaMuxer>                          mMuxer;

        int64_t                                 mPrevSampleTimeUs;

        sp<AMessage>                            mVideoFormat;
        sp<AMessage>                            mAudioFormat;

        size_t                                  mVideoTrack;
        size_t                                  mAudioTrack;

        bool                                    sendingFirstFrame;

        bool                                    mStopDecode;
        bool                                    mStopEncode;
        bool                                    mStopCamera;
        bool                                    mStopVideoInput;
        bool                                    mStopProcessEvent;
        bool                                    mStopRecord;
        bool                                    mStopSwitch;
        bool                                    mCallEnd;

        bool                                    mDecodeExited;
        bool                                    mEncodeExited;
        bool                                    mCameraExited;
        bool                                    mProcessEventExited;
        bool                                    mRequestIDRFrame;
        bool                                    mRequestIDRFrameNow;
        bool                                    mStartUplink;
        bool                                    mUplinkReady;
        bool                                    mUpNetworkReady;
        bool                                    mDownlinkReady;
        bool                                    mDownNetworkReady;
        bool                                    mStartRender;

        sp<IVideoCallEngineCallback>            mNotifyCallback;
        sp<VideoEncBitrateAdaption>             mEncBrAdapt;

        bool                                    mEnableLoopBack;
        VCEVideoLoop*                           mVideoLoop;
        bool                                    mVCInitCompleted;

    };
};


#endif


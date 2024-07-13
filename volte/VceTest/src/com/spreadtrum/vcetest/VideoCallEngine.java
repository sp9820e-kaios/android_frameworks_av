package com.spreadtrum.vcetest;

import java.lang.ref.WeakReference;
import android.os.Handler;
import android.os.Bundle;
import android.os.HandlerThread;
import android.os.Looper;
import android.os.Message;
import android.os.AsyncResult;
import android.os.SystemProperties;
import android.os.Parcel;
import android.util.Log;
import android.hardware.Camera;
import android.view.Surface;
import android.content.Context;
import android.content.SharedPreferences;
import android.preference.PreferenceManager;

public class VideoCallEngine {
    private static String TAG = "VceTestVideoCallEngine";
    private static VideoCallEngine gInstance = null;
    private EventHandler mEventHandler;
//    private Context mContext;
    private int mCameraResolution = VT_RESOLUTION_VGA_REVERSED_30;

    public static final int VT_RESOLUTION_720P = 0;                //1280*720 Frame rate:30
    public static final int VT_RESOLUTION_VGA_REVERSED_15 = 1;     //480*640 Frame rate:15
    public static final int VT_RESOLUTION_VGA_REVERSED_30 = 2;     //480*640 Frame rate:30
    public static final int VT_RESOLUTION_QVGA_REVERSED_15 = 3;    //240*320 Frame rate:15
    public static final int VT_RESOLUTION_QVGA_REVERSED_30 = 4;    //240*320 Frame rate:30
    public static final int VT_RESOLUTION_CIF = 5;                 //352*288 Frame rate:30
    public static final int VT_RESOLUTION_QCIF = 6;                //176*144 Frame rate:30
    public static final int VT_RESOLUTION_VGA_15 = 7;              //640*480 Frame rate:15
    public static final int VT_RESOLUTION_VGA_30 = 8;              //640*480 Frame rate:30
    public static final int VT_RESOLUTION_QVGA_15 = 9;             //320*240 Frame rate:15
    public static final int VT_RESOLUTION_QVGA_30 = 10;            //320*240 Frame rate:30

    Surface mRemoteSurface;
    Surface mLocalSurface;

    public static final int MEDIA_UNSOL_EVENT_READY      = 99;
    public static final int VCE_EVENT_NONE               = 1000;
    public static final int VCE_EVENT_INIT_COMPLETE      = 1001;
    public static final int VCE_EVENT_START_ENC          = 1002;
    public static final int VCE_EVENT_START_DEC          = 1003;
    public static final int VCE_EVENT_STOP_ENC           = 1004;
    public static final int VCE_EVENT_STOP_DEC           = 1005;
    public static final int VCE_EVENT_SHUTDOWN           = 1006;
    public static final int VCE_EVENT_REMOTE_ROTATE_CHANGE           = 1011;
    /* Do not change these values without updating their counterparts
     * in include/media/mediaphone.h
     */
    private static final int MEDIA_PREPARED = 1;
    private static final int MEDIA_SET_VIDEO_SIZE = 2;
    private static final int MEDIA_ERROR = 100;
    private static final int MEDIA_INFO = 200;

    private final static String CAMERA_OPEN_STR = "open_:camera_";
    private final static String CAMERA_CLOSE_STR = "close_:camera_";

    private native final int native_closePipe();
    private native final int native_waitRequestForAT();

    // const define
    static final int MAX_BUFFER_SIZE = 256*1024;
    static final int MAX_FRAME_SIZE = 176*144*3/2;

    static final int VIDEO_TYPE_H263 = 1;
    static final int VIDEO_TYPE_MPEG4 = 2;

    // codec request type
    public static final int CODEC_OPEN = 1;
    public static final int CODEC_CLOSE = 2;
    public static final int CODEC_SET_PARAM = 3;

    private int mCodecCount = 0;
    private int mCurrentCodecType = VIDEO_TYPE_H263;

    public enum CodecState  {
        CODEC_IDLE ,
        CODEC_OPEN,
        CODEC_START,
        CODEC_CLOSE;

        public String toString() {
            switch(this) {
                case CODEC_IDLE:
                    return "CODEC_IDLE";
                case CODEC_OPEN:
                    return "CODEC_OPEN";
                case CODEC_START:
                    return "CODEC_START";
                case CODEC_CLOSE:
                    return "CODEC_CLOSE";
            }
            return "CODEC_IDLE";
        }
    };

    static {
        System.loadLibrary("vcetest_jni");
    }

    public VideoCallEngine() {
        //mContext = context;
        initContext();
        gInstance = this;
        Log.i(TAG, "VideoCallEngine create E");
        init();
        postEventFromNative(new WeakReference(this), 0, 0, 0, null);
        setup(new WeakReference(this));
        Log.i(TAG, "VideoCallEngine create X");
    }

    public static VideoCallEngine getInstance(){
        if (gInstance == null){
            gInstance = new VideoCallEngine();
        }
        return gInstance;
    }

    private void initContext() {
        log("initContext() E");
        Looper looper;
        if ((looper = Looper.myLooper()) != null) {
            mEventHandler = new EventHandler(this, looper);
        } else if ((looper = Looper.getMainLooper()) != null) {
            mEventHandler = new EventHandler(this, looper);
        } else {
            mEventHandler = null;
        }

        mVideoState = VideoState.TX_ENABLED|VideoState.RX_ENABLED;
        initCameraResolution();
        log("initContext() X");
    }

    public void initCameraResolution(){
        mCameraResolution = VT_RESOLUTION_VGA_REVERSED_30;
        Log.i(TAG, "initCameraResolution():"+mCameraResolution);
    }

    public int getCameraResolution(){
        Log.i(TAG, "getCameraResolution():"+mCameraResolution);
        return mCameraResolution;
    }

    private static void postEventFromNative(Object vce_ref,
            int what, int arg1, int arg2, Object obj) {
        if (vce_ref == null) {
            return;
        }
        what = what + 1000;
        Log.i(TAG, "postEventFromNative what=" + what);

        VideoCallEngine vce = (VideoCallEngine) ((WeakReference) vce_ref).get();
        if (vce.mEventHandler != null) {
            Message msg = vce.mEventHandler.obtainMessage(what, arg1, arg2, obj);
            vce.mEventHandler.sendMessage(msg);
        }
    }

    public void releaseVideocallEngine() {
        log("release() E");
        mVideoState = VideoState.AUDIO_ONLY;
        this.release();
        log("release() X");
    }

    public void setImsLocalSurface(Surface sf){
        Log.i(TAG, "setImsLocalSurface->sf is null " + (sf == null));
        mLocalSurface = sf;
        this.setLocalSurface(sf);
    }

    public void setImsRemoteSurface(Surface sf){
        log("setImsRemoteSurface() Surface:" + sf);
        mRemoteSurface = sf;
        this.setRemoteSurface(sf);
        log("setRemoteSurface() mVideoState:" + mVideoState);
    }

    public void setImsCamera(Camera cam,int quality){
        this.setCamera(cam, quality);
    }

    public void setImsCamera(Camera cam){
        this.setCamera(cam, mCameraResolution);
    }

    public void setImsHideLocalImage(boolean enable, String filePath) {
        this.hideLocalImage(enable,filePath);
    }

    public static final int MEDIA_CALLEVENT_CAMERACLOSE = 100;
    public static final int MEDIA_CALLEVENT_CAMERAOPEN = 101;
    public static final int MEDIA_CALLEVENT_STRING = 102;
    public static final int MEDIA_CALLEVENT_CODEC_OPEN = 103;
    public static final int MEDIA_CALLEVENT_CODEC_SET_PARAM_DECODER = 104;
    public static final int MEDIA_CALLEVENT_CODEC_SET_PARAM_ENCODER = 105;
    public static final int MEDIA_CALLEVENT_CODEC_START = 106;
    public static final int MEDIA_CALLEVENT_CODEC_CLOSE = 107;
    public static final int MEDIA_CALLEVENT_MEDIA_START = 108;

    /**
     * Interface definition of a callback to be invoked to communicate some
     * info and/or warning about the h324 or call control.
     */
    public interface OnCallEventListener
    {
        boolean onCallEvent(VideoCallEngine videoCallEngine, int what, Object extra);
    }

    /**
     * Register a callback to be invoked when an info/warning is available.
     *
     * @param listener the callback that will be run
     */
    public void setOnCallEventListener(OnCallEventListener listener)
    {
        mOnCallEventListener = listener;
    }

    private OnCallEventListener mOnCallEventListener;

    public void sendString(String str) {
        Log.d(TAG, "sendString");
    }

    public void controlLocalVideo(boolean bEnable, boolean bReplaceImg) {
        Log.d(TAG, "controlLocalVideo");

        if (bReplaceImg){
            if (bEnable)
                sendString("open_:camera_");
            else
                sendString("close_:camera_");
        }
    }

    private class EventHandler extends Handler {
        private VideoCallEngine mVideoCallEngine;

        public EventHandler(VideoCallEngine mvideocallengine, Looper looper) {
            super(looper);
            mVideoCallEngine = mvideocallengine;
        }

        @Override
        public void handleMessage(Message msg) {
            Log.d(TAG, "handleMessage " + msg);

            AsyncResult ar = null;;
            if (msg.what != VCE_EVENT_REMOTE_ROTATE_CHANGE){
                ar = (AsyncResult) msg.obj;
             }
            switch(msg.what) {
                //case RIL_UNSOL_VIDEOPHONE_STRING: {
                case VCE_EVENT_NONE:{
                    return;
                }
                case VCE_EVENT_INIT_COMPLETE:{
                    mVideoState = VideoState.AUDIO_ONLY;
                    return;
                }
                case VCE_EVENT_START_ENC:{
                    log("VCE_EVENT_START_ENC, mVideoState" + mVideoState);
                    if(!VideoState.isPaused(mVideoState)){
                        mVideoState = mVideoState | VideoState.TX_ENABLED;
                    }
                    return;
                }
                case VCE_EVENT_START_DEC:{
                    log("VCE_EVENT_START_DEC, mVideoState" + mVideoState);
                    if(!VideoState.isPaused(mVideoState)){
                        mVideoState = mVideoState | VideoState.RX_ENABLED;
                    }
                    return;
                }
                case VCE_EVENT_STOP_ENC:{
                    log("VCE_EVENT_STOP_ENC, mVideoState" + mVideoState);
                    mVideoState = mVideoState & ~VideoState.TX_ENABLED;
                    return;
                }
                case VCE_EVENT_STOP_DEC:{
                    log("VCE_EVENT_STOP_ENC, mVideoState" + mVideoState);
                    mVideoState = mVideoState & ~VideoState.RX_ENABLED;
                    return;
                }
                case VCE_EVENT_SHUTDOWN:{
                    mVideoState = VideoState.AUDIO_ONLY;
                    return;
                }
                case VCE_EVENT_REMOTE_ROTATE_CHANGE:{
                    Log.d(TAG, "handleMessage VCE_EVENT_REMOTE_ROTATE_CHANGE");
                    if(msg.obj != null){
                        Parcel data = (Parcel)msg.obj;
                        int width = data.readInt();
                        int height = data.readInt();
                        log("VCE_EVENT_REMOTE_ROTATE_CHANGE, width = " + width + "height = " + height);
                    }
                    return;
                }
                default:
                    Log.e(TAG, "Unknown message type " + msg.what);
                    return;
            }
        }
    }

    private void log(String msg) {
        Log.d(TAG, msg);
    }

    private int mVideoState = VideoState.AUDIO_ONLY;
    /**
    * The video state of the call, stored as a bit-field describing whether video transmission and
    * receipt it enabled, as well as whether the video is currently muted.
    */
    private static class VideoState {
        /**
         * Call is currently in an audio-only mode with no video transmission or receipt.
         */
        public static final int AUDIO_ONLY = 0x0;

        /**
         * Video transmission is enabled.
         */
        public static final int TX_ENABLED = 0x1;

        /**
         * Video reception is enabled.
         */
        public static final int RX_ENABLED = 0x2;

        /**
         * Video signal is bi-directional.
         */
        public static final int BIDIRECTIONAL = TX_ENABLED | RX_ENABLED;

        /**
         * Video is paused.
         */
        public static final int PAUSED = 0x4;

        /**
         * Whether the video state is audio only.
         * @param videoState The video state.
         * @return Returns true if the video state is audio only.
         */
        public static boolean isAudioOnly(int videoState) {
            return !hasState(videoState, TX_ENABLED) && !hasState(videoState, RX_ENABLED);
        }

        /**
         * Whether the video transmission is enabled.
         * @param videoState The video state.
         * @return Returns true if the video transmission is enabled.
         */
        public static boolean isTransmissionEnabled(int videoState) {
            return hasState(videoState, TX_ENABLED);
        }

        /**
         * Whether the video reception is enabled.
         * @param videoState The video state.
         * @return Returns true if the video transmission is enabled.
         */
        public static boolean isReceptionEnabled(int videoState) {
            return hasState(videoState, RX_ENABLED);
        }

        /**
         * Whether the video signal is bi-directional.
         * @param videoState
         * @return Returns true if the video signal is bi-directional.
         */
        public static boolean isBidirectional(int videoState) {
            return hasState(videoState, BIDIRECTIONAL);
        }

        /**
         * Whether the video is paused.
         * @param videoState The video state.
         * @return Returns true if the video is paused.
         */
        public static boolean isPaused(int videoState) {
            return hasState(videoState, PAUSED);
        }

        /**
         * Determines if a specified state is set in a videoState bit-mask.
         *
         * @param videoState The video state bit-mask.
         * @param state The state to check.
         * @return {@code True} if the state is set.
         * {@hide}
         */
        private static boolean hasState(int videoState, int state) {
            return (videoState & state) == state;
        }
    }

    private int mLocalNativeSurfaceTexture; // accessed by native methods
    private int mRemoteNativeSurfaceTexture; // accessed by native methods

    public static native void init();

    public static native void setup(Object weak_this);

    public static native void reset();

    public static native void release();

    public static native void jfinalize();

    public static native void setRemoteSurface(Object surface);

    public static native void setLocalSurface(Object surface);

    public static native void setCamera(Object camera, int resolution);

    public static native void prepare();

    public static native void startUplink();

    public static native void stopUplink();

    public static native void startDownlink();

    public static native void stopDownlink();

    public static native void setUplinkImageFileFD(Object fileDescriptor, long offset, long length);

    public static native void selectRecordSource(int source);

    public static native void selectRecordFileFormat(int format);

    public static native void startRecord();

    public static native void stopRecord();

    public static native void setRecordFileFD(Object fileDescriptor, long offset, long length);

    public static native void setRecordMaxFileSize(long max_filesize_bytes);

    public static native int setCameraId(int id);

    public static native void setCameraPreviewSize(int size);

    public static native void setPreviewDisplayOrientation(int rotation);

    public static native void startPreview();

    public static native void stopPreview();

    public static native void setUplinkQos(int qos);

    public static native void hideLocalImage(boolean enable, String path);

    public static native void setVideoCodecType(int type);
}

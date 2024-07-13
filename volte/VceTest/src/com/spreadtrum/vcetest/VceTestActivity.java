package com.spreadtrum.vcetest;

import android.app.Activity;
import android.os.Bundle;
import android.util.Log;
import android.content.pm.ActivityInfo;

import com.spreadtrum.vcetest.VideoCallEngine;
public class VceTestActivity extends Activity {

    private static final String TAG = "VceTestActivity";

    private VceTestOrientationEventListener mOrientationEventListener;

    private VideoCallEngine mVideoCallEngine = null;

    @Override
    protected void onCreate(Bundle savedInstanceState) {
        Log.i(TAG, "onCreate");
        super.onCreate(savedInstanceState);
        mVideoCallEngine = new VideoCallEngine();
        //setRequestedOrientation(ActivityInfo.SCREEN_ORIENTATION_NOSENSOR);
        setContentView(R.layout.activity_vcetest);
        if (null == savedInstanceState) {
            getFragmentManager().beginTransaction()
                    .replace(R.id.container, VceTestFragment.newInstance())
                    .commit();
        }
        mOrientationEventListener = new VceTestOrientationEventListener(this);
        enableOrientationEventListener(true);
    }

    @Override
    public void onStart() {
        Log.i(TAG, "onStart");
        super.onStart();
    }

    @Override
    public void onPause() {
        Log.i(TAG, "onPause");
        super.onPause();
    }

    @Override
    public void onStop() {
        Log.i(TAG, "onStop");
        super.onStop();
    }

    @Override
    public void onResume() {
        Log.i(TAG, "onResume");
        super.onResume();
    }

    @Override
    public void onDestroy() {
        Log.i(TAG, "onDestroy");
        super.onDestroy();
    }

    public void enableOrientationEventListener(boolean enable) {
        if (enable) {
            mOrientationEventListener.enable(enable);
        } else {
            mOrientationEventListener.disable();
        }
    }

}

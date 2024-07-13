package com.spreadtrum.vcetest;

import android.Manifest;
import android.app.Activity;
import android.app.AlertDialog;
import android.app.Dialog;
import android.app.DialogFragment;
import android.app.Fragment;
import android.content.Context;
import android.content.DialogInterface;
import android.content.pm.PackageManager;
import android.content.res.Configuration;
import android.graphics.ImageFormat;
import android.graphics.Matrix;
import android.graphics.Point;
import android.graphics.RectF;
import android.graphics.SurfaceTexture;
import android.hardware.camera2.CameraAccessException;
import android.hardware.camera2.CameraCaptureSession;
import android.hardware.camera2.CameraCharacteristics;
import android.hardware.camera2.CameraDevice;
import android.hardware.camera2.CameraManager;
import android.hardware.camera2.CameraMetadata;
import android.hardware.camera2.CaptureRequest;
import android.hardware.camera2.CaptureResult;
import android.hardware.camera2.TotalCaptureResult;
import android.hardware.camera2.params.StreamConfigurationMap;
import android.media.Image;
import android.media.ImageReader;
import android.os.Bundle;
import android.os.Handler;
import android.os.HandlerThread;
import android.os.Environment;
import android.support.annotation.NonNull;
import android.support.v13.app.FragmentCompat;
import android.support.v4.content.ContextCompat;
import android.util.Log;
import android.util.Size;
import android.util.SparseIntArray;
import android.view.LayoutInflater;
import android.view.Surface;
import android.view.TextureView;
import android.view.View;
import android.view.ViewGroup;
import android.view.Menu;
import android.view.MenuItem;
import android.widget.Toast;
import android.widget.PopupMenu;

import java.io.File;
import java.io.FileOutputStream;
import java.io.IOException;
import java.nio.ByteBuffer;
import java.util.ArrayList;
import java.util.Arrays;
import java.util.Collections;
import java.util.Comparator;
import java.util.List;
import java.util.concurrent.Semaphore;
import java.util.concurrent.TimeUnit;

import com.spreadtrum.vcetest.VideoCallEngine;
import com.spreadtrum.vcetest.VceTestOrientationEventListener;

public class VceTestFragment extends Fragment
        implements View.OnClickListener,
                   PopupMenu.OnMenuItemClickListener,
                   FragmentCompat.OnRequestPermissionsResultCallback {

    /**
     * Conversion from screen rotation to JPEG orientation.
     */
    private static final SparseIntArray ORIENTATIONS = new SparseIntArray();
    private static final int REQUEST_CAMERA_PERMISSION = 1;
    private static final String FRAGMENT_DIALOG = "dialog";

    static {
        ORIENTATIONS.append(Surface.ROTATION_0, 90);
        ORIENTATIONS.append(Surface.ROTATION_90, 0);
        ORIENTATIONS.append(Surface.ROTATION_180, 270);
        ORIENTATIONS.append(Surface.ROTATION_270, 180);
    }

    /**
     * Tag for the {@link Log}.
     */
    private static final String TAG = "VceTestFragment";

    /**
     * Camera state: Showing camera preview.
     */
    private static final int STATE_PREVIEW = 0;

    /**
     * Camera state: Waiting for the focus to be locked.
     */
    private static final int STATE_WAITING_LOCK = 1;

    /**
     * Camera state: Waiting for the exposure to be precapture state.
     */
    private static final int STATE_WAITING_PRECAPTURE = 2;

    /**
     * Camera state: Waiting for the exposure state to be something other than precapture.
     */
    private static final int STATE_WAITING_NON_PRECAPTURE = 3;

    /**
     * Camera state: Picture was taken.
     */
    private static final int STATE_PICTURE_TAKEN = 4;

    /**
     * Max preview width that is guaranteed by Camera2 API
     */
    private static final int MAX_PREVIEW_WIDTH = 1920;

    /**
     * Max preview height that is guaranteed by Camera2 API
     */
    private static final int MAX_PREVIEW_HEIGHT = 1080;

    private static int mCamera_Id = 1;
    private static int mCamera_Size = 1;

    /**
     * {@link TextureView.SurfaceTextureListener} handles several lifecycle events on a
     * {@link TextureView}.
     */
    private final TextureView.SurfaceTextureListener mLocalTextureListener
            = new TextureView.SurfaceTextureListener() {

        @Override
        public void onSurfaceTextureAvailable(SurfaceTexture texture, int width, int height) {
            Log.i(TAG, "onSurfaceTextureAvailable, local");
            SurfaceTexture remoteTexture = mLocalView.getSurfaceTexture();
            mLocalSurface = new Surface(remoteTexture);
            openCamera(1, mCamera_Size);
        }

        @Override
        public void onSurfaceTextureSizeChanged(SurfaceTexture texture, int width, int height) {
            Log.i(TAG, "onSurfaceTextureSizeChanged, local");
            configureTransform(width, height);
        }

        @Override
        public boolean onSurfaceTextureDestroyed(SurfaceTexture texture) {
            Log.i(TAG, "onSurfaceTextureDestroyed, local");
            return true;
        }

        @Override
        public void onSurfaceTextureUpdated(SurfaceTexture texture) {
        }
    };

    private final TextureView.SurfaceTextureListener mRemoteTextureListener
            = new TextureView.SurfaceTextureListener() {

        @Override
        public void onSurfaceTextureAvailable(SurfaceTexture texture, int width, int height) {
            Log.i(TAG, "onSurfaceTextureAvailable, remote");
            SurfaceTexture remoteTexture = mRemoteView.getSurfaceTexture();
            mRemoteSurface = new Surface(remoteTexture);
            int orientation = getResources().getConfiguration().orientation;
            if (orientation == Configuration.ORIENTATION_LANDSCAPE) {
                mRemoteView.setAspectRatio(640, 480);
            } else {
                mRemoteView.setAspectRatio(480, 640);
            }
            if (mVideoCallEngine != null) {
                mVideoCallEngine.setRemoteSurface(mRemoteSurface);
            }
        }

        @Override
        public void onSurfaceTextureSizeChanged(SurfaceTexture texture, int width, int height) {
            Log.i(TAG, "onSurfaceTextureSizeChanged, remote");
            configureTransform(width, height);
        }

        @Override
        public boolean onSurfaceTextureDestroyed(SurfaceTexture texture) {
            Log.i(TAG, "onSurfaceTextureDestroyed, remote");
            return true;
        }

        @Override
        public void onSurfaceTextureUpdated(SurfaceTexture texture) {
        }
    };

    /**
     * ID of the current {@link CameraDevice}.
     */
    private String mCameraId;

    /**
     * An {@link AutoFitTextureView} for camera preview.
     */
    private AutoFitTextureView mLocalView;
    private AutoFitTextureView mRemoteView;
    private Surface mLocalSurface;
    private Surface mRemoteSurface;
    private static VideoCallEngine mVideoCallEngine;

    private View mOverflowMenuButton;
    private PopupMenu mOverflowPopupMenu;
    private PopupMenu mPopupMenu;

    private boolean hideLocalImage = false;
    private boolean enableHEVC = false;

    /**
     * A {@link CameraCaptureSession } for camera preview.
     */
    private CameraCaptureSession mCaptureSession;

    /**
     * A reference to the opened {@link CameraDevice}.
     */
    private CameraDevice mCameraDevice;

    /**
     * The {@link android.util.Size} of camera preview.
     */
    private Size mPreviewSize;

    private static int mCurrentOrientation = 0;

    public static void setPreviewOrientation(int orientation){
        if (mCurrentOrientation != orientation){
            mCurrentOrientation = orientation;
            if (mVideoCallEngine != null){
                mVideoCallEngine.setPreviewDisplayOrientation((360 - mCurrentOrientation)%360);
            }
        }
    }

    /**
     * An additional thread for running tasks that shouldn't block the UI.
     */
    private HandlerThread mBackgroundThread;

    /**
     * A {@link Handler} for running tasks in the background.
     */
    private Handler mBackgroundHandler;

    /**
     * An {@link ImageReader} that handles still image capture.
     */
    private ImageReader mImageReader;

    /**
     * This is the output file for our picture.
     */
    private File mFile;

    /**
     * This a callback object for the {@link ImageReader}. "onImageAvailable" will be called when a
     * still image is ready to be saved.
     */
    private final ImageReader.OnImageAvailableListener mOnImageAvailableListener
            = new ImageReader.OnImageAvailableListener() {

        @Override
        public void onImageAvailable(ImageReader reader) {
            mBackgroundHandler.post(new ImageSaver(reader.acquireNextImage(), mFile));
        }

    };

    /**
     * {@link CaptureRequest.Builder} for the camera preview
     */
    private CaptureRequest.Builder mPreviewRequestBuilder;

    /**
     * {@link CaptureRequest} generated by {@link #mPreviewRequestBuilder}
     */
    private CaptureRequest mPreviewRequest;

    /**
     * The current state of camera state for taking pictures.
     *
     * @see #mCaptureCallback
     */
    private int mState = STATE_PREVIEW;

    /**
     * A {@link Semaphore} to prevent the app from exiting before closing the camera.
     */
    private Semaphore mCameraOpenCloseLock = new Semaphore(1);

    /**
     * Whether the current camera device supports Flash or not.
     */
    private boolean mFlashSupported;

    /**
     * Orientation of the camera sensor
     */
    private int mSensorOrientation;

    /**
     * Shows a {@link Toast} on the UI thread.
     *
     * @param text The message to show
     */
    private void showToast(final String text) {
        final Activity activity = getActivity();
        if (activity != null) {
            activity.runOnUiThread(new Runnable() {
                @Override
                public void run() {
                    Toast.makeText(activity, text, Toast.LENGTH_SHORT).show();
                }
            });
        }
    }

    /**
     * Given {@code choices} of {@code Size}s supported by a camera, choose the smallest one that
     * is at least as large as the respective texture view size, and that is at most as large as the
     * respective max size, and whose aspect ratio matches with the specified value. If such size
     * doesn't exist, choose the largest one that is at most as large as the respective max size,
     * and whose aspect ratio matches with the specified value.
     *
     * @param choices           The list of sizes that the camera supports for the intended output
     *                          class
     * @param textureViewWidth  The width of the texture view relative to sensor coordinate
     * @param textureViewHeight The height of the texture view relative to sensor coordinate
     * @param maxWidth          The maximum width that can be chosen
     * @param maxHeight         The maximum height that can be chosen
     * @param aspectRatio       The aspect ratio
     * @return The optimal {@code Size}, or an arbitrary one if none were big enough
     */
    private static Size chooseOptimalSize(Size[] choices, int textureViewWidth,
            int textureViewHeight, int maxWidth, int maxHeight, Size aspectRatio) {
        Log.i(TAG, "chooseOptimalSize");
        // Collect the supported resolutions that are at least as big as the preview Surface
        List<Size> bigEnough = new ArrayList<>();
        // Collect the supported resolutions that are smaller than the preview Surface
        List<Size> notBigEnough = new ArrayList<>();
        int w = aspectRatio.getWidth();
        int h = aspectRatio.getHeight();
        for (Size option : choices) {
            if (option.getWidth() <= maxWidth && option.getHeight() <= maxHeight &&
                    option.getHeight() == option.getWidth() * h / w) {
                if (option.getWidth() >= textureViewWidth &&
                    option.getHeight() >= textureViewHeight) {
                    bigEnough.add(option);
                } else {
                    notBigEnough.add(option);
                }
            }
        }

        // Pick the smallest of those big enough. If there is no one big enough, pick the
        // largest of those not big enough.
        if (bigEnough.size() > 0) {
            return Collections.min(bigEnough, new CompareSizesByArea());
        } else if (notBigEnough.size() > 0) {
            return Collections.max(notBigEnough, new CompareSizesByArea());
        } else {
            Log.e(TAG, "Couldn't find any suitable preview size");
            return choices[0];
        }
    }

    public static VceTestFragment newInstance() {
        Log.i(TAG, "newInstance");
        return new VceTestFragment();
    }

    @Override
    public View onCreateView(LayoutInflater inflater, ViewGroup container,
                             Bundle savedInstanceState) {
        Log.i(TAG, "onCreateView");
        return inflater.inflate(R.layout.fragment_vcetest, container, false);
    }

    @Override
    public void onViewCreated(final View view, Bundle savedInstanceState) {
        Log.i(TAG, "onViewCreated");
        view.findViewById(R.id.switch_camera).setOnClickListener(this);
        view.findViewById(R.id.pause_camera).setOnClickListener(this);
        view.findViewById(R.id.resume_camera).setOnClickListener(this);
        mOverflowMenuButton = view.findViewById(R.id.vcetest_overflow);
        mOverflowPopupMenu = buildOptionsMenu(mOverflowMenuButton);
        mOverflowMenuButton.setOnTouchListener(mOverflowPopupMenu.getDragToOpenListener());
        mOverflowMenuButton.setOnClickListener(this);
        mLocalView = (AutoFitTextureView) view.findViewById(R.id.local_texture);
        mRemoteView = (AutoFitTextureView) view.findViewById(R.id.remote_texture);
    }

    @Override
    public boolean onMenuItemClick(MenuItem item) {
        int resId = item.getItemId();
        switch (resId) {
            case R.id.info: {
                Activity activity = getActivity();
                if (null != activity) {
                    new AlertDialog.Builder(activity)
                            .setMessage(R.string.intro_message)
                            .setPositiveButton(android.R.string.ok, null)
                            .show();
                }
                break;
            }
            case R.id.hideLocalImage: {
                hideLocalImage = true;
                String filePath =  "/data/misc/media/vertical.raw";
                mVideoCallEngine.setImsHideLocalImage(true, filePath);
                break;
            }
            case R.id.showLocalImage: {
                hideLocalImage = false;
                mVideoCallEngine.setImsHideLocalImage(false, null);
                break;
            }
            case R.id.enableHEVC: {
                enableHEVC = true;
                mVideoCallEngine.setVideoCodecType(2);
                break;
            }
            case R.id.enableAVC: {
                enableHEVC = false;
                mVideoCallEngine.setVideoCodecType(0);
                break;
            }
        }
        return true;
    }

    @Override
    public void onActivityCreated(Bundle savedInstanceState) {
        Log.i(TAG, "onActivityCreated");
        super.onActivityCreated(savedInstanceState);
        mVideoCallEngine = VideoCallEngine.getInstance();
        mFile = new File(getActivity().getExternalFilesDir(null), "pic.jpg");
    }

    @Override
    public void onResume() {
        Log.i(TAG, "onResume");
        super.onResume();
        //startBackgroundThread();

        // When the screen is turned off and turned back on, the SurfaceTexture is already
        // available, and "onSurfaceTextureAvailable" will not be called. In that case, we can open
        // a camera and start preview from here (otherwise, we wait until the surface is ready in
        // the SurfaceTextureListener).
        if (mLocalView.isAvailable()) {
            SurfaceTexture localTexture = mLocalView.getSurfaceTexture();
            mLocalSurface = new Surface(localTexture);
            openCamera(1, mCamera_Size);
        } else {
            mLocalView.setSurfaceTextureListener(mLocalTextureListener);
        }
        if (mRemoteView.isAvailable()) {
            int orientation = getResources().getConfiguration().orientation;
            if (orientation == Configuration.ORIENTATION_LANDSCAPE) {
                mRemoteView.setAspectRatio(640, 480);
            } else {
                mRemoteView.setAspectRatio(480, 640);
            }
            SurfaceTexture remoteTexture = mRemoteView.getSurfaceTexture();
            mRemoteSurface = new Surface(remoteTexture);
            if (mVideoCallEngine != null) {
                mVideoCallEngine.setRemoteSurface(mRemoteSurface);
            }
        } else {
            mRemoteView.setSurfaceTextureListener(mRemoteTextureListener);
        }
    }

    @Override
    public void onStart() {
        Log.i(TAG, "onStart");
        if (mVideoCallEngine == null) {
            mVideoCallEngine = new VideoCallEngine();
        }
        super.onStart();
    }

    @Override
    public void onPause() {
        Log.i(TAG, "onPause");
        if (mVideoCallEngine != null) {
            mVideoCallEngine.releaseVideocallEngine();
            mVideoCallEngine = null;
        }
        super.onPause();
    }

    @Override
    public void onStop() {
        Log.i(TAG, "onStop");
        super.onStop();
    }

    @Override
    public void onDestroy() {
        Log.i(TAG, "onDestroy");
        super.onDestroy();
    }

    private PopupMenu buildOptionsMenu(View invoker) {
        if(mPopupMenu == null){
            mPopupMenu = new PopupMenu(getActivity(), invoker) {
                @Override
                public void show() {
                    final Menu menu = getMenu();
                    for (int i = 0; i < menu.size(); i++) {
                        MenuItem item = menu.getItem(i);
                        item.setEnabled(true);
                        item.setVisible(true);
                        if (item.getItemId() == R.id.hideLocalImage) {
                            item.setVisible(!hideLocalImage);
                        }
                        if (item.getItemId() == R.id.showLocalImage) {
                            item.setVisible(hideLocalImage);
                        }
                        if (item.getItemId() == R.id.enableHEVC) {
                            item.setVisible(!enableHEVC);
                        }
                        if (item.getItemId() == R.id.enableAVC) {
                            item.setVisible(enableHEVC);
                        }
                    }
                    super.show();
                }
            };
        }
        mPopupMenu.getMenu().clear();
        mPopupMenu.inflate(R.menu.vcetest_options);

        mPopupMenu.setOnMenuItemClickListener(this);
        return mPopupMenu;
    }


    private void requestCameraPermission() {
        Log.i(TAG, "requestCameraPermission");
        if (FragmentCompat.shouldShowRequestPermissionRationale(this, Manifest.permission.CAMERA)) {
            new ConfirmationDialog().show(getChildFragmentManager(), FRAGMENT_DIALOG);
        } else {
            FragmentCompat.requestPermissions(this, new String[]{Manifest.permission.CAMERA},
                    REQUEST_CAMERA_PERMISSION);
        }
    }

    @Override
    public void onRequestPermissionsResult(int requestCode, @NonNull String[] permissions,
                                           @NonNull int[] grantResults) {
        Log.i(TAG, "onRequestPermissionsResult");
        if (requestCode == REQUEST_CAMERA_PERMISSION) {
            if (grantResults.length != 1 || grantResults[0] != PackageManager.PERMISSION_GRANTED) {
                ErrorDialog.newInstance(getString(R.string.request_permission))
                        .show(getChildFragmentManager(), FRAGMENT_DIALOG);
            }
        } else {
            super.onRequestPermissionsResult(requestCode, permissions, grantResults);
        }
    }

    private void openCamera(int camera_id, int camera_size){
        Log.i(TAG, "openCamera");
        if (mVideoCallEngine == null) {
            Log.i(TAG, "openCamera failed, mVideoCallEngine == null");
        }
        int ret = mVideoCallEngine.setCameraId(camera_id);
        mCamera_Id = camera_id;
        if (ret != 0){
            Log.i(TAG, "openCamera failed, setCameraId return " + ret);
        }
        mVideoCallEngine.setCameraPreviewSize(camera_size);
        mVideoCallEngine.setPreviewDisplayOrientation((360 - mCurrentOrientation)%360);

        if (mLocalSurface != null) {
            mVideoCallEngine.setLocalSurface(mLocalSurface);
            mVideoCallEngine.startPreview();
        } else {
            mVideoCallEngine.setCameraId(-1);
            Log.i(TAG, "openCamera failed, local surface is null");
        }
    }

    private void closeCamera(){
        Log.i(TAG, "closeCamera");
        if (mVideoCallEngine == null) {
            Log.i(TAG, "closeCamera failed, mVideoCallEngine == null");
        }
        int ret = mVideoCallEngine.setCameraId(-1);
        mCamera_Id = -1;
        if (ret != 0){
            Log.i(TAG, "closeCamera failed, setCameraId return " + ret);
        }
    }

    /**
     * Configures the necessary {@link android.graphics.Matrix} transformation to `mLocalView`.
     * This method should be called after the camera preview size is determined in
     * setUpCameraOutputs and also the size of `mLocalView` is fixed.
     *
     * @param viewWidth  The width of `mLocalView`
     * @param viewHeight The height of `mLocalView`
     */
    private void configureTransform(int viewWidth, int viewHeight) {
        Log.i(TAG, "configureTransform");
        Activity activity = getActivity();
        if (null == mLocalView || null == mPreviewSize || null == activity) {
            return;
        }
        int rotation = activity.getWindowManager().getDefaultDisplay().getRotation();
        Matrix matrix = new Matrix();
        RectF viewRect = new RectF(0, 0, viewWidth, viewHeight);
        RectF bufferRect = new RectF(0, 0, mPreviewSize.getHeight(), mPreviewSize.getWidth());
        float centerX = viewRect.centerX();
        float centerY = viewRect.centerY();
        if (Surface.ROTATION_90 == rotation || Surface.ROTATION_270 == rotation) {
            bufferRect.offset(centerX - bufferRect.centerX(), centerY - bufferRect.centerY());
            matrix.setRectToRect(viewRect, bufferRect, Matrix.ScaleToFit.FILL);
            float scale = Math.max(
                    (float) viewHeight / mPreviewSize.getHeight(),
                    (float) viewWidth / mPreviewSize.getWidth());
            matrix.postScale(scale, scale, centerX, centerY);
            matrix.postRotate(90 * (rotation - 2), centerX, centerY);
        } else if (Surface.ROTATION_180 == rotation) {
            matrix.postRotate(180, centerX, centerY);
        }
        mLocalView.setTransform(matrix);
    }

    /**
     * Retrieves the JPEG orientation from the specified screen rotation.
     *
     * @param rotation The screen rotation.
     * @return The JPEG orientation (one of 0, 90, 270, and 360)
     */
    private int getOrientation(int rotation) {
        Log.i(TAG, "getOrientation");
        // Sensor orientation is 90 for most devices, or 270 for some devices (eg. Nexus 5X)
        // We have to take that into account and rotate JPEG properly.
        // For devices with orientation of 90, we simply return our mapping from ORIENTATIONS.
        // For devices with orientation of 270, we need to rotate the JPEG 180 degrees.
        return (ORIENTATIONS.get(rotation) + mSensorOrientation + 270) % 360;
    }

    private void switchCamera() {
        Log.i(TAG, "switchCamera");
        if (mCamera_Id == -1){
            Log.i(TAG, "switchCamera failed, camera is not open");
            return;
        }
        if (mCamera_Id == 0){
            Log.i(TAG, "switchCamera 0->1");
            closeCamera();
            openCamera(1, mCamera_Size);
            return;
        }
        if (mCamera_Id == 1){
            Log.i(TAG, "switchCamera 1->0");
            closeCamera();
            openCamera(0, mCamera_Size);
            return;
        }
    }

    private void pauseCamera() {
        Log.i(TAG, "pauseCamera");
        closeCamera();
    }

    private void resumeCamera() {
        Log.i(TAG, "resumeCamera");
        openCamera(1, mCamera_Size);
    }

    @Override
    public void onClick(View view) {
        Log.i(TAG, "onClick");
        switch (view.getId()) {
            case R.id.switch_camera: {
                switchCamera();
                break;
            }
            case R.id.pause_camera: {
                pauseCamera();
                break;
            }
            case R.id.resume_camera: {
                resumeCamera();
                break;
            }
            case R.id.vcetest_overflow: {
                mOverflowPopupMenu.show();
                break;
            }
        }
    }

    /**
     * Saves a JPEG {@link Image} into the specified {@link File}.
     */
    private static class ImageSaver implements Runnable {

        /**
         * The JPEG image
         */
        private final Image mImage;
        /**
         * The file we save the image into.
         */
        private final File mFile;

        public ImageSaver(Image image, File file) {
            mImage = image;
            mFile = file;
        }

        @Override
        public void run() {
            ByteBuffer buffer = mImage.getPlanes()[0].getBuffer();
            byte[] bytes = new byte[buffer.remaining()];
            buffer.get(bytes);
            FileOutputStream output = null;
            try {
                output = new FileOutputStream(mFile);
                output.write(bytes);
            } catch (IOException e) {
                e.printStackTrace();
            } finally {
                mImage.close();
                if (null != output) {
                    try {
                        output.close();
                    } catch (IOException e) {
                        e.printStackTrace();
                    }
                }
            }
        }

    }

    /**
     * Compares two {@code Size}s based on their areas.
     */
    static class CompareSizesByArea implements Comparator<Size> {

        @Override
        public int compare(Size lhs, Size rhs) {
            // We cast here to ensure the multiplications won't overflow
            return Long.signum((long) lhs.getWidth() * lhs.getHeight() -
                    (long) rhs.getWidth() * rhs.getHeight());
        }

    }

    /**
     * Shows an error message dialog.
     */
    public static class ErrorDialog extends DialogFragment {

        private static final String ARG_MESSAGE = "message";

        public static ErrorDialog newInstance(String message) {
            ErrorDialog dialog = new ErrorDialog();
            Bundle args = new Bundle();
            args.putString(ARG_MESSAGE, message);
            dialog.setArguments(args);
            return dialog;
        }

        @Override
        public Dialog onCreateDialog(Bundle savedInstanceState) {
            final Activity activity = getActivity();
            return new AlertDialog.Builder(activity)
                    .setMessage(getArguments().getString(ARG_MESSAGE))
                    .setPositiveButton(android.R.string.ok, new DialogInterface.OnClickListener() {
                        @Override
                        public void onClick(DialogInterface dialogInterface, int i) {
                            activity.finish();
                        }
                    })
                    .create();
        }

    }

    /**
     * Shows OK/Cancel confirmation dialog about camera permission.
     */
    public static class ConfirmationDialog extends DialogFragment {

        @Override
        public Dialog onCreateDialog(Bundle savedInstanceState) {
            final Fragment parent = getParentFragment();
            return new AlertDialog.Builder(getActivity())
                    .setMessage(R.string.request_permission)
                    .setPositiveButton(android.R.string.ok, new DialogInterface.OnClickListener() {
                        @Override
                        public void onClick(DialogInterface dialog, int which) {
                            FragmentCompat.requestPermissions(parent,
                                    new String[]{Manifest.permission.CAMERA},
                                    REQUEST_CAMERA_PERMISSION);
                        }
                    })
                    .setNegativeButton(android.R.string.cancel,
                            new DialogInterface.OnClickListener() {
                                @Override
                                public void onClick(DialogInterface dialog, int which) {
                                    Activity activity = parent.getActivity();
                                    if (activity != null) {
                                        activity.finish();
                                    }
                                }
                            })
                    .create();
        }
    }

}

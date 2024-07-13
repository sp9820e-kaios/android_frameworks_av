#ifndef _VCEENGINE_UI_H
#define _VCEENGINE_UI_H



#include <binder/Parcel.h>
#include <gui/Surface.h>


using namespace android;


#ifdef __cplusplus
extern "C" {
#endif

typedef void (*VCECALLBACK)(int what, int ext1, int ext2, Parcel *obj);

void VideoCallEngine_init();
void VideoCallEngine_setup(VCECALLBACK callback);
void VideoCallEngine_reset();
void VideoCallEngine_release();
void VideoCallEngine_setRemoteSurface(sp<IGraphicBufferProducer>& aProducer);
int  VideoCallEngine_setCameraId(int cameraId);
void VideoCallEngine_setCameraPreviewSize(int cameraSize);
void VideoCallEngine_setPreviewDisplayOrientation(int previewOrientation);
void VideoCallEngine_startPreview();
void VideoCallEngine_stopPreview();
void VideoCallEngine_setLocalSurface(sp<IGraphicBufferProducer>& aProducer);
void VideoCallEngine_setUplinkQos(int qos);
void VideoCallEngine_hideLocalImage(bool enable, char* path);

#ifdef __cplusplus
}
#endif


#endif  //_VCEENGINE_UI_H


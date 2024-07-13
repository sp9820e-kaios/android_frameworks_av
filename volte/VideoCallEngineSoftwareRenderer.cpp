#define LOG_TAG "VideoCallEngineSoftwareRenderer"
#include <utils/Log.h>

#include "VideoCallEngineSoftwareRenderer.h"

#include <cutils/properties.h> // for property_get
#include <media/stagefright/foundation/ADebug.h>
#include <media/stagefright/foundation/AMessage.h>
#include <system/window.h>
#include <ui/GraphicBufferMapper.h>
#include <gui/IGraphicBufferProducer.h>

namespace android{
static bool runningInEmulator() {
    char prop[PROPERTY_VALUE_MAX];
    return (property_get("ro.kernel.qemu", prop, NULL) > 0);
}

static int ALIGN(int x, int y) {
    // y must be a power of 2.
    return (x + y - 1) & ~(y - 1);
}

VCESoftwareRenderer::VCESoftwareRenderer(const sp<ANativeWindow> &nativeWindow)
    : mColorFormat(OMX_COLOR_FormatUnused),
      mConverter(NULL),
      mYUVMode(None),
      mNativeWindow(nativeWindow),
      mWidth(0),
      mHeight(0),
      mCropLeft(0),
      mCropTop(0),
      mCropRight(0),
      mCropBottom(0),
      mCropWidth(0),
      mCropHeight(0) {
#ifdef ANDROID_VERSION_N
    int err = native_window_api_connect(mNativeWindow.get(),
                    NATIVE_WINDOW_API_MEDIA);
    if (err != 0){
        ALOGE("api connect error %d", err);
    }
#endif
ALOGI("VCESoftwareRenderer built");
}

VCESoftwareRenderer::~VCESoftwareRenderer() {
#ifdef ANDROID_VERSION_N
    int err = native_window_api_disconnect(mNativeWindow.get(),
            NATIVE_WINDOW_API_MEDIA);
    if (err != 0){
        ALOGE("api disconnect error %d", err);
    }
#endif
    delete mConverter;
    mConverter = NULL;
}

void VCESoftwareRenderer::resetFormatIfChanged(const sp<AMessage> &format) {
    CHECK(format != NULL);

    int32_t colorFormatNew;
    CHECK(format->findInt32("color-format", &colorFormatNew));

    int32_t widthNew, heightNew;
    CHECK(format->findInt32("width", &widthNew));
    CHECK(format->findInt32("height", &heightNew));
    ALOGE("wenqil::resetFormatIfChanged start");

    int32_t cropLeftNew, cropTopNew, cropRightNew, cropBottomNew;
    if (!format->findRect(
            "crop", &cropLeftNew, &cropTopNew, &cropRightNew, &cropBottomNew)) {
        cropLeftNew = cropTopNew = 0;
        cropRightNew = widthNew - 1;
        cropBottomNew = heightNew - 1;
    }

    if (static_cast<int32_t>(mColorFormat) == colorFormatNew &&
        mWidth == widthNew &&
        mHeight == heightNew &&
        mCropLeft == cropLeftNew &&
        mCropTop == cropTopNew &&
        mCropRight == cropRightNew &&
        mCropBottom == cropBottomNew) {
        // Nothing changed, no need to reset renderer.
        int32_t transform;
        if (!format->findInt32("transform", &transform)) {
            transform = 0;
        }
        if (mTransform != transform) {
            mTransform = transform;
            CHECK_EQ(0, native_window_set_buffers_transform(mNativeWindow.get(), transform));
        }
        return;
    }

    mColorFormat = static_cast<OMX_COLOR_FORMATTYPE>(colorFormatNew);
    mWidth = widthNew;
    mHeight = heightNew;
    mCropLeft = cropLeftNew;
    mCropTop = cropTopNew;
    mCropRight = cropRightNew;
    mCropBottom = cropBottomNew;

    mCropWidth = mCropRight - mCropLeft + 1;
    mCropHeight = mCropBottom - mCropTop + 1;

    int halFormat;
    size_t bufWidth, bufHeight;

    switch (mColorFormat) {
        case OMX_COLOR_FormatYUV420Planar:
        case OMX_TI_COLOR_FormatYUV420PackedSemiPlanar:
        case OMX_COLOR_FormatYUV420SemiPlanar:
        {
            if (!runningInEmulator()) {
                halFormat = HAL_PIXEL_FORMAT_YV12;
                bufWidth = (mCropWidth + 1) & ~1;
                bufHeight = (mCropHeight + 1) & ~1;
                break;
            }

            // fall through.
        }

        default:
            halFormat = HAL_PIXEL_FORMAT_RGB_565;
            bufWidth = mCropWidth;
            bufHeight = mCropHeight;

            mConverter = new ColorConverter(
                    mColorFormat, OMX_COLOR_Format16bitRGB565);
            CHECK(mConverter->isValid());
            break;
    }

    CHECK(mNativeWindow != NULL);
    CHECK(mCropWidth > 0);
    CHECK(mCropHeight > 0);
    CHECK(mConverter == NULL || mConverter->isValid());

    CHECK_EQ(0,
            native_window_set_usage(
            mNativeWindow.get(),
            GRALLOC_USAGE_SW_READ_NEVER | GRALLOC_USAGE_SW_WRITE_OFTEN
            | GRALLOC_USAGE_HW_TEXTURE | GRALLOC_USAGE_EXTERNAL_DISP));

    CHECK_EQ(0,
            native_window_set_scaling_mode(
            mNativeWindow.get(),
            NATIVE_WINDOW_SCALING_MODE_SCALE_TO_WINDOW));

    // Width must be multiple of 32???
    CHECK_EQ(0, native_window_set_buffers_dimensions(
                mNativeWindow.get(),
                bufWidth,
                bufHeight));
    CHECK_EQ(0, native_window_set_buffers_format(
                mNativeWindow.get(),
                halFormat));

    // NOTE: native window uses extended right-bottom coordinate
    android_native_rect_t crop;
    crop.left = mCropLeft;
    crop.top = mCropTop;
    crop.right = mCropRight + 1;
    crop.bottom = mCropBottom + 1;
    ALOGV("setting crop: [%d, %d, %d, %d] for size [%zu, %zu]",
          crop.left, crop.top, crop.right, crop.bottom, bufWidth, bufHeight);

    CHECK_EQ(0, native_window_set_crop(mNativeWindow.get(), &crop));

    int32_t transform;
    if (!format->findInt32("transform", &transform)) {
        transform = 0;
    }
    mTransform = transform;
    CHECK_EQ(0, native_window_set_buffers_transform(
                mNativeWindow.get(), transform));
    ALOGI("resetFormatIfChanged end %#x", mColorFormat);
}

void VCESoftwareRenderer::render(
        const void *data, size_t size, int64_t timestampNs,
        void* /*platformPrivate*/, const sp<AMessage>& format) {
    resetFormatIfChanged(format);

    ANativeWindowBuffer *buf;
    int err;
    if(!data){
        ALOGE("VCESoftwareRenderer render error, data:%p, size:%d, time:%llu, mColorFormat:%#x", data, size, timestampNs, mColorFormat);
        return;
    }
    if ((err = native_window_dequeue_buffer_and_wait(mNativeWindow.get(),
            &buf)) != 0) {
        ALOGW("Surface::dequeueBuffer returned error %d", err);
        return;
    }

    GraphicBufferMapper &mapper = GraphicBufferMapper::get();

    Rect bounds(mCropWidth, mCropHeight);

    void *dst;
    CHECK_EQ(0, mapper.lock(
                buf->handle, GRALLOC_USAGE_SW_WRITE_OFTEN, bounds, &dst));

    if (mConverter) {
        mConverter->convert(
                data,
                mWidth, mHeight,
                mCropLeft, mCropTop, mCropRight, mCropBottom,
                dst,
                buf->stride, buf->height,
                0, 0, mCropWidth - 1, mCropHeight - 1);
    } else if (mColorFormat == OMX_COLOR_FormatYUV420Planar) {
        if ((size_t)mWidth * mHeight * 3 / 2 > size) {
            goto skip_copying;
        }
        const uint8_t *src_y = (const uint8_t *)data;
        const uint8_t *src_u = (const uint8_t *)data + mWidth * mHeight;
        const uint8_t *src_v = src_u + (mWidth / 2 * mHeight / 2);

        uint8_t *dst_y = (uint8_t *)dst;
        size_t dst_y_size = buf->stride * buf->height;
        size_t dst_c_stride = ALIGN(buf->stride / 2, 16);
        size_t dst_c_size = dst_c_stride * buf->height / 2;
        uint8_t *dst_v = dst_y + dst_y_size;
        uint8_t *dst_u = dst_v + dst_c_size;

        for (int y = 0; y < mCropHeight; ++y) {
            memcpy(dst_y, src_y, mCropWidth);

            src_y += mWidth;
            dst_y += buf->stride;
        }

        for (int y = 0; y < (mCropHeight + 1) / 2; ++y) {
            memcpy(dst_u, src_u, (mCropWidth + 1) / 2);
            memcpy(dst_v, src_v, (mCropWidth + 1) / 2);

            src_u += mWidth / 2;
            src_v += mWidth / 2;
            dst_u += dst_c_stride;
            dst_v += dst_c_stride;
        }
    } else if (mColorFormat == OMX_TI_COLOR_FormatYUV420PackedSemiPlanar
            || mColorFormat == OMX_COLOR_FormatYUV420SemiPlanar) {
        if ((size_t)mWidth * mHeight * 3 / 2 > size) {
            goto skip_copying;
        }

        const uint8_t *src_y =
            (const uint8_t *)data;

        const uint8_t *src_uv =
            (const uint8_t *)data + mWidth * (mHeight - mCropTop / 2);

        uint8_t *dst_y = (uint8_t *)dst;

        size_t dst_y_size = buf->stride * buf->height;
        size_t dst_c_stride = ALIGN(buf->stride / 2, 16);
        size_t dst_c_size = dst_c_stride * buf->height / 2;
        uint8_t *dst_v = dst_y + dst_y_size;
        uint8_t *dst_u = dst_v + dst_c_size;

        for (int y = 0; y < mCropHeight; ++y) {
            memcpy(dst_y, src_y, mCropWidth);

            src_y += mWidth;
            dst_y += buf->stride;
        }

        for (int y = 0; y < (mCropHeight + 1) / 2; ++y) {
            size_t tmp = (mCropWidth + 1) / 2;
            for (size_t x = 0; x < tmp; ++x) {
                dst_u[x] = src_uv[2 * x];
                dst_v[x] = src_uv[2 * x + 1];
            }

            src_uv += mWidth;
            dst_u += dst_c_stride;
            dst_v += dst_c_stride;
        }
    } else {
        ALOGW("bad color format %#x", mColorFormat);
    }

skip_copying:
    CHECK_EQ(0, mapper.unlock(buf->handle));

    if ((err = native_window_set_buffers_timestamp(mNativeWindow.get(),
            timestampNs)) != 0) {
        ALOGW("Surface::set_buffers_timestamp returned error %d", err);
    }

    if ((err = mNativeWindow->queueBuffer(mNativeWindow.get(), buf,
            -1)) != 0) {
        ALOGW("Surface::queueBuffer returned error %d", err);
    }
    buf = NULL;
}

}  // namespace zmf

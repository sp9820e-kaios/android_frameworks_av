#ifndef VCE_SOFTWARE_RENDERER_H_
#define VCE_SOFTWARE_RENDERER_H_

#include <media/stagefright/ColorConverter.h>
#include <utils/RefBase.h>
#include <system/window.h>

namespace android{
struct AMessage;

class VCESoftwareRenderer {
public:
    explicit VCESoftwareRenderer(const sp<ANativeWindow> &nativeWindow);

    ~VCESoftwareRenderer();

    void render(
            const void *data, size_t size, int64_t timestampNs,
            void *platformPrivate, const sp<AMessage> &format);

private:
    enum YUVMode {
        None,
    };

    OMX_COLOR_FORMATTYPE mColorFormat;
    ColorConverter *mConverter;
    YUVMode mYUVMode;
    sp<ANativeWindow> mNativeWindow;
    int32_t mWidth, mHeight;
    int32_t mCropLeft, mCropTop, mCropRight, mCropBottom;
    int32_t mCropWidth, mCropHeight;
    int32_t mTransform;

    VCESoftwareRenderer(const VCESoftwareRenderer &);
    VCESoftwareRenderer &operator=(const VCESoftwareRenderer &);

    void resetFormatIfChanged(const sp<AMessage> &format);
};

}

#endif  // VCE_SOFTWARE_RENDERER_H_


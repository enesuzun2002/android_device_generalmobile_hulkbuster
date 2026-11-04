/*
 * H.264 encoder OMX component backed by the MediaTek MT6592 hardware encoder
 * (libvcodecdrv.so eVEncDrv*), loaded at runtime.
 */
#ifndef SOFT_HULK_AVC_ENC_H_
#define SOFT_HULK_AVC_ENC_H_

#include <media/stagefright/foundation/ABase.h>
#include <media/stagefright/omx/SoftVideoEncoderOMXComponent.h>

#include <stdint.h>

namespace android {

struct SoftHulkAVCEnc : public SoftVideoEncoderOMXComponent {
    SoftHulkAVCEnc(const char *name, const OMX_CALLBACKTYPE *callbacks,
                   OMX_PTR appData, OMX_COMPONENTTYPE **component);

    virtual OMX_ERRORTYPE internalGetParameter(OMX_INDEXTYPE index, OMX_PTR params);
    virtual OMX_ERRORTYPE internalSetParameter(OMX_INDEXTYPE index, const OMX_PTR params);
    virtual OMX_ERRORTYPE setConfig(OMX_INDEXTYPE index, const OMX_PTR params);
    virtual OMX_ERRORTYPE getExtensionIndex(const char *name, OMX_INDEXTYPE *index);
    virtual void onQueueFilled(OMX_U32 portIndex);

protected:
    virtual ~SoftHulkAVCEnc();
    virtual void onReset();

private:
    enum { kNumInputBuffers = 4, kNumOutputBuffers = 4 };

    struct ValMem {
        uint32_t type, size;
        void *va;
        uint32_t mva, align;
        void *alignVa;
        uint32_t alignMva, codec, r32, r36;
        void *m4u;
        uint32_t r44;
    };

    bool initEncoder();
    void releaseEncoder();
    // copies a planar/semi-planar frame into the padded hardware input buffer
    void fillPadded(const uint8_t *y, size_t yStride, const uint8_t *u, const uint8_t *v,
                    size_t cStride, size_t cStep);
    bool convertMdp(const uint8_t *src, size_t srcLen);
    bool encodeFrame(uint32_t type, size_t *len, bool *key);
    uint32_t drvLevel() const;
    uint32_t drvProfile() const;

    void *mHandle;
    bool mStarted;
    bool mHeaderSent;
    bool mSawInputEOS;
    bool mSignalledError;
    bool mPrependSpsPps;

    ValMem mIn, mBs;
    bool mInAlloc, mBsAlloc;
    uint32_t mInSize, mBsCap;
    int32_t mBw, mBh;
    void *mMdp;
    bool mMdpFailed;
    int mMdpErrors;
    uint8_t *mConv;      // tight I420 scratch for gralloc-metadata input
    size_t mConvSize;

    OMX_VIDEO_AVCPROFILETYPE mProfile;
    OMX_VIDEO_AVCLEVELTYPE mLevel;
    uint32_t mIntraPeriod;   // in frames, 0 = first frame only
    bool mCabac;
    uint32_t mPendingBitrate;
    bool mBitrateDirty;
    bool mKeyFrameRequest;
    uint32_t mFrames;
    double mTimeEnc, mTimeConv;

    DISALLOW_EVIL_CONSTRUCTORS(SoftHulkAVCEnc);
};

}  // namespace android

#endif  // SOFT_HULK_AVC_ENC_H_

/*
 * H.264 encoder OMX component backed by the MediaTek MT6592 hardware encoder.
 *
 * Drives libvcodecdrv.so (eVEncDrv*) / libvcodec_utility.so directly, loaded at
 * runtime. The hardware only accepts planar I420 (or YV12) in an M4U-mapped
 * buffer whose planes are padded to 16 pixels, so every input frame is converted
 * (gralloc RGBA/YUV via the stock SoftVideoEncoderOMXComponent helpers, NV12 by
 * de-interleaving) straight into a driver-allocated buffer.
 * eVEncDrvQueryCapability(1) must never be called (anti-tamper stall of ~9 s).
 */

//#define LOG_NDEBUG 0
#define LOG_TAG "SoftHulkAVCEnc"
#include <utils/Log.h>

#include "SoftHulkAVCEnc.h"

#include <media/stagefright/foundation/ADebug.h>
#include <media/stagefright/MediaDefs.h>
#include <media/hardware/HardwareAPI.h>
#include <OMX_VideoExt.h>
#include <ui/Fence.h>
#include <system/graphics.h>
#include <cutils/native_handle.h>
#include <nativebase/nativebase.h>

#include <dlfcn.h>
#include <pthread.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

namespace android {

static const uint32_t kMemType = 2;  // uncached, as validated with venctest
static const uint32_t kPrependIndex = 0x7F00AA01;

static const CodecProfileLevel kProfileLevels[] = {
    { OMX_VIDEO_AVCProfileConstrainedBaseline, OMX_VIDEO_AVCLevel5 },
    { OMX_VIDEO_AVCProfileBaseline, OMX_VIDEO_AVCLevel5 },
    { OMX_VIDEO_AVCProfileMain,     OMX_VIDEO_AVCLevel5 },
    { OMX_VIDEO_AVCProfileHigh,     OMX_VIDEO_AVCLevel5 },
};

template<class T>
static void InitOMXParams(T *params) {
    params->nSize = sizeof(T);
    params->nVersion.s.nVersionMajor = 1;
    params->nVersion.s.nVersionMinor = 0;
    params->nVersion.s.nRevision = 0;
    params->nVersion.s.nStep = 0;
}

static double nowSec() { timespec t; clock_gettime(CLOCK_MONOTONIC, &t); return t.tv_sec + t.tv_nsec * 1e-9; }

// ---------------------------------------------------------------------------
// runtime-loaded driver entry points
// ---------------------------------------------------------------------------
typedef int (*pfnCreate)(void **h, uint32_t codec);
typedef int (*pfnSet)(void *h, uint32_t type, void *p, void *p2);
typedef int (*pfnInit)(void *h);
typedef int (*pfnEnc)(void *h, uint32_t type, void *in, void *bs, void *res);
typedef int (*pfnOne)(void *h);
typedef int (*pfnMem)(void *m, uint32_t sz);
typedef int (*pfnCache)(void *m, uint32_t sz, uint32_t mode);

static struct {
    pfnCreate create;
    pfnSet set;
    pfnInit init;
    pfnEnc encode;
    pfnOne deinit, release;
    pfnMem alloc, free;
    pfnCache flush, inval;
    bool ok;
} gDrv;

static pthread_once_t gOnce = PTHREAD_ONCE_INIT;

static void *openLib(const char *name) {
    char path[128];
    static const char *dirs[] = { "/vendor/lib/", "/system/lib/", "" };
    for (size_t i = 0; i < sizeof(dirs) / sizeof(dirs[0]); i++) {
        snprintf(path, sizeof(path), "%s%s", dirs[i], name);
        void *h = dlopen(path, RTLD_NOW | RTLD_GLOBAL);
        if (h) return h;
    }
    ALOGE("dlopen(%s) failed: %s", name, dlerror());
    return NULL;
}

static void loadDriverOnce() {
    void *dl = openLib("libvcodecdrv.so");
    void *ut = openLib("libvcodec_utility.so");
    if (!dl || !ut) return;
    gDrv.create = (pfnCreate)dlsym(dl, "eVEncDrvCreate");
    gDrv.set = (pfnSet)dlsym(dl, "eVEncDrvSetParam");
    gDrv.init = (pfnInit)dlsym(dl, "eVEncDrvInit");
    gDrv.encode = (pfnEnc)dlsym(dl, "eVEncDrvEncode");
    gDrv.deinit = (pfnOne)dlsym(dl, "eVEncDrvDeInit");
    gDrv.release = (pfnOne)dlsym(dl, "eVEncDrvRelease");
    gDrv.alloc = (pfnMem)dlsym(ut, "eVideoMemAlloc");
    gDrv.free = (pfnMem)dlsym(ut, "eVideoMemFree");
    gDrv.flush = (pfnCache)dlsym(ut, "eVideoFlushCache");
    gDrv.inval = (pfnCache)dlsym(ut, "eVideoInvalidateCache");
    gDrv.ok = gDrv.create && gDrv.set && gDrv.init && gDrv.encode && gDrv.deinit &&
              gDrv.release && gDrv.alloc && gDrv.free && gDrv.flush && gDrv.inval;
    if (!gDrv.ok) ALOGE("missing driver symbols");
}


// ---------------------------------------------------------------------------
// MT6592 MDP (libdpframework DpBlitStream), loaded at runtime, used to convert the
// gralloc frames of surface input (RGBA from screenrecord/GL, YV12/NV21 from the
// camera) to the I420 the encoder wants without touching the pixels on the CPU.
// Constants follow kernel/.../video/DpDataType.h.
// ---------------------------------------------------------------------------
struct DpRect { int x, subX, y, subY, w, h; };
typedef void (*pfnDpCtor)(void *self);
typedef int (*pfnDpSetSrcFd)(void *self, int fd, unsigned *sizes, unsigned n);
typedef int (*pfnDpSetDstVaMva)(void *self, void **va, void **mva, unsigned *sizes, unsigned n);
typedef int (*pfnDpCfg)(void *self, int w, int h, int yPitch, int uvPitch, uint32_t fmt,
                        int profile, int interlace, DpRect *roi, int secure, bool flush);
typedef int (*pfnDpRun)(void *self);

static struct {
    pfnDpCtor ctor, dtor;
    pfnDpSetSrcFd setSrcFd;
    pfnDpSetDstVaMva setDst;
    pfnDpCfg setSrcCfg, setDstCfg;
    pfnDpRun run;
    bool ok;
} gDp;
static pthread_once_t gDpOnce = PTHREAD_ONCE_INIT;
static const size_t kDpObjSize = 1024;       // sizeof(DpBlitStream) is ~190 bytes
static const uint32_t kDpRGBA8888 = 0x01002022;
static const uint32_t kDpBGRA8888 = 0x01002002;
static const uint32_t kDpI420 = 0x03140848;
static const uint32_t kDpYV12 = 0x03140868;
static const uint32_t kDpNV21 = 0x0254086c;
static const uint32_t kGrallocMagic = 0x3141592;  // MTK private_handle_t magic

static void loadDpOnce() {
    void *dl = dlopen("libdpframework.so", RTLD_NOW);
    if (!dl) { ALOGW("dlopen(libdpframework.so) failed: %s", dlerror()); return; }
    gDp.ctor = (pfnDpCtor)dlsym(dl, "_ZN12DpBlitStreamC1Ev");
    gDp.dtor = (pfnDpCtor)dlsym(dl, "_ZN12DpBlitStreamD1Ev");
    gDp.setSrcFd = (pfnDpSetSrcFd)dlsym(dl, "_ZN12DpBlitStream12setSrcBufferEiPjj");
    gDp.setDst = (pfnDpSetDstVaMva)dlsym(dl, "_ZN12DpBlitStream12setDstBufferEPPvS1_Pjj");
    gDp.setSrcCfg = (pfnDpCfg)dlsym(dl, "_ZN12DpBlitStream12setSrcConfigEiiii13DP_COLOR_ENUM"
            "15DP_PROFILE_ENUM17DpInterlaceFormatP6DpRect8DpSecureb");
    gDp.setDstCfg = (pfnDpCfg)dlsym(dl, "_ZN12DpBlitStream12setDstConfigEiiii13DP_COLOR_ENUM"
            "15DP_PROFILE_ENUM17DpInterlaceFormatP6DpRect8DpSecureb");
    gDp.run = (pfnDpRun)dlsym(dl, "_ZN12DpBlitStream10invalidateEv");
    gDp.ok = gDp.ctor && gDp.dtor && gDp.setSrcFd && gDp.setDst && gDp.setSrcCfg &&
             gDp.setDstCfg && gDp.run;
    if (!gDp.ok) ALOGW("missing DpBlitStream symbols");
}

// Returns 1 on success, 0 if the buffer is not suitable (use the CPU path).
// Consumes the fence of ANW metadata.
bool SoftHulkAVCEnc::convertMdp(const uint8_t *src, size_t srcLen) {
    if (mMdpFailed) return false;
    pthread_once(&gDpOnce, loadDpOnce);
    if (!gDp.ok) { mMdpFailed = true; return false; }
    if (srcLen < sizeof(VideoNativeMetadata) || *(const MetadataBufferType *)src != kMetadataBufferTypeANWBuffer) {
        return false;
    }
    VideoNativeMetadata &meta = *(VideoNativeMetadata *)src;
    ANativeWindowBuffer *buf = meta.pBuffer;
    if (buf == NULL || buf->handle == NULL) return false;
    const native_handle_t *h = buf->handle;
    if (h->numFds < 1 || h->numInts < 17 || (uint32_t)h->data[1] != kGrallocMagic) return false;
    const int fd = h->data[0];
    const int stride = buf->stride;

    uint32_t srcFmt;
    int yPitch, uvPitch;
    unsigned sizes[3] = { 0, 0, 0 };
    unsigned n = 1;
    switch (buf->format) {
        case HAL_PIXEL_FORMAT_RGBA_8888:
        case HAL_PIXEL_FORMAT_RGBX_8888:
            srcFmt = kDpRGBA8888; yPitch = stride * 4; uvPitch = 0;
            sizes[0] = yPitch * buf->height;
            break;
        case HAL_PIXEL_FORMAT_BGRA_8888:
            srcFmt = kDpBGRA8888; yPitch = stride * 4; uvPitch = 0;
            sizes[0] = yPitch * buf->height;
            break;
        case HAL_PIXEL_FORMAT_YV12: {
            srcFmt = kDpYV12; yPitch = stride; uvPitch = ((stride / 2) + 15) & ~15;
            sizes[0] = yPitch * buf->height;
            sizes[1] = sizes[2] = uvPitch * (buf->height / 2);
            n = 3;
            break;
        }
        case HAL_PIXEL_FORMAT_YCrCb_420_SP:
            srcFmt = kDpNV21; yPitch = stride; uvPitch = stride;
            sizes[0] = yPitch * buf->height;
            sizes[1] = yPitch * buf->height / 2;
            n = 2;
            break;
        default:
            return false;
    }
    if (buf->width < mWidth || buf->height < mHeight) return false;

    if (meta.nFenceFd >= 0) {
        sp<Fence> fence = new Fence(meta.nFenceFd);
        meta.nFenceFd = -1;
        if (fence->wait(1000) != OK) { ALOGE("timed out waiting on input fence"); return false; }
    }

    if (!mMdp) {
        if (posix_memalign(&mMdp, 16, kDpObjSize) != 0) { mMdp = NULL; mMdpFailed = true; return false; }
        memset(mMdp, 0, kDpObjSize);
        gDp.ctor(mMdp);
    }
    uint8_t *base = (uint8_t *)mIn.alignVa;
    const size_t ySize = (size_t)mBw * mBh, cSize = ySize / 4;
    void *va[3] = { base, base + ySize, base + ySize + cSize };
    void *mva[3] = { (void *)(uintptr_t)mIn.alignMva, (void *)(uintptr_t)(mIn.alignMva + ySize),
                     (void *)(uintptr_t)(mIn.alignMva + ySize + cSize) };
    unsigned dstSizes[3] = { (unsigned)ySize, (unsigned)cSize, (unsigned)cSize };
    DpRect roi = { 0, 0, 0, 0, mWidth, mHeight };

    int r = gDp.setSrcFd(mMdp, fd, sizes, n);
    if (r == 0) r = gDp.setSrcCfg(mMdp, buf->width, buf->height, yPitch, uvPitch, srcFmt, 0, 0, &roi, 0, true);
    if (r == 0) r = gDp.setDst(mMdp, va, mva, dstSizes, 3);
    if (r == 0) r = gDp.setDstCfg(mMdp, mWidth, mHeight, mBw, mBw / 2, kDpI420, 0, 0, NULL, 0, true);
    if (r == 0) r = gDp.run(mMdp);
    if (r != 0) {
        ALOGW("MDP conversion failed (%d), using the CPU path", r);
        if (++mMdpErrors >= 3) mMdpFailed = true;
        return false;
    }
    return true;
}

// OMX level -> {level_idc, driver level code}
static const struct { OMX_VIDEO_AVCLEVELTYPE omx; uint32_t idc; uint32_t drv; uint32_t maxFs; uint32_t maxMbps; } kLevels[] = {
    { OMX_VIDEO_AVCLevel1,  10,  2,    99,    1485 },
    { OMX_VIDEO_AVCLevel1b,  9,  3,    99,    1485 },
    { OMX_VIDEO_AVCLevel11, 11,  4,   396,    3000 },
    { OMX_VIDEO_AVCLevel12, 12,  5,   396,    6000 },
    { OMX_VIDEO_AVCLevel13, 13,  6,   396,   11880 },
    { OMX_VIDEO_AVCLevel2,  20,  7,   396,   11880 },
    { OMX_VIDEO_AVCLevel21, 21,  8,   792,   19800 },
    { OMX_VIDEO_AVCLevel22, 22,  9,  1620,   20250 },
    { OMX_VIDEO_AVCLevel3,  30, 10,  1620,   40500 },
    { OMX_VIDEO_AVCLevel31, 31, 11,  3600,  108000 },
    { OMX_VIDEO_AVCLevel32, 32, 12,  5120,  216000 },
    { OMX_VIDEO_AVCLevel4,  40, 13,  8192,  245760 },
    { OMX_VIDEO_AVCLevel41, 41, 14,  8192,  245760 },
    { OMX_VIDEO_AVCLevel42, 42, 15,  8704,  522240 },
    { OMX_VIDEO_AVCLevel5,  50, 16, 22080,  589824 },
    { OMX_VIDEO_AVCLevel51, 51, 17, 36864,  983040 },
};
static const size_t kNumLevels = sizeof(kLevels) / sizeof(kLevels[0]);

SoftHulkAVCEnc::SoftHulkAVCEnc(
        const char *name, const OMX_CALLBACKTYPE *callbacks,
        OMX_PTR appData, OMX_COMPONENTTYPE **component)
    : SoftVideoEncoderOMXComponent(
            name, "video_encoder.avc", OMX_VIDEO_CodingAVC,
            kProfileLevels, sizeof(kProfileLevels) / sizeof(kProfileLevels[0]),
            176, 144, callbacks, appData, component),
      mHandle(NULL), mStarted(false), mHeaderSent(false), mSawInputEOS(false),
      mSignalledError(false), mPrependSpsPps(false),
      mInAlloc(false), mBsAlloc(false), mInSize(0), mBsCap(0), mBw(0), mBh(0),
      mMdp(NULL), mMdpFailed(false), mMdpErrors(0), mConv(NULL), mConvSize(0),
      mProfile(OMX_VIDEO_AVCProfileBaseline), mLevel(OMX_VIDEO_AVCLevel41),
      mIntraPeriod(30), mCabac(false), mPendingBitrate(0), mBitrateDirty(false),
      mKeyFrameRequest(false), mFrames(0), mTimeEnc(0), mTimeConv(0) {
    memset(&mIn, 0, sizeof(mIn));
    memset(&mBs, 0, sizeof(mBs));
    initPorts(kNumInputBuffers, kNumOutputBuffers, (mWidth * mHeight * 3) >> 1,
              MEDIA_MIMETYPE_VIDEO_AVC, 2);
    pthread_once(&gOnce, loadDriverOnce);
}

SoftHulkAVCEnc::~SoftHulkAVCEnc() {
    releaseEncoder();
}

void SoftHulkAVCEnc::onReset() {
    SoftVideoEncoderOMXComponent::onReset();
    releaseEncoder();
    mHeaderSent = false;
    mSawInputEOS = false;
    mSignalledError = false;
}

// ---------------------------------------------------------------------------
// parameters
// ---------------------------------------------------------------------------
OMX_ERRORTYPE SoftHulkAVCEnc::internalGetParameter(OMX_INDEXTYPE index, OMX_PTR params) {
    switch ((int32_t)index) {
        case OMX_IndexParamVideoBitrate:
        {
            OMX_VIDEO_PARAM_BITRATETYPE *bitRate = (OMX_VIDEO_PARAM_BITRATETYPE *)params;
            if (!isValidOMXParam(bitRate)) return OMX_ErrorBadParameter;
            if (bitRate->nPortIndex != kOutputPortIndex) return OMX_ErrorUndefined;
            bitRate->eControlRate = OMX_Video_ControlRateVariable;
            bitRate->nTargetBitrate = mBitrate;
            return OMX_ErrorNone;
        }
        case OMX_IndexParamVideoAvc:
        {
            OMX_VIDEO_PARAM_AVCTYPE *avc = (OMX_VIDEO_PARAM_AVCTYPE *)params;
            if (!isValidOMXParam(avc)) return OMX_ErrorBadParameter;
            if (avc->nPortIndex != kOutputPortIndex) return OMX_ErrorUndefined;
            avc->eProfile = mProfile;
            avc->eLevel = mLevel;
            avc->nRefFrames = 1;
            avc->nPFrames = mIntraPeriod ? mIntraPeriod - 1 : 0;
            avc->nBFrames = 0;
            avc->bUseHadamard = OMX_TRUE;
            avc->nAllowedPictureTypes = OMX_VIDEO_PictureTypeI | OMX_VIDEO_PictureTypeP;
            avc->nRefIdx10ActiveMinus1 = 0;
            avc->nRefIdx11ActiveMinus1 = 0;
            avc->bWeightedPPrediction = OMX_FALSE;
            avc->bconstIpred = OMX_FALSE;
            avc->bDirect8x8Inference = OMX_FALSE;
            avc->bDirectSpatialTemporal = OMX_FALSE;
            avc->nCabacInitIdc = 0;
            return OMX_ErrorNone;
        }
        default:
            return SoftVideoEncoderOMXComponent::internalGetParameter(index, params);
    }
}

OMX_ERRORTYPE SoftHulkAVCEnc::internalSetParameter(OMX_INDEXTYPE index, const OMX_PTR params) {
    switch ((int32_t)index) {
        case OMX_IndexParamVideoBitrate:
        {
            OMX_VIDEO_PARAM_BITRATETYPE *bitRate = (OMX_VIDEO_PARAM_BITRATETYPE *)params;
            if (!isValidOMXParam(bitRate)) return OMX_ErrorBadParameter;
            if (bitRate->nPortIndex != kOutputPortIndex) return OMX_ErrorUnsupportedIndex;
            mBitrate = bitRate->nTargetBitrate;
            return OMX_ErrorNone;
        }
        case OMX_IndexParamVideoAvc:
        {
            OMX_VIDEO_PARAM_AVCTYPE *avc = (OMX_VIDEO_PARAM_AVCTYPE *)params;
            if (!isValidOMXParam(avc)) return OMX_ErrorBadParameter;
            if (avc->nPortIndex != kOutputPortIndex) return OMX_ErrorUndefined;
            mProfile = avc->eProfile;
            mLevel = avc->eLevel;
            mCabac = avc->bEntropyCodingCABAC == OMX_TRUE;
            // nPFrames + 1 = distance between IDR frames; 0xFFFFFFFF = only the first
            mIntraPeriod = avc->nPFrames == 0xFFFFFFFFu ? 0 : avc->nPFrames + 1;
            return OMX_ErrorNone;
        }
        case OMX_IndexParamPortDefinition:
        {
            // The hardware rejects frames above 8160 macroblocks (1920x1088 is
            // fine, 1920x1200 is not): refuse such a size here so that the
            // client's configure fails instead of every frame failing later.
            const OMX_PARAM_PORTDEFINITIONTYPE *def = (const OMX_PARAM_PORTDEFINITIONTYPE *)params;
            if (!isValidOMXParam(def)) return OMX_ErrorBadParameter;
            if (def->nPortIndex == kInputPortIndex) {
                const OMX_U32 w = def->format.video.nFrameWidth, h = def->format.video.nFrameHeight;
                if (w > 1920 || h > 1200 || ((w + 15) / 16) * ((h + 15) / 16) > 8160) {
                    ALOGE("unsupported size %ux%u (max 8160 macroblocks)", w, h);
                    return OMX_ErrorUnsupportedSetting;
                }
            }
            return SoftVideoEncoderOMXComponent::internalSetParameter(index, params);
        }
        case kPrependIndex:
        {
            const PrependSPSPPSToIDRFramesParams *p = (const PrependSPSPPSToIDRFramesParams *)params;
            if (!isValidOMXParam(p)) return OMX_ErrorBadParameter;
            mPrependSpsPps = p->bEnable == OMX_TRUE;
            return OMX_ErrorNone;
        }
        default:
            return SoftVideoEncoderOMXComponent::internalSetParameter(index, params);
    }
}

OMX_ERRORTYPE SoftHulkAVCEnc::getExtensionIndex(const char *name, OMX_INDEXTYPE *index) {
    if (!strcmp(name, "OMX.google.android.index.prependSPSPPSToIDRFrames")) {
        *(int32_t *)index = kPrependIndex;
        return OMX_ErrorNone;
    }
    return SoftVideoEncoderOMXComponent::getExtensionIndex(name, index);
}

OMX_ERRORTYPE SoftHulkAVCEnc::setConfig(OMX_INDEXTYPE index, const OMX_PTR params) {
    switch ((int32_t)index) {
        case OMX_IndexConfigVideoIntraVOPRefresh:
        {
            OMX_CONFIG_INTRAREFRESHVOPTYPE *p = (OMX_CONFIG_INTRAREFRESHVOPTYPE *)params;
            if (!isValidOMXParam(p)) return OMX_ErrorBadParameter;
            if (p->nPortIndex != kOutputPortIndex) return OMX_ErrorBadPortIndex;
            if (p->IntraRefreshVOP) mKeyFrameRequest = true;
            return OMX_ErrorNone;
        }
        case OMX_IndexConfigVideoBitrate:
        {
            OMX_VIDEO_CONFIG_BITRATETYPE *p = (OMX_VIDEO_CONFIG_BITRATETYPE *)params;
            if (!isValidOMXParam(p)) return OMX_ErrorBadParameter;
            if (p->nPortIndex != kOutputPortIndex) return OMX_ErrorBadPortIndex;
            if (mBitrate != p->nEncodeBitrate) {
                mBitrate = p->nEncodeBitrate;
                mBitrateDirty = true;
            }
            return OMX_ErrorNone;
        }
        default:
            return SimpleSoftOMXComponent::setConfig(index, params);
    }
}

// ---------------------------------------------------------------------------
// driver session
// ---------------------------------------------------------------------------
uint32_t SoftHulkAVCEnc::drvLevel() const {
    uint32_t fps = mFramerate >> 16;
    if (fps == 0) fps = 30;
    uint32_t mbs = ((mWidth + 15) / 16) * ((mHeight + 15) / 16);
    size_t want = 0;
    for (size_t i = 0; i < kNumLevels; i++) {
        if (kLevels[i].omx == mLevel) { want = i; break; }
    }
    size_t need = 8;  // never below level 3
    while (need < kNumLevels - 1 && (mbs > kLevels[need].maxFs || mbs * fps > kLevels[need].maxMbps)) need++;
    // the hardware driver rejects levels above 4.1 (it falls back to 3.1)
    uint32_t drv = kLevels[want > need ? want : need].drv;
    return drv > 14 ? 14 : drv;
}

uint32_t SoftHulkAVCEnc::drvProfile() const {
    switch ((int32_t)mProfile) {
        case OMX_VIDEO_AVCProfileBaseline:
        case OMX_VIDEO_AVCProfileConstrainedBaseline: return 1;
        case OMX_VIDEO_AVCProfileMain: return 4;
        default: return 16;
    }
}

bool SoftHulkAVCEnc::initEncoder() {
    if (mStarted) return true;
    if (!gDrv.ok) return false;
    if (mWidth < 16 || mHeight < 16 || mWidth > 1920 || mHeight > 1200 ||
        ((mWidth + 15) / 16) * ((mHeight + 15) / 16) > 8160) {  // hardware limit: 1080p worth of macroblocks
        ALOGE("unsupported size %dx%d", mWidth, mHeight);
        return false;
    }
    mBw = (mWidth + 15) & ~15;
    mBh = (mHeight + 15) & ~15;

    int r = gDrv.create(&mHandle, 5);
    if (r || !mHandle) { ALOGE("eVEncDrvCreate -> %d", r); mHandle = NULL; return false; }
    uint32_t emi[4] = { (uint32_t)mWidth, (uint32_t)mHeight, (uint32_t)mBw, (uint32_t)mBh };
    gDrv.set(mHandle, 20, emi, 0);
    r = gDrv.init(mHandle);
    if (r) { ALOGE("eVEncDrvInit -> %d", r); gDrv.release(mHandle); mHandle = NULL; return false; }

    uint32_t fps = mFramerate >> 16;
    if (fps == 0) fps = 30;
    uint32_t bps = mBitrate;
    if (bps < 200000) bps = 200000;
    if (bps > 60000000) bps = 60000000;
    uint32_t extra[4] = { mIntraPeriod, bps, fps << 16, 0 };
    uint32_t p[32];
    memset(p, 0, sizeof(p));
    p[0] = 3;  // I420
    p[1] = drvProfile();
    p[2] = drvLevel();
    p[3] = mWidth; p[4] = mHeight; p[5] = mBw; p[6] = mBh;
    p[7] = 29; p[8] = 0; p[9] = fps;
    p[11] = (uint32_t)(uintptr_t)extra;
    r = gDrv.set(mHandle, 6, p, 0);
    if (r) {
        ALOGE("eVEncDrvSetParam(6) -> %d", r);
        gDrv.deinit(mHandle); gDrv.release(mHandle); mHandle = NULL;
        return false;
    }
    if (mPrependSpsPps) gDrv.set(mHandle, 26, NULL, 0);
    gDrv.set(mHandle, 7, 0, 0);

    mInSize = mBw * mBh * 3 / 2;
    mBsCap = editPortInfo(kOutputPortIndex)->mDef.nBufferSize;
    if (mBsCap < 256 * 1024) mBsCap = 256 * 1024;
    mBsCap = (mBsCap + 127) & ~127u;

    mIn.type = kMemType; mIn.size = mInSize + 4096; mIn.align = 512;
    if (gDrv.alloc(&mIn, sizeof(ValMem)) != 0) { ALOGE("input alloc failed"); releaseEncoder(); return false; }
    mInAlloc = true;
    mBs.type = kMemType; mBs.size = mBsCap; mBs.align = 512;
    if (gDrv.alloc(&mBs, sizeof(ValMem)) != 0) { ALOGE("bitstream alloc failed"); releaseEncoder(); return false; }
    mBsAlloc = true;
    mStarted = true;
    mFrames = 0;
    ALOGI("hardware encoder started %dx%d (buf %dx%d) %u bps %u fps gop %u profile %u level %u",
          mWidth, mHeight, mBw, mBh, bps, fps, mIntraPeriod, p[1], p[2]);
    return true;
}

void SoftHulkAVCEnc::releaseEncoder() {
    if (mHandle) {
        if (mStarted) gDrv.set(mHandle, 8, 0, 0);
        gDrv.deinit(mHandle);
        gDrv.release(mHandle);
        mHandle = NULL;
    }
    if (mInAlloc) { gDrv.free(&mIn, sizeof(ValMem)); mInAlloc = false; }
    if (mBsAlloc) { gDrv.free(&mBs, sizeof(ValMem)); mBsAlloc = false; }
    if (mMdp) { if (gDp.ok) gDp.dtor(mMdp); free(mMdp); mMdp = NULL; }
    free(mConv);
    mConv = NULL;
    mConvSize = 0;
    if (mStarted && mFrames) {
        ALOGI("encoded %u frames: hw %.1f ms/frame, convert %.1f ms/frame", mFrames,
              mTimeEnc * 1000 / mFrames, mTimeConv * 1000 / mFrames);
    }
    mStarted = false;
}

// type 1 = SPS+PPS, 4 = frame. Result in mBs.alignVa / *len.
bool SoftHulkAVCEnc::encodeFrame(uint32_t type, size_t *len, bool *key) {
    uint32_t bs[8], res[16], frm[8];
    memset(bs, 0, sizeof(bs));
    bs[0] = (uint32_t)(uintptr_t)mBs.alignVa; bs[1] = mBs.alignMva; bs[2] = mBsCap; bs[3] = bs[0];
    memset(res, 0, sizeof(res));
    res[0] = 1;
    memset(frm, 0, sizeof(frm));
    frm[0] = (uint32_t)(uintptr_t)mIn.alignVa; frm[1] = mIn.alignMva; frm[2] = mInSize;
    frm[6] = mFrames;
    gDrv.flush(&mBs, sizeof(ValMem), 1);
    int r = gDrv.encode(mHandle, type, type == 4 ? (void *)frm : NULL, bs, res);
    gDrv.inval(&mBs, sizeof(ValMem), 1);
    *len = bs[4];
    *key = ((uint8_t *)res)[12] != 0;
    if (r) { ALOGE("eVEncDrvEncode(%u) -> %d", type, r); return false; }
    return true;
}

void SoftHulkAVCEnc::fillPadded(const uint8_t *y, size_t yStride, const uint8_t *u,
                                const uint8_t *v, size_t cStride, size_t cStep) {
    uint8_t *dy = (uint8_t *)mIn.alignVa;
    uint8_t *du = dy + (size_t)mBw * mBh;
    uint8_t *dv = du + (size_t)(mBw / 2) * (mBh / 2);
    const int cw = mWidth / 2, ch = mHeight / 2, bcw = mBw / 2, bch = mBh / 2;
    for (int r = 0; r < mBh; r++) {
        const uint8_t *s = y + (size_t)(r < mHeight ? r : mHeight - 1) * yStride;
        memcpy(dy + (size_t)r * mBw, s, mWidth);
        if (mBw > mWidth) memset(dy + (size_t)r * mBw + mWidth, s[mWidth - 1], mBw - mWidth);
    }
    for (int r = 0; r < bch; r++) {
        size_t sr = (size_t)(r < ch ? r : ch - 1) * cStride;
        uint8_t *a = du + (size_t)r * bcw, *b = dv + (size_t)r * bcw;
        if (cStep == 1) {
            memcpy(a, u + sr, cw);
            memcpy(b, v + sr, cw);
        } else {
            const uint8_t *su = u + sr, *sv = v + sr;
            for (int x = 0; x < cw; x++) { a[x] = su[x * cStep]; b[x] = sv[x * cStep]; }
        }
        for (int x = cw; x < bcw; x++) { a[x] = a[cw - 1]; b[x] = b[cw - 1]; }
    }
}

// ---------------------------------------------------------------------------
// OMX buffer flow
// ---------------------------------------------------------------------------
void SoftHulkAVCEnc::onQueueFilled(OMX_U32 /* portIndex */) {
    if (mSignalledError) return;
    if (!mStarted && !initEncoder()) {
        ALOGE("failed to initialize hardware encoder");
        mSignalledError = true;
        notify(OMX_EventError, OMX_ErrorUndefined, 0, NULL);
        return;
    }

    List<BufferInfo *> &inQueue = getPortQueue(kInputPortIndex);
    List<BufferInfo *> &outQueue = getPortQueue(kOutputPortIndex);

    while (!outQueue.empty()) {
        BufferInfo *outInfo = *outQueue.begin();
        OMX_BUFFERHEADERTYPE *outHdr = outInfo->mHeader;
        outHdr->nOffset = 0;
        outHdr->nFilledLen = 0;
        outHdr->nFlags = 0;
        outHdr->nTimeStamp = 0;

        size_t len = 0;
        bool key = false;

        if (!mHeaderSent) {
            // like SoftAVCEnc, only emit the header once the first input has arrived:
            // ACodec then sees the dataspace change before the first output buffer and
            // does not report two output format changes (screenrecord's muxer breaks)
            if (inQueue.empty()) return;
            if (!encodeFrame(1, &len, &key) || len == 0 || len > outHdr->nAllocLen) {
                ALOGE("failed to generate SPS/PPS (len %zu)", len);
                mSignalledError = true;
                notify(OMX_EventError, OMX_ErrorUndefined, 0, NULL);
                return;
            }
            memcpy(outHdr->pBuffer, mBs.alignVa, len);
            mHeaderSent = true;
            outHdr->nFlags = OMX_BUFFERFLAG_CODECCONFIG;
            outHdr->nFilledLen = len;
            outQueue.erase(outQueue.begin());
            outInfo->mOwnedByUs = false;
            notifyFillBufferDone(outHdr);
            continue;
        }

        if (inQueue.empty()) return;
        BufferInfo *inInfo = *inQueue.begin();
        OMX_BUFFERHEADERTYPE *inHdr = inInfo->mHeader;
        bool eos = (inHdr->nFlags & OMX_BUFFERFLAG_EOS) != 0;

        bool haveFrame = inHdr->nFilledLen != 0;
        if (haveFrame) {
            OMX_ERRORTYPE err = validateInputBuffer(inHdr);
            if (err != OMX_ErrorNone) {
                ALOGE("invalid input buffer (%u bytes)", inHdr->nFilledLen);
                mSignalledError = true;
                notify(OMX_EventError, OMX_ErrorUndefined, 0, NULL);
                return;
            }
            double t0 = nowSec();
            const uint8_t *src = inHdr->pBuffer + inHdr->nOffset;
            const size_t w = mWidth, h = mHeight;
            if (mInputDataIsMeta && convertMdp(src, inHdr->nFilledLen)) {
                // converted by the MDP straight into the hardware buffer
            } else if (mInputDataIsMeta) {
                const uint8_t *res;
                if (mBw == mWidth && mBh == mHeight) {
                    // planar layout is identical: convert straight into the hardware buffer
                    res = extractGraphicBuffer((uint8_t *)mIn.alignVa, w * h * 3 / 2, src,
                                               inHdr->nFilledLen, w, h);
                } else {
                    if (!mConv) {
                        mConvSize = w * h * 3 / 2;
                        mConv = (uint8_t *)malloc(mConvSize);
                    }
                    res = mConv ? extractGraphicBuffer(mConv, mConvSize, src, inHdr->nFilledLen, w, h)
                                : NULL;
                    if (res) fillPadded(mConv, w, mConv + w * h, mConv + w * h + (w / 2) * (h / 2),
                                        w / 2, 1);
                }
                if (!res) {
                    ALOGE("extractGraphicBuffer failed");
                    mSignalledError = true;
                    notify(OMX_EventError, OMX_ErrorUndefined, 0, NULL);
                    return;
                }
            } else if (mColorFormat == OMX_COLOR_FormatYUV420SemiPlanar) {
                const uint8_t *uv = src + w * h;
                fillPadded(src, w, uv, uv + 1, w, 2);
            } else {
                fillPadded(src, w, src + w * h, src + w * h + (w / 2) * (h / 2), w / 2, 1);
            }
            double t1 = nowSec();
            mTimeConv += t1 - t0;

            if (mBitrateDirty) {
                uint32_t bp[4] = { 0, mBitrate < 200000 ? 200000 : (mBitrate > 60000000 ? 60000000 : mBitrate), 0, 0 };
                gDrv.set(mHandle, 17, bp, 0);
                mBitrateDirty = false;
            }
            if (mKeyFrameRequest) {
                gDrv.set(mHandle, 11, NULL, 0);
                mKeyFrameRequest = false;
            }
            bool ok = encodeFrame(4, &len, &key);
            mTimeEnc += nowSec() - t1;
            if (!ok || len > outHdr->nAllocLen) {
                ALOGE("encode failed (len %zu, out %u)", len, outHdr->nAllocLen);
                mSignalledError = true;
                notify(OMX_EventError, OMX_ErrorUndefined, 0, NULL);
                return;
            }
            mFrames++;
        }

        // input consumed (the hardware encode is synchronous)
        inQueue.erase(inQueue.begin());
        inInfo->mOwnedByUs = false;
        int64_t ts = inHdr->nTimeStamp;
        notifyEmptyBufferDone(inHdr);

        if (len == 0 && !eos) continue;  // dropped frame, output buffer stays queued

        if (len) memcpy(outHdr->pBuffer, mBs.alignVa, len);
        outHdr->nFilledLen = len;
        outHdr->nTimeStamp = ts;
        outHdr->nFlags = (key ? OMX_BUFFERFLAG_SYNCFRAME : 0) | (eos ? OMX_BUFFERFLAG_EOS : 0);
        outQueue.erase(outQueue.begin());
        outInfo->mOwnedByUs = false;
        notifyFillBufferDone(outHdr);
        if (eos) mSawInputEOS = true;
    }
}

}  // namespace android

android::SoftOMXComponent *createHulkAVCEncoder(
        const char *name, const OMX_CALLBACKTYPE *callbacks,
        OMX_PTR appData, OMX_COMPONENTTYPE **component) {
    return new android::SoftHulkAVCEnc(name, callbacks, appData, component);
}

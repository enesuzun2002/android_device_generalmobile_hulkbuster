/*
 * H.264 decoder OMX component backed by the MediaTek MT6592 hardware decoder.
 *
 * The decoder is driven through libvcodecdrv.so / libvcodec_utility.so which
 * are dlopen()ed at runtime. Output frames come out of the hardware in MTK
 * "block" format and are converted to I420 by a small worker thread so that
 * the conversion of frame N overlaps with the hardware decoding of frame N+1.
 */

//#define LOG_NDEBUG 0
#define LOG_TAG "SoftHulkAVCDec"
#include <utils/Log.h>

#include "SoftHulkAVCDec.h"
#include "HulkDetile.h"

#include <media/stagefright/foundation/ADebug.h>
#include <media/stagefright/MediaDefs.h>
#include <OMX_VideoExt.h>
#include <media/hardware/HardwareAPI.h>
#include <ui/GraphicBufferMapper.h>
#include <ui/Rect.h>
#include <system/graphics.h>
#include <hardware/gralloc.h>
#include <cutils/native_handle.h>

#include <cutils/properties.h>
#include <dlfcn.h>
#include <stdio.h>
#include <pthread.h>
#include <string.h>
#include <time.h>

namespace android {

static double nowSec() { timespec t; clock_gettime(CLOCK_MONOTONIC, &t); return t.tv_sec + t.tv_nsec * 1e-9; }
static double gTDec, gTWait, gTDet, gLastFbd, gMaxGap, gBusy, gInval, gOQ, gDrain, gPre, gDeliver; static unsigned gN, gQCalls, gNoOut, gNoIn, gGap50, gDecN;

#define componentName "video_decoder.avc"
#define codingType    OMX_VIDEO_CodingAVC

static const uint32_t kBsSize = 2 * 1024 * 1024;
static const uint32_t kMemType = 2;  // as validated with vdectest; CPU view is cached, we invalidate explicitly
static const int kMaxFbs = 24;

static const CodecProfileLevel kProfileLevels[] = {
    { OMX_VIDEO_AVCProfileConstrainedBaseline, OMX_VIDEO_AVCLevel42 },
    { OMX_VIDEO_AVCProfileBaseline, OMX_VIDEO_AVCLevel42 },
    { OMX_VIDEO_AVCProfileMain,     OMX_VIDEO_AVCLevel42 },
    { OMX_VIDEO_AVCProfileConstrainedHigh, OMX_VIDEO_AVCLevel42 },
    { OMX_VIDEO_AVCProfileHigh,     OMX_VIDEO_AVCLevel42 },
};

// ---------------------------------------------------------------------------
// runtime-loaded driver entry points
// ---------------------------------------------------------------------------
typedef int (*pfnCreate)(void **h, uint32_t codec);
typedef int (*pfnSet)(void *h, uint32_t type, void *p, void *p2);
typedef int (*pfnInit)(void *h, void *bs, void *out);
typedef int (*pfnDecode)(void *h, void *bs, void *fb);
typedef int (*pfnGet)(void *h, uint32_t type, void *p, void *out);
typedef int (*pfnOne)(void *h);
typedef int (*pfnMem)(void *m, uint32_t sz);
typedef int (*pfnCache)(void *m, uint32_t sz, uint32_t mode);

static struct {
    pfnCreate create;
    pfnSet set;
    pfnInit init;
    pfnDecode decode;
    pfnGet get;
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
    gDrv.create = (pfnCreate)dlsym(dl, "eVDecDrvCreate");
    gDrv.set = (pfnSet)dlsym(dl, "eVDecDrvSetParam");
    gDrv.init = (pfnInit)dlsym(dl, "eVDecDrvInit");
    gDrv.decode = (pfnDecode)dlsym(dl, "eVDecDrvDecode");
    gDrv.get = (pfnGet)dlsym(dl, "eVDecDrvGetParam");
    gDrv.deinit = (pfnOne)dlsym(dl, "eVDecDrvDeInit");
    gDrv.release = (pfnOne)dlsym(dl, "eVDecDrvRelease");
    gDrv.alloc = (pfnMem)dlsym(ut, "eVideoMemAlloc");
    gDrv.free = (pfnMem)dlsym(ut, "eVideoMemFree");
    gDrv.flush = (pfnCache)dlsym(ut, "eVideoFlushCache");
    gDrv.inval = (pfnCache)dlsym(ut, "eVideoInvalidateCache");
    gDrv.ok = gDrv.create && gDrv.set && gDrv.init && gDrv.decode && gDrv.get && gDrv.deinit &&
              gDrv.release && gDrv.alloc && gDrv.free && gDrv.flush && gDrv.inval;
    if (!gDrv.ok) ALOGE("missing driver symbols");
}

// ---------------------------------------------------------------------------
// MT6592 MDP (libdpframework DpBlitStream), loaded at runtime. The class has no
// public header; the entry points below were taken from the exported symbols and
// from how libMtkOmxVdec::ConvertFrameToYV12 drives it.
// ---------------------------------------------------------------------------
struct DpRect { int x, subX, y, subY, w, h; };
typedef void (*pfnDpCtor)(void *self);
typedef int (*pfnDpSetBufVaMva)(void *self, void **va, void **mva, unsigned *sizes, unsigned n);
typedef int (*pfnDpSetBufFd)(void *self, int fd, unsigned *sizes, unsigned n);
typedef int (*pfnDpCfg)(void *self, int w, int h, int yPitch, int uvPitch, uint32_t fmt,
                        int profile, int interlace, DpRect *roi, int secure, bool flush);
typedef int (*pfnDpRun)(void *self);

static struct {
    pfnDpCtor ctor, dtor;
    pfnDpSetBufVaMva setSrc;
    pfnDpSetBufFd setDstFd;
    pfnDpCfg setSrcCfg, setDstCfg;
    pfnDpRun run;
    bool ok;
} gDp;
static pthread_once_t gDpOnce = PTHREAD_ONCE_INIT;

static const size_t kDpObjSize = 1024;       // sizeof(DpBlitStream) is ~190 bytes
static const uint32_t kDpColorBlockNV12 = 0x0a55004c;  // MTK 16x32 / 16x16 block, 2 planes
static const uint32_t kDpColorYV12 = 0x03140868;       // Y, V, U planar (I420 is 0x03140848)
static const uint32_t kGrallocMagic = 0x3141592;        // MTK private_handle_t magic

static void loadDpOnce() {
    void *dl = dlopen("libdpframework.so", RTLD_NOW);
    if (!dl) { ALOGW("dlopen(libdpframework.so) failed: %s", dlerror()); return; }
    gDp.ctor = (pfnDpCtor)dlsym(dl, "_ZN12DpBlitStreamC1Ev");
    gDp.dtor = (pfnDpCtor)dlsym(dl, "_ZN12DpBlitStreamD1Ev");
    gDp.setSrc = (pfnDpSetBufVaMva)dlsym(dl, "_ZN12DpBlitStream12setSrcBufferEPPvS1_Pjj");
    gDp.setDstFd = (pfnDpSetBufFd)dlsym(dl, "_ZN12DpBlitStream12setDstBufferEiPjj");
    gDp.setSrcCfg = (pfnDpCfg)dlsym(dl, "_ZN12DpBlitStream12setSrcConfigEiiii13DP_COLOR_ENUM"
            "15DP_PROFILE_ENUM17DpInterlaceFormatP6DpRect8DpSecureb");
    gDp.setDstCfg = (pfnDpCfg)dlsym(dl, "_ZN12DpBlitStream12setDstConfigEiiii13DP_COLOR_ENUM"
            "15DP_PROFILE_ENUM17DpInterlaceFormatP6DpRect8DpSecureb");
    gDp.run = (pfnDpRun)dlsym(dl, "_ZN12DpBlitStream10invalidateEv");
    gDp.ok = gDp.ctor && gDp.dtor && gDp.setSrc && gDp.setDstFd && gDp.setSrcCfg &&
             gDp.setDstCfg && gDp.run;
    if (!gDp.ok) ALOGW("missing DpBlitStream symbols");
}

bool SoftHulkAVC::loadDriver() {
    pthread_once(&gOnce, loadDriverOnce);
    return gDrv.ok;
}

// ---------------------------------------------------------------------------
// minimal SPS parser (dimensions + cropping only)
// ---------------------------------------------------------------------------
namespace {
struct BitReader {
    const uint8_t *d; size_t nbits; size_t pos;
    BitReader(const uint8_t *data, size_t n) : d(data), nbits(n * 8), pos(0) {}
    uint32_t bit() { if (pos >= nbits) { pos++; return 0; } uint32_t b = (d[pos >> 3] >> (7 - (pos & 7))) & 1; pos++; return b; }
    uint32_t bits(int n) { uint32_t v = 0; while (n--) v = (v << 1) | bit(); return v; }
    uint32_t ue() {
        int z = 0;
        while (!bit() && z < 32 && pos <= nbits) z++;
        if (z == 0) return 0;
        return ((1u << z) - 1) + bits(z);
    }
    int32_t se() { uint32_t k = ue(); return (k & 1) ? (int32_t)((k + 1) >> 1) : -(int32_t)(k >> 1); }
    bool overrun() const { return pos > nbits; }
};

void skipScalingList(BitReader &br, int size) {
    int last = 8, next = 8;
    for (int j = 0; j < size; j++) {
        if (next != 0) {
            int delta = br.se();
            next = (last + delta + 256) % 256;
        }
        last = (next == 0) ? last : next;
    }
}

// nal: points at NAL header byte
bool parseSps(const uint8_t *nal, size_t n, int *w, int *h, int *codedW, int *codedH) {
    if (n < 5) return false;
    std::vector<uint8_t> rbsp;
    rbsp.reserve(n);
    int zeros = 0;
    for (size_t i = 1; i < n; i++) {
        if (zeros >= 2 && nal[i] == 3) { zeros = 0; continue; }
        zeros = nal[i] == 0 ? zeros + 1 : 0;
        rbsp.push_back(nal[i]);
    }
    BitReader br(rbsp.data(), rbsp.size());
    uint32_t profile = br.bits(8);
    br.bits(8);
    br.bits(8);
    br.ue();
    uint32_t chroma = 1;
    if (profile == 100 || profile == 110 || profile == 122 || profile == 244 || profile == 44 ||
        profile == 83 || profile == 86 || profile == 118 || profile == 128 || profile == 138 ||
        profile == 139 || profile == 134 || profile == 135) {
        chroma = br.ue();
        if (chroma == 3) br.bit();
        br.ue();
        br.ue();
        br.bit();
        if (br.bit()) {
            int cnt = (chroma != 3) ? 8 : 12;
            for (int i = 0; i < cnt; i++) {
                if (br.bit()) skipScalingList(br, i < 6 ? 16 : 64);
            }
        }
    }
    br.ue();
    uint32_t poc = br.ue();
    if (poc == 0) {
        br.ue();
    } else if (poc == 1) {
        br.bit(); br.se(); br.se();
        uint32_t cnt = br.ue();
        for (uint32_t i = 0; i < cnt && i < 256; i++) br.se();
    }
    br.ue();
    br.bit();
    uint32_t wm = br.ue(), hm = br.ue();
    uint32_t fmo = br.bit();
    if (!fmo) { br.bit(); }
    br.bit();
    uint32_t cl = 0, cr = 0, ct = 0, cb = 0;
    if (br.bit()) { cl = br.ue(); cr = br.ue(); ct = br.ue(); cb = br.ue(); }
    if (br.overrun() || wm > 511 || hm > 511) return false;
    int cux = (chroma == 0 || chroma == 3) ? 1 : 2;
    int cuy = ((chroma == 1) ? 2 : 1) * (2 - (int)fmo);
    if (chroma == 0) cuy = 2 - (int)fmo;
    *codedW = (wm + 1) * 16;
    *codedH = (2 - (int)fmo) * (hm + 1) * 16;
    *w = *codedW - cux * (int)(cl + cr);
    *h = *codedH - cuy * (int)(ct + cb);
    return *w > 0 && *h > 0;
}
}  // namespace

// ---------------------------------------------------------------------------

SoftHulkAVC::SoftHulkAVC(
        const char *name,
        const OMX_CALLBACKTYPE *callbacks,
        OMX_PTR appData,
        OMX_COMPONENTTYPE **component)
    : SoftVideoDecoderOMXComponent(
            name, componentName, codingType,
            kProfileLevels, ARRAY_SIZE(kProfileLevels),
            320 /* width */, 240 /* height */, callbacks,
            appData, component),
      mH(NULL), mDecInit(false), mDecW(0), mDecH(0), mCodedW(0), mCodedH(0),
      mBsAlloc(false), mFbSize(0),
      mSpsW(0), mSpsH(0), mSpsCodedW(0), mSpsCodedH(0), mLastTs(0),
      mReceivedEOS(false), mEosFlushed(false), mSignalledError(false),
      mNeedReinit(false), mNewW(0), mNewH(0), mFramesOut(0), mDecodeErrors(0),
      mWBusy(false), mWExit(false), mGen(0), mANW(false), mMdp(NULL), mMdpFails(0),
      mMdpDisabled(false), mDumpFile(NULL), mDumpLeft(0), mDumpIn(NULL), mInCount(0), mDecCount(0) {
    memset(&mBs, 0, sizeof(mBs));
    initPorts(
            1 /* numMinInputBuffers */, kNumBuffers, 1024 * 1024,
            1 /* numMinOutputBuffers */, kNumOutBuffers, MEDIA_MIMETYPE_VIDEO_AVC);
    {
        // debug aid: debug.hulk.dump=<nframes> dumps the first frames as I420
        char v[PROPERTY_VALUE_MAX];
        property_get("debug.hulk.dump", v, "0");
        mDumpLeft = atoi(v);
        if (mDumpLeft > 0) mDumpFile = fopen("/data/local/tmp/hulkdump.yuv", "wb");
        if (!mDumpFile) mDumpLeft = 0;
        property_get("debug.hulk.dumpin", v, "0");
        if (atoi(v) > 0) mDumpIn = fopen("/data/local/tmp/hulkin.h264", "wb");
    }
    mWorker = std::thread(&SoftHulkAVC::workerLoop, this);
}

SoftHulkAVC::~SoftHulkAVC() {
    {
        std::unique_lock<std::mutex> l(mWMutex);
        mWCv.wait(l, [&] { return mJobs.empty() && !mWBusy; });
        mWExit = true;
        mWCv.notify_all();
    }
    mWorker.join();
    destroyDecoder();
    freePool();
    if (mDumpFile) fclose(mDumpFile);
    if (mDumpIn) fclose(mDumpIn);
    if (mBsAlloc) {
        gDrv.free(&mBs, sizeof(mBs));
        mBsAlloc = false;
    }
}

void SoftHulkAVC::noteCommand() {
    // Any frame still being converted when a command arrives is stale.
    mGen++;
}

int SoftHulkAVC::getColorAspectPreference() {
    return kPreferContainer;
}

int SoftHulkAVC::findFb(const void *p) {
    for (size_t i = 0; i < mFbs.size(); i++) {
        if (&mFbs[i]->fb == p) return (int)i;
    }
    return -1;
}

// ---------------------------------------------------------------------------
// worker thread: block format -> I420 into the OMX output buffer
// ---------------------------------------------------------------------------
void SoftHulkAVC::workerLoop() {
    std::unique_lock<std::mutex> l(mWMutex);
    for (;;) {
        mWCv.wait(l, [&] { return mWExit || !mJobs.empty(); });
        if (mJobs.empty()) {
            if (mWExit) break;
            continue;
        }
        Job j = mJobs.front();
        mJobs.pop_front();
        mWBusy = true;
        l.unlock();

        OMX_BUFFERHEADERTYPE *hdr = j.hdr;
        if (j.gen == mGen.load()) {
            double td = nowSec();
            if (j.anw) {
                bool ok = !mMdpDisabled && convertToGralloc(j);
                if (ok) {
                    mMdpFails = 0;
                } else {
                    if (!mMdpDisabled && ++mMdpFails >= 8) {
                        ALOGW("MDP conversion keeps failing, using the CPU path from now on");
                        mMdpDisabled = true;
                    }
                    if (!convertToGrallocCpu(j)) {
                        hdr->nFilledLen = 0;
                        hdr->nTimeStamp = 0;
                    }
                }
            } else {
                uint8_t *dst = hdr->pBuffer + hdr->nOffset;
                size_t sizeY = (size_t)j.outW * j.outH;
                gDrv.inval(&mFbs[j.idx]->mem, sizeof(ValMem), 1);
                hulkDetileI420((const uint8_t *)mFbs[j.idx]->mem.alignVa, j.stride, j.sliceH, j.w, j.h,
                               dst, j.outW, dst + sizeY, dst + sizeY + sizeY / 4, j.outW / 2);
            }
            gTDet += nowSec() - td;
        } else {
            hdr->nFilledLen = 0;
            hdr->nTimeStamp = 0;
            ALOGW("dropping stale frame (gen %u != %u)", j.gen, mGen.load());
        }
        {
            std::lock_guard<std::mutex> fl(mFbLock);
            FbEntry *e = mFbs[j.idx];
            e->pending = false;
            if (e->released) mFree.push_back(j.idx);
        }
        if (mDumpFile && mDumpLeft > 0 && !j.anw && j.gen == mGen.load()) {
            uint8_t *dst = hdr->pBuffer + hdr->nOffset;
            size_t sizeY = (size_t)j.outW * j.outH;
            for (int y = 0; y < j.h; y++) fwrite(dst + (size_t)y * j.outW, 1, j.w, mDumpFile);
            for (int p = 0; p < 2; p++)
                for (int y = 0; y < j.h / 2; y++)
                    fwrite(dst + sizeY + p * (sizeY / 4) + (size_t)y * (j.outW / 2), 1, j.w / 2, mDumpFile);
            if (--mDumpLeft == 0) { fclose(mDumpFile); mDumpFile = NULL; }
        }
        { double t=nowSec(); if (gLastFbd>0){double g=t-gLastFbd; if(g>gMaxGap)gMaxGap=g; if(g>0.05)gGap50++;} gLastFbd=t; }
        notifyFillBufferDone(hdr);

        l.lock();
        mWBusy = false;
        mWCv.notify_all();
    }
    if (mMdp) {
        l.unlock();
        gDp.dtor(mMdp);
        free(mMdp);
        mMdp = NULL;
    }
}

namespace {
struct GrallocInfo {
    int fd;
    int width, height, stride, vstride;
    size_t size;
    uint32_t usage;
    bool valid;
};

// MTK gralloc (gralloc.mt6592) private_handle_t: fd in data[0], magic in data[1].
GrallocInfo parseGralloc(const native_handle_t *h) {
    GrallocInfo g;
    memset(&g, 0, sizeof(g));
    g.fd = -1;
    if (h == NULL || h->numFds < 1 || h->numInts < 17 || (uint32_t)h->data[1] != kGrallocMagic ||
            h->data[7] != HAL_PIXEL_FORMAT_YV12) {
        return g;
    }
    g.fd = h->data[0];
    g.usage = (uint32_t)h->data[3];
    g.size = (size_t)h->data[4];
    g.width = h->data[5];
    g.height = h->data[6];
    g.stride = h->data[8];
    g.vstride = h->data[16];
    g.valid = g.fd >= 0 && g.stride >= g.width && g.vstride >= g.height && g.width > 0 && g.height > 0;
    return g;
}
}  // namespace

// Block (MTK 420 BLK) -> YV12 in the gralloc buffer, entirely on the MDP.
bool SoftHulkAVC::convertToGralloc(const Job &j) {
    pthread_once(&gDpOnce, loadDpOnce);
    if (!gDp.ok) return false;
    GrallocInfo g = parseGralloc((const native_handle_t *)j.hdr->pBuffer);
    if (!g.valid || (g.usage & GRALLOC_USAGE_SW_READ_MASK) || j.w > g.width || j.h > g.height) {
        return false;
    }
    const size_t cs = ((g.stride / 2) + 15) & ~(size_t)15;
    const size_t ySize = (size_t)g.stride * g.vstride;
    const size_t cSize = cs * ((g.vstride + 1) / 2);
    if (ySize + 2 * cSize > g.size) return false;

    if (!mMdp) {
        if (posix_memalign(&mMdp, 16, kDpObjSize) != 0) { mMdp = NULL; return false; }
        memset(mMdp, 0, kDpObjSize);
        gDp.ctor(mMdp);
    }
    uint8_t *base = (uint8_t *)mFbs[j.idx]->mem.alignVa;
    const uint32_t mva = mFbs[j.idx]->mem.alignMva;
    const unsigned ySrc = (unsigned)j.stride * j.sliceH;
    void *va[2] = { base, base + ySrc };
    void *mvas[2] = { (void *)(uintptr_t)mva, (void *)(uintptr_t)(mva + ySrc) };
    unsigned srcSizes[2] = { ySrc, ySrc / 2 };
    DpRect roi = { 0, 0, 0, 0, j.w, j.h };
    unsigned dstSizes[3] = { (unsigned)ySize, (unsigned)cSize, (unsigned)cSize };

    int r = gDp.setSrc(mMdp, va, mvas, srcSizes, 2);
    if (r == 0) r = gDp.setSrcCfg(mMdp, j.stride, j.sliceH, j.stride * 32, j.stride * 16,
                                  kDpColorBlockNV12, 0, 0, &roi, 0, true);
    if (r == 0) r = gDp.setDstFd(mMdp, g.fd, dstSizes, 3);
    if (r == 0) r = gDp.setDstCfg(mMdp, j.w, j.h, g.stride, (int)cs, kDpColorYV12, 0, 0, NULL, 0, true);
    if (r == 0) r = gDp.run(mMdp);
    if (r != 0) {
        ALOGW("MDP conversion failed (%d)", r);
        return false;
    }
    return true;
}

// CPU fallback for gralloc output (also used for buffers the CPU will read).
bool SoftHulkAVC::convertToGrallocCpu(const Job &j) {
    const native_handle_t *h = (const native_handle_t *)j.hdr->pBuffer;
    android_ycbcr ycc;
    memset(&ycc, 0, sizeof(ycc));
    status_t err = GraphicBufferMapper::get().lockYCbCr(
            h, GRALLOC_USAGE_SW_WRITE_OFTEN, Rect(j.w, j.h), &ycc);
    if (err != OK || !ycc.y || !ycc.cb || !ycc.cr) {
        ALOGE("gralloc lockYCbCr failed (%d)", err);
        return false;
    }
    gDrv.inval(&mFbs[j.idx]->mem, sizeof(ValMem), 1);
    hulkDetileI420((const uint8_t *)mFbs[j.idx]->mem.alignVa, j.stride, j.sliceH, j.w, j.h,
                   (uint8_t *)ycc.y, (int)ycc.ystride, (uint8_t *)ycc.cb, (uint8_t *)ycc.cr,
                   (int)ycc.cstride);
    GraphicBufferMapper::get().unlock(h);
    return true;
}

// ---------------------------------------------------------------------------
// native window (gralloc) buffer extensions
// ---------------------------------------------------------------------------
enum {
    kHulkEnableNativeBuffersIndex = 0x7F00FF01,
    kHulkGetNativeBufferUsageIndex = 0x7F00FF02,
    kHulkUseNativeBuffer2Index = 0x7F00FF03,
};

OMX_ERRORTYPE SoftHulkAVC::getExtensionIndex(const char *name, OMX_INDEXTYPE *index) {
    if (!strcmp(name, "OMX.google.android.index.enableAndroidNativeBuffers")) {
        *(int32_t *)index = kHulkEnableNativeBuffersIndex;
        return OMX_ErrorNone;
    } else if (!strcmp(name, "OMX.google.android.index.getAndroidNativeBufferUsage")) {
        *(int32_t *)index = kHulkGetNativeBufferUsageIndex;
        return OMX_ErrorNone;
    } else if (!strcmp(name, "OMX.google.android.index.useAndroidNativeBuffer2")) {
        *(int32_t *)index = kHulkUseNativeBuffer2Index;
        return OMX_ErrorNone;
    }
    return SoftVideoDecoderOMXComponent::getExtensionIndex(name, index);
}

OMX_ERRORTYPE SoftHulkAVC::internalGetParameter(OMX_INDEXTYPE index, OMX_PTR params) {
    if ((int32_t)index == kHulkGetNativeBufferUsageIndex) {
        GetAndroidNativeBufferUsageParams *p = (GetAndroidNativeBufferUsageParams *)params;
        if (!isValidOMXParam(p)) return OMX_ErrorBadParameter;
        if (p->nPortIndex != kOutputPortIndex) return OMX_ErrorBadPortIndex;
        // The MDP writes through ION, no CPU access is needed; the CPU fallback
        // path writes the buffer occasionally.
        p->nUsage = GRALLOC_USAGE_SW_WRITE_RARELY;
        return OMX_ErrorNone;
    }
    return SoftVideoDecoderOMXComponent::internalGetParameter(index, params);
}

OMX_ERRORTYPE SoftHulkAVC::internalSetParameter(OMX_INDEXTYPE index, const OMX_PTR params) {
    if ((int32_t)index == kHulkEnableNativeBuffersIndex) {
        EnableAndroidNativeBuffersParams *p = (EnableAndroidNativeBuffersParams *)params;
        if (!isValidOMXParam(p)) return OMX_ErrorBadParameter;
        if (p->nPortIndex != kOutputPortIndex) return OMX_ErrorBadPortIndex;
        mANW = p->enable == OMX_TRUE;
        // The native window is configured with the port's colour format, which for
        // gralloc buffers has to be a HAL pixel format.
        mOutputFormat = mANW ? (OMX_COLOR_FORMATTYPE)HAL_PIXEL_FORMAT_YV12
                             : OMX_COLOR_FormatYUV420Planar;
        // The consumer holds a few buffers; ask for enough extra ones that decoding and
        // conversion can run ahead of the display at 60 fps.
        editPortInfo(kOutputPortIndex)->mDef.nBufferCountMin = mANW ? kNumOutBuffersAnwMin : 1;
        updatePortDefinitions(false /* updateCrop */, false /* updateInputSize */);
        ALOGI("native window buffers %s", mANW ? "enabled" : "disabled");
        return OMX_ErrorNone;
    }
    return SoftVideoDecoderOMXComponent::internalSetParameter(index, params);
}

void SoftHulkAVC::waitWorkerIdle() {
    std::unique_lock<std::mutex> l(mWMutex);
    mWCv.wait(l, [&] { return mJobs.empty() && !mWBusy; });
}

// ---------------------------------------------------------------------------
// driver management
// ---------------------------------------------------------------------------
bool SoftHulkAVC::allocPool(uint32_t size, int count) {
    if (!mFbs.empty() && mFbSize >= size && (int)mFbs.size() >= count) return true;
    freePool();
    for (int i = 0; i < count; i++) {
        FbEntry *e = new FbEntry;
        memset(e, 0, sizeof(*e));
        e->mem.type = kMemType;
        e->mem.size = size;
        e->mem.align = 512;
        if (gDrv.alloc(&e->mem, sizeof(ValMem)) != 0) {
            ALOGE("eVideoMemAlloc(%u) failed", size);
            delete e;
            freePool();
            return false;
        }
        e->allocated = true;
        e->fb.w[0] = (uint32_t)e->mem.alignVa;
        e->fb.w[1] = e->mem.alignMva;
        e->fb.w[2] = size;
        mFbs.push_back(e);
    }
    mFbSize = size;
    return true;
}

void SoftHulkAVC::freePool() {
    for (FbEntry *e : mFbs) {
        if (e->allocated) gDrv.free(&e->mem, sizeof(ValMem));
        delete e;
    }
    mFbs.clear();
    mFree.clear();
    mFbSize = 0;
}

bool SoftHulkAVC::createDecoder(const uint8_t *au, size_t len) {
    if (!loadDriver()) return false;
    if (!mBsAlloc) {
        memset(&mBs, 0, sizeof(mBs));
        mBs.type = kMemType; mBs.size = kBsSize; mBs.align = 512;
        if (gDrv.alloc(&mBs, sizeof(ValMem)) != 0) {
            ALOGE("bitstream alloc failed");
            return false;
        }
        mBsAlloc = true;
    }
    if (len > kBsSize) return false;

    int alignW = (mCodedW + 15) & ~15, alignH = (mCodedH + 31) & ~31;
    uint32_t fbSize = alignW * alignH * 3 / 2 + 4096;

    if (gDrv.create(&mH, 0x100) != 0 || !mH) {
        ALOGE("eVDecDrvCreate failed");
        mH = NULL;
        return false;
    }
    uint32_t sz = (uint32_t)mDecW | ((uint32_t)mDecH << 16);
    gDrv.set(mH, 19, &sz, NULL);

    memcpy(mBs.alignVa, au, len);
    gDrv.flush(&mBs, sizeof(ValMem), 1);
    Bs bs = { (uint32_t)mBs.alignVa, mBs.alignMva, (uint32_t)len, (uint32_t)mBs.alignVa,
              (uint32_t)mBs.alignVa + (uint32_t)len, 0, (uint32_t)mBs.alignVa, 0 };
    uint32_t out[16];
    memset(out, 0, sizeof(out));
    int r = gDrv.init(mH, &bs, out);
    if (r != 0) {
        ALOGE("eVDecDrvInit failed %d", r);
        gDrv.release(mH);
        mH = NULL;
        return false;
    }
    uint32_t ca[3] = { 2, 16, 8 };
    gDrv.set(mH, 17, ca, NULL);
    uint32_t cb[2] = { 16, 8 };
    gDrv.set(mH, 18, cb, NULL);

    uint32_t dpb = 0;
    gDrv.get(mH, 22, NULL, &dpb);
    int count = (int)dpb + 7;
    if (count < 10) count = 10;
    if (count > kMaxFbs) count = kMaxFbs;
    ALOGI("decoder created %dx%d (coded %dx%d) dpb=%u fbs=%d x %u, %u output buffers%s", mDecW, mDecH,
          mCodedW, mCodedH, dpb, count, fbSize, (unsigned)editPortInfo(kOutputPortIndex)->mDef.nBufferCountActual,
          mANW ? " (native window)" : "");
    if (!allocPool(fbSize, count)) {
        gDrv.deinit(mH);
        gDrv.release(mH);
        mH = NULL;
        return false;
    }
    {
        std::lock_guard<std::mutex> fl(mFbLock);
        mFree.clear();
        for (size_t i = 0; i < mFbs.size(); i++) {
            memset(&mFbs[i]->fb.w[3], 0, sizeof(mFbs[i]->fb.w) - 3 * sizeof(uint32_t));
            mFbs[i]->pending = mFbs[i]->released = false;
            mFree.push_back((int)i);
        }
    }
    mDecInit = true;
    return true;
}

void SoftHulkAVC::destroyDecoder() {
    if (mH) {
        gDrv.deinit(mH);
        gDrv.release(mH);
        mH = NULL;
    }
    mDecInit = false;
}

void SoftHulkAVC::resetState() {
    waitWorkerIdle();
    destroyDecoder();
    mPending.clear();
    mTs.clear();
    mNeedReinit = false;
    mReceivedEOS = false;
    mEosFlushed = false;
    std::lock_guard<std::mutex> fl(mFbLock);
    mFree.clear();
    for (size_t i = 0; i < mFbs.size(); i++) {
        mFbs[i]->pending = mFbs[i]->released = false;
        mFree.push_back((int)i);
    }
}

void SoftHulkAVC::drain() {
    for (;;) {
        Fb *d = NULL;
        gDrv.get(mH, 2, NULL, &d);
        if (!d) break;
        int idx = findFb(d);
        if (idx < 0) {
            ALOGW("driver displayed unknown fb %p", d);
            continue;
        }
        uint32_t pi[9];
        memset(pi, 0, sizeof(pi));
        gDrv.get(mH, 4, NULL, pi);
        int64_t ts = mLastTs;
        if (!mTs.empty()) {
            ts = *mTs.begin();
            mTs.erase(mTs.begin());
        }
        mLastTs = ts;
        {
            std::lock_guard<std::mutex> fl(mFbLock);
            mFbs[idx]->pending = true;
        }
        PendingFrame pf;
        pf.idx = idx; pf.ts = ts; pf.w = mDecW; pf.h = mDecH;
        pf.stride = pi[2] ? (int)pi[2] : ((mCodedW + 15) & ~15);
        pf.sliceH = pi[3] ? (int)pi[3] : ((mCodedH + 31) & ~31);
        mPending.push_back(pf);
    }
    for (;;) {
        Fb *f = NULL;
        gDrv.get(mH, 3, NULL, &f);
        if (!f) break;
        int idx = findFb(f);
        if (idx < 0) continue;
        std::lock_guard<std::mutex> fl(mFbLock);
        mFbs[idx]->released = true;
        if (!mFbs[idx]->pending) mFree.push_back(idx);
    }
}

bool SoftHulkAVC::decodeAu(const uint8_t *au, size_t len) {
    if (len > kBsSize) {
        ALOGE("access unit too large: %zu", len);
        return false;
    }
    int idx = -1;
    {
        std::lock_guard<std::mutex> fl(mFbLock);
        if (!mFree.empty()) { idx = mFree.back(); mFree.pop_back(); }
    }
    if (idx < 0) {
        waitWorkerIdle();
        std::lock_guard<std::mutex> fl(mFbLock);
        if (!mFree.empty()) { idx = mFree.back(); mFree.pop_back(); }
    }
    if (idx >= 0) mFbs[idx]->released = false; else ALOGW("no free fb for decode");

    double tp = nowSec();
    memcpy(mBs.alignVa, au, len);
    {
        // flush only the bytes we wrote (flushing the whole 2 MB buffer costs ~1.7 ms per AU)
        ValMem part = mBs;
        part.size = (uint32_t)((len + 4095) & ~(size_t)4095);
        gDrv.flush(&part, sizeof(ValMem), 1);
    }
    Bs bs = { (uint32_t)mBs.alignVa, mBs.alignMva, (uint32_t)len, (uint32_t)mBs.alignVa,
              (uint32_t)mBs.alignVa + (uint32_t)len, 0, (uint32_t)mBs.alignVa, 0 };
    gPre += nowSec() - tp;
    if (mDumpIn) { fwrite(au, 1, len, mDumpIn); fflush(mDumpIn); }
    double t0 = nowSec();
    int rc = gDrv.decode(mH, &bs, idx >= 0 ? &mFbs[idx]->fb : NULL);
    gTDec += nowSec() - t0; gDecN++;
    if (rc != 0) {
        if ((mDecodeErrors++ & 63) == 0) ALOGW("decode returned %d (count %u)", rc, mDecodeErrors);
    }
    size_t before = mPending.size();
    (void)before;
    double tdr = nowSec(); drain(); gDrain += nowSec() - tdr;
    ALOGV("decode rc=%d len=%zu pending=%zu", rc, len, mPending.size());
    return rc == 0;
}

void SoftHulkAVC::flushDriver() {
    if (!mH || !mDecInit) return;
    gDrv.decode(mH, NULL, NULL);
    for (int k = 0; k < 4; k++) drain();
}

// ---------------------------------------------------------------------------
// bitstream helpers
// ---------------------------------------------------------------------------
SoftHulkAVC::NalScan SoftHulkAVC::scanNals(const uint8_t *d, size_t n) {
    NalScan s;
    size_t i = 0;
    // find start codes
    std::vector<size_t> starts;  // offset of first byte after 00 00 01
    for (i = 0; i + 3 <= n; i++) {
        if (d[i] == 0 && d[i + 1] == 0 && d[i + 2] == 1) {
            starts.push_back(i + 3);
            i += 2;
        }
    }
    for (size_t k = 0; k < starts.size(); k++) {
        size_t b = starts[k];
        size_t e = (k + 1 < starts.size()) ? starts[k + 1] - 3 : n;
        while (e > b && d[e - 1] == 0) e--;  // trailing zeros / 4-byte start code
        if (e <= b) continue;
        int t = d[b] & 0x1f;
        size_t scb = b - 3;
        if (scb > 0 && d[scb - 1] == 0) scb--;
        if (t == 7) {
            s.hasSps = true;
            mSps.assign(d + scb, d + e);
            int w, h, cw, ch;
            if (parseSps(d + b, e - b, &w, &h, &cw, &ch)) {
                s.spsOk = true; s.w = w; s.h = h; s.codedW = cw; s.codedH = ch;
                mSpsW = w; mSpsH = h; mSpsCodedW = cw; mSpsCodedH = ch;
            }
        } else if (t == 8) {
            s.hasPps = true;
            mPps.assign(d + scb, d + e);
        } else if (t == 1 || t == 5) {
            s.hasSlice = true;
            if (t == 5) s.hasIdr = true;
        }
    }
    return s;
}

// ---------------------------------------------------------------------------
// OMX plumbing
// ---------------------------------------------------------------------------
void SoftHulkAVC::onReset() {
    SoftVideoDecoderOMXComponent::onReset();
    mSignalledError = false;
    resetState();
}

void SoftHulkAVC::onPortFlushCompleted(OMX_U32 portIndex) {
    (void)portIndex;
    resetState();
}

SoftHulkAVC::Result SoftHulkAVC::processInput(OMX_BUFFERHEADERTYPE *hdr) {
    const uint8_t *data = hdr->pBuffer + hdr->nOffset;
    size_t len = hdr->nFilledLen;
    bool eos = hdr->nFlags & OMX_BUFFERFLAG_EOS;
    bool config = hdr->nFlags & OMX_BUFFERFLAG_CODECCONFIG;
    if (eos) mReceivedEOS = true;
    ALOGV("input len=%zu flags=0x%x ts=%lld pending=%zu", len, hdr->nFlags, (long long)hdr->nTimeStamp, mPending.size());
    if (eos) ALOGI("EOS input len=%zu out=%u inputs=%u decoded=%u", len, mFramesOut, mInCount, mDecCount);

    NalScan s;
    if (len > 0) s = scanNals(data, len);
    mInCount++;
    if (len > 0 && !config && !s.hasSlice) ALOGW("input #%u len=%zu flags=0x%x has no slice NAL (first bytes %02x %02x %02x %02x %02x)", mInCount, len, hdr->nFlags, data[0], data[1], data[2], data[3], data[4]);

    // Resolution handling
    if (s.spsOk) {
        if (mDecInit && (s.w != mDecW || s.h != mDecH)) {
            ALOGI("resolution change %dx%d -> %dx%d", mDecW, mDecH, s.w, s.h);
            flushDriver();
            mNewW = s.w; mNewH = s.h;
            mNeedReinit = true;
            return kKeep;
        }
        if (!mDecInit && ((uint32_t)s.w != mWidth || (uint32_t)s.h != mHeight)) {
            bool reset = false;
            handlePortSettingsChange(&reset, s.w, s.h, mOutputFormat);
            if (reset) return config ? kStopConsumed : kStop;
        }
    }

    if (config || !s.hasSlice) {
        if (eos) {
            flushDriver();
            mEosFlushed = true;
        }
        return kConsume;
    }

    // picture data
    const uint8_t *au = data;
    size_t auLen = len;
    if (!mDecInit) {
        if (!s.hasIdr) return kConsume;  // wait for a random access point
        if (mSps.empty() || mPps.empty() || mSpsW == 0) {
            ALOGW("no SPS/PPS yet, dropping AU");
            return kConsume;
        }
        mAuBuf.clear();
        if (!s.hasSps) mAuBuf.insert(mAuBuf.end(), mSps.begin(), mSps.end());
        if (!s.hasPps) mAuBuf.insert(mAuBuf.end(), mPps.begin(), mPps.end());
        mAuBuf.insert(mAuBuf.end(), data, data + len);
        au = mAuBuf.data();
        auLen = mAuBuf.size();

        mDecW = s.spsOk ? s.w : mSpsW;
        mDecH = s.spsOk ? s.h : mSpsH;
        mCodedW = s.spsOk ? s.codedW : mSpsCodedW;
        mCodedH = s.spsOk ? s.codedH : mSpsCodedH;
        if (((uint32_t)mDecW != mWidth || (uint32_t)mDecH != mHeight)) {
            bool reset = false;
            handlePortSettingsChange(&reset, mDecW, mDecH, mOutputFormat);
            if (reset) return kStop;
        }
        if (!createDecoder(au, auLen)) {
            notify(OMX_EventError, OMX_ErrorUnsupportedSetting, 0, NULL);
            mSignalledError = true;
            return kStop;
        }
    }

    mTs.insert(hdr->nTimeStamp);
    mDecCount++;
    if (!decodeAu(au, auLen)) {
        // the driver rejected the unit; forget its timestamp
        auto it = mTs.find(hdr->nTimeStamp);
        if (it != mTs.end()) mTs.erase(it);
    }
    if (eos) {
        flushDriver();
        mEosFlushed = true;
    }
    return kConsume;
}

bool SoftHulkAVC::deliverFrame(List<BufferInfo *> &outQ) {
    PendingFrame pf = mPending.front();
    BufferInfo *outInfo = *outQ.begin();
    OMX_BUFFERHEADERTYPE *hdr = outInfo->mHeader;
    uint32_t outW = outputBufferWidth(), outH = outputBufferHeight();

    mPending.pop_front();
    outQ.erase(outQ.begin());
    outInfo->mOwnedByUs = false;

    hdr->nOffset = 0;
    hdr->nFlags = 0;
    hdr->nTimeStamp = pf.ts;
    if ((uint32_t)pf.w > outW || (uint32_t)pf.h > outH ||
            (!mANW && hdr->nAllocLen < outW * outH * 3 / 2)) {
        ALOGE("output buffer too small / frame too big: %dx%d vs %ux%u alloc %u", pf.w, pf.h,
              outW, outH, hdr->nAllocLen);
        hdr->nFilledLen = 0;
        std::lock_guard<std::mutex> fl(mFbLock);
        mFbs[pf.idx]->pending = false;
        if (mFbs[pf.idx]->released) mFree.push_back(pf.idx);
        notifyFillBufferDone(hdr);
        return false;
    }
    hdr->nFilledLen = outW * outH * 3 / 2;

    if (mUpdateColorAspects) {
        notify(OMX_EventPortSettingsChanged, kOutputPortIndex, kDescribeColorAspectsIndex, NULL);
        mUpdateColorAspects = false;
    }

    double tw = nowSec();
    {
        // bounded queue: only block if the worker is far behind
        std::unique_lock<std::mutex> l(mWMutex);
        mWCv.wait(l, [&] { return mJobs.size() < kMaxJobs; });
    }
    gTWait += nowSec() - tw;
    if (++gN % 60 == 0) {
        ALOGV("TIMING per frame ms: decode %.1f waitWorker %.1f detile %.1f | qcalls %u noOut %u noIn %u gaps>50ms %u maxgap %.0fms (decn %u) pre %.2f drain %.2f deliver %.2f busy %.1f inval %.1f avgOutQ %.2f", gTDec * 1000 / 60, gTWait * 1000 / 60, gTDet * 1000 / 60, gQCalls, gNoOut, gNoIn, gGap50, gMaxGap*1000, gDecN, gPre*1000/60, gDrain*1000/60, gDeliver*1000/60, gBusy*1000/60, gInval*1000/60, gOQ/(gQCalls?gQCalls:1));
        gBusy = gInval = gOQ = gPre = gDrain = gDeliver = 0;
        gTDec = gTWait = gTDet = 0; gQCalls = gNoOut = gNoIn = gGap50 = gDecN = 0; gMaxGap = 0;
    }
    {
        std::lock_guard<std::mutex> l(mWMutex);
        Job j;
        j.idx = pf.idx; j.hdr = hdr; j.anw = mANW;
        j.w = pf.w; j.h = pf.h; j.stride = pf.stride; j.sliceH = pf.sliceH;
        j.outW = outW; j.outH = outH;
        j.gen = mGen.load();
        mJobs.push_back(j);
        mWCv.notify_all();
    }
    mFramesOut++;
    return true;
}

void SoftHulkAVC::onQueueFilled(OMX_U32 portIndex) {
    (void)portIndex;
    if (mSignalledError || mOutputPortSettingsChange != NONE) return;

    List<BufferInfo *> &inQ = getPortQueue(kInputPortIndex);
    List<BufferInfo *> &outQ = getPortQueue(kOutputPortIndex);
    gQCalls++; gOQ += outQ.size(); double tq0 = nowSec();
    struct Busy { double t; ~Busy(){ gBusy += nowSec()-t; } } busy{tq0};

    for (;;) {
        { double td = nowSec(); while (!mPending.empty() && !outQ.empty()) deliverFrame(outQ); gDeliver += nowSec() - td; }
        if (!mPending.empty()) return;

        if (mEosFlushed) {
            if (outQ.empty()) return;
            waitWorkerIdle();
            BufferInfo *outInfo = *outQ.begin();
            OMX_BUFFERHEADERTYPE *hdr = outInfo->mHeader;
            ALOGI("sending EOS, frames out %u", mFramesOut);
            hdr->nFilledLen = 0;
            hdr->nOffset = 0;
            hdr->nTimeStamp = 0;
            hdr->nFlags = OMX_BUFFERFLAG_EOS;
            outQ.erase(outQ.begin());
            outInfo->mOwnedByUs = false;
            notifyFillBufferDone(hdr);
            mEosFlushed = false;
            mReceivedEOS = false;
            return;
        }

        if (mNeedReinit) {
            waitWorkerIdle();
            destroyDecoder();
            mTs.clear();
            mNeedReinit = false;
            bool reset = false;
            handlePortSettingsChange(&reset, mNewW, mNewH, mOutputFormat);
            if (reset) return;
        }

        if (inQ.empty() || outQ.empty()) { if (outQ.empty()) gNoOut++; else gNoIn++; return; }

        BufferInfo *inInfo = *inQ.begin();
        OMX_BUFFERHEADERTYPE *hdr = inInfo->mHeader;
        if (hdr == NULL) {
            inQ.erase(inQ.begin());
            inInfo->mOwnedByUs = false;
            continue;
        }

        Result r = processInput(hdr);
        if (r == kKeep) continue;
        if (r == kStop) return;
        inQ.erase(inQ.begin());
        inInfo->mOwnedByUs = false;
        notifyEmptyBufferDone(hdr);
        if (r == kStopConsumed) return;
        if (mSignalledError) return;
    }
}

}  // namespace android

android::SoftOMXComponent *createHulkAVCDecoder(
        const char *name, const OMX_CALLBACKTYPE *callbacks, OMX_PTR appData,
        OMX_COMPONENTTYPE **component) {
    return new android::SoftHulkAVC(name, callbacks, appData, component);
}

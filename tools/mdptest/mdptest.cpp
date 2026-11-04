#include <dlfcn.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <ui/GraphicBuffer.h>
#include <system/graphics.h>
#include <cutils/native_handle.h>
#include "HulkDetile.h"
using namespace android;

struct ValMem { uint32_t type, size; void *va; uint32_t mva, align; void *alignVa; uint32_t alignMva, codec, r32, r36; void *m4u; uint32_t r44; };
struct DpRect { int x, sx, y, sy, w, h; };
typedef int (*pfnMem)(void *m, uint32_t sz);
typedef int (*pfnCache)(void *m, uint32_t sz, uint32_t mode);
static double now() { timespec t; clock_gettime(CLOCK_MONOTONIC, &t); return t.tv_sec + t.tv_nsec * 1e-9; }

int main(int argc, char **argv) {
    int W = argc > 1 ? atoi(argv[1]) : 1920, H = argc > 2 ? atoi(argv[2]) : 1080;
    uint32_t srcFmt = argc > 3 ? strtoul(argv[3], 0, 16) : 0x0a55004c;
    uint32_t dstFmt = argc > 4 ? strtoul(argv[4], 0, 16) : 0x03140848;
    int mode = argc > 5 ? atoi(argv[5]) : 0;  // 0 = fd dst, 1 = va dst
    void *dp = dlopen("libdpframework.so", RTLD_NOW);
    void *ut = dlopen("libvcodec_utility.so", RTLD_NOW | RTLD_GLOBAL);
    void *ge = dlopen("libgralloc_extra.so", RTLD_NOW);
    if (!dp || !ut) { printf("dlopen failed %s\n", dlerror()); return 1; }
    auto memAlloc = (pfnMem)dlsym(ut, "eVideoMemAlloc");
    auto memFlush = (pfnCache)dlsym(ut, "eVideoFlushCache");
    typedef void (*ctor_t)(void *);
    typedef int (*sbva_t)(void *, void **, void **, unsigned *, unsigned);
    typedef int (*sbfd_t)(void *, int, unsigned *, unsigned);
    typedef int (*sbvo_t)(void *, void **, unsigned *, unsigned);
    typedef int (*cfg_t)(void *, int, int, int, int, uint32_t, int, int, DpRect *, int, bool);
    typedef int (*inv_t)(void *);
    auto ctor = (ctor_t)dlsym(dp, "_ZN12DpBlitStreamC1Ev");
    auto dtor = (ctor_t)dlsym(dp, "_ZN12DpBlitStreamD1Ev");
    auto setSrcVaMva = (sbva_t)dlsym(dp, "_ZN12DpBlitStream12setSrcBufferEPPvS1_Pjj");
    auto setDstFd = (sbfd_t)dlsym(dp, "_ZN12DpBlitStream12setDstBufferEiPjj");
    auto setDstVa = (sbvo_t)dlsym(dp, "_ZN12DpBlitStream12setDstBufferEPPvPjj");
    auto setSrcCfg = (cfg_t)dlsym(dp, "_ZN12DpBlitStream12setSrcConfigEiiii13DP_COLOR_ENUM15DP_PROFILE_ENUM17DpInterlaceFormatP6DpRect8DpSecureb");
    auto setDstCfg = (cfg_t)dlsym(dp, "_ZN12DpBlitStream12setDstConfigEiiii13DP_COLOR_ENUM15DP_PROFILE_ENUM17DpInterlaceFormatP6DpRect8DpSecureb");
    auto invalidate = (inv_t)dlsym(dp, "_ZN12DpBlitStream10invalidateEv");
    if (!ctor || !dtor || !setSrcVaMva || !setDstFd || !setSrcCfg || !setDstCfg || !invalidate) { printf("missing syms\n"); return 1; }

    int stride16 = (W + 15) & ~15, slice = (H + 31) & ~31;
    // synthetic block source
    ValMem m; memset(&m, 0, sizeof(m)); m.type = 2; m.size = stride16 * slice * 3 / 2 + 4096; m.align = 512;
    if (memAlloc(&m, sizeof(m))) { printf("alloc failed\n"); return 1; }
    uint8_t *s = (uint8_t *)m.alignVa;
    srand(1);
    for (size_t i = 0; i < (size_t)stride16 * slice * 3 / 2; i++) s[i] = (uint8_t)(rand() >> 7);
    memFlush(&m, sizeof(m), 1);

    sp<GraphicBuffer> gb = new GraphicBuffer(W, H, HAL_PIXEL_FORMAT_YV12, 1, 0x2900 | 0x20 * (mode == 1), "mdptest");
    printf("gb status %d stride %d usage %llx handle fds %d ints %d\n", gb->initCheck(), gb->getStride(), (unsigned long long)gb->getUsage(), gb->handle->numFds, gb->handle->numInts);
    for (int i = 0; i < gb->handle->numFds + gb->handle->numInts; i++) printf(" data[%d]=%d(0x%x)", i, gb->handle->data[i], gb->handle->data[i]);
    printf("\n");
    if (ge) {
        typedef int (*q_t)(buffer_handle_t, int *, int *);
        auto gif = (q_t)dlsym(ge, "gralloc_extra_getIonFd");
        int a = -7, b = -7;
        if (gif) { int r = gif(gb->handle, &a, &b); printf("getIonFd r=%d a=%d b=%d\n", r, a, b); }
        typedef int (*mva_t)(buffer_handle_t, int *);
        auto gm = (mva_t)dlsym(ge, "gralloc_extra_getMVA");
        if (gm) { int mva = 0; int r = gm(gb->handle, &mva); printf("getMVA r=%d mva=%x\n", r, mva); }
    }
    android_ycbcr ycc; memset(&ycc, 0, sizeof(ycc));
    uint8_t *va = NULL;
    int r = gb->lockYCbCr(GRALLOC_USAGE_SW_READ_OFTEN | GRALLOC_USAGE_SW_WRITE_OFTEN, &ycc);
    printf("lockYCbCr r=%d y=%p cb=%p cr=%p ystride=%zu cstride=%zu chroma_step=%zu\n", r, ycc.y, ycc.cb, ycc.cr, ycc.ystride, ycc.cstride, ycc.chroma_step);
    if (r != 0) { r = gb->lock(GRALLOC_USAGE_SW_READ_OFTEN | GRALLOC_USAGE_SW_WRITE_OFTEN, (void **)&va); printf("lock r=%d va=%p\n", r, va); ycc.y = va; }
    else va = (uint8_t *)ycc.y;
    int ys = gb->getStride();
    size_t cs = ((ys / 2) + 15) & ~15;
    if (ycc.cstride) cs = ycc.cstride;
    size_t ySize = (size_t)ys * H;
    printf("assumed cs %zu cr off %zd cb off %zd\n", cs, (uint8_t *)ycc.cr - va, (uint8_t *)ycc.cb - va);
    memset(va, 0x77, ySize + cs * H);  // poison
    int fd = gb->handle->data[0];

    uint8_t dp_obj[2048] __attribute__((aligned(16)));
    memset(dp_obj, 0, sizeof(dp_obj));
    ctor(dp_obj);
    int nloops = 1;
    double t0 = now();
    void *vaL[2] = { s, s + (size_t)stride16 * slice };
    void *mvaL[2] = { (void *)(uintptr_t)m.alignMva, (void *)(uintptr_t)(m.alignMva + stride16 * slice) };
    unsigned szL[2] = { (unsigned)(stride16 * slice), (unsigned)(stride16 * slice / 2) };
    r = setSrcVaMva(dp_obj, vaL, mvaL, szL, 2);
    printf("setSrc r=%d\n", r);
    DpRect sr = { 0, 0, 0, 0, W, H };
    r = setSrcCfg(dp_obj, stride16, slice, stride16 * 32, stride16 * 16, srcFmt, 0, 0, &sr, 0, true);
    printf("setSrcCfg r=%d\n", r);
    unsigned dsz[3] = { (unsigned)ySize, (unsigned)(cs * H / 2), (unsigned)(cs * H / 2) };
    if (mode == 0) r = setDstFd(dp_obj, fd, dsz, 3);
    else { void *dv[3] = { va, va + ySize, va + ySize + cs * H / 2 }; r = setDstVa(dp_obj, dv, dsz, 3); }
    printf("setDst r=%d\n", r);
    r = setDstCfg(dp_obj, W, H, ys, cs, dstFmt, 0, 0, NULL, 0, true);
    printf("setDstCfg r=%d\n", r);
    r = invalidate(dp_obj);
    printf("invalidate r=%d  %.2f ms\n", r, (now() - t0) * 1000);
    // second run for timing
    t0 = now();
    for (int i = 0; i < 10; i++) { r = invalidate(dp_obj); }
    printf("10x invalidate r=%d avg %.2f ms\n", r, (now() - t0) * 100);
    dtor(dp_obj);

    // reference
    uint8_t *ref = (uint8_t *)malloc(W * H * 3 / 2);
    hulkDetileI420(s, stride16, slice, W, H, ref, W, ref + W * H, ref + W * H + W * H / 4, W / 2);
    size_t bad[3] = {0, 0, 0}, badSw = 0;
    for (int y = 0; y < H; y++) for (int x = 0; x < W; x++) if (va[(size_t)y * ys + x] != ref[(size_t)y * W + x]) bad[0]++;
    uint8_t *p1 = va + ySize, *p2 = va + ySize + cs * H / 2;
    // plane1 compare against U (ref+W*H) and V
    uint8_t *refU = ref + W * H, *refV = refU + W * H / 4;
    size_t b1u = 0, b1v = 0;
    for (int y = 0; y < H / 2; y++) for (int x = 0; x < W / 2; x++) {
        if (p1[(size_t)y * cs + x] != refU[y * (W / 2) + x]) b1u++;
        if (p1[(size_t)y * cs + x] != refV[y * (W / 2) + x]) b1v++;
    }
    size_t b2u = 0;
    for (int y = 0; y < H / 2; y++) for (int x = 0; x < W / 2; x++)
        if (p2[(size_t)y * cs + x] != refU[y * (W / 2) + x]) b2u++;
    printf("plane2 vs U mismatches %zu\n", b2u);
    printf("Y mismatches %zu / %d ; plane1 vs U %zu vs V %zu (of %d)\n", bad[0], W * H, b1u, b1v, W * H / 4);
    gb->unlock();
    return 0;
}

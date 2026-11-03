// vdectest: H.264 Annex-B -> raw YUV using MTK libvcodecdrv.so directly.
// usage: vdectest in.h264 out.yuv [W H] [-f fmt] [-n maxframes] [-raw]
#include <ctype.h>
#include <dlfcn.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdint.h>
#include <time.h>
#include <vector>
#include "HulkDetile.h"

typedef uint32_t u32;

// ---- driver-visible memory (VAL_MEMORY_T, 48 bytes, from eVideoMemAlloc RE) ----
struct ValMem {
    u32 type;      // 0..2 -> malloc+M4U map (2 = non-cacheable flag to M4U)
    u32 size;
    void *va;      // raw
    u32 mva;       // M4U address of raw va
    u32 align;     // min 64
    void *alignVa; // aligned va
    u32 alignMva;  // aligned mva
    u32 codec;     // 0 = vdec module id 28
    u32 r32, r36;
    void *m4u;     // handle (out)
    u32 r44;
};

// Bitstream struct passed to eVDecDrvInit / eVDecDrvDecode.
struct Bs {
    u32 va, mva, size, rd, wr, r20, orgva, r28;
};

// FRAMEBUF_T: 0 va, 4 mva, 8 size ... +64 status (driver writes); OMX wraps 128B
struct Fb {
    u32 w[32];
};

typedef int (*pfnCreate)(void **h, u32 codec);
typedef int (*pfnQuery)(u32 type, void *in, void *out);
typedef int (*pfnSet)(void *h, u32 type, void *p, void *p2);
typedef int (*pfnInit)(void *h, Bs *bs, void *out);
typedef int (*pfnDecode)(void *h, Bs *bs, Fb *fb);
typedef int (*pfnGet)(void *h, u32 type, void *p, void *out);
typedef int (*pfnDeInit)(void *h);
typedef int (*pfnRelease)(void *h);
typedef int (*pfnInitMVA)(void **m4u);
typedef int (*pfnMemAlloc)(ValMem *m, u32 sz);
typedef int (*pfnCache)(ValMem *m, u32 sz, u32 mode);

static pfnCreate drvCreate; static pfnQuery drvQuery; static pfnSet drvSet; static pfnInit drvInit;
static pfnDecode drvDecode; static pfnGet drvGet; static pfnDeInit drvDeInit; static pfnRelease drvRelease;
static pfnMemAlloc memAlloc; static pfnCache cacheFlush, cacheInval;

static double now() { timespec t; clock_gettime(CLOCK_MONOTONIC, &t); return t.tv_sec + t.tv_nsec * 1e-9; }

static void *sym(void *lib, const char *n) {
    void *p = dlsym(lib, n);
    if (!p) { fprintf(stderr, "missing sym %s\n", n); exit(2); }
    return p;
}

static bool allocMem(ValMem *m, u32 size, u32 align, u32 type) {
    memset(m, 0, sizeof(*m));
    m->type = type; m->size = size; m->align = align;
    int r = memAlloc(m, sizeof(*m));
    if (r != 0) { fprintf(stderr, "eVideoMemAlloc(%u) failed %d\n", size, r); return false; }
    return true;
}

// MTK block format (fmt 2): Y = 16x32-byte tiles, tiles in raster order, tile rows contiguous;
// UV = NV12-interleaved, 16-byte x 16-row tiles (8 chroma pairs wide), raster order.
// Output here: cropped I420.
static void detile(const uint8_t *src, int stride, int sliceH, int w, int h, uint8_t *dst) {
    uint8_t *dy = dst, *du = dst + w * h, *dv = du + (w / 2) * (h / 2);
    for (int y = 0; y < h; y++) {
        int by = y >> 5, ry = y & 31;
        const uint8_t *row = src + (size_t)by * (stride / 16) * 512 + ry * 16;
        for (int bx = 0; bx < (w + 15) / 16; bx++) {
            int n = (w - bx * 16) < 16 ? (w - bx * 16) : 16;
            memcpy(dy + (size_t)y * w + bx * 16, row + (size_t)bx * 512, n);
        }
    }
    const uint8_t *uvb = src + (size_t)stride * sliceH;
    for (int y = 0; y < h / 2; y++) {
        int by = y >> 4, ry = y & 15;
        const uint8_t *row = uvb + (size_t)by * (stride / 16) * 256 + ry * 16;
        for (int cx = 0; cx < w / 2; cx++) {
            const uint8_t *p = row + (size_t)(cx >> 3) * 256 + (cx & 7) * 2;
            du[(size_t)y * (w / 2) + cx] = p[0];
            dv[(size_t)y * (w / 2) + cx] = p[1];
        }
    }
}

// Annex-B splitting into access units
struct AU { size_t off, len; };
static std::vector<AU> splitAUs(const uint8_t *d, size_t n) {
    std::vector<AU> out;
    // find NAL starts
    std::vector<size_t> nals;
    for (size_t i = 0; i + 3 < n; i++)
        if (d[i] == 0 && d[i + 1] == 0 && d[i + 2] == 1) nals.push_back(i);
    size_t auStart = (size_t)-1; bool haveSlice = false;
    for (size_t k = 0; k < nals.size(); k++) {
        size_t p = nals[k];
        size_t sc = (p > 0 && d[p - 1] == 0) ? p - 1 : p; // include 4-byte start code
        int t = d[p + 3] & 0x1f;
        bool first = false;
        if (t == 1 || t == 5) first = (d[p + 4] & 0x80); // first_mb_in_slice == 0
        bool newAu = false;
        if (t == 9) newAu = true;
        else if ((t == 7 || t == 8 || t == 6) && haveSlice) newAu = true;
        else if ((t == 1 || t == 5) && first && haveSlice) newAu = true;
        if (newAu && auStart != (size_t)-1) {
            out.push_back({auStart, sc - auStart}); auStart = sc; haveSlice = false;
        }
        if (auStart == (size_t)-1) auStart = sc;
        if (t == 1 || t == 5) haveSlice = true;
    }
    if (auStart != (size_t)-1) out.push_back({auStart, n - auStart});
    return out;
}

int main(int argc, char **argv) {
    if (argc < 3) { fprintf(stderr, "usage: %s in.h264 out.yuv [W H] [-f fmt] [-n max] [-v]\n", argv[0]); return 1; }
    const char *inPath = argv[1], *outPath = argv[2];
    int memType = 2; int W = 1280, H = 720, fmt = 2, maxFrames = 1 << 30, nfb = 12; int verbose = 0, raw = 0; std::vector<uint8_t> outBuf;
    for (int i = 3; i < argc; i++) {
        if (!strcmp(argv[i], "-f") && i + 1 < argc) fmt = atoi(argv[++i]);
        else if (!strcmp(argv[i], "-n") && i + 1 < argc) maxFrames = atoi(argv[++i]);
        else if (!strcmp(argv[i], "-b") && i + 1 < argc) nfb = atoi(argv[++i]);
        else if (!strcmp(argv[i], "-t") && i + 1 < argc) memType = atoi(argv[++i]);
        else if (!strcmp(argv[i], "-v")) verbose = 1;
        else if (!strcmp(argv[i], "-raw")) raw = 1;
        else if (i + 1 < argc && isdigit(argv[i][0]) && isdigit(argv[i + 1][0])) { W = atoi(argv[i]); H = atoi(argv[++i]); }
    }
    void *dl = dlopen("/system/lib/libvcodecdrv.so", RTLD_NOW | RTLD_GLOBAL);
    void *ut = dlopen("/system/lib/libvcodec_utility.so", RTLD_NOW | RTLD_GLOBAL);
    if (!dl || !ut) { fprintf(stderr, "dlopen: %s\n", dlerror()); return 2; }
    drvCreate = (pfnCreate)sym(dl, "eVDecDrvCreate"); drvQuery = (pfnQuery)sym(dl, "eVDecDrvQueryCapability");
    drvSet = (pfnSet)sym(dl, "eVDecDrvSetParam"); drvInit = (pfnInit)sym(dl, "eVDecDrvInit");
    drvDecode = (pfnDecode)sym(dl, "eVDecDrvDecode"); drvGet = (pfnGet)sym(dl, "eVDecDrvGetParam");
    drvDeInit = (pfnDeInit)sym(dl, "eVDecDrvDeInit"); drvRelease = (pfnRelease)sym(dl, "eVDecDrvRelease");
    memAlloc = (pfnMemAlloc)sym(ut, "eVideoMemAlloc");
    cacheFlush = (pfnCache)sym(ut, "eVideoFlushCache"); cacheInval = (pfnCache)sym(ut, "eVideoInvalidateCache");

    FILE *f = fopen(inPath, "rb");
    if (!f) { perror(inPath); return 1; }
    fseek(f, 0, SEEK_END); long fsz = ftell(f); fseek(f, 0, SEEK_SET);
    uint8_t *data = (uint8_t *)malloc(fsz);
    if (fread(data, 1, fsz, f) != (size_t)fsz) return 1;
    fclose(f);
    std::vector<AU> aus = splitAUs(data, fsz);
    printf("%zu access units\n", aus.size()); fflush(stdout);

    FILE *fo = fopen(outPath, "wb");
    if (!fo) { perror(outPath); return 1; }

    void *h = nullptr;
    int r = drvCreate(&h, 0x100);
    printf("Create -> %d h=%p\n", r, h); fflush(stdout);
    if (r) return 3;

    u32 caps[16]; memset(caps, 0, sizeof caps);
    r = drvQuery(3, nullptr, caps);
    printf("Query(3) -> %d caps: %u %u %u %u %u %u %u %u\n", r, caps[0], caps[1], caps[2], caps[3], caps[4], caps[5], caps[6], caps[7]);
    fflush(stdout);

    // bitstream buffer
    ValMem bsMem; u32 bsSize = 2 * 1024 * 1024;
    if (!allocMem(&bsMem, bsSize, 512, memType)) return 4;
    printf("bs va=%p mva=0x%x alignVa=%p alignMva=0x%x\n", bsMem.va, bsMem.mva, bsMem.alignVa, bsMem.alignMva); fflush(stdout);
    Bs bs; memset(&bs, 0, sizeof bs);
    auto fillBs = [&](const AU &a) {
        memcpy(bsMem.alignVa, data + a.off, a.len);
        cacheFlush(&bsMem, sizeof bsMem, 1);
        bs.va = (u32)bsMem.alignVa; bs.mva = bsMem.alignMva; bs.size = a.len;
        bs.rd = bs.va; bs.wr = bs.va + a.len; bs.orgva = bs.va;
    };

    u32 sz = (W) | (H << 16);
    r = drvSet(h, 19, &sz, nullptr);
    printf("SetParam(19 size %dx%d) -> %d\n", W, H, r); fflush(stdout);

    // first AUs until first slice: SPS/PPS/IDR is in aus[0]
    fillBs(aus[0]);
    u32 out[16]; memset(out, 0, sizeof out);
    r = drvInit(h, &bs, out);
    printf("Init -> %d out: %u %u %u %u %u %u %u\n", r, out[0], out[1], out[2], out[3], out[4], out[5], out[6]); fflush(stdout);
    if (r) return 5;

    u32 ca[3] = {(u32)fmt, 16, 8};
    r = drvSet(h, 17, ca, nullptr);
    u32 cb[2] = {16, 8};
    int r2 = drvSet(h, 18, cb, nullptr);
    printf("SetParam(17 fmt %d)=%d SetParam(18)=%d\n", fmt, r, r2); fflush(stdout);

    u32 dpb = 0;
    r = drvGet(h, 22, nullptr, &dpb);
    printf("Get(22 dpb) -> %d %u\n", r, dpb); fflush(stdout);

    // frame buffers
    int AW = (W + 15) & ~15, AH = (H + 31) & ~31;
    u32 fbSize = AW * AH * 3 / 2 + 4096;
    std::vector<Fb *> fbs; std::vector<ValMem> fbm(nfb); std::vector<Fb *> freeList;
    for (int i = 0; i < nfb; i++) {
        if (!allocMem(&fbm[i], fbSize, 512, memType)) return 6;
        memset(fbm[i].alignVa, 0x55, fbSize);
        cacheFlush(&fbm[i], sizeof(ValMem), 1);
        Fb *fb = (Fb *)calloc(1, sizeof(Fb));
        fb->w[0] = (u32)fbm[i].alignVa; fb->w[1] = fbm[i].alignMva; fb->w[2] = fbSize;
        fbs.push_back(fb); freeList.push_back(fb);
    }
    auto fbIndex = [&](Fb *p) { for (size_t i = 0; i < fbs.size(); i++) if (fbs[i] == p) return (int)i; return -1; };
    printf("allocated %d fbs of %u (aligned %dx%d) first va=0x%x mva=0x%x\n", nfb, fbSize, AW, AH, fbs[0]->w[0], fbs[0]->w[1]);
    fflush(stdout);

    double tInv = 0, tDet = 0; int nout = 0; double t0 = now(); int ndec = 0;
    auto drain = [&]() {
        for (;;) {
            Fb *d = nullptr;
            drvGet(h, 2, nullptr, &d);
            if (!d) break;
            int idx = fbIndex(d);
            u32 pi[9]; memset(pi, 0, sizeof pi);
            drvGet(h, 4, nullptr, pi);
            if (verbose || nout < 3)
                printf("disp fb#%d pic %u %u %u %u %u %u %u %u %u\n", idx, pi[0], pi[1], pi[2], pi[3], pi[4], pi[5], pi[6], pi[7], pi[8]);
            if (idx >= 0 && nout < maxFrames) {
                double ta = now(); cacheInval(&fbm[idx], sizeof(ValMem), 1); tInv += now() - ta; ta = now();
                if (raw) fwrite(fbm[idx].alignVa, 1, AW * AH * 3 / 2, fo);
                else {
                    int ow = pi[0] ? pi[0] : W, oh = pi[1] ? pi[1] : H;
                    int st = pi[2] ? pi[2] : AW, sh = pi[3] ? pi[3] : AH;
                    outBuf.resize((size_t)ow * oh * 3 / 2);
                    hulkDetileI420((const uint8_t *)fbm[idx].alignVa, st, sh, ow, oh, outBuf.data(), ow, outBuf.data() + ow * oh, outBuf.data() + ow * oh + (ow / 2) * (oh / 2), ow / 2);
                    tDet += now() - ta; fwrite(outBuf.data(), 1, outBuf.size(), fo);
                }
            }
            nout++;
        }
        for (;;) {
            Fb *fr = nullptr;
            drvGet(h, 3, nullptr, &fr);
            if (!fr) break;
            if (verbose) printf("free fb#%d\n", fbIndex(fr));
            freeList.push_back(fr);
        }
    };

    for (size_t i = 0; i < aus.size() && nout < maxFrames; i++) {
        fillBs(aus[i]);
        Fb *fb = nullptr;
        if (!freeList.empty()) { fb = freeList.back(); freeList.pop_back(); }
        int rc = drvDecode(h, &bs, fb);
        ndec++;
        if (verbose || i < 3) { printf("Decode #%zu len=%zu fb=#%d -> %d\n", i, aus[i].len, fb ? fbIndex(fb) : -1, rc); fflush(stdout); }
        drain();
    }
    int rc = drvDecode(h, nullptr, nullptr);
    printf("flush -> %d\n", rc); fflush(stdout);
    for (int k = 0; k < 4; k++) drain();
    double t1 = now();
    printf("decoded %d AUs, output %d frames in %.3fs = %.1f fps\n", ndec, nout, t1 - t0, nout / (t1 - t0));
    printf("inval %.3fs detile %.3fs\n", tInv, tDet);
    fclose(fo);
    drvDeInit(h);
    drvRelease(h);
    return 0;
}

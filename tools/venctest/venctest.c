// venctest: raw I420 -> H.264 Annex-B using MTK HW encoder via libvcodecdrv.so (eVEncDrv*), no OMX.
// usage: venctest in.yuv out.h264 W H [-b bps] [-g gopFrames] [-f fps] [-n maxFrames] [-p prof] [-l level]
//                [-F fmt(3=I420,5=YV12)] [-t memtype] [-s (prepend SPS/PPS to each IDR)] [-v]
// NEVER calls eVEncDrvQueryCapability(1) (drvb anti-tamper stall).
#include <dlfcn.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdint.h>
#include <time.h>
typedef uint32_t u32;

typedef struct { u32 type, size; void *va; u32 mva, align; void *alignVa; u32 alignMva, codec, r32, r36; void *m4u; u32 r44; } ValMem;
typedef int (*pfnCreate)(void **h, u32 id);
typedef int (*pfnSet)(void *h, u32 type, void *p, void *p2);
typedef int (*pfnInit)(void *h);
typedef int (*pfnEnc)(void *h, u32 type, void *in, void *bs, void *res);
typedef int (*pfnDe)(void *h);
typedef int (*pfnMem)(ValMem *m, u32 sz);
typedef int (*pfnCache)(ValMem *m, u32 sz, u32 mode);

static pfnCreate drvCreate; static pfnSet drvSet; static pfnInit drvInit; static pfnEnc drvEnc;
static pfnDe drvDeInit, drvRelease; static pfnMem memAlloc; static pfnCache cacheFlush, cacheInval;

static double now(void) { struct timespec t; clock_gettime(CLOCK_MONOTONIC, &t); return t.tv_sec + t.tv_nsec * 1e-9; }
static void *sym(void *l, const char *n) { void *p = dlsym(l, n); if (!p) { fprintf(stderr, "missing %s\n", n); exit(2); } return p; }
static int allocMem(ValMem *m, u32 size, u32 align, u32 type) {
    memset(m, 0, sizeof *m); m->type = type; m->size = size; m->align = align;
    int r = memAlloc(m, sizeof *m);
    if (r) fprintf(stderr, "eVideoMemAlloc(%u) failed %d\n", size, r);
    return r == 0;
}

int main(int argc, char **argv) {
    if (argc < 5) { fprintf(stderr, "usage: %s in.yuv out.h264 W H [-b bps] [-g gop] [-f fps] [-n max] [-p prof] [-l level] [-F fmt] [-t memtype] [-s] [-v]\n", argv[0]); return 1; }
    const char *inPath = argv[1], *outPath = argv[2];
    int W = atoi(argv[3]), H = atoi(argv[4]);
    u32 bps = 4000000, gop = 30, fps = 30, prof = 16, level = 0, fmt = 3, memType = 2;
    int maxFrames = 1 << 30, verbose = 0, spsEvery = 0, forceAt = -1;
    for (int i = 5; i < argc; i++) {
        if (!strcmp(argv[i], "-b") && i + 1 < argc) bps = atoi(argv[++i]);
        else if (!strcmp(argv[i], "-g") && i + 1 < argc) gop = atoi(argv[++i]);
        else if (!strcmp(argv[i], "-f") && i + 1 < argc) fps = atoi(argv[++i]);
        else if (!strcmp(argv[i], "-n") && i + 1 < argc) maxFrames = atoi(argv[++i]);
        else if (!strcmp(argv[i], "-p") && i + 1 < argc) prof = atoi(argv[++i]);
        else if (!strcmp(argv[i], "-l") && i + 1 < argc) level = atoi(argv[++i]);
        else if (!strcmp(argv[i], "-F") && i + 1 < argc) fmt = atoi(argv[++i]);
        else if (!strcmp(argv[i], "-t") && i + 1 < argc) memType = atoi(argv[++i]);
        else if (!strcmp(argv[i], "-k") && i + 1 < argc) forceAt = atoi(argv[++i]);
        else if (!strcmp(argv[i], "-s")) spsEvery = 1;
        else if (!strcmp(argv[i], "-v")) verbose = 1;
    }
    if (!level) { long mbs = ((W + 15) / 16) * ((H + 15) / 16); level = mbs > 3600 ? 13 : (mbs > 1620 ? 11 : 10); } // 4.0 / 3.1 / 3.0
    int BW = (W + 15) & ~15, BH = (H + 15) & ~15;

    void *dl = dlopen("/system/lib/libvcodecdrv.so", RTLD_NOW | RTLD_GLOBAL);
    void *ut = dlopen("/system/lib/libvcodec_utility.so", RTLD_NOW | RTLD_GLOBAL);
    if (!dl || !ut) { fprintf(stderr, "dlopen: %s\n", dlerror()); return 2; }
    drvCreate = sym(dl, "eVEncDrvCreate"); drvSet = sym(dl, "eVEncDrvSetParam"); drvInit = sym(dl, "eVEncDrvInit");
    drvEnc = sym(dl, "eVEncDrvEncode"); drvDeInit = sym(dl, "eVEncDrvDeInit"); drvRelease = sym(dl, "eVEncDrvRelease");
    memAlloc = sym(ut, "eVideoMemAlloc"); cacheFlush = sym(ut, "eVideoFlushCache"); cacheInval = sym(ut, "eVideoInvalidateCache");

    FILE *fi = fopen(inPath, "rb"); if (!fi) { perror(inPath); return 1; }
    FILE *fo = fopen(outPath, "wb"); if (!fo) { perror(outPath); return 1; }

    void *h = NULL; int r;
    r = drvCreate(&h, 5); printf("Create -> %d h=%p\n", r, h); if (r) return 3;
    u32 emi[4] = { (u32)W, (u32)H, (u32)BW, (u32)BH };
    r = drvSet(h, 20, emi, 0); printf("SetParam(20 EMI) -> %d\n", r);
    r = drvInit(h); printf("Init -> %d\n", r); if (r) return 4;

    u32 extra[4] = { gop, bps, fps << 16, 0 };
    u32 p[32]; memset(p, 0, sizeof p);
    p[0] = fmt; p[1] = prof; p[2] = level; p[3] = W; p[4] = H; p[5] = BW; p[6] = BH; p[7] = 29; p[8] = 0; p[9] = fps;
    p[11] = (u32)(uintptr_t)extra;
    r = drvSet(h, 6, p, 0); printf("SetParam(6) fmt=%u prof=%u level=%u %dx%d buf %dx%d gop=%u bps=%u fps=%u -> %d\n", fmt, prof, level, W, H, BW, BH, gop, bps, fps, r);
    if (r) return 5;
    if (spsEvery) { r = drvSet(h, 26, NULL, 0); printf("SetParam(26 sps every IDR) -> %d\n", r); }
    drvSet(h, 7, 0, 0);

    u32 inSize = BW * BH * 3 / 2, bsCap = 2 * 1024 * 1024;
    ValMem inM, bsM;
    if (!allocMem(&inM, inSize + 4096, 512, memType)) return 6;
    if (!allocMem(&bsM, bsCap, 512, memType)) return 6;
    printf("in va=%p mva=0x%x ; bs va=%p mva=0x%x\n", inM.alignVa, inM.alignMva, bsM.alignVa, bsM.alignMva);
    u32 bs[8]; u32 res[16];
    #define SETBS() do { memset(bs, 0, sizeof bs); bs[0] = (u32)(uintptr_t)bsM.alignVa; bs[1] = bsM.alignMva; bs[2] = bsCap; bs[3] = bs[0]; memset(res, 0, sizeof res); res[0] = 1; } while (0)

    // headers
    SETBS(); cacheFlush(&bsM, sizeof bsM, 1);
    r = drvEnc(h, 1, NULL, bs, res);
    cacheInval(&bsM, sizeof bsM, 1);
    printf("Encode(1 hdr) -> %d status=%u len=%u :", r, res[0], bs[4]);
    { uint8_t *b = bsM.alignVa; for (u32 i = 0; i < bs[4] && i < 48; i++) printf(" %02x", b[i]); printf("\n"); }
    if (bs[4] && bs[4] < bsCap) fwrite(bsM.alignVa, 1, bs[4], fo);

    // frame loop
    uint8_t *yuv = malloc((size_t)W * H * 3 / 2);
    uint8_t *dst = inM.alignVa;
    int cw = W / 2, ch = H / 2, bcw = BW / 2, bch = BH / 2;
    long total = 0; int nf = 0, nkey = 0, ndrop = 0; double tEnc = 0, t0 = now();
    while (nf + ndrop < maxFrames && fread(yuv, 1, (size_t)W * H * 3 / 2, fi) == (size_t)W * H * 3 / 2) {
        int idx = nf + ndrop;
        // copy into padded planes (replicate edges)
        uint8_t *dy = dst, *d1 = dst + BW * BH, *d2 = d1 + bcw * bch;
        const uint8_t *sy = yuv, *su = yuv + W * H, *sv = su + cw * ch;
        for (int y = 0; y < BH; y++) { int sr = y < H ? y : H - 1; memcpy(dy + y * BW, sy + sr * W, W); if (BW > W) memset(dy + y * BW + W, sy[sr * W + W - 1], BW - W); }
        const uint8_t *s1 = fmt == 5 ? sv : su, *s2 = fmt == 5 ? su : sv;
        for (int y = 0; y < bch; y++) { int sr = y < ch ? y : ch - 1;
            memcpy(d1 + y * bcw, s1 + sr * cw, cw); memcpy(d2 + y * bcw, s2 + sr * cw, cw);
            if (bcw > cw) { memset(d1 + y * bcw + cw, s1[sr * cw + cw - 1], bcw - cw); memset(d2 + y * bcw + cw, s2[sr * cw + cw - 1], bcw - cw); } }
        cacheFlush(&inM, sizeof inM, 1);
        if (idx == forceAt) { r = drvSet(h, 11, NULL, 0); printf("force IDR at %d -> %d\n", idx, r); }
        u32 frm[8] = { (u32)(uintptr_t)inM.alignVa, inM.alignMva, inSize, 0, 0, 0, (u32)idx, 0 };
        SETBS();
        double ta = now();
        r = drvEnc(h, 4, frm, bs, res);
        tEnc += now() - ta;
        cacheInval(&bsM, sizeof bsM, 1);
        u32 len = bs[4]; uint8_t key = ((uint8_t *)res)[12];
        if (verbose || r || (res[0] != 4 && res[0] != 1) || idx < 3) printf("frame %d rc=%d status=%u len=%u key=%u\n", idx, r, res[0], len, key);
        if (r) break;
        if (res[0] != 3 && len && len <= bsCap) { fwrite(bsM.alignVa, 1, len, fo); total += len; nf++; nkey += key; if (key && !verbose) printf("  IDR at frame %d (len %u)\n", idx, len); }
        else if (res[0] == 3 || !len) ndrop++;
        else break;
    }
    double t1 = now();
    int n = nf + ndrop;
    printf("encoded %d frames (%d dropped, %d key) payload %ld B; encode-call time %.3fs = %.1f fps; wall %.3fs = %.1f fps\n",
           nf, ndrop, nkey, total, tEnc, n / tEnc, t1 - t0, n / (t1 - t0));
    printf("bitrate %.0f bps (target %u) at %u fps nominal\n", total * 8.0 / ((double)n / fps), bps, fps);
    fclose(fo);
    drvSet(h, 8, 0, 0); drvDeInit(h); drvRelease(h);
    return 0;
}

// mctest: raw YUV -> H.264 through MediaCodec (NDK AMediaCodec), ByteBuffer input.
// usage: mctest in.yuv out.h264 W H [-b bps] [-f fps] [-i iframeSec] [-c colorFormat(19=I420,21=NV12)] [-n maxFrames] [-N name] [-S gralloc-format: 1=RGBA 842094169=YV12 17=NV21]
// -S uses surface input (like camera/screenrecord): frames are written into the encoder's Surface.
// in.yuv is planar I420 frames; with -c 21 they are interleaved on the fly.
#include <media/NdkMediaCodec.h>
#include <media/NdkMediaFormat.h>
#include <android/native_window.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdint.h>
#include <time.h>
#include <unistd.h>

static double now(void) { struct timespec t; clock_gettime(CLOCK_MONOTONIC, &t); return t.tv_sec + t.tv_nsec * 1e-9; }
static double cpuNow(void) { struct timespec t; clock_gettime(CLOCK_PROCESS_CPUTIME_ID, &t); return t.tv_sec + t.tv_nsec * 1e-9; }

int main(int argc, char **argv) {
    if (argc < 5) { fprintf(stderr, "usage: %s in.yuv out.h264 W H [-b bps] [-f fps] [-i sec] [-c fmt] [-n max] [-N name]\n", argv[0]); return 1; }
    int W = atoi(argv[3]), H = atoi(argv[4]);
    int bps = 4000000, fps = 30, iint = 1, color = 19, maxFrames = 1 << 30;
    const char *name = NULL; int surfFmt = 0;
    for (int i = 5; i < argc; i++) {
        if (!strcmp(argv[i], "-b") && i + 1 < argc) bps = atoi(argv[++i]);
        else if (!strcmp(argv[i], "-f") && i + 1 < argc) fps = atoi(argv[++i]);
        else if (!strcmp(argv[i], "-i") && i + 1 < argc) iint = atoi(argv[++i]);
        else if (!strcmp(argv[i], "-c") && i + 1 < argc) color = atoi(argv[++i]);
        else if (!strcmp(argv[i], "-n") && i + 1 < argc) maxFrames = atoi(argv[++i]);
        else if (!strcmp(argv[i], "-S") && i + 1 < argc) surfFmt = atoi(argv[++i]);
        else if (!strcmp(argv[i], "-N") && i + 1 < argc) name = argv[++i];
    }
    FILE *fi = fopen(argv[1], "rb"), *fo = fopen(argv[2], "wb");
    if (!fi || !fo) { perror("open"); return 1; }
    size_t fsz = (size_t)W * H * 3 / 2;
    uint8_t *yuv = malloc(fsz);

    AMediaCodec *c = name ? AMediaCodec_createCodecByName(name) : AMediaCodec_createEncoderByType("video/avc");
    if (!c) { fprintf(stderr, "create failed\n"); return 2; }
    AMediaFormat *f = AMediaFormat_new();
    AMediaFormat_setString(f, AMEDIAFORMAT_KEY_MIME, "video/avc");
    AMediaFormat_setInt32(f, AMEDIAFORMAT_KEY_WIDTH, W);
    AMediaFormat_setInt32(f, AMEDIAFORMAT_KEY_HEIGHT, H);
    AMediaFormat_setInt32(f, AMEDIAFORMAT_KEY_BIT_RATE, bps);
    AMediaFormat_setInt32(f, AMEDIAFORMAT_KEY_FRAME_RATE, fps);
    AMediaFormat_setInt32(f, AMEDIAFORMAT_KEY_I_FRAME_INTERVAL, iint);
    AMediaFormat_setInt32(f, AMEDIAFORMAT_KEY_COLOR_FORMAT, surfFmt ? 0x7F000789 : color);
    media_status_t st = AMediaCodec_configure(c, f, NULL, NULL, AMEDIACODEC_CONFIGURE_FLAG_ENCODE);
    if (st) { fprintf(stderr, "configure -> %d\n", st); return 3; }
    ANativeWindow *win = NULL;
    if (surfFmt) {
        st = AMediaCodec_createInputSurface(c, &win);
        if (st || !win) { fprintf(stderr, "createInputSurface -> %d\n", st); return 3; }
        ANativeWindow_setBuffersGeometry(win, W, H, surfFmt);
    }
    st = AMediaCodec_start(c);
    if (st) { fprintf(stderr, "start -> %d\n", st); return 3; }

    int inN = 0, outN = 0, eosSent = 0, done = 0, key = 0;
    long bytes = 0;
    double t0 = now(), c0 = cpuNow();
    while (!done) {
        if (!eosSent && surfFmt) {
            ANativeWindow_Buffer b;
            if (inN < maxFrames && fread(yuv, 1, fsz, fi) == fsz) {
                if (ANativeWindow_lock(win, &b, NULL)) { fprintf(stderr, "lock failed\n"); return 4; }
                const uint8_t *Y = yuv, *U = yuv + (size_t)W * H, *V = U + (size_t)W * H / 4;
                uint8_t *d = b.bits;
                if (surfFmt == 842094169) {          // YV12: Y, V, U; chroma stride align16(stride/2)
                    size_t cs = ((b.stride / 2) + 15) & ~15;
                    for (int y = 0; y < H; y++) memcpy(d + (size_t)y * b.stride, Y + (size_t)y * W, W);
                    uint8_t *dv = d + (size_t)b.stride * b.height, *du = dv + cs * (b.height / 2);
                    for (int y = 0; y < H / 2; y++) { memcpy(dv + y * cs, V + (size_t)y * (W / 2), W / 2); memcpy(du + y * cs, U + (size_t)y * (W / 2), W / 2); }
                } else if (surfFmt == 17) {          // NV21: Y, VU
                    for (int y = 0; y < H; y++) memcpy(d + (size_t)y * b.stride, Y + (size_t)y * W, W);
                    uint8_t *dc = d + (size_t)b.stride * b.height;
                    for (int y = 0; y < H / 2; y++) for (int x = 0; x < W / 2; x++) { dc[(size_t)y * b.stride + 2 * x] = V[(size_t)y * (W / 2) + x]; dc[(size_t)y * b.stride + 2 * x + 1] = U[(size_t)y * (W / 2) + x]; }
                } else {                              // RGBA from the luma only (grey ramp test)
                    for (int y = 0; y < H; y++) for (int x = 0; x < W; x++) { uint8_t g = Y[(size_t)y * W + x]; uint8_t *p = d + ((size_t)y * b.stride + x) * 4; p[0] = p[1] = p[2] = g; p[3] = 255; }
                }
                ANativeWindow_unlockAndPost(win);
                inN++;
                usleep(1000000 / fps / 2);
            } else {
                AMediaCodec_signalEndOfInputStream(c);
                eosSent = 1;
            }
        } else if (!eosSent) {
            ssize_t ib = AMediaCodec_dequeueInputBuffer(c, 2000);
            if (ib >= 0) {
                size_t cap; uint8_t *buf = AMediaCodec_getInputBuffer(c, ib, &cap);
                if (inN < maxFrames && fread(yuv, 1, fsz, fi) == fsz) {
                    if (color == 21) {
                        memcpy(buf, yuv, (size_t)W * H);
                        uint8_t *uv = buf + (size_t)W * H; const uint8_t *u = yuv + (size_t)W * H, *v = u + (size_t)W * H / 4;
                        for (size_t k = 0; k < (size_t)W * H / 4; k++) { uv[2 * k] = u[k]; uv[2 * k + 1] = v[k]; }
                    } else memcpy(buf, yuv, fsz);
                    AMediaCodec_queueInputBuffer(c, ib, 0, fsz, (uint64_t)inN * 1000000 / fps, 0);
                    inN++;
                } else {
                    AMediaCodec_queueInputBuffer(c, ib, 0, 0, (uint64_t)inN * 1000000 / fps, AMEDIACODEC_BUFFER_FLAG_END_OF_STREAM);
                    eosSent = 1;
                }
            }
        }
        AMediaCodecBufferInfo info;
        ssize_t ob = AMediaCodec_dequeueOutputBuffer(c, &info, 2000);
        if (ob >= 0) {
            size_t cap; uint8_t *buf = AMediaCodec_getOutputBuffer(c, ob, &cap);
            if (info.size > 0) { fwrite(buf + info.offset, 1, info.size, fo); bytes += info.size; }
            if (!(info.flags & AMEDIACODEC_BUFFER_FLAG_CODEC_CONFIG)) { outN++; if (info.flags & 1) key++; }
            if (info.flags & AMEDIACODEC_BUFFER_FLAG_END_OF_STREAM) done = 1;
            AMediaCodec_releaseOutputBuffer(c, ob, 0);
        }
    }
    double t1 = now(), c1 = cpuNow();
    printf("%dx%d color %d: in %d out %d (key %d) bytes %ld bitrate %.0f bps; wall %.3fs = %.1f fps; test-process cpu %.2fs\n",
           W, H, color, inN, outN, key, bytes, bytes * 8.0 / (inN / (double)fps), t1 - t0, outN / (t1 - t0), c1 - c0);
    AMediaCodec_stop(c);
    AMediaCodec_delete(c);
    fclose(fo);
    return 0;
}

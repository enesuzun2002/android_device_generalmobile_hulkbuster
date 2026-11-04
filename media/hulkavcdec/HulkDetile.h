/*
 * Conversion from the MediaTek "block" frame layout produced by libvcodecdrv
 * (Y: 16x32 byte tiles, UV: NV12-interleaved 16x16 byte tiles, both in raster
 * order) to cropped planar I420 with arbitrary destination strides.
 */
#ifndef HULK_DETILE_H_
#define HULK_DETILE_H_

#include <stddef.h>
#include <stdint.h>
#include <string.h>
#if defined(__ARM_NEON__) || defined(__ARM_NEON)
#include <arm_neon.h>
#endif

static inline void hulkDetileI420(
        const uint8_t *src, int srcStride, int srcSliceH, int w, int h,
        uint8_t *dstY, int dstYStride, uint8_t *dstU, uint8_t *dstV, int dstCStride) {
    const int tilesPerRow = srcStride / 16;

    // ---- luma ----
    for (int y0 = 0; y0 < h; y0 += 32) {
        const int rows = (h - y0) < 32 ? (h - y0) : 32;
        const uint8_t *tileRow = src + (size_t)(y0 >> 5) * tilesPerRow * 512;
        uint8_t *d = dstY + (size_t)y0 * dstYStride;
        int bx = 0;
#if defined(__ARM_NEON__) || defined(__ARM_NEON)
        if (rows == 32) {
            // four tiles at a time: every destination row gets a full 64-byte burst
            for (; (bx + 4) * 16 <= w; bx += 4) {
                const uint8_t *t = tileRow + (size_t)bx * 512;
                uint8_t *dd = d + bx * 16;
                for (int k = 0; k < 4 * 512; k += 64) __builtin_prefetch(t + 4 * 512 + k);
                for (int r = 0; r < 32; r++) {
                    uint8x16_t a0 = vld1q_u8(t + r * 16), a1 = vld1q_u8(t + 512 + r * 16);
                    uint8x16_t a2 = vld1q_u8(t + 1024 + r * 16), a3 = vld1q_u8(t + 1536 + r * 16);
                    uint8_t *o = dd + (size_t)r * dstYStride;
                    vst1q_u8(o, a0); vst1q_u8(o + 16, a1); vst1q_u8(o + 32, a2); vst1q_u8(o + 48, a3);
                }
            }
        }
#endif
        for (; bx * 16 < w; bx++) {
            const uint8_t *t = tileRow + (size_t)bx * 512;
            uint8_t *dd = d + bx * 16;
            const int n = (w - bx * 16) < 16 ? (w - bx * 16) : 16;
            if (n == 16) {
#if defined(__ARM_NEON__) || defined(__ARM_NEON)
                __builtin_prefetch(t + 512 * 2);
                __builtin_prefetch(t + 512 * 2 + 64);
                __builtin_prefetch(t + 512 * 2 + 128);
                __builtin_prefetch(t + 512 * 2 + 192);
                __builtin_prefetch(t + 512 * 2 + 256);
                __builtin_prefetch(t + 512 * 2 + 320);
                __builtin_prefetch(t + 512 * 2 + 384);
                __builtin_prefetch(t + 512 * 2 + 448);
                int r = 0;
                for (; r + 8 <= rows; r += 8) {
                    uint8x16_t a0 = vld1q_u8(t + r * 16 + 0), a1 = vld1q_u8(t + r * 16 + 16);
                    uint8x16_t a2 = vld1q_u8(t + r * 16 + 32), a3 = vld1q_u8(t + r * 16 + 48);
                    uint8x16_t a4 = vld1q_u8(t + r * 16 + 64), a5 = vld1q_u8(t + r * 16 + 80);
                    uint8x16_t a6 = vld1q_u8(t + r * 16 + 96), a7 = vld1q_u8(t + r * 16 + 112);
                    uint8_t *o = dd + (size_t)r * dstYStride;
                    vst1q_u8(o, a0); o += dstYStride; vst1q_u8(o, a1); o += dstYStride;
                    vst1q_u8(o, a2); o += dstYStride; vst1q_u8(o, a3); o += dstYStride;
                    vst1q_u8(o, a4); o += dstYStride; vst1q_u8(o, a5); o += dstYStride;
                    vst1q_u8(o, a6); o += dstYStride; vst1q_u8(o, a7);
                }
                for (; r < rows; r++) {
                    vst1q_u8(dd + (size_t)r * dstYStride, vld1q_u8(t + r * 16));
                }
#else
                for (int r = 0; r < rows; r++) {
                    memcpy(dd + (size_t)r * dstYStride, t + r * 16, 16);
                }
#endif
            } else {
                for (int r = 0; r < rows; r++) {
                    memcpy(dd + (size_t)r * dstYStride, t + r * 16, n);
                }
            }
        }
    }

    // ---- chroma ----
    const int cw = w / 2, ch = h / 2;
    const uint8_t *uvBase = src + (size_t)srcStride * srcSliceH;
    for (int y0 = 0; y0 < ch; y0 += 16) {
        const int rows = (ch - y0) < 16 ? (ch - y0) : 16;
        const uint8_t *tileRow = uvBase + (size_t)(y0 >> 4) * tilesPerRow * 256;
        uint8_t *du = dstU + (size_t)y0 * dstCStride;
        uint8_t *dv = dstV + (size_t)y0 * dstCStride;
        int bx = 0;
#if defined(__ARM_NEON__) || defined(__ARM_NEON)
        if (rows == 16) {
            // eight chroma tiles at a time: 64-byte bursts into both planes
            for (; (bx + 8) * 8 <= cw; bx += 8) {
                const uint8_t *t = tileRow + (size_t)bx * 256;
                for (int k = 0; k < 8 * 256; k += 64) __builtin_prefetch(t + 8 * 256 + k);
                for (int r = 0; r < 16; r++) {
                    uint8x8x2_t p0 = vld2_u8(t + r * 16), p1 = vld2_u8(t + 256 + r * 16);
                    uint8x8x2_t p2 = vld2_u8(t + 512 + r * 16), p3 = vld2_u8(t + 768 + r * 16);
                    uint8x8x2_t p4 = vld2_u8(t + 1024 + r * 16), p5 = vld2_u8(t + 1280 + r * 16);
                    uint8x8x2_t p6 = vld2_u8(t + 1536 + r * 16), p7 = vld2_u8(t + 1792 + r * 16);
                    uint8_t *ou = du + (size_t)r * dstCStride + bx * 8;
                    uint8_t *ov = dv + (size_t)r * dstCStride + bx * 8;
                    vst1q_u8(ou, vcombine_u8(p0.val[0], p1.val[0])); vst1q_u8(ou + 16, vcombine_u8(p2.val[0], p3.val[0]));
                    vst1q_u8(ou + 32, vcombine_u8(p4.val[0], p5.val[0])); vst1q_u8(ou + 48, vcombine_u8(p6.val[0], p7.val[0]));
                    vst1q_u8(ov, vcombine_u8(p0.val[1], p1.val[1])); vst1q_u8(ov + 16, vcombine_u8(p2.val[1], p3.val[1]));
                    vst1q_u8(ov + 32, vcombine_u8(p4.val[1], p5.val[1])); vst1q_u8(ov + 48, vcombine_u8(p6.val[1], p7.val[1]));
                }
            }
        }
#endif
        for (; bx * 8 < cw; bx++) {
            const uint8_t *t = tileRow + (size_t)bx * 256;
            const int n = (cw - bx * 8) < 8 ? (cw - bx * 8) : 8;
            if (n == 8) {
#if defined(__ARM_NEON__) || defined(__ARM_NEON)
                __builtin_prefetch(t + 256 * 2);
                __builtin_prefetch(t + 256 * 2 + 64);
                __builtin_prefetch(t + 256 * 2 + 128);
                __builtin_prefetch(t + 256 * 2 + 192);
                for (int r = 0; r < rows; r++) {
                    uint8x8x2_t p = vld2_u8(t + r * 16);
                    vst1_u8(du + (size_t)r * dstCStride + bx * 8, p.val[0]);
                    vst1_u8(dv + (size_t)r * dstCStride + bx * 8, p.val[1]);
                }
                continue;
#endif
            }
            for (int r = 0; r < rows; r++) {
                for (int i = 0; i < n; i++) {
                    du[(size_t)r * dstCStride + bx * 8 + i] = t[r * 16 + 2 * i];
                    dv[(size_t)r * dstCStride + bx * 8 + i] = t[r * 16 + 2 * i + 1];
                }
            }
        }
    }
}

#endif  // HULK_DETILE_H_

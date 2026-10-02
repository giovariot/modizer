//
//  CtUtil.h
//  ModizerMini
//
//  Tiny helpers shared by the backend files.
//

#ifndef CT_UTIL_H
#define CT_UTIL_H

#include <stdint.h>
#include <string.h>
#include <stdio.h>
#include <math.h>

static inline void ctCopyStr(char *dst, size_t cap, const char *src) {
    if (!dst || cap == 0) return;
    if (!src) { dst[0] = '\0'; return; }
    snprintf(dst, cap, "%s", src);
}

static inline void ctTrim(char *s) {
    if (!s) return;
    size_t n = strlen(s);
    while (n > 0 && (s[n - 1] == ' ' || s[n - 1] == '\t' || s[n - 1] == '\n' || s[n - 1] == '\r')) s[--n] = '\0';
    size_t start = 0;
    while (s[start] == ' ') start++;
    if (start) memmove(s, s + start, strlen(s + start) + 1);
}

static inline int16_t ctClip16(float v) {
    if (v > 32767.0f) return 32767;
    if (v < -32768.0f) return -32768;
    return (int16_t)lrintf(v);
}

/// Streaming linear resampler for backends whose native rate is not 44100.
/// Feed it successive equal-size input buffers; it yields a variable number of
/// output frames per call and keeps interpolation state across buffers.
struct CtStreamResampler {
    double ratio = 1.0;      // input frames per output frame
    double next = 0.0;       // input time of the next output frame
    int64_t base = 0;        // input frame index of buffers[0]
    int16_t prev[2] = {0, 0};

    void reset() { next = 0.0; base = 0; prev[0] = prev[1] = 0; }

    /// Resamples one input buffer; returns how many output frames were written.
    int process(const int16_t *in, int n, int16_t *out, int outCap) {
        if (n <= 0 || outCap <= 0) return 0;
        int produced = 0;
        while (produced < outCap) {
            double t = next;
            if (t >= (double)(base + n)) break;
            int64_t i0 = (int64_t)floor(t);
            float f = (float)(t - (double)i0);
            int16_t a0[2], a1[2];
            if (i0 < base) { a0[0] = prev[0]; a0[1] = prev[1]; }
            else { int k = (int)(i0 - base); a0[0] = in[k * 2]; a0[1] = in[k * 2 + 1]; }
            int64_t i1 = i0 + 1;
            if (i1 >= base + n) break;   // need the next buffer
            int k1 = (int)(i1 - base);
            a1[0] = in[k1 * 2];
            a1[1] = in[k1 * 2 + 1];
            out[produced * 2] = ctClip16(a0[0] + (a1[0] - a0[0]) * f);
            out[produced * 2 + 1] = ctClip16(a0[1] + (a1[1] - a0[1]) * f);
            produced++;
            next += ratio;
        }
        prev[0] = in[(n - 1) * 2];
        prev[1] = in[(n - 1) * 2 + 1];
        base += n;
        return produced;
    }
};

#endif /* CT_UTIL_H */

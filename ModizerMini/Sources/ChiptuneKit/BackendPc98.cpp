//
//  BackendPc98.cpp
//  ModizerMini  —  FMP (libfmpmini) and PMD (libpmdmini)
//

#include "ChiptuneBackend.h"
#include "CtUtil.h"
#include <string.h>
#include <stdio.h>

#include "fmpmini.h"

namespace {

struct FmpBackend : CtDecoder {
    static const int nativeFrames = 2048;
    static const int fifoCap = 8192;
    CtStreamResampler res;
    int16_t *in = nullptr;
    int16_t *fifo = nullptr;
    int fifoAvail = 0;
    bool ended_ = false;
    char nameBuf[64];

    ~FmpBackend() override { fmpmini_close(); free(in); free(fifo); }

    FmpBackend() {
        in = (int16_t *)calloc((size_t)nativeFrames * 2, sizeof(int16_t));
        fifo = (int16_t *)calloc((size_t)fifoCap * 2, sizeof(int16_t));
        res.ratio = 55467.0 / CT_SAMPLE_RATE;
    }

    void refill() {
        fmpmini_render(in, nativeFrames);
        fifoAvail += res.process(in, nativeFrames, fifo + fifoAvail * 2, fifoCap - fifoAvail);
    }

    void render(int16_t *dst, int frames) override {
        while (fifoAvail < frames) {
            int before = fifoAvail;
            refill();
            if (fifoAvail == before) break;
        }
        int take = frames < fifoAvail ? frames : fifoAvail;
        memcpy(dst, fifo, (size_t)take * 2 * sizeof(int16_t));
        if (take < frames) memset(dst + take * 2, 0, (size_t)(frames - take) * 2 * sizeof(int16_t));
        fifoAvail -= take;
        memmove(fifo, fifo + take * 2, (size_t)fifoAvail * 2 * sizeof(int16_t));
    }
    void seekMs(int ms) override {
        fmpmini_reset();
        int frames = ms * 55467 / 1000;
        while (frames > 0) {
            int n = frames > nativeFrames ? nativeFrames : frames;
            fmpmini_render(in, n);
            frames -= n;
        }
        res.reset();
        fifoAvail = 0;
        ended_ = false;
    }
    bool ended() override { return ended_; }
    int voiceCount() override { return fmpmini_channelsNb(); }
    const char *voiceName(int v) override {
        snprintf(nameBuf, sizeof(nameBuf), "FMP ch %d", v + 1);
        return nameBuf;
    }
};

} // namespace

CtDecoder *ct_create_fmp(const char *path, int subsong, CtTrackInfo *info) {
    (void)subsong;
    fmpmini_init();
    if (!fmpmini_loadFile(path)) { fmpmini_close(); return nullptr; }

    FmpBackend *b = new FmpBackend();
    b->refill(); // prime the resampler

    if (info) {
        const char *n = fmpmini_getName();
        ctCopyStr(info->title, sizeof(info->title), (n && n[0]) ? n : "FMP tune");
        const char *ext = strrchr(path, '.');
        ctCopyStr(info->format, sizeof(info->format), ext ? ext + 1 : "FMP");
        ctCopyStr(info->system, sizeof(info->system), "PC-98");
        info->channels = fmpmini_channelsNb();
        info->subsongs = 1;
        info->durationMs = (int)fmpmini_getLength();
        info->backend = CT_BACKEND_GME;
    }
    return b;
}

// ---------------------------------------------------------------------------

#include "pmdmini.h"

namespace {

struct PmdBackend : CtDecoder {
    bool ended_ = false;
    char *arg[4] = {nullptr, nullptr, nullptr, nullptr};
    char dir[1024] = {0};
    char nameBuf[64];

    ~PmdBackend() override { pmd_stop(); free(arg[1]); }

    void render(int16_t *dst, int frames) override {
        pmd_renderer(dst, frames);
    }
    void seekMs(int ms) override {
        pmd_stop();
        pmd_play(arg, dir, 0);
        int frames = ms * CT_SAMPLE_RATE / 1000;
        while (frames > 0) {
            int n = frames > 1024 ? 1024 : frames;
            pmd_renderer(nullptr, n);
            frames -= n;
        }
        ended_ = false;
    }
    bool ended() override { return ended_; }
    int voiceCount() override { return pmd_get_tracks() > 0 ? pmd_get_tracks() : 16; }
    const char *voiceName(int v) override {
        snprintf(nameBuf, sizeof(nameBuf), "PMD ch %d", v + 1);
        return nameBuf;
    }
};

} // namespace

CtDecoder *ct_create_pmd(const char *path, int subsong, CtTrackInfo *info) {
    (void)subsong;
    char tmp[1024];
    snprintf(tmp, sizeof(tmp), "%s", path);
    char *slash = strrchr(tmp, '/');
    if (slash) *slash = '\0';
    else snprintf(tmp, sizeof(tmp), ".");

    pmd_init(tmp);
    pmd_setrate(CT_SAMPLE_RATE);
    if (!pmd_is_pmd(path)) return nullptr;

    PmdBackend *b = new PmdBackend();
    ctCopyStr(b->dir, sizeof(b->dir), tmp);
    b->arg[0] = (char *)"pmd";
    b->arg[1] = strdup(path);
    b->arg[2] = nullptr;
    if (pmd_play(b->arg, b->dir, 0) != 0) { delete b; return nullptr; }

    if (info) {
        char title[256] = {0};
        char compo[256] = {0};
        pmd_get_title(title);
        pmd_get_compo(compo);
        ctCopyStr(info->title, sizeof(info->title), title[0] ? title : "PMD tune");
        ctCopyStr(info->format, sizeof(info->format), "PMD");
        ctCopyStr(info->system, sizeof(info->system), "PC-98");
        if (compo[0]) {
            size_t l = strlen(info->format);
            snprintf(info->format + l, sizeof(info->format) - l, " · %s", compo);
        }
        info->channels = pmd_get_tracks();
        info->subsongs = 1;
        info->durationMs = pmd_length_msec();
        info->backend = CT_BACKEND_GME;
    }
    return b;
}

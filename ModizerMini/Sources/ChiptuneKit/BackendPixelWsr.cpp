//
//  BackendPixelWsr.cpp
//  ModizerMini  —  PxTone / Organya and WonderSwan
//

#include "ChiptuneBackend.h"
#include "CtUtil.h"
#include <stdlib.h>
#include <string.h>
#include <stdio.h>

// ------------------------------- PxTone / Organya --------------------------

extern "C" {
int org_play(const char *fn, char *buf);
int org_getlength(void);
int org_gensamples(char *buf, int samplesNb);
int org_setPosition(int pos_ms);
void org_setLoopNb(int loopNb);
void unload_org(void);
}

#include "pxtnService.h"
#include "pxtnDescriptor.h"
#include "pxtnError.h"

namespace {

struct PixelBackend : CtDecoder {
    bool organya = false;
    pxtnService *svc = nullptr;
    pxtnDescriptor *desc = nullptr;
    char *fileBuffer = nullptr;
    bool ended_ = false;
    char nameBuf[64];

    ~PixelBackend() override {
        if (organya) { unload_org(); }
        else { delete svc; delete desc; }
        free(fileBuffer);
    }

    void render(int16_t *dst, int frames) override {
        if (organya) {
            if (!org_gensamples((char *)dst, frames)) ended_ = true;
        } else if (svc) {
            if (!svc->Moo(dst, frames * 2 * 2)) ended_ = true;
        }
    }
    void seekMs(int ms) override {
        if (organya) { org_setPosition(ms); ended_ = false; }
    }
    void setLooping(bool loop) override {
        if (organya) org_setLoopNb(loop ? -1 : 0);
        else if (svc) svc->moo_set_loop(loop);
    }
    bool ended() override { return ended_; }
    int voiceCount() override { return organya ? 16 : (svc ? svc->Unit_Num() : 16); }
    const char *voiceName(int v) override {
        if (!organya && svc) {
            const char *n = svc->Unit_Get(v) ? svc->Unit_Get(v)->get_name_buf(NULL) : nullptr;
            if (n && n[0]) { snprintf(nameBuf, sizeof(nameBuf), "%s", n); return nameBuf; }
        }
        snprintf(nameBuf, sizeof(nameBuf), "Organya %d", v + 1);
        return nameBuf;
    }
};

} // namespace

CtDecoder *ct_create_pixel(const char *path, int subsong, CtTrackInfo *info) {
    (void)subsong;
    FILE *f = fopen(path, "rb");
    if (!f) return nullptr;
    fseek(f, 0, SEEK_END); long size = ftell(f); fseek(f, 0, SEEK_SET);
    if (size <= 0) { fclose(f); return nullptr; }
    char *buf = (char *)malloc((size_t)size + 1);
    if (!buf || fread(buf, 1, (size_t)size, f) != (size_t)size) { free(buf); fclose(f); return nullptr; }
    fclose(f);
    buf[size] = '\0';

    const char *ext = strrchr(path, '.');
    bool organya = ext && strcasecmp(ext, ".org") == 0;

    PixelBackend *b = new PixelBackend();
    b->organya = organya;
    b->fileBuffer = buf;

    if (organya) {
        if (org_play(path, buf) == 0) { delete b; return nullptr; }
        if (info) {
            const char *slash = strrchr(path, '/');
            ctCopyStr(info->title, sizeof(info->title), slash ? slash + 1 : path);
            char *dot = strrchr(info->title, '.');
            if (dot) *dot = '\0';
            ctCopyStr(info->format, sizeof(info->format), "Organya");
            ctCopyStr(info->system, sizeof(info->system), "Cave Story");
            info->channels = 16;
            info->durationMs = org_getlength();
            info->backend = CT_BACKEND_GME;
        }
    } else {
        b->svc = new pxtnService();
        b->desc = new pxtnDescriptor();
        if (!b->svc->init()) { delete b; return nullptr; }
        b->svc->set_destination_quality(2, CT_SAMPLE_RATE);
        b->desc->set_memory_r(buf, (int)size);
        if (!b->svc->read(b->desc) || !b->svc->tones_ready()) { delete b; return nullptr; }
        pxtnVOMITPREPARATION prep;
        memset(&prep, 0, sizeof(prep));
        prep.start_pos_float = 0;
        prep.master_volume = 1.0f;
        b->svc->moo_preparation(&prep);
        if (info) {
            const char *name = b->svc->text ? b->svc->text->get_name_buf(NULL) : nullptr;
            ctCopyStr(info->title, sizeof(info->title), (name && name[0]) ? name : "PxTone tune");
            ctCopyStr(info->format, sizeof(info->format), "PxTone");
            ctCopyStr(info->system, sizeof(info->system), "PxTone");
            info->channels = b->svc->Unit_Num();
            info->instruments = b->svc->Woice_Num();
            info->durationMs = (int)((int64_t)b->svc->moo_get_total_sample() * 1000 / CT_SAMPLE_RATE);
            info->backend = CT_BACKEND_GME;
        }
    }
    return b;
}

// ------------------------------- WonderSwan --------------------------------

#include "wsr_player_common/wsr_player_intf.h"

namespace oswan { extern WSRPlayerApi g_wsr_player_api; }

namespace {

struct WsrBackend : CtDecoder {
    static const int nativeFrames = 2048;
    static const int fifoCap = 8192;
    WSRPlayerApi *api = nullptr;
    CtStreamResampler res;
    int16_t *in = nullptr;
    int16_t *fifo = nullptr;
    int fifoAvail = 0;
    int tracks[256];
    int trackCount = 0;
    bool ended_ = false;
    bool loaded = false;

    ~WsrBackend() override {
        if (loaded) api->pCloseWSR();
        free(in);
        free(fifo);
    }

    WsrBackend() {
        in = (int16_t *)calloc((size_t)nativeFrames * 2, sizeof(int16_t));
        fifo = (int16_t *)calloc((size_t)fifoCap * 2, sizeof(int16_t));
        res.ratio = 48000.0 / CT_SAMPLE_RATE;
    }

    void refill() {
        api->pUpdateWSR(in, (unsigned)(nativeFrames * 4), (unsigned)nativeFrames);
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
        api->pResetWSR(0);
        res.reset();
        fifoAvail = 0;
        int frames = ms * 48000 / 1000;
        while (frames > 0) {
            int n = frames > nativeFrames ? nativeFrames : frames;
            api->pUpdateWSR(in, (unsigned)(n * 4), (unsigned)n);
            frames -= n;
        }
        ended_ = false;
    }
    bool ended() override { return ended_; }
    int voiceCount() override { return 6; }
    const char *voiceName(int v) override {
        static char buf[32];
        const char *names[6] = {"PCM 1", "PCM 2", "PCM 3", "PCM 4", "Voice", "Noise"};
        snprintf(buf, sizeof(buf), "%s", v < 6 ? names[v] : "Ch");
        return buf;
    }
};

} // namespace

CtDecoder *ct_create_wsr(const char *path, int subsong, CtTrackInfo *info) {
    FILE *f = fopen(path, "rb");
    if (!f) return nullptr;
    fseek(f, 0, SEEK_END); long size = ftell(f); fseek(f, 0, SEEK_SET);
    if (size <= 0) { fclose(f); return nullptr; }
    unsigned char *buf = (unsigned char *)malloc((size_t)size);
    if (!buf || fread(buf, 1, (size_t)size, f) != (size_t)size) { free(buf); fclose(f); return nullptr; }
    fclose(f);

    WsrBackend *b = new WsrBackend();
    b->api = &oswan::g_wsr_player_api;
    if (!b->api->pLoadWSR(buf, (unsigned)size)) { free(buf); delete b; return nullptr; }
    free(buf);
    b->api->pSetFrequency(48000);
    b->loaded = true;
    b->api->pResetWSR(subsong > 0 ? (unsigned)subsong : 0);
    b->refill();

    if (info) {
        const char *slash = strrchr(path, '/');
        ctCopyStr(info->title, sizeof(info->title), slash ? slash + 1 : path);
        char *dot = strrchr(info->title, '.');
        if (dot) *dot = '\0';
        ctCopyStr(info->format, sizeof(info->format), "WSR");
        ctCopyStr(info->system, sizeof(info->system), "WonderSwan");
        info->channels = 6;
        info->subsongs = 1;
        info->durationMs = -1;
        info->backend = CT_BACKEND_GME;
    }
    return b;
}

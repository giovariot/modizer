//
//  BackendMac.cpp
//  ModizerMini  —  Monkey's Audio (.ape)
//

#include "ChiptuneBackend.h"
#include "CtUtil.h"
#include <stdlib.h>
#include <string.h>
#include <stdio.h>

#define MACLIB_COMPILE 1
#define DLLEXPORT
#include "NoWindows.h"
#include "MACLib.h"
#include "CharacterHelper.h"

namespace {

struct MacBackend : CtDecoder {
    IAPEDecompress *dec = nullptr;
    int rate = CT_SAMPLE_RATE;
    int channels = 2;
    static const int nativeFrames = 4096;
    static const int fifoCap = 16384;
    CtStreamResampler res;
    int16_t *in = nullptr;
    int16_t *fifo = nullptr;
    int fifoAvail = 0;

    ~MacBackend() override {
        if (dec) delete dec;
        free(in);
        free(fifo);
    }

    MacBackend() {
        in = (int16_t *)calloc((size_t)nativeFrames * 2, sizeof(int16_t));
        fifo = (int16_t *)calloc((size_t)fifoCap * 2, sizeof(int16_t));
    }

    void refill() {
        int got = 0;
        dec->GetData((char *)in, nativeFrames, &got);
        if (got <= 0) return;
        if (rate == CT_SAMPLE_RATE) {
            memcpy(fifo + fifoAvail * 2, in, (size_t)got * 2 * sizeof(int16_t));
            fifoAvail += got;
        } else {
            fifoAvail += res.process(in, got, fifo + fifoAvail * 2, fifoCap - fifoAvail);
        }
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
        if (!dec) return;
        dec->Seek((int)((int64_t)ms * rate / 1000));
        res.reset();
        fifoAvail = 0;
    }
    int voiceCount() override { return channels; }
};

} // namespace

CtDecoder *ct_create_mac(const char *path, int subsong, CtTrackInfo *info) {
    (void)subsong;
    str_utf16 *wpath = CAPECharacterHelper::GetUTF16FromANSI(path);
    int err = 0;
    IAPEDecompress *dec = CreateIAPEDecompress(wpath, &err);
    delete[] wpath;
    if (!dec || err != 0) { if (dec) delete dec; return nullptr; }

    MacBackend *b = new MacBackend();
    b->dec = dec;
    b->rate = (int)dec->GetInfo(APE_INFO_SAMPLE_RATE);
    b->channels = (int)dec->GetInfo(APE_INFO_CHANNELS);
    if (b->channels != 2) b->channels = 2;
    b->res.ratio = (double)b->rate / CT_SAMPLE_RATE;
    b->refill();

    if (info) {
        const char *slash = strrchr(path, '/');
        char name[256];
        snprintf(name, sizeof(name), "%s", slash ? slash + 1 : path);
        char *dot = strrchr(name, '.');
        if (dot) *dot = '\0';
        ctCopyStr(info->title, sizeof(info->title), name);
        ctCopyStr(info->format, sizeof(info->format), "APE");
        ctCopyStr(info->system, sizeof(info->system), "Monkey's Audio");
        info->channels = b->channels;
        info->subsongs = 1;
        info->durationMs = (int)dec->GetInfo(APE_INFO_LENGTH_MS);
        info->backend = CT_BACKEND_GME;
    }
    return b;
}

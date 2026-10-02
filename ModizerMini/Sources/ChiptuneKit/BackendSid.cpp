//
//  BackendSid.cpp
//  ModizerMini  —  SID tunes via libsidplayfp / reSIDfp
//

#include "ChiptuneBackend.h"
#include "CtUtil.h"
#include <string.h>

#include <sidplayfp/sidplayfp.h>
#include <sidplayfp/SidTune.h>
#include <sidplayfp/SidInfo.h>
#include <sidplayfp/SidTuneInfo.h>
#include <sidplayfp/SidConfig.h>
#include <builders/residfp-builder/residfp.h>

namespace {

struct SidBackend : CtDecoder {
    sidplayfp engine;
    SidTune *tune = nullptr;
    ReSIDfpBuilder *builder = nullptr;
    int nVoices = 0;

    ~SidBackend() override {
        delete builder;
        delete tune;
    }

    void render(int16_t *dst, int frames) override {
        int produced = 0;
        while (produced < frames) {
            int n = engine.play(4000);
            if (n <= 0) break;
            unsigned int mixed = engine.mix(dst + produced * 2, (unsigned int)n);
            int got = (int)mixed / 2;
            if (got <= 0) break;
            produced += got;
            if (produced > frames) break;
        }
    }
    void seekMs(int ms) override {
        // No library seek: restart and fast-forward by rendering.
        engine.load(tune);
        int frames = ms * CT_SAMPLE_RATE / 1000;
        int16_t scratch[4096];
        while (frames > 0) {
            int chunk = frames > 2048 ? 2048 : frames;
            render(scratch, chunk);
            frames -= chunk;
        }
    }
    int voiceCount() override { return nVoices; }
    const char *voiceName(int v) override {
        static char buf[32];
        snprintf(buf, sizeof(buf), "SID %d.%d", v / 4 + 1, v % 4 + 1);
        return buf;
    }
};

} // namespace

CtDecoder *ct_create_sid(const char *path, int subsong, CtTrackInfo *info) {
    SidTune *tune = new SidTune(path, nullptr, false);
    if (!tune->getStatus()) { delete tune; return nullptr; }

    SidBackend *b = new SidBackend();
    b->tune = tune;

    SidConfig cfg = b->engine.config();
    cfg.frequency = CT_SAMPLE_RATE;
    cfg.playback = SidConfig::STEREO;
    cfg.samplingMethod = SidConfig::INTERPOLATE;
    cfg.defaultSidModel = SidConfig::MOS6581;
    cfg.defaultC64Model = SidConfig::PAL;
    cfg.forceC64Model = false;
    cfg.forceSidModel = false;
    cfg.digiBoost = true;
    cfg.powerOnDelay = 0x2000;
    b->builder = new ReSIDfpBuilder("ReSIDfp");
    cfg.sidEmulation = b->builder;
    if (!b->engine.config(cfg)) { delete b; return nullptr; }

    const SidTuneInfo *ti = tune->getInfo();
    int songs = ti ? ti->songs() : 1;
    if (subsong > 0 && subsong <= songs) tune->selectSong((unsigned short)subsong);

    if (!b->engine.load(tune)) { delete b; return nullptr; }
    b->engine.initMixer(true);

    b->nVoices = (int)b->engine.installedSIDs() * 4;

    if (info) {
        ctCopyStr(info->title, sizeof(info->title), (ti && ti->infoString(0)) ? ti->infoString(0) : "SID tune");
        ctCopyStr(info->format, sizeof(info->format), "SID");
        ctCopyStr(info->system, sizeof(info->system), "Commodore 64");
        info->channels = b->nVoices;
        info->instruments = b->nVoices;
        info->subsongs = songs > 0 ? songs : 1;
        info->currentSubsong = ti ? (int)ti->startSong() - 1 : 0;
        info->durationMs = -1;
        info->backend = CT_BACKEND_GME;
    }
    return b;
}

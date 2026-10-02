//
//  BackendMisc.cpp
//  ModizerMini  —  V2M, AdLib (adplug), Atari SNDH, MSX (libkss)
//

#include "ChiptuneBackend.h"
#include "CtUtil.h"
#include <stdlib.h>
#include <string.h>
#include <stdio.h>

// ------------------------------- V2M ---------------------------------------

#include "v2mplayer.h"

namespace {

struct V2mBackend : CtDecoder {
    V2MPlayer *player = nullptr;
    float *fbuf = nullptr;
    bool ended_ = false;

    ~V2mBackend() override {
        if (player) { player->Close(); delete player; }
        free(fbuf);
    }
    void render(int16_t *dst, int frames) override {
        if (!player) return;
        player->Render(fbuf, (uint32_t)frames);
        for (int i = 0; i < frames * 2; i++) dst[i] = ctClip16(fbuf[i] * 32767.0f);
    }
    void seekMs(int ms) override { if (player) player->Play((uint32_t)ms); }
    int voiceCount() override { return 16; }
    const char *voiceName(int v) override {
        static char buf[32];
        snprintf(buf, sizeof(buf), "V2 voice %d", v + 1);
        return buf;
    }
};

} // namespace

CtDecoder *ct_create_v2m(const char *path, int subsong, CtTrackInfo *info) {
    (void)subsong;
    (void)path;
    V2MPlayer *player = new V2MPlayer();
    player->Init(1000);
    // The V2M data needs to be loaded by the caller; only plain .v2m is handled
    // here, so open the file directly.
    FILE *f = fopen(path, "rb");
    if (!f) { delete player; return nullptr; }
    fseek(f, 0, SEEK_END); long size = ftell(f); fseek(f, 0, SEEK_SET);
    unsigned char *data = (unsigned char *)malloc((size_t)size);
    if (!data || fread(data, 1, (size_t)size, f) != (size_t)size) { free(data); fclose(f); delete player; return nullptr; }
    fclose(f);
    player->Open(data, CT_SAMPLE_RATE);
    free(data);

    V2mBackend *b = new V2mBackend();
    b->player = player;
    b->fbuf = (float *)malloc(sizeof(float) * 2 * 4096);

    if (info) {
        ctCopyStr(info->title, sizeof(info->title), "V2M");
        ctCopyStr(info->format, sizeof(info->format), "V2M");
        ctCopyStr(info->system, sizeof(info->system), "V2");
        info->channels = 16;
        info->subsongs = 1;
        info->durationMs = -1;
        info->backend = CT_BACKEND_GME;
    }
    return b;
}

// ------------------------------- AdLib -------------------------------------

#include "adplug.h"
#include "wemuopl.h"
#include "players.h"

namespace {

struct AdplugBackend : CtDecoder {
    CPlayer *player = nullptr;
    Copl *opl = nullptr;
    bool ended_ = false;

    ~AdplugBackend() override { delete player; delete opl; }

    void render(int16_t *dst, int frames) override {
        if (!player || !opl) return;
        double refresh = player->getrefresh();
        int perTick = refresh > 0 ? (int)(CT_SAMPLE_RATE / refresh) : frames;
        if (perTick <= 0) perTick = 1;
        int written = 0;
        while (written < frames) {
            player->update();
            int n = frames - written;
            if (n > perTick) n = perTick;
            opl->update(dst + written * 2, n);
            written += n;
        }
    }
    void seekMs(int ms) override {
        if (player) { player->seek((unsigned int)ms); ended_ = false; }
    }
    int voiceCount() override { return 18; }
    const char *voiceName(int v) override {
        static char buf[32];
        snprintf(buf, sizeof(buf), "OPL ch %d", v + 1);
        return buf;
    }
};

} // namespace

CtDecoder *ct_create_adplug(const char *path, int subsong, CtTrackInfo *info) {
    CWemuopl *opl = new CWemuopl(CT_SAMPLE_RATE, true, true);
    CPlayer *player = CAdPlug::factory(path, opl);
    if (!player) { delete opl; return nullptr; }
    if (subsong > 0 && subsong <= player->getsubsongs()) player->rewind((unsigned int)subsong);

    AdplugBackend *b = new AdplugBackend();
    b->player = player;
    b->opl = opl;

    if (info) {
        ctCopyStr(info->title, sizeof(info->title),
                  player->gettitle() != "" ? player->gettitle().c_str() : "AdLib tune");
        ctCopyStr(info->format, sizeof(info->format),
                  player->gettype() != "" ? player->gettype().c_str() : "AdLib");
        ctCopyStr(info->system, sizeof(info->system), "OPL");
        info->channels = 18;
        info->instruments = player->getinstruments();
        info->subsongs = player->getsubsongs() > 0 ? player->getsubsongs() : 1;
        info->durationMs = (int)player->songlength();
        info->backend = CT_BACKEND_GME;
    }
    return b;
}

// ------------------------------- Atari SNDH --------------------------------

#include "AtariAudio.h"

namespace {

struct AtariBackend : CtDecoder {
    SndhFile sndh;
    bool ended_ = false;

    void render(int16_t *dst, int frames) override {
        sndh.AudioRender(dst, frames);          // mono
        for (int i = frames - 1; i >= 0; i--) {
            dst[i * 2 + 1] = dst[i];
            dst[i * 2] = dst[i];
        }
    }
    void seekMs(int ms) override {
        sndh.InitSubSong(currentSub);
        int frames = ms * CT_SAMPLE_RATE / 1000;
        int16_t scratch[4096];
        while (frames > 0) {
            int n = frames > 2048 ? 2048 : frames;
            render(scratch, n);
            frames -= n;
        }
    }
    int currentSub = 0;
    int voiceCount() override { return 4; }
    const char *voiceName(int v) override {
        static char buf[32];
        if (v == 3) snprintf(buf, sizeof(buf), "DMA");
        else snprintf(buf, sizeof(buf), "YM2149 ch %c", 'A' + v);
        return buf;
    }
};

} // namespace

CtDecoder *ct_create_atari(const char *path, int subsong, CtTrackInfo *info) {
    FILE *f = fopen(path, "rb");
    if (!f) return nullptr;
    fseek(f, 0, SEEK_END); long size = ftell(f); fseek(f, 0, SEEK_SET);
    unsigned char *data = (unsigned char *)malloc((size_t)size);
    if (!data || fread(data, 1, (size_t)size, f) != (size_t)size) { free(data); fclose(f); return nullptr; }
    fclose(f);

    AtariBackend *b = new AtariBackend();
    if (!b->sndh.Load(data, (uint32_t)size, CT_SAMPLE_RATE)) { free(data); delete b; return nullptr; }
    int count = b->sndh.GetSubsongCount();
    int sub = (subsong >= 0 && subsong < count) ? subsong : b->sndh.GetDefaultSubsong();
    b->currentSub = sub;
    b->sndh.InitSubSong(sub);
    free(data);

    if (info) {
        SndhFile::SubSongInfo si;
        memset(&si, 0, sizeof(si));
        b->sndh.GetSubsongInfo(sub, si);
        ctCopyStr(info->title, sizeof(info->title), si.musicTitle[0] ? si.musicTitle : "Atari SNDH");
        ctCopyStr(info->format, sizeof(info->format), "SNDH");
        ctCopyStr(info->system, sizeof(info->system), "Atari ST");
        info->channels = 4;
        info->subsongs = count > 0 ? count : 1;
        info->currentSubsong = sub;
        info->durationMs = -1;
        info->backend = CT_BACKEND_GME;
    }
    return b;
}

// ------------------------------- MSX (KSS) ---------------------------------

#include "kssplay.h"

namespace {

struct KssBackend : CtDecoder {
    KSS *kss = nullptr;
    KSSPLAY *play = nullptr;
    bool ended_ = false;

    ~KssBackend() override {
        if (play) KSSPLAY_delete(play);
        if (kss) KSS_delete(kss);
    }
    void render(int16_t *dst, int frames) override {
        if (play) KSSPLAY_calc(play, dst, (uint32_t)frames);
    }
    void seekMs(int ms) override {
        // libkss has no seek; restart and skip silently.
        if (play) KSSPLAY_reset(play, song, 0);
        int frames = ms * CT_SAMPLE_RATE / 1000;
        while (frames > 0) {
            int n = frames > 1024 ? 1024 : frames;
            KSSPLAY_calc_silent(play, (uint32_t)n);
            frames -= n;
        }
    }
    uint32_t song = 0;
    int voiceCount() override { return 16; }
    const char *voiceName(int v) override {
        static char buf[32];
        snprintf(buf, sizeof(buf), "KSS ch %d", v + 1);
        return buf;
    }
};

} // namespace

CtDecoder *ct_create_kss(const char *path, int subsong, CtTrackInfo *info) {
    KSS *kss = KSS_load_file((char *)path);
    if (!kss) return nullptr;
    KSSPLAY *play = KSSPLAY_new(CT_SAMPLE_RATE, 2, 16);
    if (!play) { KSS_delete(kss); return nullptr; }
    KSSPLAY_set_data(play, kss);
    KSSPLAY_set_master_volume(play, 64);
    uint32_t song = (subsong > 0) ? (uint32_t)subsong : 0;
    KSSPLAY_reset(play, song, 0);

    KssBackend *b = new KssBackend();
    b->kss = kss;
    b->play = play;
    b->song = song;

    if (info) {
        ctCopyStr(info->title, sizeof(info->title), kss->title[0] ? (const char *)kss->title : "KSS tune");
        ctCopyStr(info->format, sizeof(info->format), "KSS");
        ctCopyStr(info->system, sizeof(info->system), "MSX");
        info->channels = 16;
        info->subsongs = (kss->trk_max - kss->trk_min) + 1;
        info->currentSubsong = (int)song;
        info->durationMs = -1;
        info->backend = CT_BACKEND_GME;
    }
    return b;
}

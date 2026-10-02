//
//  BackendSimple.cpp
//  ModizerMini  —  ASAP (Atari), Hively/AHX, ST-Sound, V2M
//

#include "ChiptuneBackend.h"
#include "CtUtil.h"
#include <stdlib.h>
#include <string.h>
#include <stdio.h>

#include "asap.h"

namespace {

struct AsapBackend : CtDecoder {
    ASAP *asap = nullptr;
    unsigned char *module = nullptr;
    int songs = 1;

    ~AsapBackend() override {
        if (asap) ASAP_Delete(asap);
        free(module);
    }

    void render(int16_t *dst, int frames) override {
        ASAP_Generate(asap, (unsigned char *)dst, frames * 2 * 2, ASAPSampleFormat_S16_L_E);
    }
    void seekMs(int ms) override { ASAP_Seek(asap, ms); }
    int voiceCount() override { return 4; }
    const char *voiceName(int v) override {
        static char buf[32];
        snprintf(buf, sizeof(buf), "POKEY %d.%d", v / 4 + 1, v % 4 + 1);
        return buf;
    }
};

} // namespace

CtDecoder *ct_create_asap(const char *path, int subsong, CtTrackInfo *info) {
    FILE *f = fopen(path, "rb");
    if (!f) return nullptr;
    fseek(f, 0, SEEK_END); long size = ftell(f); fseek(f, 0, SEEK_SET);
    if (size <= 0) { fclose(f); return nullptr; }
    unsigned char *buf = (unsigned char *)malloc((size_t)size);
    if (!buf || fread(buf, 1, (size_t)size, f) != (size_t)size) { free(buf); fclose(f); return nullptr; }
    fclose(f);

    ASAP *asap = ASAP_New();
    if (!ASAP_Load(asap, path, buf, (int)size)) { ASAP_Delete(asap); free(buf); return nullptr; }
    const ASAPInfo *ai = ASAP_GetInfo(asap);
    int songs = ASAPInfo_GetSongs(ai);
    int song = (subsong >= 0 && subsong < songs) ? subsong : ASAPInfo_GetDefaultSong(ai);
    ASAP_PlaySong(asap, song, ASAPInfo_GetDuration(ai, song));

    AsapBackend *b = new AsapBackend();
    b->asap = asap;
    b->module = buf;
    b->songs = songs;

    if (info) {
        ctCopyStr(info->title, sizeof(info->title),
                  ASAPInfo_GetTitle(ai) ? ASAPInfo_GetTitle(ai) : "Atari tune");
        ctCopyStr(info->format, sizeof(info->format), "SAP");
        ctCopyStr(info->system, sizeof(info->system), "Atari POKEY");
        if (ASAPInfo_GetAuthor(ai)) ctCopyStr(info->format, sizeof(info->format), ASAPInfo_GetAuthor(ai));
        info->channels = ASAPInfo_GetChannels(ai) * 4;
        info->subsongs = songs > 0 ? songs : 1;
        info->currentSubsong = song;
        info->durationMs = ASAPInfo_GetDuration(ai, song);
        info->backend = CT_BACKEND_GME;
    }
    return b;
}

// ---------------------------------------------------------------------------

#include "hvl_replay.h"

namespace {

struct HvlBackend : CtDecoder {
    hvl_tune *tune = nullptr;
    bool ended_ = false;
    int sampleToWrite = 0;

    ~HvlBackend() override { if (tune) hvl_FreeTune(tune); }

    void render(int16_t *dst, int frames) override {
        if (!tune) return;
        int done = 0;
        while (done < frames) {
            if (sampleToWrite <= 0) {
                hvl_play_irq(tune);
                sampleToWrite = tune->ht_Frequency / 50 / (tune->ht_SpeedMultiplier ? tune->ht_SpeedMultiplier : 1);
                if (!sampleToWrite) sampleToWrite = 1;
            }
            int chunk = frames - done;
            if (chunk > sampleToWrite) chunk = sampleToWrite;
            hvl_mixchunk(tune, chunk, (int8_t *)(dst + done * 2), (int8_t *)(dst + done * 2) + 2, 4);
            done += chunk;
            sampleToWrite -= chunk;
        }
        if (tune->ht_SongEndReached) ended_ = true;
    }
    void seekMs(int ms) override {
        if (tune) { hvl_Seek(tune, ms); ended_ = false; }
    }
    bool ended() override { return ended_; }
    int voiceCount() override { return tune ? tune->ht_Channels : 0; }
    const char *voiceName(int v) override {
        static char buf[32];
        snprintf(buf, sizeof(buf), "Voice %d", v + 1);
        return buf;
    }
};

} // namespace

CtDecoder *ct_create_hvl(const char *path, int subsong, CtTrackInfo *info) {
    static bool inited = false;
    if (!inited) { hvl_InitReplayer(); inited = true; }
    hvl_tune *tune = hvl_LoadTune((TEXT *)path, CT_SAMPLE_RATE, 1);
    if (!tune) return nullptr;
    if (subsong > 0 && subsong <= (int)tune->ht_SubsongNr) hvl_InitSubsong(tune, (uint32_t)subsong);

    HvlBackend *b = new HvlBackend();
    b->tune = tune;

    if (info) {
        ctCopyStr(info->title, sizeof(info->title), tune->ht_Name ? tune->ht_Name : "Hively tune");
        ctCopyStr(info->format, sizeof(info->format), tune->ht_ModType ? "HVL" : "AHX");
        ctCopyStr(info->system, sizeof(info->system), "Amiga");
        info->channels = tune->ht_Channels;
        info->instruments = tune->ht_InstrumentNr;
        info->subsongs = tune->ht_SubsongNr ? tune->ht_SubsongNr : 1;
        info->durationMs = (int)hvl_GetPlayTime(tune);
        info->backend = CT_BACKEND_GME;
    }
    return b;
}

// ---------------------------------------------------------------------------

#include "StSoundLibrary.h"

namespace {

struct YmBackend : CtDecoder {
    YMMUSIC *music = nullptr;
    bool ended_ = false;

    ~YmBackend() override { if (music) { ymMusicStop(music); ymMusicDestroy(music); } }

    void render(int16_t *dst, int frames) override {
        if (!music) return;
        if (!ymMusicComputeStereo(music, (ymsample *)dst, frames)) ended_ = true;
    }
    void seekMs(int ms) override {
        if (music && ymMusicIsSeekable(music)) ymMusicSeek(music, (ymu32)ms);
    }
    bool ended() override { return ended_; }
    int voiceCount() override { return 3; }
    const char *voiceName(int v) override {
        static char buf[32];
        snprintf(buf, sizeof(buf), "YM %c", 'A' + v);
        return buf;
    }
};

} // namespace

CtDecoder *ct_create_stsound(const char *path, int subsong, CtTrackInfo *info) {
    (void)subsong;
    YMMUSIC *music = ymMusicCreate();
    if (!music) return nullptr;
    if (!ymMusicLoad(music, path)) { ymMusicDestroy(music); return nullptr; }
    ymMusicSetLowpassFiler(music, YMTRUE);
    ymMusicSetLoopMode(music, YMFALSE);
    ymMusicPlay(music);

    YmBackend *b = new YmBackend();
    b->music = music;

    if (info) {
        ymMusicInfo_t mi;
        memset(&mi, 0, sizeof(mi));
        ymMusicGetInfo(music, &mi);
        ctCopyStr(info->title, sizeof(info->title), mi.pSongName ? mi.pSongName : "YM tune");
        ctCopyStr(info->format, sizeof(info->format), mi.pSongType ? mi.pSongType : "YM");
        ctCopyStr(info->system, sizeof(info->system), mi.pSongPlayer ? mi.pSongPlayer : "Atari ST");
        info->channels = 3;
        info->subsongs = 1;
        info->durationMs = (int)mi.musicTimeInMs;
        info->backend = CT_BACKEND_GME;
    }
    return b;
}

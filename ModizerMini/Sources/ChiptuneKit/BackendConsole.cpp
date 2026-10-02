//
//  BackendConsole.cpp
//  ModizerMini  —  NES (libnsfplay) and PC-Engine/MSX-style (libnez)
//

#include "ChiptuneBackend.h"
#include "CtUtil.h"
#include <string.h>
#include <stdio.h>

// ------------------------------- NSFPlay -----------------------------------

#include "nsfplay.h"
#include "nsf.h"

namespace {

struct NsfBackend : CtDecoder {
    xgm::NSFPlayer *player = nullptr;
    xgm::NSFPlayerConfig *config = nullptr;
    xgm::NSF *nsf = nullptr;
    bool ended_ = false;
    char nameBuf[64];

    ~NsfBackend() override {
        delete player;
        delete config;
        delete nsf;
    }

    void render(int16_t *dst, int frames) override {
        if (!player) return;
        xgm::UINT32 got = player->Render(dst, (xgm::UINT32)frames);
        if (got < (xgm::UINT32)frames) ended_ = true;
    }
    void seekMs(int ms) override {
        if (!player) return;
        player->Reset();
        int frames = ms * CT_SAMPLE_RATE / 1000;
        while (frames > 0) {
            int n = frames > 2048 ? 2048 : frames;
            player->Skip((xgm::UINT32)n);
            frames -= n;
        }
        ended_ = false;
    }
    bool ended() override { return ended_; }
    int voiceCount() override { return 12; }
    const char *voiceName(int v) override {
        snprintf(nameBuf, sizeof(nameBuf), "NES voice %d", v + 1);
        return nameBuf;
    }
};

} // namespace

CtDecoder *ct_create_nsfplay(const char *path, int subsong, CtTrackInfo *info) {
    xgm::NSFPlayerConfig *config = new xgm::NSFPlayerConfig();
    (*config)["RATE"] = CT_SAMPLE_RATE;
    (*config)["MASTER_VOLUME"] = 128;
    (*config)["MASK"] = 0;
    (*config)["PLAY_ADVANCE"] = 0;
    (*config)["LOOP_NUM"] = 0;

    xgm::NSF *nsf = new xgm::NSF();
    nsf->SetDefaults(150000, 3000, 0);
    if (!nsf->LoadFile(path)) { delete config; delete nsf; return nullptr; }

    xgm::NSFPlayer *player = new xgm::NSFPlayer();
    player->SetConfig(config);
    player->Load(nsf);
    player->SetPlayFreq(CT_SAMPLE_RATE);
    player->SetChannels(2);

    int songs = nsf->GetSongNum();
    int song = (subsong > 0 && subsong <= songs) ? subsong : nsf->GetSong();
    player->SetSong(song);
    player->Reset();

    NsfBackend *b = new NsfBackend();
    b->player = player;
    b->config = config;
    b->nsf = nsf;

    if (info) {
        const char *t = nsf->GetTitleString("%L", subsong > 0 ? subsong : 0);
        ctCopyStr(info->title, sizeof(info->title), (t && t[0]) ? t : "NES music");
        ctCopyStr(info->format, sizeof(info->format), nsf->is_nsfe ? "NSFe" : "NSF");
        ctCopyStr(info->system, sizeof(info->system), "Nintendo NES");
        info->channels = 12;
        info->subsongs = songs > 0 ? songs : 1;
        info->currentSubsong = song > 0 ? song - 1 : 0;
        info->durationMs = nsf->GetLength();
        info->backend = CT_BACKEND_GME;
    }
    return b;
}

// ------------------------------- NEZ ---------------------------------------

#include "nezplug.h"

namespace {

struct NezBackend : CtDecoder {
    NEZ_PLAY *play = nullptr;
    bool ended_ = false;
    char nameBuf[64];

    ~NezBackend() override { if (play) NEZDelete(play); }

    void render(int16_t *dst, int frames) override {
        if (!play) return;
        NEZRender(play, dst, (Uint)frames);
    }
    void seekMs(int ms) override {
        if (!play) return;
        NEZReset(play);
        int frames = ms * CT_SAMPLE_RATE / 1000;
        int16_t scratch[2048];
        while (frames > 0) {
            int n = frames > 1024 ? 1024 : frames;
            NEZRender(play, scratch, (Uint)n);
            frames -= n;
        }
        ended_ = false;
    }
    bool ended() override { return ended_; }
    int voiceCount() override { return 6; }
    const char *voiceName(int v) override {
        snprintf(nameBuf, sizeof(nameBuf), "Voice %d", v + 1);
        return nameBuf;
    }
};

} // namespace

CtDecoder *ct_create_nez(const char *path, int subsong, CtTrackInfo *info) {
    FILE *f = fopen(path, "rb");
    if (!f) return nullptr;
    fseek(f, 0, SEEK_END); long size = ftell(f); fseek(f, 0, SEEK_SET);
    if (size <= 0) { fclose(f); return nullptr; }
    unsigned char *buf = (unsigned char *)malloc((size_t)size);
    if (!buf || fread(buf, 1, (size_t)size, f) != (size_t)size) { free(buf); fclose(f); return nullptr; }
    fclose(f);

    NEZ_PLAY *play = NEZNew();
    if (!play) { free(buf); return nullptr; }
    NEZSetFrequency(play, CT_SAMPLE_RATE);
    NEZSetChannel(play, 2);
    NEZLoad(play, (Uint8 *)buf, (Uint)size);
    free(buf);

    int maxSongs = (int)NEZGetSongMax(play);
    if (subsong > 0 && subsong <= maxSongs) NEZSetSongNo(play, (Uint)subsong);
    NEZReset(play);

    NezBackend *b = new NezBackend();
    b->play = play;

    if (info) {
        ctCopyStr(info->title, sizeof(info->title), "Nez tune");
        const char *ext = strrchr(path, '.');
        ctCopyStr(info->format, sizeof(info->format), ext ? ext + 1 : "NEZ");
        ctCopyStr(info->system, sizeof(info->system), "PC Engine / MSX");
        info->channels = 6;
        info->subsongs = maxSongs > 0 ? maxSongs : 1;
        info->durationMs = -1;
        info->backend = CT_BACKEND_GME;
    }
    return b;
}

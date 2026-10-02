//
//  BackendXsf.cpp
//  ModizerMini  —  2SF (Nintendo DS) and NCSF (Nintendo DS) via libxsf
//

#include "ChiptuneBackend.h"
#include "CtUtil.h"
#include <string>
#include <vector>
#include <string.h>
#include <stdio.h>

#include "XSFPlayer.h"
#include "XSFPlayer_2SF.h"
#include "XSFPlayer_NCSF.h"
#include "XSFConfig.h"

namespace {

struct XsfBackend : CtDecoder {
    XSFPlayer *player = nullptr;
    XSFConfig *config = nullptr;
    bool ended_ = false;
    char nameBuf[64];

    ~XsfBackend() override {
        if (player) { player->Terminate(); delete player; }
        delete config;
    }

    void render(int16_t *dst, int frames) override {
        if (!player) return;
        std::vector<uint8_t> buf((size_t)frames * 4);
        unsigned written = 0;
        player->FillBuffer(buf, written);
        int got = (int)(written / 4);
        if (got > frames) got = frames;
        if (got > 0) memcpy(dst, buf.data(), (size_t)got * 4);
        if (done() || got < frames) {
            ended_ = true;
            if (got < frames) memset(dst + got * 2, 0, (size_t)(frames - got) * 4);
        }
    }
    bool done() const {
        return player->currentSample >= player->GetLengthInSamples() && player->GetLengthInSamples() > 0;
    }
    void seekMs(int ms) override {
        if (!player) return;
        player->Terminate();
        player->Load();
        player->SeekTop();
        int frames = ms * CT_SAMPLE_RATE / 1000;
        std::vector<uint8_t> buf(2048 * 4);
        while (frames > 0) {
            unsigned written = 0;
            int n = frames > 2048 ? 2048 : frames;
            player->GenerateSamples(buf, 0, (unsigned)n);
            frames -= n;
        }
        ended_ = false;
    }
    bool ended() override { return ended_; }
    int voiceCount() override { return 16; }
    const char *voiceName(int v) override {
        snprintf(nameBuf, sizeof(nameBuf), "NDS ch %d", v + 1);
        return nameBuf;
    }
};

} // namespace

CtDecoder *ct_create_xsf(const char *path, int subsong, CtTrackInfo *info) {
    (void)subsong;
    const char *dot = strrchr(path, '.');
    if (!dot) return nullptr;
    bool is2sf = !strcasecmp(dot + 1, "2sf") || !strcasecmp(dot + 1, "mini2sf");
    bool isNcsf = !strcasecmp(dot + 1, "ncsf") || !strcasecmp(dot + 1, "minincsf");
    if (!is2sf && !isNcsf) return nullptr;

    XSFConfig *config = XSFConfig::Create();
    if (config) config->LoadConfig();

    XSFPlayer *player = is2sf ? (XSFPlayer *)new XSFPlayer_2SF(path)
                              : (XSFPlayer *)new XSFPlayer_NCSF(path);
    if (config) {
        config->playInfinitely = false;
        config->CopyConfigToMemory(player, true);
    }
    if (!player->Load()) { delete player; delete config; return nullptr; }

    XsfBackend *b = new XsfBackend();
    b->player = player;
    b->config = config;

    if (info) {
        const XSFFile *file = player->GetXSFFile();
        std::string title, artist;
        if (file) {
            title = file->GetAllTags()["title"];
            artist = file->GetAllTags()["artist"];
        }
        ctCopyStr(info->title, sizeof(info->title), title.empty() ? "NDS sound" : title.c_str());
        ctCopyStr(info->format, sizeof(info->format), is2sf ? "2SF" : "NCSF");
        ctCopyStr(info->system, sizeof(info->system), "Nintendo DS");
        if (!artist.empty()) {
            size_t l = strlen(info->format);
            snprintf(info->format + l, sizeof(info->format) - l, " \xc2\xb7 %s", artist.c_str());
        }
        unsigned long long rate = player->GetSampleRate();
        info->channels = 16;
        info->subsongs = 1;
        info->durationMs = rate > 0 ? (int)(player->GetLengthInSamples() * 1000 / rate) : -1;
        info->backend = CT_BACKEND_GME;
    }
    return b;
}

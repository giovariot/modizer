//
//  BackendFurnace.cpp
//  ModizerMini  —  Furnace tracker engine
//

#include "ChiptuneBackend.h"
#include "CtUtil.h"
#include <stdlib.h>
#include <string.h>
#include <stdio.h>
#include <climits>

#include <string>
#include <vector>
#include "FurnacePlayer.h"

namespace {

struct FurnaceBackend : CtDecoder {
    FurnacePlayer *player = nullptr;
    FurnaceSongInfo songInfo;
    bool ended_ = false;
    char nameBuf[96];

    ~FurnaceBackend() override {
        if (player) { player->stop(); delete player; }
    }

    void render(int16_t *dst, int frames) override {
        if (player) player->render(dst, frames);
    }
    void seekMs(int ms) override {
        if (player) player->seek(ms / 1000.0);
        ended_ = false;
    }
    bool ended() override { return ended_; }
    int voiceCount() override { return songInfo.channels; }
    const char *voiceName(int v) override {
        const char *n = player ? player->getChannelLongName(v) : nullptr;
        snprintf(nameBuf, sizeof(nameBuf), "%s", (n && n[0]) ? n : "");
        return nameBuf;
    }
    void voiceState(int v, int *note, float *level, bool *active) override {
        if (!player) return;
        FurnaceChannelLiveState s = player->getChannelLiveState(v);
        bool on = s.note != INT_MIN;
        if (note) *note = on ? s.note + 60 : 0;
        if (level) *level = s.volume > 0 ? s.volume / 127.0f : 0;
        if (active) *active = on;
    }
};

} // namespace

CtDecoder *ct_create_furnace(const char *path, int subsong, CtTrackInfo *info) {
    FILE *f = fopen(path, "rb");
    if (!f) return nullptr;
    fseek(f, 0, SEEK_END); long size = ftell(f); fseek(f, 0, SEEK_SET);
    if (size <= 0) { fclose(f); return nullptr; }
    uint8_t *data = (uint8_t *)malloc((size_t)size);
    if (!data || fread(data, 1, (size_t)size, f) != (size_t)size) { free(data); fclose(f); return nullptr; }
    fclose(f);

    FurnacePlayer *player = new FurnacePlayer();
    if (!player->init(CT_SAMPLE_RATE) || !player->load(data, (size_t)size, path)) {
        free(data); delete player; return nullptr;
    }
    free(data);

    FurnaceSongInfo si = player->getInfo();
    int subs = si.subsongCount > 0 ? si.subsongCount : 1;
    if (subsong > 0 && subsong < subs) { player->selectSong(subsong); si = player->getInfo(); }

    FurnaceBackend *b = new FurnaceBackend();
    b->player = player;
    b->songInfo = si;

    if (info) {
        ctCopyStr(info->title, sizeof(info->title), si.title.empty() ? "Furnace tune" : si.title.c_str());
        ctCopyStr(info->format, sizeof(info->format), "Furnace");
        ctCopyStr(info->system, sizeof(info->system), si.systemName.empty() ? "Chip" : si.systemName.c_str());
        info->channels = si.channels;
        info->subsongs = subs;
        info->currentSubsong = si.currentSubsong;
        info->durationMs = (int)(player->getDuration() * 1000.0);
        info->backend = CT_BACKEND_GME;
    }
    return b;
}

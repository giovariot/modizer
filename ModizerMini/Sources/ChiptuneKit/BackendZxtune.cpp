//
//  BackendZxtune.cpp
//  ModizerMini  —  ZXTune (ZX Spectrum and friends)
//

#include "ChiptuneBackend.h"
#include "CtUtil.h"
#include <stdlib.h>
#include <string.h>
#include <stdio.h>

#include <string>
#include <vector>
#include "Spectre.h"

namespace {

struct ZxtuneBackend : CtDecoder {
    ZxTuneWrapper *wrapper = nullptr;
    char nameBuf[64];

    ~ZxtuneBackend() override { delete wrapper; }

    void render(int16_t *dst, int frames) override {
        if (wrapper) wrapper->render_sound(dst, (size_t)frames);
    }
    void seekMs(int ms) override {
        if (wrapper) wrapper->seek_position(ms);
    }
    int voiceCount() override { return wrapper ? wrapper->get_channels_count() : 0; }
    const char *voiceName(int v) override {
        const char *n = wrapper ? wrapper->get_channel_name(v) : nullptr;
        snprintf(nameBuf, sizeof(nameBuf), "%s", (n && n[0]) ? n : "");
        return nameBuf;
    }
};

} // namespace

CtDecoder *ct_create_zxtune(const char *path, int subsong, CtTrackInfo *info) {
    FILE *f = fopen(path, "rb");
    if (!f) return nullptr;
    fseek(f, 0, SEEK_END); long size = ftell(f); fseek(f, 0, SEEK_SET);
    if (size <= 0) { fclose(f); return nullptr; }
    char *data = (char *)malloc((size_t)size);
    if (!data || fread(data, 1, (size_t)size, f) != (size_t)size) { free(data); fclose(f); return nullptr; }
    fclose(f);

    ZxTuneWrapper *wrapper = new ZxTuneWrapper(std::string(path), data, (size_t)size, CT_SAMPLE_RATE);
    free(data);
    wrapper->parseModules();
    wrapper->setLoopMode(0);

    SongInfo si;
    int totalTracks = 1;
    wrapper->get_song_info(0, si);
    if (si.get_total_tracks()) totalTracks = atoi(si.get_total_tracks());
    if (totalTracks < 1) totalTracks = 1;
    int track = (subsong > 0 && subsong <= totalTracks) ? subsong : 1;
    wrapper->decodeInitialize((unsigned)track, si);

    ZxtuneBackend *b = new ZxtuneBackend();
    b->wrapper = wrapper;

    if (info) {
        const char *title = si.get_title();
        ctCopyStr(info->title, sizeof(info->title), (title && title[0]) ? title : "ZXTune module");
        const char *codec = si.get_codec();
        const char *prog = si.get_program();
        snprintf(info->format, sizeof(info->format), "%s%s%s",
                 codec ? codec : "ZXTune", (prog && prog[0]) ? "/" : "", (prog && prog[0]) ? prog : "");
        ctCopyStr(info->system, sizeof(info->system), "ZX Spectrum");
        info->channels = wrapper->get_channels_count();
        info->subsongs = totalTracks;
        info->currentSubsong = track - 1;
        info->durationMs = wrapper->get_max_position();
        info->backend = CT_BACKEND_GME;
    }
    return b;
}

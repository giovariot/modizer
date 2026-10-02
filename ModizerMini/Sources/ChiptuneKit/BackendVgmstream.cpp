//
//  BackendVgmstream.cpp
//  ModizerMini
//

#include "ChiptuneBackend.h"
#include "CtUtil.h"
#include <string.h>
#include <stdio.h>

extern "C" {
#include "libvgmstream.h"
#include "libvgmstream_streamfile.h"
}

namespace {

struct VgmstreamBackend : CtDecoder {
    libvgmstream_t *lib = nullptr;
    libstreamfile_t *file = nullptr;
    bool ended_ = false;
    int channels = 2;

    ~VgmstreamBackend() override {
        if (lib) { libvgmstream_close_stream(lib); libvgmstream_free(lib); }
        if (file) libstreamfile_close(file);
    }

    void configure(bool loop) {
        libvgmstream_config_t cfg;
        memset(&cfg, 0, sizeof(cfg));
        cfg.allow_play_forever = true;
        cfg.play_forever = loop;
        cfg.loop_count = loop ? -1 : 1.0;
        cfg.fade_time = 10.0;
        cfg.force_loop = loop;
        cfg.ignore_fade = !loop;
        cfg.stereo_track = 1;
        cfg.auto_downmix_channels = 2;
        cfg.force_sfmt = LIBVGMSTREAM_SFMT_PCM16;
        libvgmstream_setup(lib, &cfg);
    }

    void render(int16_t *dst, int frames) override {
        if (!lib) return;
        int got = libvgmstream_fill(lib, dst, frames);
        if (got <= 0) ended_ = true;
    }
    void seekMs(int ms) override {
        if (!lib) return;
        int64_t rate = lib->format ? lib->format->sample_rate : CT_SAMPLE_RATE;
        libvgmstream_seek(lib, (int64_t)(ms / 1000.0 * rate));
        ended_ = false;
    }
    void setLooping(bool loop) override {
        if (lib) configure(loop);
    }
    bool ended() override { return ended_; }
    int voiceCount() override { return lib && lib->format ? lib->format->channels : 0; }
};

} // namespace

CtDecoder *ct_create_vgmstream(const char *path, int subsong, CtTrackInfo *info) {
    libstreamfile_t *file = libstreamfile_open_from_stdio(path);
    if (!file) return nullptr;
    libvgmstream_t *lib = libvgmstream_init();
    if (!lib) { libstreamfile_close(file); return nullptr; }

    if (libvgmstream_open_stream(lib, file, subsong) != 0 || lib->format == nullptr) {
        libvgmstream_free(lib);
        libstreamfile_close(file);
        return nullptr;
    }

    VgmstreamBackend *b = new VgmstreamBackend();
    b->lib = lib;
    b->file = file;
    b->channels = lib->format->channels;
    b->configure(false);

    if (info) {
        int rate = lib->format->sample_rate;
        ctCopyStr(info->title, sizeof(info->title),
                  lib->format->stream_name[0] ? lib->format->stream_name : "Unknown track");
        ctTrim(info->title);
        ctCopyStr(info->format, sizeof(info->format),
                  lib->format->meta_name[0] ? lib->format->meta_name : "Game audio");
        ctCopyStr(info->system, sizeof(info->system),
                  lib->format->codec_name[0] ? lib->format->codec_name : "vgmstream");
        info->channels = lib->format->channels;
        info->subsongs = lib->format->subsong_count > 0 ? lib->format->subsong_count : 1;
        info->currentSubsong = lib->format->subsong_index;
        int64_t samples = lib->format->play_samples > 0 ? lib->format->play_samples : lib->format->stream_samples;
        info->durationMs = rate > 0 ? (int)(samples * 1000 / rate) : -1;
        info->backend = CT_BACKEND_GME;
        if (info->title[0] == '\0') ctCopyStr(info->title, sizeof(info->title), lib->format->codec_name);
    }
    return b;
}

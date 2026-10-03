//
//  BackendXmpGme.cpp
//  ModizerMini
//
//  libxmp (tracker modules) and Game_Music_Emu (console chiptunes).
//

#include "ChiptuneBackend.h"
#include "CtUtil.h"

#include "xmp.h"
#include "gme.h"

namespace {

struct XmpBackend : CtDecoder {
    xmp_context ctx = nullptr;
    struct xmp_frame_info fi;
    bool ended_ = false;
    int ins = 0;
    char scratchName[64];

    ~XmpBackend() override {
        if (ctx) { xmp_end_player(ctx); xmp_release_module(ctx); xmp_free_context(ctx); }
    }

    void render(int16_t *dst, int frames) override {
        if (!ctx) return;
        if (xmp_play_buffer(ctx, dst, frames * 2 * (int)sizeof(int16_t), 1) == 0) {
            xmp_get_frame_info(ctx, &fi);
        } else {
            ended_ = true;
        }
    }
    void seekMs(int ms) override {
        if (!ctx) return;
        xmp_seek_time(ctx, ms);
        xmp_play_buffer(ctx, NULL, 0, 0);
        ended_ = false;
    }
    bool ended() override { return ended_; }

    int voiceCount() override { return ins; }
    const char *voiceName(int v) override {
        snprintf(scratchName, sizeof(scratchName), "Channel %d", v + 1);
        return scratchName;
    }
    const char *voiceInstrument(int v) override {
        struct xmp_module_info mi;
        xmp_get_module_info(ctx, &mi);
        if (!mi.mod) return "";
        int i = fi.channel_info[v].instrument;
        if (i < 0 || i >= mi.mod->ins) return "";
        return mi.mod->xxi[i].name;
    }
    const char *voiceSample(int v) override {
        struct xmp_module_info mi;
        xmp_get_module_info(ctx, &mi);
        if (!mi.mod) return "";
        int smp = fi.channel_info[v].sample;
        if (smp < 0 || smp >= mi.mod->smp) return "";
        return mi.mod->xxs[smp].name;
    }
    const char *comment() override {
        struct xmp_module_info mi;
        xmp_get_module_info(ctx, &mi);
        return mi.comment ? mi.comment : "";
    }
    int instrumentCount() override {
        struct xmp_module_info mi;
        xmp_get_module_info(ctx, &mi);
        return mi.mod ? mi.mod->ins : 0;
    }
    const char *instrumentName(int index) override {
        struct xmp_module_info mi;
        xmp_get_module_info(ctx, &mi);
        if (!mi.mod || index < 0 || index >= mi.mod->ins) return "";
        return mi.mod->xxi[index].name;
    }
    void voiceState(int v, int *note, float *level, bool *active) override {
        const struct xmp_channel_info *ci = &fi.channel_info[v];
        if (note) *note = (ci->note >= 0x80) ? 0 : ci->note;
        if (level) *level = (float)ci->volume / 64.0f;
        if (active) *active = ci->period != 0;
    }
};

} // namespace

CtDecoder *ct_create_xmp(const char *path, int subsong, CtTrackInfo *info) {
    (void)subsong;
    xmp_context ctx = xmp_create_context();
    if (!ctx) return nullptr;
    if (xmp_load_module(ctx, path) != 0) { xmp_free_context(ctx); return nullptr; }
    if (xmp_start_player(ctx, CT_SAMPLE_RATE, 0) != 0) {
        xmp_release_module(ctx); xmp_free_context(ctx); return nullptr;
    }
    xmp_play_buffer(ctx, NULL, 0, 0);

    struct xmp_module_info mi;
    xmp_get_module_info(ctx, &mi);
    struct xmp_frame_info fi;
    xmp_get_frame_info(ctx, &fi);

    XmpBackend *b = new XmpBackend();
    b->ctx = ctx;
    b->fi = fi;
    b->ins = mi.mod ? mi.mod->chn : 0;

    if (info) {
        if (mi.mod) {
            ctCopyStr(info->title, sizeof(info->title), mi.mod->name);
            ctTrim(info->title);
            ctCopyStr(info->format, sizeof(info->format),
                      mi.mod->type[0] ? mi.mod->type : "Tracker");
            info->channels = mi.mod->chn;
            info->instruments = mi.mod->ins;
            info->samples = mi.mod->smp;
        }
        ctCopyStr(info->system, sizeof(info->system), "Tracker");
        info->subsongs = 1;
        info->durationMs = fi.total_time > 0 ? fi.total_time : -1;
        info->backend = CT_BACKEND_XMP;
        if (info->title[0] == '\0') ctCopyStr(info->title, sizeof(info->title), "Untitled module");
    }
    return b;
}

// ---------------------------------------------------------------------------

namespace {

struct GmeBackend : CtDecoder {
    Music_Emu *emu = nullptr;
    bool ended_ = false;
    int nVoices = 0;
    int subsong = 0;
    char nameBuf[64];
    char commentBuf[1024] = {0};

    ~GmeBackend() override { if (emu) gme_delete(emu); }
    const char *comment() override { return commentBuf; }

    void render(int16_t *dst, int frames) override {
        if (!emu) return;
        if (gme_play(emu, frames * 2, dst) != 0) { ended_ = true; return; }
        if (gme_track_ended(emu)) ended_ = true;
    }
    void seekMs(int ms) override {
        if (!emu) return;
        gme_seek(emu, ms);
        ended_ = false;
    }
    void setLooping(bool) override { ended_ = false; }
    bool ended() override { return ended_; }

    int voiceCount() override { return nVoices; }
    const char *voiceName(int v) override { return gme_voice_name(emu, v); }
};

} // namespace

CtDecoder *ct_create_gme(const char *path, int subsong, CtTrackInfo *info) {
    Music_Emu *emu = nullptr;
    if (gme_open_file(path, &emu, CT_SAMPLE_RATE) != 0 || emu == nullptr) return nullptr;

    int tracks = gme_track_count(emu);
    if (tracks <= 0) tracks = 1;
    if (subsong < 0 || subsong >= tracks) subsong = 0;
    if (gme_start_track(emu, subsong) != 0) { gme_delete(emu); return nullptr; }
    gme_ignore_silence(emu, 1);

    gme_info_t *gi = nullptr;
    gme_track_info(emu, &gi, subsong);
    int duration = -1;
    if (gi) {
        if (gi->play_length > 0) duration = gi->play_length;
        else if (gi->length > 0) duration = gi->length;
    }
    if (duration > 0) gme_set_fade_msecs(emu, duration, 1000);

    GmeBackend *b = new GmeBackend();
    b->emu = emu;
    b->nVoices = gme_voice_count(emu);
    b->subsong = subsong;
    if (gi && gi->comment && gi->comment[0]) ctCopyStr(b->commentBuf, sizeof(b->commentBuf), gi->comment);

    if (info) {
        const char *title = "Unknown track";
        if (gi) {
            if (gi->song && gi->song[0]) title = gi->song;
            else if (gi->game && gi->game[0]) title = gi->game;
        }
        const char *sys = gme_type_system(gme_type(emu));
        ctCopyStr(info->title, sizeof(info->title), title);
        ctCopyStr(info->format, sizeof(info->format), sys ? sys : "Game music");
        ctCopyStr(info->system, sizeof(info->system), sys ? sys : "Console");
        info->channels = gme_voice_count(emu);
        info->subsongs = tracks;
        info->currentSubsong = subsong;
        info->durationMs = duration;
        info->backend = CT_BACKEND_GME;
    }
    if (gi) gme_free_info(gi);
    return b;
}

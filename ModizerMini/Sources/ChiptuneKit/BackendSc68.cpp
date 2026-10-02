//
//  BackendSc68.cpp
//  ModizerMini  —  Atari ST / Amiga (sc68)
//

#include "ChiptuneBackend.h"
#include "CtUtil.h"
#include <stdlib.h>
#include <string.h>
#include <stdio.h>

extern "C" {
#include "sc68/sc68.h"
}

static sc68_t *g_sc68 = nullptr;
extern "C" int getNumberTraceStreams(void) { return g_sc68 ? 4 : 0; }
extern "C" short **getScopeBuffers(void) { return nullptr; }
// sc68 optional curl VFS (not compiled in)
extern "C" int vfs68_curl_init(void) { return -1; }
extern "C" int vfs68_curl_shutdown(void) { return 0; }

namespace {

struct Sc68Backend : CtDecoder {
    sc68_t *s = nullptr;
    int track = 1;
    bool ended_ = false;
    char nameBuf[64];

    ~Sc68Backend() override {
        if (s) { sc68_stop(s); sc68_close(s); }
    }

    void render(int16_t *dst, int frames) override {
        if (!s) return;
        int n = frames;
        int code = sc68_process(s, dst, &n);
        if (code & SC68_END) ended_ = true;
    }
    void seekMs(int ms) override {
        if (!s) return;
        sc68_play(s, (unsigned)track, 0);
        int frames = ms * CT_SAMPLE_RATE / 1000;
        int16_t scratch[2048];
        while (frames > 0) {
            int n = frames > 1024 ? 1024 : frames;
            int cnt = n;
            int16_t buf[2048];
            sc68_process(s, buf, &cnt);
            frames -= n;
        }
        ended_ = false;
    }
    bool ended() override { return ended_; }
    int voiceCount() override { return 4; }
    const char *voiceName(int v) override {
        if (v >= 3) snprintf(nameBuf, sizeof(nameBuf), "Paula %d", v + 1);
        else snprintf(nameBuf, sizeof(nameBuf), "YM ch %c", 'A' + v);
        return nameBuf;
    }
};

} // namespace

CtDecoder *ct_create_sc68(const char *path, int subsong, CtTrackInfo *info) {
    static bool inited = false;
    if (!inited) {
        sc68_init_t init;
        memset(&init, 0, sizeof(init));
        static char *argv[1] = { (char *)"ModizerMini" };
        init.argc = 1;
        init.argv = argv;
        init.flags.no_load_config = 1;
        init.flags.no_save_config = 1;
        if (sc68_init(&init)) return nullptr;
        inited = true;
    }

    sc68_t *s = sc68_create(nullptr);
    if (!s) return nullptr;
    if (sc68_load_uri(s, path)) { sc68_close(s); return nullptr; }

    sc68_music_info_t mi;
    memset(&mi, 0, sizeof(mi));
    sc68_music_info(s, &mi, SC68_CUR_TRACK, 0);
    int tracks = mi.tracks > 0 ? mi.tracks : 1;
    int track = (subsong > 0 && subsong <= tracks) ? subsong : (mi.dsk.track ? (int)mi.dsk.track : 1);
    sc68_play(s, (unsigned)track, 0);

    Sc68Backend *b = new Sc68Backend();
    b->s = s;
    b->track = track;

    if (info) {
        ctCopyStr(info->title, sizeof(info->title),
                  mi.title ? mi.title : (mi.dsk.tags || mi.title ? mi.title : "SC68 tune"));
        if (info->title[0] == '\0') {
            const char *slash = strrchr(path, '/');
            ctCopyStr(info->title, sizeof(info->title), slash ? slash + 1 : "SC68 tune");
        }
        ctCopyStr(info->format, sizeof(info->format), mi.format ? mi.format : "SC68");
        ctCopyStr(info->system, sizeof(info->system), mi.trk.hw ? mi.trk.hw : "Atari ST");
        info->channels = 4;
        info->subsongs = tracks;
        info->currentSubsong = track - 1;
        sc68_music_info(s, &mi, (unsigned)track, 0);
        info->durationMs = (int)mi.trk.time_ms;
        info->backend = CT_BACKEND_GME;
    }
    return b;
}

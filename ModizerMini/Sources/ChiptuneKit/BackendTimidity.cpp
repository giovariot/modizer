//
//  BackendTimidity.cpp
//  ModizerMini  —  MIDI via libtim (Timidity)
//

#include "ChiptuneBackend.h"
#include "CtUtil.h"
#include <stdlib.h>
#include <string.h>
#include <stdio.h>
#include <pthread.h>
#include <unistd.h>

extern "C" {
#include "timidity/timidity.h"
#include "timidity/common.h"
#include "timidity/instrum.h"
#include "timidity/playmidi.h"
#include "timidity/resample.h"
#include "timidity/output.h"
#include "timidity/controls.h"
}

extern "C" {
int tim_init(char *path);
int tim_main(int argc, char **argv);
int tim_close(void);
int tim_midilength, tim_pending_seek, tim_current_voices, tim_lyrics_started;
}

// Host globals expected by libtim.
bool tim_force_soundfont = false;
char tim_force_soundfont_path[1024] = {0};
char tim_config_file_path[1024] = {0};
int mdz_tim_only_precompute = 0;
float tim_tempo_ratio = 1.0f;
int tim_loop_max = 0;
volatile int intr = 0;

// ---- FIFO ------------------------------------------------------------------

#define TIM_FIFO_CAP (1 << 19)
static int16_t timFifo[TIM_FIFO_CAP];
static int timRead = 0, timWrite = 0;
static pthread_mutex_t timMutex = PTHREAD_MUTEX_INITIALIZER;

static int tim_open_output(void) { return 0; }
static void tim_close_output(void) {}
static int tim_output_data(char *buf, int32 nbytes) {
    if (!buf || nbytes <= 0) return 0;
    int16_t *src = (int16_t *)buf;
    int samples = nbytes / 2;
    pthread_mutex_lock(&timMutex);
    for (int i = 0; i < samples; i++) {
        int next = (timWrite + 1) % TIM_FIFO_CAP;
        if (next == timRead) break;
        timFifo[timWrite] = src[i];
        timWrite = next;
    }
    pthread_mutex_unlock(&timMutex);
    return 0;
}
static int tim_acntl(int request, void *arg) { (void)request; (void)arg; return -1; }

extern "C" PlayMode ios_play_mode = {
    44100, PE_16BIT | PE_SIGNED, PF_PCM_STREAM,
    -1,
    {0},
    (char *)"ModizerMini pcm device", 'd',
    (char *)"",
    tim_open_output,
    tim_close_output,
    tim_output_data,
    tim_acntl
};

namespace {

struct TimidityBackend : CtDecoder {
    pthread_t thread = 0;
    volatile bool ended_ = false;
    bool precomputed = false;
    char filepath[1024];

    ~TimidityBackend() override {
        intr = 1;
        if (thread) pthread_join(thread, nullptr);
    }

    static void *mainThread(void *arg) {
        TimidityBackend *b = (TimidityBackend *)arg;
        char *argv[1];
        argv[0] = b->filepath;
        tim_main(1, argv);
        b->ended_ = true;
        return nullptr;
    }

    void render(int16_t *dst, int frames) override {
        int needed = frames * 2, out = 0;
        while (out < needed) {
            pthread_mutex_lock(&timMutex);
            while (out < needed && timRead != timWrite) {
                ((int16_t *)dst)[out++] = timFifo[timRead];
                timRead = (timRead + 1) % TIM_FIFO_CAP;
            }
            pthread_mutex_unlock(&timMutex);
            if (out < needed) {
                if (ended_) break;
                usleep(1000);
            }
        }
        if (out < needed) memset((int16_t *)dst + out, 0, (size_t)(needed - out) * sizeof(int16_t));
    }
    void seekMs(int ms) override { tim_pending_seek = (int)ms; ended_ = false; }
    bool ended() override { return ended_ && timRead == timWrite; }
    int voiceCount() override { return 64; }
    const char *voiceName(int v) override {
        static char buf[16];
        if (v >= 16) return "";
        snprintf(buf, sizeof(buf), "Ch %d", v + 1);
        return buf;
    }
    const char *voiceInstrument(int v) override {
        // General MIDI instrument name for the channel's current program.
        if (v < 0 || v >= 16) return "";
        char *n = channel_instrum_name(v);
        return (n && n[0]) ? n : "";
    }
};

} // namespace

CtDecoder *ct_create_timidity(const char *path, int subsong, CtTrackInfo *info) {
    (void)subsong;
    const char *slash = strrchr(path, '/');
    char dir[1024];
    if (slash) { size_t n = (size_t)(slash - path); if (n > 1023) n = 1023; memcpy(dir, path, n); dir[n] = 0; }
    else snprintf(dir, sizeof(dir), ".");

    tim_init(dir);
    tim_force_soundfont = false;
    tim_loop_max = 0;
    tim_tempo_ratio = 1.0f;
    timRead = timWrite = 0;

    TimidityBackend *b = new TimidityBackend();
    snprintf(b->filepath, sizeof(b->filepath), "%s", path);
    pthread_create(&b->thread, nullptr, TimidityBackend::mainThread, b);

    if (info) {
        const char *name = slash ? slash + 1 : path;
        ctCopyStr(info->title, sizeof(info->title), name);
        char *dot = strrchr(info->title, '.');
        if (dot) *dot = '\0';
        ctCopyStr(info->format, sizeof(info->format), "MIDI");
        ctCopyStr(info->system, sizeof(info->system), "MIDI");
        info->channels = 64;
        info->subsongs = 1;
        info->durationMs = -1;
        info->backend = CT_BACKEND_GME;
    }
    return b;
}

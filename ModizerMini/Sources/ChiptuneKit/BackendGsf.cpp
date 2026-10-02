//
//  BackendGsf.cpp
//  ModizerMini  —  GSF (Game Boy Advance) via libgsf
//
//  libgsf is push based: it calls writeSound() from its own emulation loop, so
//  the emulation runs on a thread and feeds a small FIFO the engine drains.
//

#include "ChiptuneBackend.h"
#include "CtUtil.h"
#include <stdlib.h>
#include <string.h>
#include <stdio.h>
#include <pthread.h>
#include <unistd.h>

typedef int BOOL;
extern "C" {
#include "gsf.h"
}

// ---- globals the library expects -------------------------------------------
extern "C" {
int defvolume = 1000;
int relvolume = 1000;
int TrackLength = 0;
int FadeLength = 0;
int IgnoreTrackLength = 0, DefaultLength = 150000;
int playforever = 0;
int fileoutput = 0;
int TrailingSilence = 1000;
int DetectSilence = 0, silencedetected = 0, silencelength = 5;
}

int cpupercent = 0, sndSamplesPerSec = 44100, sndNumChannels = 2;
int sndBitsPerSample = 16;
int deflen = 120, deffade = 4;

extern "C" {
unsigned short soundFinalWave[1470];
extern int soundBufferLen;
}

double decode_pos_ms = 0;
int seek_needed = -1;

// ---- FIFO shared with writeSound -------------------------------------------

#define GSF_FIFO_CAP (1 << 18)
static int16_t gsfFifo[GSF_FIFO_CAP];
static int gsfRead = 0, gsfWrite = 0;
static pthread_mutex_t gsfMutex = PTHREAD_MUTEX_INITIALIZER;

static int gsfParseTime(const char *v) {
    if (!v) return 0;
    double total = 0; const char *p = v; char *end = nullptr;
    while (*p) {
        double num = strtod(p, &end);
        if (end == p) break;
        if (*end == ':') { total = total * 60 + num; p = end + 1; }
        else { total += num; p = end; }
    }
    return (int)(total * 1000.0);
}

extern "C" void end_of_track(void) {}

extern "C" void writeSound(void) {
    int bytes = soundBufferLen;
    if (bytes <= 0) return;
    int samples = bytes / 2;
    pthread_mutex_lock(&gsfMutex);
    for (int i = 0; i < samples; i++) {
        int next = (gsfWrite + 1) % GSF_FIFO_CAP;
        if (next == gsfRead) break; // full: drop
        gsfFifo[gsfWrite] = (int16_t)soundFinalWave[i];
        gsfWrite = next;
    }
    pthread_mutex_unlock(&gsfMutex);
    decode_pos_ms += (bytes / (2 * (sndNumChannels ? sndNumChannels : 2)) * 1000.0) / (double)(sndSamplesPerSec ? sndSamplesPerSec : 44100);
}

namespace {

struct GsfBackend : CtDecoder {
    pthread_t thread;
    volatile bool running = false;
    volatile bool ended_ = false;
    char nameBuf[64];

    ~GsfBackend() override {
        running = false;
        if (thread) pthread_join(thread, nullptr);
        GSFClose();
    }

    static void *emulationMain(void *arg) {
        GsfBackend *b = (GsfBackend *)arg;
        while (b->running) {
            if (!EmulationLoop()) break;
        }
        b->ended_ = true;
        return nullptr;
    }

    void render(int16_t *dst, int frames) override {
        int needed = frames * 2;
        int out = 0;
        while (out < needed) {
            if (gsfRead == gsfWrite) { usleep(2000); if (gsfRead == gsfWrite) break; }
            pthread_mutex_lock(&gsfMutex);
            while (out < needed && gsfRead != gsfWrite) {
                ((int16_t *)dst)[out++] = gsfFifo[gsfRead];
                gsfRead = (gsfRead + 1) % GSF_FIFO_CAP;
            }
            pthread_mutex_unlock(&gsfMutex);
        }
        if (out < needed) memset((int16_t *)dst + out, 0, (size_t)(needed - out) * sizeof(int16_t));
    }
    void seekMs(int ms) override {
        seek_needed = ms;
        decode_pos_ms = 0;
        pthread_mutex_lock(&gsfMutex);
        gsfRead = gsfWrite = 0;
        pthread_mutex_unlock(&gsfMutex);
        ended_ = false;
    }
    bool ended() override { return ended_ && gsfRead == gsfWrite; }
    int voiceCount() override { return 6; }
    const char *voiceName(int v) override {
        snprintf(nameBuf, sizeof(nameBuf), v < 4 ? "DMG %d" : "DirectSound %d", (v % 4) + 1);
        return nameBuf;
    }
};

} // namespace

CtDecoder *ct_create_gsf(const char *path, int subsong, CtTrackInfo *info) {
    (void)subsong;
    GsfBackend *b = new GsfBackend();
    gsfRead = gsfWrite = 0;
    decode_pos_ms = 0;
    seek_needed = -1;
    IgnoreTrackLength = 0;
    DetectSilence = 1;
    TrailingSilence = 1000;
    playforever = 0;
    TrackLength = 0;
    FadeLength = 0;

    if (GSFRun((char *)path) != 0) { delete b; return nullptr; }

    int trackMs = DefaultLength;
    TrackLength = trackMs;
    FadeLength = 0;

    b->running = true;
    pthread_create(&b->thread, nullptr, GsfBackend::emulationMain, b);

    if (info) {
        const char *slash = strrchr(path, '/');
        ctCopyStr(info->title, sizeof(info->title), slash ? slash + 1 : path);
        char *dot = strrchr(info->title, '.');
        if (dot) *dot = '\0';
        ctCopyStr(info->format, sizeof(info->format), "GSF");
        ctCopyStr(info->system, sizeof(info->system), "Game Boy Advance");
        info->channels = 6;
        info->subsongs = 1;
        info->durationMs = TrackLength > 0 ? TrackLength : -1;
        info->backend = CT_BACKEND_GME;
    }
    return b;
}

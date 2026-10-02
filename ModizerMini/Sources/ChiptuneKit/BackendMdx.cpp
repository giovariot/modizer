//
//  BackendMdx.cpp
//  ModizerMini  —  MDX / PDX (PC-98)
//
//  mdx_play() runs the whole song, pushing PCM through mdx_update(), so the
//  emulation lives on its own thread and feeds a FIFO.
//

#include "ChiptuneBackend.h"
#include "CtUtil.h"
#include <stdlib.h>
#include <string.h>
#include <stdio.h>
#include <pthread.h>
#include <unistd.h>

extern "C" {
#include "mdx.h"
}

// Globals the MDX engine expects from its host.
extern "C" int PLAYBACK_FREQ = 44100;
extern "C" int MDXshoudlReset = 0;

#define MDX_FIFO_CAP (1 << 18)
static int16_t mdxFifo[MDX_FIFO_CAP];
static int mdxRead = 0, mdxWrite = 0;
static pthread_mutex_t mdxMutex = PTHREAD_MUTEX_INITIALIZER;

extern "C" void mdx_update(unsigned char *data, int len, int end_reached) {
    (void)end_reached;
    if (!data || len <= 0) return;
    int samples = len / 2;
    int16_t *src = (int16_t *)data;
    pthread_mutex_lock(&mdxMutex);
    for (int i = 0; i < samples; i++) {
        int next = (mdxWrite + 1) % MDX_FIFO_CAP;
        if (next == mdxRead) break;
        mdxFifo[mdxWrite] = src[i];
        mdxWrite = next;
    }
    pthread_mutex_unlock(&mdxMutex);
}

namespace {

struct MdxBackend : CtDecoder {
    MDX_DATA *mdx = nullptr;
    PDX_DATA *pdx = nullptr;
    pthread_t thread = 0;
    bool ended_ = false;
    char nameBuf[64];

    ~MdxBackend() override {
        if (thread) pthread_join(thread, nullptr);
        mdx_stop();
        if (mdx) mdx_close(mdx, pdx);
    }

    static void *playMain(void *arg) {
        MdxBackend *b = (MdxBackend *)arg;
        mdx_play(b->mdx, b->pdx);
        b->ended_ = true;
        return nullptr;
    }

    void render(int16_t *dst, int frames) override {
        int needed = frames * 2, out = 0;
        while (out < needed) {
            pthread_mutex_lock(&mdxMutex);
            while (out < needed && mdxRead != mdxWrite) {
                ((int16_t *)dst)[out++] = mdxFifo[mdxRead];
                mdxRead = (mdxRead + 1) % MDX_FIFO_CAP;
            }
            pthread_mutex_unlock(&mdxMutex);
            if (out < needed) {
                if (ended_) break;
                usleep(1000);
            }
        }
        if (out < needed) memset((int16_t *)dst + out, 0, (size_t)(needed - out) * sizeof(int16_t));
    }
    void seekMs(int ms) override {
        (void)ms;   // restart-and-skip is not wired for MDX
    }
    bool ended() override { return ended_ && mdxRead == mdxWrite; }
    int voiceCount() override { int n = mdx_get_realtracks(); return n > 0 ? n : 16; }
    const char *voiceName(int v) override {
        snprintf(nameBuf, sizeof(nameBuf), "MDX ch %d", v + 1);
        return nameBuf;
    }
};

} // namespace

CtDecoder *ct_create_mdx(const char *path, int subsong, CtTrackInfo *info) {
    (void)subsong;
    MDX_DATA *mdx = nullptr;
    PDX_DATA *pdx = nullptr;
    if (mdx_load((char *)path, &mdx, &pdx, 0) != 0 || !mdx) return nullptr;

    MdxBackend *b = new MdxBackend();
    b->mdx = mdx;
    b->pdx = pdx;
    mdxRead = mdxWrite = 0;
    pthread_create(&b->thread, nullptr, MdxBackend::playMain, b);

    if (info) {
        unsigned char *t = mdx_get_title(mdx);
        ctCopyStr(info->title, sizeof(info->title), (t && t[0]) ? (const char *)t : "MDX tune");
        ctCopyStr(info->format, sizeof(info->format), pdx ? "MDX/PDX" : "MDX");
        ctCopyStr(info->system, sizeof(info->system), "PC-98");
        info->channels = b->voiceCount();
        info->subsongs = 1;
        info->durationMs = mdx_get_length(mdx, pdx);
        info->backend = CT_BACKEND_GME;
    }
    return b;
}

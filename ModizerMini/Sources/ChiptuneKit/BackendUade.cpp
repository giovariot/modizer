//
//  BackendUade.cpp
//  ModizerMini  —  Amiga modules via UADE
//
//  UADE is split in a frontend and an emulator core that normally talk over a
//  socketpair. Here the core runs in a thread and the pair is replaced by the
//  shared in-memory IPC buffers of unixatomic.c (fd://0 / fd://1), exactly as
//  Modizer does.
//

#include "ChiptuneBackend.h"
#include "CtUtil.h"
#include <stdlib.h>
#include <string.h>
#include <stdio.h>
#include <pthread.h>
#include <unistd.h>
#include <arpa/inet.h>
#include <limits.h>

extern "C" {
#include "uadeipc.h"
#include "eagleplayer.h"
#include "effects.h"
#include "uadestate.h"
#include "uadecontrol.h"
#include "uadeconf.h"
#include "unixsupport.h"
}

extern "C" int uade_main(int argc, char **argv);
extern "C" int uade_alloc_song(struct uade_state *state, const char *name);
extern "C" void uade_unalloc_song(struct uade_state *state);

// Host symbols required by unixatomic.c.
extern "C" {
pthread_mutex_t uade_mutex;
void uade_dummy_wait(void) { usleep(500); }
}

extern char g_ct_resource_path[1024];

// ---------------------------------------------------------------------------
// PCM FIFO
// ---------------------------------------------------------------------------

#define UADE_FIFO_CAP (1 << 19)
static int16_t uadeFifo[UADE_FIFO_CAP];
static int uadeRead = 0, uadeWrite = 0;
static pthread_mutex_t uadeFifoMutex = PTHREAD_MUTEX_INITIALIZER;

static void uadeFifoPush(const int16_t *src, int samples) {
    pthread_mutex_lock(&uadeFifoMutex);
    for (int i = 0; i < samples; i++) {
        int next = (uadeWrite + 1) % UADE_FIFO_CAP;
        if (next == uadeRead) break;
        uadeFifo[uadeWrite] = src[i];
        uadeWrite = next;
    }
    pthread_mutex_unlock(&uadeFifoMutex);
}

// ---------------------------------------------------------------------------
// shared state
// ---------------------------------------------------------------------------

namespace {

struct UadeBackend : CtDecoder {
    pthread_t coreThread = 0;
    pthread_t pullThread = 0;
    volatile bool running = false;
    volatile bool ended_ = false;
    char formatName[128] = {0};
    char playerName[128] = {0};
    bool coreStarted = false;
    bool looping_ = false;
    char nameBuf[64];

    ~UadeBackend() override {
        running = false;
        if (coreStarted) {
            // ask the core to stop and wait briefly
            uade_send_short_message(UADE_EXIT, &uadeState->ipc);
        }
        if (pullThread) pthread_join(pullThread, nullptr);
        if (coreStarted && coreThread) pthread_join(coreThread, nullptr);
    }

    struct uade_state *uadeState = nullptr;

    void render(int16_t *dst, int frames) override {
        int needed = frames * 2, out = 0;
        while (out < needed) {
            pthread_mutex_lock(&uadeFifoMutex);
            while (out < needed && uadeRead != uadeWrite) {
                ((int16_t *)dst)[out++] = uadeFifo[uadeRead];
                uadeRead = (uadeRead + 1) % UADE_FIFO_CAP;
            }
            pthread_mutex_unlock(&uadeFifoMutex);
            if (out < needed) {
                if (ended_) break;
                usleep(1000);
            }
        }
        if (out < needed) memset((int16_t *)dst + out, 0, (size_t)(needed - out) * sizeof(int16_t));
    }
    void seekMs(int ms) override { (void)ms; }
    void setLooping(bool loop) override { looping_ = loop; }
    bool ended() override { return ended_ && uadeRead == uadeWrite; }
    int voiceCount() override { return 4; }
    const char *voiceName(int v) override {
        snprintf(nameBuf, sizeof(nameBuf), "Paula ch %d", v + 1);
        return nameBuf;
    }

    static void *coreMain(void *arg);
    static void *pullMain(void *arg);
};

// one instance at a time (UADE global state is global)
static struct uade_state uadeStateStore;
static UadeBackend *g_activeBackend = nullptr;
static char uadeConfigName[PATH_MAX];
static char uadeScoreName[PATH_MAX];
static char uadePlayerName[PATH_MAX];
static bool uadeInitialised = false;

void *UadeBackend::coreMain(void *arg) {
    (void)arg;
    int argc = 5;
    char *argv[5];
    char buf[5 * 32];
    for (int i = 0; i < argc; i++) argv[i] = buf + 32 * i;
    strcpy(argv[0], "uadecore");
    strcpy(argv[1], "-i");
    strcpy(argv[2], "fd://0");
    strcpy(argv[3], "-o");
    strcpy(argv[4], "fd://0");
    uade_main(argc, argv);
    if (g_activeBackend) g_activeBackend->ended_ = true;
    return nullptr;
}

void *UadeBackend::pullMain(void *arg) {
    UadeBackend *b = (UadeBackend *)arg;
    struct uade_state *state = b->uadeState;
    struct uade_ipc *ipc = &state->ipc;
    struct uade_effect *ue = &state->effects;

    uint8_t space[UADE_MAX_MESSAGE_SIZE];
    struct uade_msg *um = (struct uade_msg *)space;

    uade_effect_reset_internals();

    while (b->running) {
        int left = uade_read_request(ipc);
        if (uade_send_short_message(UADE_COMMAND_TOKEN, ipc)) break;

        int songEnd = 0, done = 0;
        while (!done && b->running) {
            if (uade_receive_message(um, sizeof(space), ipc) <= 0) { done = 1; b->running = false; break; }
            switch (um->msgtype) {
                case UADE_COMMAND_TOKEN:
                    done = 1;
                    break;
                case UADE_REPLY_DATA:
                    if (um->size) {
                        uade_effect_run(ue, (int16_t *)um->data, um->size / 4);
                        uadeFifoPush((const int16_t *)um->data, um->size / 2);
                    }
                    break;
                case UADE_REPLY_SONG_END:
                    songEnd = 1;
                    break;
                case UADE_REPLY_FORMATNAME:
                    uade_check_fix_string(um, 128);
                    ctCopyStr(b->formatName, sizeof(b->formatName), (const char *)um->data);
                    break;
                case UADE_REPLY_PLAYERNAME:
                    uade_check_fix_string(um, 128);
                    ctCopyStr(b->playerName, sizeof(b->playerName), (const char *)um->data);
                    break;
                case UADE_REPLY_SUBSONG_INFO: {
                    uint32_t *p = (uint32_t *)um->data;
                    if (um->size >= 12) {
                        state->song->min_subsong = ntohl(p[0]);
                        state->song->max_subsong = ntohl(p[1]);
                        state->song->cur_subsong = ntohl(p[2]);
                    }
                    break;
                }
                default:
                    break;
            }
            (void)left;
        }

        if (songEnd && !b->looping_) break;
    }

    b->ended_ = true;
    return nullptr;
}

} // namespace

CtDecoder *ct_create_uade(const char *path, int subsong, CtTrackInfo *info) {
    (void)subsong;
    if (!uadeInitialised) {
        pthread_mutex_init(&uade_mutex, nullptr);
        memset(&uadeStateStore, 0, sizeof(uadeStateStore));
        struct uade_state baseState;
        memset(&baseState, 0, sizeof(baseState));
        const char *res = g_ct_resource_path[0] ? g_ct_resource_path : ".";
        char basedir[PATH_MAX];
        snprintf(basedir, sizeof(basedir), "%s/UADE", res);
        snprintf(baseState.config.basedir.name, PATH_MAX, "%s", basedir);
        baseState.config.basedir_set = 1;
        char confname[PATH_MAX];
        uade_load_initial_config(confname, sizeof(confname), &uadeStateStore.config, &baseState.config);
        snprintf(uadeStateStore.config.basedir.name, PATH_MAX, "%s", basedir);
        snprintf(uadeConfigName, PATH_MAX, "%s/uaerc", basedir);
        snprintf(uadeScoreName, PATH_MAX, "%s/score", basedir);
        uadeInitialised = true;
    }

    struct uade_state *state = &uadeStateStore;
    state->ipc.state = UADE_INITIAL_STATE;

    UadeBackend *b = new UadeBackend();
    b->uadeState = state;
    g_activeBackend = b;

    // start the core thread first: it blocks waiting for the config name
    b->running = true;
    pthread_create(&b->coreThread, nullptr, UadeBackend::coreMain, nullptr);
    b->coreStarted = true;
    // give the core a moment to set up its IPC endpoint
    usleep(20000);

    state->ipc.input = uade_ipc_set_input("fd://1");
    state->ipc.output = uade_ipc_set_output("fd://1");

    if (uade_send_string(UADE_COMMAND_CONFIG, uadeConfigName, &state->ipc)) {
        delete b;
        return nullptr;
    }

    char modulename[PATH_MAX];
    snprintf(modulename, sizeof(modulename), "%s", path);
    state->song = nullptr;
    state->ep = nullptr;
    if (!uade_is_our_file(modulename, 0, state)) { delete b; return nullptr; }

    if (strcmp(state->ep->playername, "custom") == 0) {
        snprintf(uadePlayerName, PATH_MAX, "%s", modulename);
        modulename[0] = 0;
    } else {
        snprintf(uadePlayerName, PATH_MAX, "%s/%s", state->config.basedir.name, state->ep->playername);
    }

    char songname[PATH_MAX];
    snprintf(songname, sizeof(songname), "%s", modulename[0] ? modulename : uadePlayerName);

    if (!uade_alloc_song(state, songname)) { delete b; return nullptr; }
    if (state->ep != nullptr) uade_set_ep_attributes(state);

    state->config.no_postprocessing = 0;
    state->config.headphones = 0;
    state->config.gain_enable = 0;
    state->config.normalise = 0;
    state->config.panning_enable = 0;
    state->config.no_ep_end = 0;
    state->config.frequency = CT_SAMPLE_RATE;
    state->config.use_ntsc = 0;
    uade_set_effects(state);

    int ret = uade_song_initialization(uadeScoreName, uadePlayerName, modulename, state);
    if (ret) {
        uade_unalloc_song(state);
        delete b;
        return nullptr;
    }

    uadeRead = uadeWrite = 0;
    pthread_create(&b->pullThread, nullptr, UadeBackend::pullMain, b);

    if (info) {
        const char *slash = strrchr(path, '/');
        char name[256];
        snprintf(name, sizeof(name), "%s", slash ? slash + 1 : path);
        char *dot = strrchr(name, '.');
        if (dot) *dot = '\0';
        ctCopyStr(info->title, sizeof(info->title), name);
        ctCopyStr(info->format, sizeof(info->format), "Amiga module");
        ctCopyStr(info->system, sizeof(info->system), "Amiga (UADE)");
        info->channels = 4;
        info->subsongs = 1;
        info->durationMs = state->song ? state->song->playtime : -1;
        info->backend = CT_BACKEND_GME;
    }
    return b;
}

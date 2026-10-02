//
//  BackendVgm.cpp
//  ModizerMini  —  libvgm (VGM / S98 / DRO / GYM)
//

#include "ChiptuneBackend.h"
#include "CtUtil.h"
#include <string.h>
#include <stdio.h>

#include "player/playera.hpp"
#include "player/vgmplayer.hpp"
#include "player/s98player.hpp"
#include "player/droplayer.hpp"
#include "player/gymplayer.hpp"
#include "utils/DataLoader.h"
#include "utils/FileLoader.h"

namespace {

struct VgmBackend : CtDecoder {
    PlayerA player;
    DATA_LOADER *loader = nullptr;
    bool ended_ = false;
    int nVoices = 0;
    char nameBuf[64];

    ~VgmBackend() override {
        player.UnregisterAllPlayers();
        if (loader) DataLoader_Deinit(loader);
    }

    void render(int16_t *dst, int frames) override {
        if (player.GetState() & PLAYSTATE_END) { ended_ = true; return; }
        player.Render((UINT32)frames * 2 * 2, (void *)dst);
    }
    void seekMs(int ms) override {
        player.Seek(PLAYPOS_SAMPLE, (UINT32)(ms * CT_SAMPLE_RATE / 1000));
        ended_ = false;
    }
    bool ended() override { return ended_; }
    int voiceCount() override { return nVoices; }
    const char *voiceName(int v) override {
        snprintf(nameBuf, sizeof(nameBuf), "VGM ch %d", v + 1);
        return nameBuf;
    }
};

} // namespace

CtDecoder *ct_create_vgm(const char *path, int subsong, CtTrackInfo *info) {
    (void)subsong;
    VgmBackend *b = new VgmBackend();
    PlayerA &player = b->player;

    player.RegisterPlayerEngine(new VGMPlayer);
    player.RegisterPlayerEngine(new S98Player);
    player.RegisterPlayerEngine(new DROPlayer);
    player.RegisterPlayerEngine(new GYMPlayer);
    player.SetOutputSettings(CT_SAMPLE_RATE, 2, 16, 512);

    PlayerA::Config cfg = player.GetConfiguration();
    cfg.masterVol = 0x10000;
    cfg.loopCount = 2;
    cfg.fadeSmpls = (UINT32)(CT_SAMPLE_RATE * 5);
    cfg.endSilenceSmpls = 0;
    player.SetConfiguration(cfg);

    DATA_LOADER *loader = FileLoader_Init(path);
    if (!loader) { delete b; return nullptr; }
    b->loader = loader;
    DataLoader_SetPreloadBytes(loader, 0x100);
    if (DataLoader_Load(loader) != 0) { delete b; return nullptr; }
    if (player.LoadFile(loader) != 0) { delete b; return nullptr; }

    player.Start();

    // Count voices from the devices used by the song.
    PLR_SONG_INFO sInf;
    memset(&sInf, 0, sizeof(sInf));
    std::vector<PLR_DEV_INFO> devs;
    PlayerBase *eng = player.GetPlayer();
    if (eng) {
        eng->GetSongInfo(sInf);
        eng->GetSongDeviceInfo(devs);
    }
    b->nVoices = devs.empty() ? 16 : (int)devs.size() * 4;
    if (b->nVoices > 64) b->nVoices = 64;

    if (info) {
        const char *slash = strrchr(path, '/');
        ctCopyStr(info->title, sizeof(info->title), slash ? slash + 1 : path);
        char *dot = strrchr(info->title, '.');
        if (dot) *dot = '\0';
        ctCopyStr(info->format, sizeof(info->format), "VGM");
        ctCopyStr(info->system, sizeof(info->system), "Game chips");
        info->channels = b->nVoices;
        info->subsongs = 1;
        info->durationMs = -1;
        info->backend = CT_BACKEND_GME;
    }
    return b;
}

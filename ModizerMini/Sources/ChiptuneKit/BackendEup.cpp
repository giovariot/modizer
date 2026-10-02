//
//  BackendEup.cpp
//  ModizerMini  —  EUP (FM Towns) via libeupmini
//

#include "ChiptuneBackend.h"
#include "CtUtil.h"
#include <stdlib.h>
#include <string.h>
#include <stdio.h>
#include <string>

#include "eupplayer.hpp"
#include "eupplayer_townsEmulator.hpp"
#include "audioout.hpp"

// The library writes into this global ring buffer.
struct pcm_struct eup_pcm;
extern "C" int eup_mutemask = 0;

namespace {

#pragma pack(push, 1)
struct EupHeader {
    char title[32];
    char artist[8];
    char dummy[44];
    char trk_name[32][16];
    char short_trk_name[32][8];
    char trk_mute[32];
    char trk_port[32];
    char trk_midi_ch[32];
    char trk_key_bias[32];
    char trk_transpose[32];
    char trk_play_filter[32][7];
    char instruments_name[128][4];
    char fm_midi_ch[6];
    char pcm_midi_ch[8];
    char fm_file_name[8];
    char pcm_file_name[8];
    char reserved[260];
    char appli_name[8];
    char appli_version[2];
    int32_t size;
    char signature;
    char first_tempo;
};
#pragma pack(pop)

static FILE *open_in_dir(const std::string &name, const char *dir) {
    std::string path = std::string(dir) + "/" + name;
    return fopen(path.c_str(), "rb");
}

struct EupBackend : CtDecoder {
    EUPPlayer *player = nullptr;
    EUP_TownsEmulator *dev = nullptr;
    uint8_t *buf = nullptr;
    char nameBuf[64];

    ~EupBackend() override {
        if (player) player->stopPlaying();
        delete player;
        delete dev;
        free(buf);
    }

    void render(int16_t *dst, int frames) override {
        int out = 0, needed = frames * 2;
        int idle = 0;
        while (out < needed) {
            if (eup_pcm.read_pos == eup_pcm.write_pos) {
                if (!player->isPlaying()) break;
                if (idle++ > 200) break;
                player->nextTick(false);
                continue;
            }
            int16_t *rb = (int16_t *)eup_pcm.buffer;
            while (out < needed && eup_pcm.read_pos != eup_pcm.write_pos) {
                ((int16_t *)dst)[out++] = rb[eup_pcm.read_pos];
                eup_pcm.read_pos = (eup_pcm.read_pos + 1) % streamAudioBufferSamples;
            }
        }
        if (out < needed) memset((int16_t *)dst + out, 0, (size_t)(needed - out) * sizeof(int16_t));
    }
    void seekMs(int ms) override { (void)ms; }
    int voiceCount() override { return 14; }
    const char *voiceName(int v) override {
        snprintf(nameBuf, sizeof(nameBuf), v < 6 ? "FM %d" : "PCM %d", v < 6 ? v + 1 : v - 5);
        return nameBuf;
    }
};

} // namespace

CtDecoder *ct_create_eup(const char *path, int subsong, CtTrackInfo *info) {
    (void)subsong;
    struct EupHeader header;
    FILE *f = fopen(path, "rb");
    if (!f) return nullptr;
    if (fread(&header, 1, sizeof(header), f) != sizeof(header)) { fclose(f); return nullptr; }
    long fileSize = 0;
    fseek(f, 0, SEEK_END); fileSize = ftell(f); fseek(f, 0, SEEK_SET);
    if (fileSize < 2048 + 12) { fclose(f); return nullptr; }
    uint8_t *data = (uint8_t *)malloc((size_t)fileSize);
    if (!data || fread(data, 1, (size_t)fileSize, f) != (size_t)fileSize) { free(data); fclose(f); return nullptr; }
    fclose(f);

    EUP_TownsEmulator *dev = new EUP_TownsEmulator();
    EUPPlayer *player = new EUPPlayer();
    dev->rate(streamAudioRate);
    player->outputDevice(dev);

    memset(&eup_pcm, 0, sizeof(eup_pcm));
    eup_pcm.on = 1;
    eup_pcm.read_pos = 0;

    player->tempo(header.first_tempo + 30);
    for (int trk = 0; trk < 32; trk++) player->mapTrack_toChannel(trk, header.trk_midi_ch[trk]);
    for (int n = 0; n < 6; n++) dev->assignFmDeviceToChannel(header.fm_midi_ch[n], n);
    for (int n = 0; n < 8; n++) dev->assignPcmDeviceToChannel(header.pcm_midi_ch[n]);

    // default FM instrument
    {
        uint8_t instrument[48] = {
            ' ', ' ', ' ', ' ', ' ', ' ', ' ', ' ',
            17, 33, 10, 17, 25, 10, 57, 0,
            154, 152, 218, 216, 15, 12, 7, 12,
            0, 5, 3, 5, 38, 40, 70, 40,
            20, 0xc0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,
        };
        for (int n = 0; n < 128; n++) dev->setFmInstrumentParameter(n, instrument);
    }

    // locate the EUP directory for .fmb / .pmb
    std::string eupDir(path);
    size_t slash = eupDir.rfind('/');
    eupDir = (slash == std::string::npos) ? "." : eupDir.substr(0, slash);
    const char *home = getenv("HOME");
    std::string instDir = eupDir;
    if (home) { instDir += ";"; instDir += home; instDir += "/.eupplay"; }

    // FMB
    {
        char fn[16];
        memcpy(fn, header.fm_file_name, 8); fn[8] = 0;
        std::string fmb = std::string(fn) + ".fmb";
        FILE *ff = open_in_dir(fmb, eupDir.c_str());
        if (!ff && home) ff = open_in_dir(fmb, (std::string(home) + "/.eupplay").c_str());
        if (ff) {
            fseek(ff, 0, SEEK_END); long sz = ftell(ff); fseek(ff, 0, SEEK_SET);
            uint8_t *fb = (uint8_t *)malloc((size_t)sz);
            if (fb && fread(fb, 1, (size_t)sz, ff) == (size_t)sz) {
                for (int n = 0; n < (sz - 8) / 48; n++) dev->setFmInstrumentParameter(n, fb + 8 + 48 * n);
            }
            free(fb);
            fclose(ff);
        }
    }
    // PMB
    {
        char fn[16];
        memcpy(fn, header.pcm_file_name, 8); fn[8] = 0;
        std::string pmb = std::string(fn) + ".pmb";
        FILE *ff = open_in_dir(pmb, eupDir.c_str());
        if (!ff && home) ff = open_in_dir(pmb, (std::string(home) + "/.eupplay").c_str());
        if (ff) {
            fseek(ff, 0, SEEK_END); long sz = ftell(ff); fseek(ff, 0, SEEK_SET);
            uint8_t *pb = (uint8_t *)malloc((size_t)sz);
            if (pb && fread(pb, 1, (size_t)sz, ff) == (size_t)sz) dev->setPcmInstrumentParameters(pb, (int)sz);
            free(pb);
            fclose(ff);
        }
    }

    player->startPlaying(data + 2048 + 6);

    EupBackend *b = new EupBackend();
    b->player = player;
    b->dev = dev;
    b->buf = data;

    if (info) {
        char title[33];
        memcpy(title, header.title, 32); title[32] = 0;
        for (int i = 0; i < 32; i++) if (title[i] < 0x20) title[i] = ' ';
        ctTrim(title);
        ctCopyStr(info->title, sizeof(info->title), title[0] ? title : "EUP tune");
        ctCopyStr(info->format, sizeof(info->format), "Euphony");
        ctCopyStr(info->system, sizeof(info->system), "FM Towns");
        info->channels = 14;
        info->subsongs = 1;
        info->durationMs = -1;
        info->backend = CT_BACKEND_GME;
    }
    return b;
}

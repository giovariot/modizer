//
//  BackendPt3.cpp
//  ModizerMini  —  ZX Spectrum / AY (PT3 + ayumi)
//

#include "ChiptuneBackend.h"
#include "CtUtil.h"
#include <stdlib.h>
#include <string.h>
#include <stdio.h>

extern "C" {
#include "pt3player.h"
#include "ayumi.h"
#include "load_text.h"
}

#define PT3_CHIP 0

namespace {

struct Pt3Backend : CtDecoder {
    struct ayumi ay;
    struct ay_data t;
    uint8_t regs[14];
    uint8_t *musicBuf = nullptr;
    int isrStep = 882;
    int sampleCounter = 0;
    int chips = 1;
    bool ended_ = false;
    char nameBuf[64];

    ~Pt3Backend() override { free(musicBuf); }

    void updateState() {
        func_getregs(regs, PT3_CHIP);
        ayumi_set_tone(&ay, 0, (regs[1] << 8) | regs[0]);
        ayumi_set_tone(&ay, 1, (regs[3] << 8) | regs[2]);
        ayumi_set_tone(&ay, 2, (regs[5] << 8) | regs[4]);
        ayumi_set_noise(&ay, regs[6]);
        ayumi_set_mixer(&ay, 0, regs[7] & 1, (regs[7] >> 3) & 1, regs[8] >> 4);
        ayumi_set_mixer(&ay, 1, (regs[7] >> 1) & 1, (regs[7] >> 4) & 1, regs[9] >> 4);
        ayumi_set_mixer(&ay, 2, (regs[7] >> 2) & 1, (regs[7] >> 5) & 1, regs[10] >> 4);
        ayumi_set_volume(&ay, 0, regs[8] & 0xf);
        ayumi_set_volume(&ay, 1, regs[9] & 0xf);
        ayumi_set_volume(&ay, 2, regs[10] & 0xf);
        ayumi_set_envelope(&ay, (regs[12] << 8) | regs[11]);
        if (regs[13] != 255) ayumi_set_envelope_shape(&ay, regs[13]);
    }

    void render(int16_t *dst, int frames) override {
        for (int i = 0; i < frames; i++) {
            if (sampleCounter >= isrStep) {
                if (func_play_tick(PT3_CHIP)) ended_ = true;
                updateState();
                sampleCounter = 0;
            }
            ayumi_process(&ay, PT3_CHIP);
            if (t.dc_filter_on) ayumi_remove_dc(&ay);
            dst[i * 2] = ctClip16((float)ay.left);
            dst[i * 2 + 1] = ctClip16((float)ay.right);
            sampleCounter++;
        }
        if (ended_) memset(dst, 0, (size_t)frames * 2 * sizeof(int16_t));
    }
    void seekMs(int ms) override {
        func_restart_music(PT3_CHIP);
        int frames = ms * CT_SAMPLE_RATE / 1000;
        sampleCounter = 0;
        int16_t scratch[2048];
        while (frames > 0) {
            int n = frames > 512 ? 512 : frames;
            render(scratch, n);
            frames -= n;
        }
        ended_ = false;
    }
    bool ended() override { return ended_; }
    int voiceCount() override { return chips * 3; }
    const char *voiceName(int v) override {
        snprintf(nameBuf, sizeof(nameBuf), "AY %d%c", v / 3 + 1, 'A' + (v % 3));
        return nameBuf;
    }
};

} // namespace

CtDecoder *ct_create_pt3(const char *path, int subsong, CtTrackInfo *info) {
    (void)subsong;
    FILE *f = fopen(path, "rb");
    if (!f) return nullptr;
    fseek(f, 0, SEEK_END); long size = ftell(f); fseek(f, 0, SEEK_SET);
    if (size <= 0) { fclose(f); return nullptr; }
    uint8_t *buf = (uint8_t *)malloc((size_t)size);
    if (!buf || fread(buf, 1, (size_t)size, f) != (size_t)size) { free(buf); fclose(f); return nullptr; }
    fclose(f);

    int chips = func_setup_music(buf, (int)size, PT3_CHIP, 1);
    if (chips <= 0) { free(buf); return nullptr; }

    Pt3Backend *b = new Pt3Backend();
    b->musicBuf = buf;
    b->chips = chips;

    b->t.sample_rate = CT_SAMPLE_RATE;
    b->t.is_ym = 1;
    b->t.clock_rate = 1750000;
    b->t.frame_rate = 50;
    b->t.eqp_stereo_on = 1;
    b->t.dc_filter_on = 1;
    b->t.note_table = -1;
    b->t.pan[0] = 0.4; b->t.pan[1] = 0.5; b->t.pan[2] = 0.6;
    ayumi_configure(&b->ay, b->t.is_ym, b->t.clock_rate, b->t.sample_rate);
    for (int i = 0; i < 3; i++) ayumi_set_pan(&b->ay, i, b->t.pan[i], b->t.eqp_stereo_on);
    b->isrStep = (int)(b->t.sample_rate / b->t.frame_rate);
    b->updateState();

    if (info) {
        const char *slash = strrchr(path, '/');
        ctCopyStr(info->title, sizeof(info->title), slash ? slash + 1 : path);
        char *dot = strrchr(info->title, '.');
        if (dot) *dot = '\0';
        ctCopyStr(info->format, sizeof(info->format), "PT3");
        ctCopyStr(info->system, sizeof(info->system), "ZX Spectrum AY");
        info->channels = chips * 3;
        info->subsongs = 1;
        info->durationMs = -1;
        info->backend = CT_BACKEND_GME;
    }
    return b;
}

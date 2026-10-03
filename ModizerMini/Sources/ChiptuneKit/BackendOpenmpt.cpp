//
//  BackendOpenmpt.cpp
//  ModizerMini
//

#include "ChiptuneBackend.h"
#include "CtUtil.h"
#include <stdlib.h>
#include <stdio.h>

#include "libopenmpt.h"

namespace {

struct OpenmptBackend : CtDecoder {
    openmpt_module *mod = nullptr;
    int nVoices = 0;
    char nameBuf[64];
    char commentBuf[4096] = {0};

    ~OpenmptBackend() override { if (mod) openmpt_module_destroy(mod); }
    const char *comment() override { return commentBuf; }
    int instrumentCount() override { return mod ? openmpt_module_get_num_instruments(mod) : 0; }
    const char *instrumentName(int index) override {
        if (!mod) return "";
        const char *n = openmpt_module_get_instrument_name(mod, (int32_t)index);
        snprintf(nameBuf, sizeof(nameBuf), "%s", (n && n[0]) ? n : "");
        if (n) openmpt_free_string(n);
        return nameBuf;
    }

    void render(int16_t *dst, int frames) override {
        if (mod) openmpt_module_read_interleaved_stereo(mod, CT_SAMPLE_RATE, (size_t)frames, dst);
    }
    void seekMs(int ms) override {
        if (mod) openmpt_module_set_position_seconds(mod, ms / 1000.0);
    }
    int voiceCount() override { return nVoices; }
    const char *voiceName(int v) override {
        const char *n = openmpt_module_get_channel_name(mod, v);
        snprintf(nameBuf, sizeof(nameBuf), "%s", (n && n[0]) ? n : "");
        return nameBuf;
    }
    void voiceState(int v, int *note, float *level, bool *active) override {
        uint32_t n = openmpt_module_get_current_channel_note(mod, v);
        float vu = openmpt_module_get_current_channel_vu_mono(mod, v);
        if (note) *note = (n > 0 && n < 128) ? (int)n : 0;
        if (level) *level = vu;
        if (active) *active = vu > 0.001f;
    }
};

} // namespace

CtDecoder *ct_create_openmpt(const char *path, int subsong, CtTrackInfo *info) {
    FILE *f = fopen(path, "rb");
    if (!f) return nullptr;
    fseek(f, 0, SEEK_END);
    long size = ftell(f);
    fseek(f, 0, SEEK_SET);
    if (size <= 0) { fclose(f); return nullptr; }
    void *data = malloc((size_t)size);
    if (!data) { fclose(f); return nullptr; }
    bool readOk = fread(data, 1, (size_t)size, f) == (size_t)size;
    fclose(f);
    if (!readOk) { free(data); return nullptr; }

    int err = 0;
    const char *errmsg = nullptr;
    openmpt_module *mod = openmpt_module_create_from_memory2(data, (size_t)size, nullptr, nullptr,
                                                             nullptr, nullptr, &err, &errmsg, nullptr);
    free(data);
    if (!mod) return nullptr;

    openmpt_module_set_render_param(mod, OPENMPT_MODULE_RENDER_STEREOSEPARATION_PERCENT, 100);
    openmpt_module_set_render_param(mod, OPENMPT_MODULE_RENDER_INTERPOLATIONFILTER_LENGTH, 8);

    int subs = openmpt_module_get_num_subsongs(mod);
    if (subs > 0 && subsong >= 0 && subsong < subs) openmpt_module_select_subsong(mod, subsong);

    OpenmptBackend *b = new OpenmptBackend();
    b->mod = mod;
    b->nVoices = openmpt_module_get_num_channels(mod);
    {
        const char *msg = openmpt_module_get_metadata(mod, "message");
        if (msg && msg[0]) ctCopyStr(b->commentBuf, sizeof(b->commentBuf), msg);
        if (msg) openmpt_free_string(msg);
    }

    if (info) {
        const char *title = openmpt_module_get_metadata(mod, "title");
        const char *type = openmpt_module_get_metadata(mod, "type_long");
        const char *artist = openmpt_module_get_metadata(mod, "artist");
        ctCopyStr(info->title, sizeof(info->title), (title && title[0]) ? title : "Untitled module");
        ctCopyStr(info->format, sizeof(info->format), (type && type[0]) ? type : "Module");
        ctCopyStr(info->system, sizeof(info->system), "Tracker");
        if (artist && artist[0]) {
            size_t l = strlen(info->format);
            snprintf(info->format + l, sizeof(info->format) - l, " · %s", artist);
        }
        info->channels = openmpt_module_get_num_channels(mod);
        info->instruments = openmpt_module_get_num_instruments(mod);
        info->samples = openmpt_module_get_num_samples(mod);
        info->subsongs = subs > 0 ? subs : 1;
        info->currentSubsong = openmpt_module_get_selected_subsong(mod);
        info->durationMs = (int)(openmpt_module_get_duration_seconds(mod) * 1000.0);
        info->backend = CT_BACKEND_XMP;
        if (title) openmpt_free_string(title);
        if (type) openmpt_free_string(type);
        if (artist) openmpt_free_string(artist);
    }
    return b;
}

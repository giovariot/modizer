//
//  ChiptuneBackend.h
//  ModizerMini
//
//  One small interface implemented by every decoder, so the engine can treat
//  libxmp, Game_Music_Emu, libopenmpt, vgmstream, … the same way.
//

#ifndef CHIPTUNE_BACKEND_H
#define CHIPTUNE_BACKEND_H

#include "ChiptuneDecoder.h"
#include <stdint.h>

static const int CT_SAMPLE_RATE = 44100;

struct CtDecoder {
    virtual ~CtDecoder() {}

    /// Fills @p frames interleaved stereo signed 16-bit samples.
    virtual void render(int16_t *dst, int frames) = 0;

    virtual void seekMs(int ms) { (void)ms; }
    virtual bool ended() { return false; }
    virtual void setLooping(bool looping) { (void)looping; }

    virtual int voiceCount() { return 0; }
    virtual const char *voiceName(int voice) { (void)voice; return ""; }
    virtual const char *voiceInstrument(int voice) { (void)voice; return ""; }
    virtual const char *voiceSample(int voice) { (void)voice; return ""; }
    virtual const char *comment() { return ""; }
    virtual int instrumentCount() { return 0; }
    virtual const char *instrumentName(int index) { (void)index; return ""; }
    virtual void voiceState(int voice, int *note, float *level, bool *active) {
        (void)voice;
        if (note) *note = 0;
        if (level) *level = 0;
        if (active) *active = false;
    }
};

// Each factory returns nullptr when it does not handle the file.
CtDecoder *ct_create_xmp(const char *path, int subsong, CtTrackInfo *info);
CtDecoder *ct_create_gme(const char *path, int subsong, CtTrackInfo *info);
CtDecoder *ct_create_openmpt(const char *path, int subsong, CtTrackInfo *info);
CtDecoder *ct_create_vgmstream(const char *path, int subsong, CtTrackInfo *info);
CtDecoder *ct_create_vgm(const char *path, int subsong, CtTrackInfo *info);
CtDecoder *ct_create_sid(const char *path, int subsong, CtTrackInfo *info);
CtDecoder *ct_create_asap(const char *path, int subsong, CtTrackInfo *info);
CtDecoder *ct_create_stsound(const char *path, int subsong, CtTrackInfo *info);
CtDecoder *ct_create_sc68(const char *path, int subsong, CtTrackInfo *info);
CtDecoder *ct_create_hvl(const char *path, int subsong, CtTrackInfo *info);
CtDecoder *ct_create_kss(const char *path, int subsong, CtTrackInfo *info);
CtDecoder *ct_create_nez(const char *path, int subsong, CtTrackInfo *info);
CtDecoder *ct_create_pt3(const char *path, int subsong, CtTrackInfo *info);
CtDecoder *ct_create_v2m(const char *path, int subsong, CtTrackInfo *info);
CtDecoder *ct_create_adplug(const char *path, int subsong, CtTrackInfo *info);
CtDecoder *ct_create_atari(const char *path, int subsong, CtTrackInfo *info);
CtDecoder *ct_create_nsfplay(const char *path, int subsong, CtTrackInfo *info);
CtDecoder *ct_create_pixel(const char *path, int subsong, CtTrackInfo *info);
CtDecoder *ct_create_wsr(const char *path, int subsong, CtTrackInfo *info);
CtDecoder *ct_create_fmp(const char *path, int subsong, CtTrackInfo *info);
CtDecoder *ct_create_pmd(const char *path, int subsong, CtTrackInfo *info);
CtDecoder *ct_create_mdx(const char *path, int subsong, CtTrackInfo *info);
CtDecoder *ct_create_eup(const char *path, int subsong, CtTrackInfo *info);
CtDecoder *ct_create_psf(const char *path, int subsong, CtTrackInfo *info);
CtDecoder *ct_create_xsf(const char *path, int subsong, CtTrackInfo *info);
CtDecoder *ct_create_gsf(const char *path, int subsong, CtTrackInfo *info);
CtDecoder *ct_create_furnace(const char *path, int subsong, CtTrackInfo *info);
CtDecoder *ct_create_zxtune(const char *path, int subsong, CtTrackInfo *info);
CtDecoder *ct_create_mac(const char *path, int subsong, CtTrackInfo *info);
CtDecoder *ct_create_timidity(const char *path, int subsong, CtTrackInfo *info);
CtDecoder *ct_create_uade(const char *path, int subsong, CtTrackInfo *info);

#endif /* CHIPTUNE_BACKEND_H */

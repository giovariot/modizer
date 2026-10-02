//
//  BackendPsf.cpp
//  ModizerMini
//
//  PSF family: PSF/PSF2 (PlayStation), SSF/DSF (Saturn/Dreamcast),
//  QSF (Capcom QSound), USF (Nintendo 64), SNSF (SNES).
//

#include "ChiptuneBackend.h"
#include "CtUtil.h"
#include <stdlib.h>
#include <string.h>
#include <stdio.h>
#include <ctype.h>

#include "psflib.h"
#include "psf2fs.h"

#include "emuconfig.h"
#include "psx.h"
#include "iop.h"
#include "r3000.h"
#include "spu.h"

#include "sega.h"
#include "qsound.h"
#include "usf.h"
#include "snsf_drvimpl.h"

// ---------------------------------------------------------------------------
// helpers
// ---------------------------------------------------------------------------

static uint32_t ct_le32(const void *p) {
    const uint8_t *b = (const uint8_t *)p;
    return (uint32_t)b[0] | ((uint32_t)b[1] << 8) | ((uint32_t)b[2] << 16) | ((uint32_t)b[3] << 24);
}
static uint16_t ct_le16(const void *p) {
    const uint8_t *b = (const uint8_t *)p;
    return (uint16_t)(b[0] | (b[1] << 8));
}
static void ct_st_le32(void *p, uint32_t v) {
    uint8_t *b = (uint8_t *)p;
    b[0] = v & 0xff; b[1] = (v >> 8) & 0xff; b[2] = (v >> 16) & 0xff; b[3] = (v >> 24) & 0xff;
}

static void *psf_fopen(const char *path) { return fopen(path, "rb"); }
static size_t psf_fread(void *ptr, size_t size, size_t count, void *handle) { return fread(ptr, size, count, (FILE *)handle); }
static int psf_fseek(void *handle, int64_t offset, int whence) { return fseeko((FILE *)handle, offset, whence); }
static int psf_fclose(void *handle) { return fclose((FILE *)handle); }
static int64_t psf_ftell(void *handle) { return ftello((FILE *)handle); }

static psf_file_callbacks ct_psf_fs = { "/", psf_fopen, psf_fread, psf_fseek, psf_fclose, psf_ftell };

// ---------------------------------------------------------------------------
// PSF1 (PlayStation)
// ---------------------------------------------------------------------------

struct psf1_state { void *emu; bool first; unsigned refresh; };

static int psf1_info_cb(void *ctx, const char *name, const char *value) {
    struct psf1_state *s = (struct psf1_state *)ctx;
    if (!s->refresh && !strcasecmp(name, "_refresh")) s->refresh = (unsigned)atoi(value);
    return 0;
}

static int psf1_loader_cb(void *ctx, const uint8_t *exe, size_t exe_size,
                          const uint8_t *reserved, size_t reserved_size) {
    (void)reserved; (void)reserved_size;
    struct psf1_state *s = (struct psf1_state *)ctx;
    if (exe_size < 0x800) return -1;
    uint32_t addr = ct_le32(exe + 0x18) & 0x1fffff;
    uint32_t size = (uint32_t)exe_size - 0x800;
    if (addr < 0x10000 || size > 0x1f0000 || addr + size > 0x200000) return -1;
    void *iop = psx_get_iop_state(s->emu);
    iop_upload_to_ram(iop, addr, exe + 0x800, size);
    if (!s->refresh) {
        if (!strncasecmp((const char *)exe + 113, "Japan", 5)) s->refresh = 60;
        else if (!strncasecmp((const char *)exe + 113, "Europe", 6)) s->refresh = 50;
        else if (!strncasecmp((const char *)exe + 113, "North America", 13)) s->refresh = 60;
    }
    if (s->first) {
        void *r3000 = iop_get_r3000_state(iop);
        r3000_setreg(r3000, R3000_REG_PC, ct_le32(exe + 0x10));
        r3000_setreg(r3000, R3000_REG_GEN + 29, ct_le32(exe + 0x34));
        s->first = false;
    }
    return 0;
}

// ---------------------------------------------------------------------------
// PSF2
// ---------------------------------------------------------------------------

static int EMU_CALL ct_virtual_readfile(void *ctx, const char *path, int offset, char *buffer, int length) {
    return psf2fs_virtual_readfile(ctx, path, offset, buffer, length);
}

// ---------------------------------------------------------------------------
// SSF / DSF
// ---------------------------------------------------------------------------

struct sdsf_state { uint8_t *data; size_t data_size; };

static int sdsf_loader_cb(void *ctx, const uint8_t *exe, size_t exe_size,
                          const uint8_t *reserved, size_t reserved_size) {
    (void)reserved; (void)reserved_size;
    if (exe_size < 4) return -1;
    struct sdsf_state *s = (struct sdsf_state *)ctx;
    uint8_t *dst = s->data;
    if (s->data_size < 4) {
        s->data = dst = (uint8_t *)malloc(exe_size);
        s->data_size = exe_size;
        memcpy(dst, exe, exe_size);
        return 0;
    }
    uint32_t dst_start = ct_le32(dst) & 0x7fffff;
    uint32_t src_start = ct_le32(exe) & 0x7fffff;
    size_t dst_len = s->data_size - 4;
    size_t src_len = exe_size - 4;
    if (dst_len > 0x800000) dst_len = 0x800000;
    if (src_len > 0x800000) src_len = 0x800000;
    if (src_start < dst_start) {
        uint32_t diff = dst_start - src_start;
        s->data_size = dst_len + 4 + diff;
        s->data = dst = (uint8_t *)realloc(dst, s->data_size);
        memmove(dst + 4 + diff, dst + 4, dst_len);
        memset(dst + 4, 0, diff);
        dst_len += diff;
        dst_start = src_start;
        ct_st_le32(dst, dst_start);
    }
    if (src_start + src_len > dst_start + dst_len) {
        size_t diff = (src_start + src_len) - (dst_start + dst_len);
        s->data_size = dst_len + 4 + diff;
        s->data = dst = (uint8_t *)realloc(dst, s->data_size);
        memset(dst + 4 + dst_len, 0, diff);
    }
    memcpy(dst + 4 + (src_start - dst_start), exe + 4, src_len);
    return 0;
}

// ---------------------------------------------------------------------------
// QSF
// ---------------------------------------------------------------------------

struct qsf_state { uint8_t *key; uint32_t key_size; uint8_t *z80; uint32_t z80_size; uint8_t *smp; uint32_t smp_size; };

static int qsf_upload(struct qsf_state *s, const char *section, uint32_t start, const uint8_t *data, uint32_t size) {
    uint8_t **arr = NULL; uint32_t *arrSize = NULL; uint32_t maxSize = 0x7fffffff;
    if (!strcmp(section, "KEY")) { arr = &s->key; arrSize = &s->key_size; maxSize = 11; }
    else if (!strcmp(section, "Z80")) { arr = &s->z80; arrSize = &s->z80_size; }
    else if (!strcmp(section, "SMP")) { arr = &s->smp; arrSize = &s->smp_size; }
    else return -1;
    uint32_t newSize = start + size;
    if (newSize > maxSize) return -1;
    if (newSize > *arrSize) {
        *arr = (uint8_t *)realloc(*arr, newSize);
        *arrSize = newSize;
        memset(*arr + (newSize - size), 0, size);
    }
    memcpy(*arr + start, data, size);
    return 0;
}

static int qsf_loader_cb(void *ctx, const uint8_t *exe, size_t exe_size,
                         const uint8_t *reserved, size_t reserved_size) {
    (void)reserved; (void)reserved_size;
    struct qsf_state *s = (struct qsf_state *)ctx;
    for (;;) {
        char sec[4];
        if (exe_size < 11) break;
        memcpy(sec, exe, 3); sec[3] = 0; exe += 3; exe_size -= 3;
        uint32_t ofs = ct_le32(exe); exe += 4; exe_size -= 4;
        uint32_t size = ct_le32(exe); exe += 4; exe_size -= 4;
        if (size > exe_size) return -1;
        if (qsf_upload(s, sec, ofs, exe, size) < 0) return -1;
        exe += size; exe_size -= size;
    }
    return 0;
}

// ---------------------------------------------------------------------------
// SNSF
// ---------------------------------------------------------------------------

struct snsf_state { int base_set; uint32_t base; uint8_t *data; size_t data_size; uint8_t *sram; size_t sram_size; };

static int snsf_loader_cb(void *ctx, const uint8_t *exe, size_t exe_size,
                          const uint8_t *reserved, size_t reserved_size) {
    if (exe_size < 8) return -1;
    struct snsf_state *s = (struct snsf_state *)ctx;
    unsigned xofs = ct_le32(exe);
    unsigned xsize = ct_le32(exe + 4);
    if (xsize > exe_size - 8) return -1;
    if (!s->base_set) { s->base = xofs; s->base_set = 1; } else xofs += s->base;
    if (!s->data) {
        unsigned rsize = xofs + xsize;
        rsize--;
        rsize |= rsize >> 1; rsize |= rsize >> 2; rsize |= rsize >> 4;
        rsize |= rsize >> 8; rsize |= rsize >> 16; rsize++;
        s->data = (uint8_t *)calloc(rsize + 10, 1);
        s->data_size = rsize;
    }
    memcpy(s->data + xofs, exe + 8, xsize);
    if (reserved_size >= 8) {
        s->sram_size = reserved_size;
        s->sram = (uint8_t *)malloc(reserved_size);
        if (s->sram) memcpy(s->sram, reserved, reserved_size);
    }
    return 0;
}

// ---------------------------------------------------------------------------
// USF
// ---------------------------------------------------------------------------

static unsigned long usf_length_ms = 0;
static unsigned long usf_fade_ms = 0;
static unsigned int usf_enable_compare = 0;
static unsigned int usf_enable_fifo_full = 0;
static uint8_t *usf_state_ptr = nullptr;

static int usf_loader_cb(void *ctx, const uint8_t *exe, size_t exe_size,
                         const uint8_t *reserved, size_t reserved_size) {
    (void)ctx;
    if (exe && exe_size > 0) return -1;
    return usf_upload_section(usf_state_ptr, reserved, reserved_size);
}

static unsigned long parseTimeMs(const char *v) {
    if (!v) return 0;
    double total = 0; const char *p = v;
    char *end = nullptr;
    while (*p) {
        double num = strtod(p, &end);
        if (end == p) break;
        if (*end == ':') { total = total * 60 + num; p = end + 1; }
        else { total += num; p = end; }
        while (*p == ':' || *p == ' ') p++;
    }
    return (unsigned long)(total * 1000.0);
}

static int usf_info_cb(void *ctx, const char *name, const char *value) {
    (void)ctx;
    if (!strcasecmp(name, "length")) usf_length_ms += parseTimeMs(value);
    else if (!strcasecmp(name, "fade")) usf_fade_ms += parseTimeMs(value);
    else if (!strcasecmp(name, "_enablecompare")) usf_enable_compare = 1;
    else if (!strcasecmp(name, "_enablefifofull")) usf_enable_fifo_full = 1;
    return 0;
}

static int generic_info_cb(void *ctx, const char *name, const char *value) {
    (void)ctx;
    if (!strcasecmp(name, "length")) usf_length_ms = parseTimeMs(value);
    else if (!strcasecmp(name, "fade")) usf_fade_ms = parseTimeMs(value);
    return 0;
}

// ---------------------------------------------------------------------------
// backend
// ---------------------------------------------------------------------------

namespace {

enum PsfKind { PSF_PSX1, PSF_PSX2, PSF_SEGA, PSF_QSOUND, PSF_USF, PSF_SNSF };

struct PsfBackend : CtDecoder {
    PsfKind kind;
    void *core = nullptr;
    void *extra = nullptr;          // psf2fs / sdsf_state / qsf_state / snsf_state
    struct psf1_state p1;
    int hcRate = CT_SAMPLE_RATE;
    bool ended_ = false;
    char nameBuf[64];

    ~PsfBackend() override {
        if (kind == PSF_SNSF) snsf_term();
        if (kind == PSF_PSX1 || kind == PSF_PSX2) free(core);
        if (kind == PSF_SEGA) free(core);
        if (kind == PSF_QSOUND) free(core);
        if (kind == PSF_USF) free(core);
        if (extra) {
            if (kind == PSF_SEGA) { struct sdsf_state *s = (struct sdsf_state *)extra; free(s->data); free(s); }
            else if (kind == PSF_QSOUND) { struct qsf_state *s = (struct qsf_state *)extra; free(s->key); free(s->z80); free(s->smp); free(s); }
            else if (kind == PSF_SNSF) { struct snsf_state *s = (struct snsf_state *)extra; free(s->data); free(s->sram); free(s); }
            else if (kind == PSF_PSX2) { psf2fs_delete(extra); }
        }
    }

    void render(int16_t *dst, int frames) override {
        uint32_t howmany = (uint32_t)frames;
        switch (kind) {
            case PSF_PSX1:
            case PSF_PSX2:
                psx_execute(core, 0x7fffffff, dst, &howmany, 0);
                break;
            case PSF_SEGA:
                sega_execute(core, 0x7fffffff, dst, &howmany);
                break;
            case PSF_QSOUND:
                qsound_execute(core, 0x7fffffff, dst, &howmany);
                break;
            case PSF_USF: {
                int32_t rate = hcRate;
                usf_render(core, dst, (size_t)frames, &rate);
                break;
            }
            case PSF_SNSF:
                snsf_gen(dst, (unsigned)frames);
                break;
        }
    }
    void seekMs(int ms) override {
        if (kind == PSF_USF) usf_restart(core);
        int frames = ms * hcRate / 1000;
        int16_t scratch[2048];
        while (frames > 0) {
            int n = frames > 1024 ? 1024 : frames;
            render(scratch, n);
            frames -= n;
        }
        ended_ = false;
    }
    bool ended() override { return ended_; }
    int voiceCount() override {
        switch (kind) {
            case PSF_PSX1: return 24;
            case PSF_PSX2: return 48;
            case PSF_SEGA: return 64;
            case PSF_QSOUND: return 19;
            case PSF_USF: return 32;
            case PSF_SNSF: return 8;
        }
        return 0;
    }
    const char *voiceName(int v) override {
        const char *pfx = "Voice";
        switch (kind) {
            case PSF_PSX1: case PSF_PSX2: pfx = "SPU"; break;
            case PSF_SEGA: pfx = "SCSP"; break;
            case PSF_QSOUND: pfx = "PCM"; break;
            case PSF_USF: pfx = "N64"; break;
            case PSF_SNSF: pfx = "SPC700"; break;
        }
        snprintf(nameBuf, sizeof(nameBuf), "%s %d", pfx, v + 1);
        return nameBuf;
    }
};

} // namespace

CtDecoder *ct_create_psf(const char *path, int subsong, CtTrackInfo *info) {
    const char *dot = strrchr(path, '.');
    if (!dot) return nullptr;
    const char *ext = dot + 1;
    usf_length_ms = 0; usf_fade_ms = 0; usf_enable_compare = 0; usf_enable_fifo_full = 0;

    PsfBackend *b = new PsfBackend();
    int ver = 0;

    if (!strcasecmp(ext, "psf") || !strcasecmp(ext, "minipsf")) {
        b->kind = PSF_PSX1;
        b->hcRate = 44100;
        b->core = calloc(1, psx_get_state_size(1));
        psx_clear_state(b->core, 1);
        b->p1.emu = b->core; b->p1.first = true; b->p1.refresh = 0;
        ver = psf_load(path, &ct_psf_fs, 1, psf1_loader_cb, &b->p1, psf1_info_cb, &b->p1, 1);
        if (ver < 0) { delete b; return nullptr; }
        psx_set_refresh(b->core, b->p1.refresh ? b->p1.refresh : 60);
        void *iop = psx_get_iop_state(b->core);
        iop_set_compat(iop, IOP_COMPAT_HARSH);
        void *spu = iop_get_spu_state(iop);
        spu_enable_main(spu, 1);
        spu_enable_reverb(spu, 0);
    } else if (!strcasecmp(ext, "psf2") || !strcasecmp(ext, "minipsf2")) {
        b->kind = PSF_PSX2;
        b->hcRate = 48000;
        b->extra = psf2fs_create();
        b->core = calloc(1, psx_get_state_size(2));
        psx_clear_state(b->core, 2);
        b->p1.emu = b->core; b->p1.first = true; b->p1.refresh = 0;
        ver = psf_load(path, &ct_psf_fs, 2, psf2fs_load_callback, b->extra, psf1_info_cb, &b->p1, 1);
        if (ver < 0) { delete b; return nullptr; }
        psx_set_readfile(b->core, ct_virtual_readfile, b->extra);
        void *iop = psx_get_iop_state(b->core);
        iop_set_compat(iop, IOP_COMPAT_HARSH);
        void *spu = iop_get_spu_state(iop);
        spu_enable_main(spu, 1);
        spu_enable_reverb(spu, 0);
    } else if (!strcasecmp(ext, "ssf") || !strcasecmp(ext, "minissf") ||
               !strcasecmp(ext, "dsf") || !strcasecmp(ext, "minidsf")) {
        b->kind = PSF_SEGA;
        b->hcRate = 44100;
        int segaType = (tolower(ext[0]) == 'd') ? 0x12 : 0x11;
        struct sdsf_state *s = (struct sdsf_state *)calloc(1, sizeof(struct sdsf_state));
        b->extra = s;
        ver = psf_load(path, &ct_psf_fs, (uint8_t)segaType, sdsf_loader_cb, s, 0, 0, 0);
        if (ver < 0 || !s->data) { delete b; return nullptr; }
        b->core = calloc(1, sega_get_state_size(segaType - 0x10));
        sega_clear_state(b->core, segaType - 0x10);
        sega_enable_dry(b->core, 1);
        sega_enable_dsp(b->core, 1);
        sega_enable_dsp_dynarec(b->core, 0);
        sega_upload_program(b->core, s->data + 4, (uint32_t)(s->data_size - 4));
    } else if (!strcasecmp(ext, "qsf") || !strcasecmp(ext, "miniqsf")) {
        b->kind = PSF_QSOUND;
        b->hcRate = 24038;
        struct qsf_state *s = (struct qsf_state *)calloc(1, sizeof(struct qsf_state));
        b->extra = s;
        ver = psf_load(path, &ct_psf_fs, 0x41, qsf_loader_cb, s, 0, 0, 0);
        if (ver < 0) { delete b; return nullptr; }
        b->core = calloc(1, qsound_get_state_size());
        qsound_clear_state(b->core);
        if (s->key_size == 11) {
            qsound_set_kabuki_key(b->core, ct_le32(s->key), ct_le16(s->key + 2), ct_le16(s->key + 4), ct_le16(s->key + 6));
        }
        qsound_set_z80_rom(b->core, s->z80, s->z80_size);
        qsound_set_sample_rom(b->core, s->smp, s->smp_size);
    } else if (!strcasecmp(ext, "usf") || !strcasecmp(ext, "miniusf")) {
        b->kind = PSF_USF;
        void *state = malloc(usf_get_state_size());
        usf_clear(state);
        usf_state_ptr = (uint8_t *)state;
        b->core = state;
        ver = psf_load(path, &ct_psf_fs, 0x21, usf_loader_cb, state, usf_info_cb, state, 1);
        if (ver < 0) { delete b; return nullptr; }
        usf_set_compare(state, (int)usf_enable_compare);
        usf_set_fifo_full(state, (int)usf_enable_fifo_full);
        usf_set_hle_audio(state, 1);
        int32_t rate = CT_SAMPLE_RATE;
        usf_render(state, nullptr, 0, &rate);
        b->hcRate = rate > 0 ? rate : CT_SAMPLE_RATE;
    } else if (!strcasecmp(ext, "snsf") || !strcasecmp(ext, "minisnsf") ||
               !strcasecmp(ext, "spu") || !strcasecmp(ext, "minispu")) {
        b->kind = PSF_SNSF;
        b->hcRate = CT_SAMPLE_RATE;
        struct snsf_state *s = (struct snsf_state *)calloc(1, sizeof(struct snsf_state));
        b->extra = s;
        ver = psf_load(path, &ct_psf_fs, 0x23, snsf_loader_cb, s, 0, 0, 0);
        if (ver < 0 || !s->data) { delete b; return nullptr; }
        snsf_start(s->data, (int32_t)s->data_size, s->sram, (int32_t)s->sram_size);
    } else {
        delete b;
        return nullptr;
    }

    if (info) {
        const char *slash = strrchr(path, '/');
        ctCopyStr(info->title, sizeof(info->title), slash ? slash + 1 : path);
        char *d = strrchr(info->title, '.');
        if (d) *d = '\0';
        ctCopyStr(info->format, sizeof(info->format), "PSF");
        const char *sys = "PlayStation";
        switch (b->kind) {
            case PSF_PSX2: sys = "PlayStation 2"; ctCopyStr(info->format, sizeof(info->format), "PSF2"); break;
            case PSF_SEGA: sys = (tolower(ext[0]) == 'd') ? "Dreamcast" : "Sega Saturn"; ctCopyStr(info->format, sizeof(info->format), "SSF/DSF"); break;
            case PSF_QSOUND: sys = "Capcom QSound"; ctCopyStr(info->format, sizeof(info->format), "QSF"); break;
            case PSF_USF: sys = "Nintendo 64"; ctCopyStr(info->format, sizeof(info->format), "USF"); break;
            case PSF_SNSF: sys = "Super Nintendo"; ctCopyStr(info->format, sizeof(info->format), "SNSF"); break;
            default: break;
        }
        ctCopyStr(info->system, sizeof(info->system), sys);
        info->channels = b->voiceCount();
        info->durationMs = usf_length_ms > 0 ? (int)usf_length_ms : -1;
        info->backend = CT_BACKEND_GME;
    }
    return b;
}

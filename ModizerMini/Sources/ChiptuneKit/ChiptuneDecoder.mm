//
//  ChiptuneDecoder.mm
//  ModizerMini
//
//  Engine: owns the audio-thread state, the per-voice buffers shared with the
//  decoders, and dispatches to whichever backend understands the file.
//

#include "ChiptuneDecoder.h"
#include "ChiptuneBackend.h"
#include "ModizerVoicesData.h"

#include <stdint.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>
#include <stdio.h>
#include <pthread.h>

#define CT_MAX_FRAMES 8192
#define CT_WAVE_CAP   2048
#define CT_VOICE_RING (SOUND_BUFFER_SIZE_SAMPLE * 4 * 4)

// ---------------------------------------------------------------------------
// Per-voice buffers shared with the patched decoders.
// ---------------------------------------------------------------------------

short int pmBuffer[PM_BUFFER_SIZE * 2];
int pmBufferPosWrite, pmBufferPosRead;
signed char *m_voice_buff[SOUND_MAXVOICES_BUFFER_FX];
signed int *m_voice_buff_accumul_temp[SOUND_MAXVOICES_BUFFER_FX];
unsigned char *m_voice_buff_accumul_temp_cnt[SOUND_MAXVOICES_BUFFER_FX];
int m_voice_buff_adjustement;
int m_voice_fadeout_factor;
int64_t m_voice_current_ptr[SOUND_MAXVOICES_BUFFER_FX];
int64_t m_voice_prev_current_ptr[SOUND_MAXVOICES_BUFFER_FX];
int m_voice_ChipID[SOUND_MAXVOICES_BUFFER_FX];
int m_voice_systemColor[SOUND_VOICES_MAX_ACTIVE_CHIPS];
int m_voice_voiceColor[SOUND_MAXVOICES_BUFFER_FX];
unsigned int vgm_last_vol[SOUND_MAXVOICES_BUFFER_FX];
unsigned int vgm_last_note[SOUND_MAXVOICES_BUFFER_FX];
unsigned char vgm_last_instr[SOUND_MAXVOICES_BUFFER_FX];
unsigned int vgm_last_sample_address[SOUND_MAXVOICES_BUFFER_FX];
unsigned int vgm_last_sample_address_inst[256];
unsigned char vgm_last_sample_address_lastupdate[SOUND_MAXVOICES_BUFFER_FX];
unsigned char m_voice_channel_mapping[256];
unsigned char m_channel_voice_mapping[256];
int m_genNumVoicesChannels, m_genNumMidiVoicesChannels;
signed char m_voice_current_system, m_voice_current_systemSub;
int m_voice_current_samplerate;
double m_voice_current_rateratio;
char m_voice_current_systemPairedOfs;
char m_voice_current_total;
int m_voicesForceOfs;
char m_voicesStatus[SOUND_MAXMOD_CHANNELS];
char vgmVRC7, vgm2610b;
int HC_voicesMuteMask1, HC_voicesMuteMask2;
int64_t generic_mute_mask;
int64_t mdz_ratio_fp_cnt, mdz_ratio_fp_inc, mdz_ratio_fp_inv_inc;
double mdz_pbratio;

// ---------------------------------------------------------------------------

struct CtEngine {
    double sampleRate;
    CtDecoder *backend;
    bool loaded;
    bool paused;
    bool looping;
    bool ended;
    int subsong;
    float volume;
    int voiceMask;
    double positionSeconds;

    short *scratch;
    float *wave;
    int waveWrite;

    pthread_mutex_t lock;
};

namespace {

const char *extensionOf(const char *path) {
    if (path == NULL) return "";
    const char *dot = strrchr(path, '.');
    const char *slash = strrchr(path, '/');
    if (dot != NULL && (slash == NULL || dot > slash)) return dot + 1;
    return "";
}

bool inList(const char *ext, const char *list) {
    if (ext == NULL || ext[0] == '\0') return false;
    size_t n = strlen(ext);
    const char *p = list;
    while (p && *p) {
        const char *end = strchr(p, ',');
        size_t len = end ? (size_t)(end - p) : strlen(p);
        if (len == n && strncasecmp(p, ext, n) == 0) return true;
        if (!end) break;
        p = end + 1;
    }
    return false;
}

} // namespace

// Extension routing. Earlier entries take precedence.
static CtDecoder *createForExtension(const char *path, int subsong, CtTrackInfo *info) {
    const char *e = extensionOf(path);

    // Tracker modules: libxmp first (it fills the per-voice waveforms).
    if (inList(e, "mod,xm,it,s3m,669,mtm,stm,ult,umx,mdl,med,okt,ptm,rtm,sfx,stx,sym,"
                  "far,gdm,imf,ims,dbm,digi,emod,flt,fnk,mgt,mmd1,mmd3,psm,gtk,pru,coco,liq"))
        return ct_create_xmp(path, subsong, info);
    // Formats only libopenmpt understands.
    if (inList(e, "mptm,mo3,itp,itgz,mt2,pt36,puma,ntk,fst,amf,amf0,mus,arch,xpk,wow"))
        return ct_create_openmpt(path, subsong, info);

    if (inList(e, "sap,cmc,cm3,cmr,cms,dmc,dlt,mpt,mpd,rmt,tmc,tm8,tm2"))
        return ct_create_asap(path, subsong, info);
    if (inList(e, "ym")) return ct_create_stsound(path, subsong, info);
    if (inList(e, "ahx,hvl")) return ct_create_hvl(path, subsong, info);
    if (inList(e, "sndh")) return ct_create_atari(path, subsong, info);
    if (inList(e, "sc68")) return ct_create_sc68(path, subsong, info);
    if (inList(e, "sid,mus,c64,prg,p00,rsid,psid,str")) return ct_create_sid(path, subsong, info);
    if (inList(e, "kss,mgs,bgm,opx,mpk,mbm")) return ct_create_kss(path, subsong, info);
    if (inList(e, "hes,sgc")) return ct_create_nez(path, subsong, info);
    if (inList(e, "nsf,nsfe")) return ct_create_nsfplay(path, subsong, info);
    if (inList(e, "pt3")) return ct_create_pt3(path, subsong, info);
    if (inList(e, "v2m,v2mz")) return ct_create_v2m(path, subsong, info);
    if (inList(e, "mid,midi")) return ct_create_timidity(path, subsong, info);
    if (inList(e, "mdx")) return ct_create_mdx(path, subsong, info);
    if (inList(e, "m,m2,mz")) return ct_create_pmd(path, subsong, info);
    if (inList(e, "opi,ovi,ozi")) return ct_create_fmp(path, subsong, info);
    if (inList(e, "eup")) return ct_create_eup(path, subsong, info);
    if (inList(e, "org,ptcop,pttune")) return ct_create_pixel(path, subsong, info);
    if (inList(e, "wsr")) return ct_create_wsr(path, subsong, info);
    if (inList(e, "fur,dmf,ftm,0cc")) return ct_create_furnace(path, subsong, info);
    if (inList(e, "ape,mac")) return ct_create_mac(path, subsong, info);
    if (inList(e, "gsf,minigsf")) return ct_create_gsf(path, subsong, info);
    if (inList(e, "2sf,mini2sf,ncsf,minincsf,psf,minipsf,psf2,minipsf2,dsf,minidsf,qsf,miniqsf,ssf,minissf,usf,miniusf,snsf,minisnsf,spu,minispu"))
        return ct_create_psf(path, subsong, info);
    if (inList(e, "gsf,minigsf,gbs")) { /* gbs handled below */ }
    if (inList(e, "vgm,vgz,gym,dro,s98,sndh")) return ct_create_vgm(path, subsong, info);
    if (inList(e, "opus,adx,brstm,hca,fsb,bik,str,wem,xma,at3,at9,ea,spc,ps,ss2,mib,mp3,ogg,wav,aif,aiff,flac,cfg,bn9,ctp,ddc,ogg2,logg,nwa,ogg3,smpl,vag,vaw,vs,vgmstream"))
        return ct_create_vgmstream(path, subsong, info);
    if (inList(e, "a2m,adl,adlib,agd,amd,as3m,bam,bmf,cmf,d00,dfm,dm0,dmo,dr0,dtm,got,ha2,hsc,hsp,hsq,imf,ims,jbm,ksm,laa,lds,mad,mdi,mkj,msc,mtk,rac,rad,raw,rix,rol,sa2,sat,sci,sdb,sng,sop,sqx,xad,xms,xsm,wlf"))
        return ct_create_adplug(path, subsong, info);
    return nullptr;
}

static CtDecoder *createByProbing(const char *path, int subsong, CtTrackInfo *info) {
    CtDecoder *b = ct_create_openmpt(path, subsong, info);
    if (b) return b;
    b = ct_create_xmp(path, subsong, info);
    if (b) return b;
    b = ct_create_gme(path, subsong, info);
    if (b) return b;
    b = ct_create_vgmstream(path, subsong, info);
    if (b) return b;
    b = ct_create_uade(path, subsong, info);
    return b;
}

// ---------------------------------------------------------------------------
// lifecycle
// ---------------------------------------------------------------------------

// The voice ring buffers are shared by the patched decoders. Allocate them
// once per process: several engines can exist at the same time (e.g. when the
// Quick Look extension switches file) and must not free each other's buffers.
static bool g_voiceBuffersReady = false;
static void ensureVoiceBuffers(void) {
    if (g_voiceBuffersReady) return;
    for (int i = 0; i < SOUND_MAXVOICES_BUFFER_FX; i++)
        m_voice_buff[i] = (signed char *)calloc(CT_VOICE_RING, 1);
    g_voiceBuffersReady = true;
}

CtEngine *ct_engine_create(double sampleRate) {
    if (sampleRate <= 0) sampleRate = CT_SAMPLE_RATE;
    CtEngine *e = (CtEngine *)calloc(1, sizeof(CtEngine));
    if (!e) return NULL;
    e->sampleRate = sampleRate;
    e->volume = 1.0f;
    e->scratch = (short *)calloc((size_t)CT_MAX_FRAMES * 2, sizeof(short));
    e->wave = (float *)calloc(CT_WAVE_CAP, sizeof(float));
    ensureVoiceBuffers();
    pthread_mutex_init(&e->lock, NULL);
    return e;
}

void ct_engine_unload(CtEngine *engine) {
    CtEngine *e = (CtEngine *)engine;
    if (!e) return;
    pthread_mutex_lock(&e->lock);
    delete e->backend;
    e->backend = nullptr;
    e->loaded = false;
    e->ended = false;
    for (int i = 0; i < SOUND_MAXVOICES_BUFFER_FX; i++) {
        if (m_voice_buff[i]) memset(m_voice_buff[i], 0, CT_VOICE_RING);
        m_voice_current_ptr[i] = 0;
        m_voice_prev_current_ptr[i] = 0;
        vgm_last_note[i] = 0;
        vgm_last_vol[i] = 0;
        vgm_last_instr[i] = 0;
    }
    pthread_mutex_unlock(&e->lock);
}

void ct_engine_destroy(CtEngine *engine) {
    CtEngine *e = (CtEngine *)engine;
    if (!e) return;
    ct_engine_unload(e);
    pthread_mutex_destroy(&e->lock);
    free(e->scratch);
    free(e->wave);
    free(e);
}

bool ct_engine_is_loaded(const CtEngine *engine) {
    const CtEngine *e = (const CtEngine *)engine;
    return e && e->loaded;
}

bool ct_engine_load(CtEngine *engine, const char *path, int subsong, CtTrackInfo *outInfo) {
    CtEngine *e = (CtEngine *)engine;
    if (!e || !path) return false;
    ct_engine_unload(e);

    CtTrackInfo local;
    memset(&local, 0, sizeof(local));
    CtTrackInfo *info = outInfo ? outInfo : &local;
    memset(info, 0, sizeof(CtTrackInfo));

    pthread_mutex_lock(&e->lock);
    CtDecoder *b = createForExtension(path, subsong, info);
    if (b == nullptr) b = createByProbing(path, subsong, info);
    if (b != nullptr) {
        e->backend = b;
        e->loaded = true;
        e->paused = false;
        e->looping = false;
        e->ended = false;
        e->subsong = subsong;
        e->positionSeconds = 0;
        e->waveWrite = 0;
        memset(e->wave, 0, CT_WAVE_CAP * sizeof(float));
        b->setLooping(false);
    }
    pthread_mutex_unlock(&e->lock);
    return b != nullptr;
}

// ---------------------------------------------------------------------------
// transport
// ---------------------------------------------------------------------------

void ct_engine_pause(CtEngine *engine, bool paused) {
    CtEngine *e = (CtEngine *)engine;
    if (!e) return;
    pthread_mutex_lock(&e->lock);
    e->paused = paused;
    pthread_mutex_unlock(&e->lock);
}

bool ct_engine_is_paused(const CtEngine *engine) {
    const CtEngine *e = (const CtEngine *)engine;
    return e ? e->paused : true;
}

void ct_engine_seek(CtEngine *engine, double seconds) {
    CtEngine *e = (CtEngine *)engine;
    if (!e || seconds < 0) return;
    pthread_mutex_lock(&e->lock);
    if (e->backend) {
        e->backend->seekMs((int)(seconds * 1000.0));
        e->ended = false;
        e->positionSeconds = seconds;
    }
    pthread_mutex_unlock(&e->lock);
}

void ct_engine_select_subsong(CtEngine *engine, int index) {
    CtEngine *e = (CtEngine *)engine;
    if (!e || index < 0) return;
    pthread_mutex_lock(&e->lock);
    // Sub-song switching is handled by reloading from the caller side; backends
    // that support it in place override selectSubsong through voiceState.
    e->subsong = index;
    pthread_mutex_unlock(&e->lock);
}

void ct_engine_set_looping(CtEngine *engine, bool looping) {
    CtEngine *e = (CtEngine *)engine;
    if (!e) return;
    pthread_mutex_lock(&e->lock);
    e->looping = looping;
    if (e->backend) e->backend->setLooping(looping);
    if (!looping) e->ended = false;
    pthread_mutex_unlock(&e->lock);
}

double ct_engine_current_time(const CtEngine *engine) {
    const CtEngine *e = (const CtEngine *)engine;
    if (!e) return 0;
    pthread_mutex_lock((pthread_mutex_t *)&e->lock);
    double t = e->positionSeconds;
    pthread_mutex_unlock((pthread_mutex_t *)&e->lock);
    return t;
}

bool ct_engine_end_reached(const CtEngine *engine) {
    const CtEngine *e = (const CtEngine *)engine;
    if (!e) return true;
    pthread_mutex_lock((pthread_mutex_t *)&e->lock);
    bool ended = e->ended || (e->backend && e->backend->ended());
    pthread_mutex_unlock((pthread_mutex_t *)&e->lock);
    return ended;
}

void ct_engine_set_volume(CtEngine *engine, float volume) {
    CtEngine *e = (CtEngine *)engine;
    if (!e) return;
    if (volume < 0) volume = 0;
    if (volume > 1) volume = 1;
    e->volume = volume;
}

float ct_engine_volume(const CtEngine *engine) {
    const CtEngine *e = (const CtEngine *)engine;
    return e ? e->volume : 0;
}

// ---------------------------------------------------------------------------
// rendering
// ---------------------------------------------------------------------------

void ct_render_planar(CtEngine *engine, float *left, float *right, int frames) {
    CtEngine *e = (CtEngine *)engine;
    if (!e || frames <= 0) return;
    if (frames > CT_MAX_FRAMES) frames = CT_MAX_FRAMES;
    memset(left, 0, (size_t)frames * sizeof(float));
    memset(right, 0, (size_t)frames * sizeof(float));

    pthread_mutex_lock(&e->lock);
    if (!e->loaded || e->paused || e->backend == nullptr || (e->ended && !e->looping)) {
        pthread_mutex_unlock(&e->lock);
        return;
    }

    short *dst = e->scratch;
    memset(dst, 0, (size_t)frames * 2 * sizeof(short));
    e->backend->render(dst, frames);
    if (e->backend->ended()) {
        e->ended = true;
        if (e->looping) {
            e->backend->seekMs(0);
            e->ended = false;
            memset(dst, 0, (size_t)frames * 2 * sizeof(short));
            e->backend->render(dst, frames);
        }
    }

    float v = e->volume;
    for (int i = 0; i < frames; i++) {
        float l = dst[i * 2] / 32768.0f * v;
        float r = dst[i * 2 + 1] / 32768.0f * v;
        left[i] = l;
        right[i] = r;
        e->wave[e->waveWrite] = (l + r) * 0.5f;
        e->waveWrite = (e->waveWrite + 1) % CT_WAVE_CAP;
    }
    e->positionSeconds += (double)frames / e->sampleRate;
    pthread_mutex_unlock(&e->lock);
}

int ct_engine_copy_waveform(const CtEngine *engine, float *out, int frames) {
    const CtEngine *e = (const CtEngine *)engine;
    if (!e || !out || frames <= 0) return 0;
    if (frames > CT_WAVE_CAP) frames = CT_WAVE_CAP;
    for (int i = 0; i < frames; i++) {
        int idx = (e->waveWrite - frames + i + CT_WAVE_CAP * 2) % CT_WAVE_CAP;
        out[i] = e->wave[idx];
    }
    return frames;
}

// ---------------------------------------------------------------------------
// per-voice info
// ---------------------------------------------------------------------------

int ct_voice_count(const CtEngine *engine) {
    const CtEngine *e = (const CtEngine *)engine;
    if (!e || !e->backend) return 0;
    return e->backend->voiceCount();
}

const char *ct_voice_name(const CtEngine *engine, int voice) {
    const CtEngine *e = (const CtEngine *)engine;
    if (!e || !e->backend) return "";
    return e->backend->voiceName(voice);
}

const char *ct_voice_instrument(const CtEngine *engine, int voice) {
    const CtEngine *e = (const CtEngine *)engine;
    if (!e || !e->backend) return "";
    return e->backend->voiceInstrument(voice);
}

void ct_voice_state(const CtEngine *engine, int voice, int *note, int *instrument,
                    float *level, bool *active) {
    const CtEngine *e = (const CtEngine *)engine;
    if (instrument) *instrument = 0;
    if (!e || !e->backend) {
        if (note) *note = 0;
        if (level) *level = 0;
        if (active) *active = false;
        return;
    }
    pthread_mutex_lock((pthread_mutex_t *)&e->lock);
    e->backend->voiceState(voice, note, level, active);
    pthread_mutex_unlock((pthread_mutex_t *)&e->lock);
}

const char *ct_voice_sample(const CtEngine *engine, int voice) {
    const CtEngine *e = (const CtEngine *)engine;
    if (!e || !e->backend) return "";
    return e->backend->voiceSample(voice);
}

const char *ct_module_comment(const CtEngine *engine) {
    const CtEngine *e = (const CtEngine *)engine;
    if (!e || !e->backend) return "";
    return e->backend->comment();
}

int ct_voice_waveform(const CtEngine *engine, int voice, float *out, int frames) {
    const CtEngine *e = (const CtEngine *)engine;
    if (!e || !out || frames <= 0 || voice < 0 || voice >= SOUND_MAXVOICES_BUFFER_FX) return 0;
    if (frames > CT_VOICE_RING) frames = CT_VOICE_RING;
    signed char *buf = m_voice_buff[voice];
    if (buf == NULL) return 0;
    uint64_t mask = (uint64_t)(e->voiceMask ? e->voiceMask : (SOUND_BUFFER_SIZE_SAMPLE * 4 * 2 - 1));
    int64_t idx = m_voice_current_ptr[voice] >> MODIZER_OSCILLO_OFFSET_FIXEDPOINT;
    for (int i = 0; i < frames; i++) {
        uint64_t p = (uint64_t)(idx - frames + i) & mask;
        out[i] = buf[p] / 128.0f;
    }
    return frames;
}

char g_ct_resource_path[1024] = {0};

void ct_engine_set_resource_path(const char *path) {
    if (!path) { g_ct_resource_path[0] = 0; return; }
    snprintf(g_ct_resource_path, sizeof(g_ct_resource_path), "%s", path);
}

// ---------------------------------------------------------------------------
// formats
// ---------------------------------------------------------------------------

const char *ct_supported_extensions(void) {
    static const char *list =
        "mod,xm,it,s3m,669,mtm,stm,ult,umx,mdl,med,okt,ptm,rtm,sfx,stx,sym,far,"
        "gdm,imf,ims,dbm,digi,emod,flt,fnk,mgt,mmd1,mmd3,okt,psm,pt3,gtk,"
        "mptm,mo3,itp,itgz,umx,mt2,pt36,puma,ntk,fst,amf,pru,coco,liq,chip,"
        "nsf,nsfe,spc,gbs,vgm,vgz,gym,hes,kss,sap,ay,sid,mus,c64,prg,p00,rsid,psid,"
        "ym,sc68,ahx,hvl,sndh,2sf,mini2sf,ncsf,minincsf,psf,minipsf,psf2,minipsf2,"
        "dsf,minidsf,qsf,miniqsf,ssf,minissf,usf,miniusf,snsf,minisnsf,spu,minispu,"
        "gsf,minigsf,wsr,org,ptcop,pttune,fur,dmf,ftm,0cc,v2m,v2mz,mid,midi,"
        "mdx,m,m2,mz,opi,ovi,ozi,eup,ape,mac,"
        "adx,brstm,hca,fsb,bik,str,wem,xma,at3,at9,mp3,ogg,wav,aif,aiff,flac,"
        "ss2,mib,nwa,vag,vaw,vgmstream,ass,smpl,ctp,ddc,logg,g719,ps,aa3,aax,"
        "sap,cmc,cm3,cmr,cms,dmc,dlt,mpt,mpd,rmt,tmc,tm8,tm2";
    return list;
}

bool ct_can_play(const char *path) {
    const char *e = extensionOf(path);
    if (e[0] == '\0') return false;
    return inList(e, ct_supported_extensions()) || true; // engines can also probe content
}

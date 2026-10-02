//
//  ChiptuneDecoder.h
//  ModizerMini
//
//  Minimal, UI-free C interface on top of the Modizer project's portable
//  decoders (libxmp for tracker modules, Game_Music_Emu for console chiptunes).
//
//  Everything is driven from the audio render thread through ct_render_planar();
//  the UI thread reads lightweight snapshots through the ct_*_info functions and
//  asks for per-voice "solo" analysis buffers to draw the little oscilloscopes.
//

#ifndef CHIPTUNE_DECODER_H
#define CHIPTUNE_DECODER_H

#include <stdbool.h>

#ifdef __cplusplus
extern "C" {
#endif

/// Backend that is currently decoding a file, purely informational.
typedef enum {
    CT_BACKEND_NONE = 0,
    CT_BACKEND_XMP = 1,  ///< tracker module, rendered by libxmp
    CT_BACKEND_GME = 2,  ///< console chiptune, rendered by Game_Music_Emu
} CtBackend;

/// Metadata extracted once at load time.
typedef struct CtTrackInfo {
    char title[256];     ///< module / track title, empty when unknown
    char format[64];     ///< "MOD", "S3M", "NSF", "SPC", ...
    char system[64];     ///< "Tracker", "Nintendo", "Commodore 64", ...
    int  channels;       ///< instrument / voice count, 0 when unknown
    int  instruments;    ///< defined instruments, 0 when unknown
    int  samples;        ///< defined samples, 0 when unknown
    int  subsongs;       ///< number of sub-songs / tracks
    int  currentSubsong; ///< 0-based index of the loaded sub-song
    int  durationMs;     ///< estimated length in ms, -1 when unknown
    int  backend;        ///< CtBackend
} CtTrackInfo;

typedef struct CtEngine CtEngine;

/// Creates an engine that renders at @c sampleRate Hz (stereo, float planar).
CtEngine *ct_engine_create(double sampleRate);

/// Stops playback and releases every resource.
void ct_engine_destroy(CtEngine *engine);

/// Loads @p path. Returns false on failure, in which case @p outInfo is untouched.
/// When @p subsong is out of range, sub-song 0 is used.
bool ct_engine_load(CtEngine *engine, const char *path, int subsong, CtTrackInfo *outInfo);

/// Unloads the current file (keeps the engine usable).
void ct_engine_unload(CtEngine *engine);

bool ct_engine_is_loaded(const CtEngine *engine);

// MARK: transport -----------------------------------------------------------

void ct_engine_pause(CtEngine *engine, bool paused);
bool ct_engine_is_paused(const CtEngine *engine);

/// Jumps to @p seconds. The next render call resumes from there.
void ct_engine_seek(CtEngine *engine, double seconds);

/// Selects another sub-song of the loaded file.
void ct_engine_select_subsong(CtEngine *engine, int index);

/// When enabled the shim reports the end of the current pass through the
/// track so the caller can decide whether to loop or advance.
void ct_engine_set_looping(CtEngine *engine, bool looping);

/// Monotonic position inside the current pass, in seconds.
double ct_engine_current_time(const CtEngine *engine);

/// True once the whole track has been replayed at least once (non-looping help).
bool ct_engine_end_reached(const CtEngine *engine);

/// Volume applied by the shim, 0...1.
void  ct_engine_set_volume(CtEngine *engine, float volume);
float ct_engine_volume(const CtEngine *engine);

// MARK: rendering -----------------------------------------------------------

/// Renders @p frames stereo frames into two planar float buffers.
/// Safe to call from the real-time audio thread; never allocates.
void ct_render_planar(CtEngine *engine, float *left, float *right, int frames);

/// Copies the most recent @p frames of the mono downmix (the big oscilloscope).
/// Returns how many frames were actually copied into @p out.
int ct_engine_copy_waveform(const CtEngine *engine, float *out, int frames);

// MARK: per-voice info ------------------------------------------------------

int ct_voice_count(const CtEngine *engine);
const char *ct_voice_name(const CtEngine *engine, int voice);
const char *ct_voice_instrument(const CtEngine *engine, int voice);

/// Fills the live state of a voice. Any pointer may be NULL.
/// @p note is a MIDI note number (0 = silent), @p level is 0...1.
void ct_voice_state(const CtEngine *engine, int voice,
                    int *note, int *instrument, float *level, bool *active);

/// Copies the most recent @p frames of @p voice's own waveform (the decoders in
/// this repo already stash each voice separately). The values are scaled to
/// -1...1. Returns the number of frames actually written; a voice that the
/// current backend does not expose yields a flat line.
int ct_voice_waveform(const CtEngine *engine, int voice, float *out, int frames);

/// Tells the decoders where the app's resources live (UADE needs its data files).
void ct_engine_set_resource_path(const char *path);

// MARK: formats -------------------------------------------------------------

/// Semicolon-separated lowercase extension list (no dots) understood by the app.
const char *ct_supported_extensions(void);

/// True when the shim has a decoder for this path's extension.
bool ct_can_play(const char *path);

#ifdef __cplusplus
}
#endif

#endif /* CHIPTUNE_DECODER_H */

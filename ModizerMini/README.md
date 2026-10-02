# Modizer Mini

A small, native macOS player for the chiptune / tracker formats that Modizer
supports — built so that these files can be played with a double click or the
space bar in Finder, without launching the full app.

It is intentionally tiny and only reuses what already exists:

* the **portable decoders** that ship in this repository (`libs/libxmp`,
  `libs/libGME`) — no code is duplicated, the Xcode target compiles the very
  same sources;
* **SwiftUI + AppKit** and **SF Symbols** for the whole interface;
* **AVAudioEngine** for output and **Quick Look** for the Finder integration.

No custom graphics, drawing code beyond a polyline, or third-party UI
frameworks.

## What it plays

All the decoder libraries of the Modizer repository are built for native macOS
(`Tools/build-decoders.sh`, `Tools/build-extras.sh`) and linked into the app.
Each one is wrapped by a small `CtDecoder` adapter, so the engine treats them
all the same way:

| Decoder | Formats | State |
| --- | --- | --- |
| libxmp | tracker modules (`mod xm it s3m 669 …`) | ✅ |
| libopenmpt | trackers + `mptm mo3 …` | ✅ |
| Game_Music_Emu | `nsf nsfe spc gbs vgm vgz gym hes kss sap ay` | ✅ |
| vgmstream | game audio (`adx brstm hca fsb wav mp3 …`) | ✅ |
| libsidplayfp / reSIDfp | `sid mus c64 …` | ✅ |
| ASAP | Atari SAP | ✅ |
| Hively | `ahx hvl` | ✅ |
| ST-Sound | `ym` | ✅ |
| V2M | `v2m` | ✅ |
| AdPlug | AdLib / OPL | ✅ |
| AtariAudio | `sndh` | ✅ |
| libkss | MSX `kss mgs bgm …` | ✅ |
| libnsfplay | `nsf nsfe` | ✅ |
| libnez | `hes sgc …` | ✅ |
| libfmpmini / libpmdmini | PC-98 `ovi opi ozi m m2 mz` | ✅ |
| PT3 | ZX Spectrum `pt3` | ✅ |
| libpixel | PxTone / Organya `ptcop pttune org` | ✅ |
| libwonderswan | `wsr` | ✅ |
| libfurnace | `fur dmf ftm 0cc` | ✅ |
| libzxtune | ZX Spectrum tracker formats | ✅ |
| libvgm | `vgm vgz dro s98 gym` | ✅ |
| libxsf | `2sf mini2sf ncsf minincsf` | ✅ |
| PSF family | `psf psf2 ssf dsf qsf usf snsf spu` | ✅ |
| libgsf | `gsf minigsf` | ✅ |
| monkeyaudiocodec | `ape` | ✅ |
| sc68 | Atari ST / Amiga `.sc68` | ✅ |
| mdxplay | PC-98 `mdx pdx` | ✅ |
| libeupmini | FM Towns `eup` | ✅ |
| libtim | `mid midi` (Timidity) | ✅ |
| uade | Amiga modules | library built, adapter pending (IPC based) |

Notes:

* `gbs`/`gb` are handled by Game_Music_Emu, so `gbsplay` is not wired.
* SID files are handled by libsidplayfp/reSIDfp, so `websid` is not wired.
* Amiga modules (the `uade` set) and MIDI (`timidity`) still need their
  adapters; the libraries are built and linked.

Everything in `libs/` compiles for native macOS and is linked, so the remaining
adapters are one `ct_create_*` factory each in `Sources/ChiptuneKit/Backend*.cpp`
(the not-yet-written ones live in `BackendStubs.cpp`).

## Interface

* **Sidebar** – the playlist. Drag files in (or use `+`), reorder by dragging,
  click a row to play, `⌫` to remove.
* **Centre** – the currently playing file: title, format, system, a few
  statistics, a large live oscilloscope, and the instrument list.
* **Instrument list** – one row per voice/instrument with its name, the current
  note, a level meter and its own small oscilloscope. The per-voice waveforms
  come from the decoder buffers that this repository already fills
  (`m_voice_buff`, see `src/ModizerVoicesData.h`).
* **Transport** – shuffle, previous / play / next, repeat (off → all → one),
  seek bar and volume.
* **Quick Look** – press space on a supported file in Finder.

Keyboard: `space` play/pause, `⌘→` / `⌘←` next/previous, `⇧⌘S` shuffle,
`⇧⌘R` repeat.

## Building

The Xcode project is generated from `project.yml` with
[XcodeGen](https://github.com/yonaskolb/XcodeGen):

```sh
brew install xcodegen          # once
cd ModizerMini
xcodegen generate
open ModizerMini.xcodeproj
```

or from the command line:

```sh
xcodebuild -project ModizerMini.xcodeproj -scheme ModizerMini \
           -configuration Release -destination 'platform=macOS' build
```

The app icon and the document icon are generated with Core Graphics:

```sh
./Tools/generate-assets.sh
```

Requires Xcode 27 / macOS 27 SDK (the decoders are plain C/C++).

## Architecture

```
ModizerMini.xcodeproj
├── ChiptuneKit          static library: libxmp + libGME + the C shim
│   ├── Sources/ChiptuneKit/ChiptuneDecoder.{h,mm}
│   ├── ../libs/libxmp/libxmp-master/src
│   └── ../libs/libGME/gme
├── ModizerMini          the SwiftUI app
│   ├── Sources/App      views + playlist model
│   └── Sources/Shared   the AVAudioEngine wrapper (also used by the extension)
└── ModizerMiniQuickLook Quick Look preview extension
```

`ChiptuneDecoder.mm` hides both decoders behind one small C API
(`ct_engine_load`, `ct_render_planar`, `ct_voice_*`, `ct_voice_waveform`).
Because the decoders in this repository were already patched to copy every
voice into `m_voice_buff`, the per-instrument oscilloscopes are essentially
free.

## Notes

* The app is sandboxed (`files.user-selected.read-write`); the Quick Look
  extension is sandboxed read-only, as Apple requires.
* If the full **Modizer** app is installed it also declares most of these
  extensions, so macOS may resolve e.g. `.mod` to `com.yoyofr.modizer.mod`.
  The Quick Look extension therefore lists Modizer's identifiers too.
* Quick Look extensions must be enabled once in *System Settings → General →
  Login Items & Extensions → Quick Look*.
* Adding another decoder (for example libopenmpt for the broader tracker set)
  only means adding its sources to the `ChiptuneKit` target and teaching
  `ChiptuneDecoder.mm` which extensions it handles.

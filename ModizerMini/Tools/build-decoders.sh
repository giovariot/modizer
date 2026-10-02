#!/usr/bin/env bash
#
# Builds every decoder library of this repository for native macOS and copies
# the static archives into ModizerMini/Vendor/lib.
#
# Format:  project|target|output archive|build directory|extra settings
#
set -uo pipefail

HERE="$(cd "$(dirname "$0")/.." && pwd)"   # ModizerMini
ROOT="$(cd "$HERE/.." && pwd)"             # repository root
OUT="$HERE/Vendor/lib"
WORK="${TMPDIR:-/tmp}/modizermini-decoders"

mkdir -p "$OUT" "$WORK"

if [[ -f "$OUT/.decoders-complete" && "${MODIZERMINI_FORCE_DECODERS:-0}" != "1" ]]; then
  echo "decoders already built (delete $OUT/.decoders-complete to rebuild)"
  exit 0
fi

SPECS=(
  "libs/binio/binio.xcodeproj|binio|binio||"
  "libs/Adplug/adplug.xcodeproj|adplug|adplug||"
  "libs/libatariaudio/libatariaudio.xcodeproj|libatariaudio|atariaudio||"
  "libs/libeupmini/libeupmini.xcodeproj|libeupmini|eupmini||"
  "libs/libfmpmini/libfmpmini.xcodeproj|libfmpmini|fmpmini||"
  "libs/libpmdmini/libpmdmini.xcodeproj|libpmdmini|pmdmini||"
  "libs/libpt3/libpt3/libpt3.xcodeproj|libpt3|pt3||"
  "libs/libpixel/libpixel.xcodeproj|libpixel|pixel||"
  "libs/libwonderswan/libwonderswan.xcodeproj|libwonderswan|wonderswan||"
  "libs/libkss/libkss.xcodeproj|libkss|kss||"
  "libs/libnez/nez/nez.xcodeproj|nez|nez||"
  "libs/libgsf/libgsf/gsf.xcodeproj|gsf|gsf||"
  "libs/libnsfplay/libnsfplay.xcodeproj|libnsfplay|nsfplay||"
  "libs/libfurnace/libfurnace.xcodeproj|libfurnace|furnace||"
  "libs/libvgm/libvgm.xcodeproj|libvgm|vgm||"
  "libs/v2mplayer/v2mplayer.xcodeproj|v2mplayer|v2mplayer||"
  "libs/gbsplay/gbsplay.xcodeproj|gbsplay|gbsplay||"
  "libs/libopenmpt/mpg123-openmpt/mpg123-openmpt.xcodeproj|mpg123-openmpt|mpg123||"
  "libs/libopenmpt/ogg-openmpt/ogg-openmpt.xcodeproj|ogg-openmpt|ogg||"
  "libs/libopenmpt/vorbis-openmpt/vorbis-openmpt.xcodeproj|vorbis-openmpt|vorbis||"
  "libs/libopenmpt/libopenmpt-modizer/libopenmpt-modizer.xcodeproj|libopenmpt-modizer|openmpt||"
  # libresidfp must land in the same build directory as libsidplayfp
  "libs/libsidplayfp/libresidfp/libresidfp.xcodeproj|libresidfp|residfp|sidplayfp|"
  "libs/libsidplayfp/libsidplayfp.xcodeproj|libsidplayfp|sidplayfp||"
  "libs/highlyexperimental/highlyexperimental.xcodeproj|highlyexperimental|highlyexperimental||"
  "libs/highlytheoritical/highlytheoritical.xcodeproj|highlytheoritical|highlytheoritical||"
  "libs/HighlyQuixotic/HighlyQuixotic.xcodeproj|HighlyQuixotic|HighlyQuixotic||"
  "libs/libxsf/libxsf/libxsf.xcodeproj|libxsf|xsf||"
  "libs/vio2sf/vio2sf.xcodeproj|vio2sf|vio2sf||"
  "libs/snsf/snsf.xcodeproj|snsf|snsf||"
  "libs/libLazyusf/libLazyusf old/Lazyusf.xcodeproj|Lazyusf|lazyusf||USER_HEADER_SEARCH_PATHS=\$(SRCROOT)"
  "libs/libpsflib/psflib/psflib.xcodeproj|psflib|psflib||"
  "libs/libtim/tim.xcodeproj|tim|tim||"
  "libs/websid/websid.xcodeproj|websid|websid||"
  "libs/uade-2.13/uade/uade.xcodeproj|uade|uade||"
  "libs/monkeyaudiocodec/monkeyaudiocodec.xcodeproj|monkeyaudiocodec|monkeyaudio||"
  "libs/libchpconv/libchpconv.xcodeproj|libchpconv|chpconv||"
  "libs/libg719/libg719.xcodeproj|libg719|g719||"
  "libs/libzxtune/libzxtune.xcodeproj|libzxtune|zxtune||"
)

FAILED=()
OK=()
for spec in "${SPECS[@]}"; do
  IFS='|' read -r proj tgt name builddir extra <<< "$spec"
  builddir="${builddir:-$name}"
  if [[ ! -e "$ROOT/$proj" ]]; then echo "SKIP  $name (missing $proj)"; continue; fi
  echo "==> $name"
  # shellcheck disable=SC2086
  if xcodebuild -project "$ROOT/$proj" -target "$tgt" -sdk macosx -configuration Release \
        CONFIGURATION_BUILD_DIR="$WORK/$builddir" OBJROOT="$WORK/$builddir/obj" SYMROOT="$WORK/$builddir/sym" \
        MACOSX_DEPLOYMENT_TARGET=27.0 $extra \
        build > "$WORK/$name.log" 2>&1; then
    archive="$(find "$WORK/$builddir" -maxdepth 1 -name "lib$tgt.a" | head -1)"
    [[ -z "$archive" ]] && archive="$(find "$WORK/$builddir" -maxdepth 1 -name '*.a' | head -1)"
    if [[ -n "$archive" ]]; then cp "$archive" "$OUT/$name.a"; OK+=("$name"); else FAILED+=("$name(no .a)"); fi
  else
    FAILED+=("$name")
    echo "    FAILED: $(grep -m1 'error:' "$WORK/$name.log" || tail -1 "$WORK/$name.log")"
  fi
done

echo "==> vgmstream (without FFmpeg)"
if xcodebuild -project "$ROOT/libs/libvgmstream/vgmstream/vgmstream.xcodeproj" -target vgmstream -sdk macosx -configuration Release \
      CONFIGURATION_BUILD_DIR="$WORK/vgmstream" OBJROOT="$WORK/vgmstream/obj" SYMROOT="$WORK/vgmstream/sym" \
      MACOSX_DEPLOYMENT_TARGET=27.0 \
      OTHER_CFLAGS="-DVGM_USE_MPEG=1 -DVGM_USE_VORBIS=1 -DVGM_USE_ATRAC9=1 -DVGM_USE_G719=1 -DVGM_USE_G7221=1" \
      build > "$WORK/vgmstream.log" 2>&1; then
  cp "$WORK/vgmstream/libvgmstream.a" "$OUT/vgmstream.a" 2>/dev/null || cp "$(find "$WORK/vgmstream" -maxdepth 1 -name '*.a' | head -1)" "$OUT/vgmstream.a"
  OK+=("vgmstream")
else
  FAILED+=("vgmstream"); echo "    FAILED: $(grep -m1 'error:' "$WORK/vgmstream.log")"
fi

echo
echo "=== built (${#OK[@]}) ==="; printf '%s\n' "${OK[@]}"
echo "=== failed (${#FAILED[@]}) ==="; printf '%s\n' "${FAILED[@]:-}"
if [[ ${#FAILED[@]} -eq 0 ]]; then touch "$OUT/.decoders-complete"; fi

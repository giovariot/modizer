#!/usr/bin/env bash
#
# Builds the decoder libraries that have no Xcode project of their own by
# compiling their sources directly with clang.
#
set -uo pipefail

HERE="$(cd "$(dirname "$0")/.." && pwd)"
ROOT="$(cd "$HERE/.." && pwd)"
OUT="$HERE/Vendor/lib"
WORK="${TMPDIR:-/tmp}/modizermini-extras"
mkdir -p "$OUT" "$WORK"

if [[ -f "$OUT/.extras-complete" && "${MODIZERMINI_FORCE_DECODERS:-0}" != "1" ]]; then
  echo "extras already built"
  exit 0
fi

compile_lib() {
  local name="$1"; shift
  local incs="$1"; shift
  local defs="$1"; shift
  local objdir="$WORK/$name"
  rm -rf "$objdir"; mkdir -p "$objdir"
  local i=0
  local src ext cc
  for src in "$@"; do
    ext="${src##*.}"
    cc=clang
    [ "$ext" = "cpp" ] && cc=clang++
    local incflags=()
    for d in $incs; do incflags+=("-I$ROOT/$d"); done
    # Some decoders in this repository #include "../../../../src/ModizerVoicesData.h".
    incflags+=("-I$HERE/Vendor/shiminc/a")
    if ! $cc -c -O2 -fPIC -Wno-everything $defs "${incflags[@]}" "$ROOT/$src" -o "$objdir/$(printf '%03d' "$i")_$(basename "$src" | tr '.' '_').o" 2>"$WORK/$name.err"; then
      echo "  FAIL $name: $src"; head -3 "$WORK/$name.err"; return 1
    fi
    i=$((i+1))
  done
  libtool -static -o "$OUT/$name.a" "$objdir"/*.o >/dev/null 2>&1
  echo "  OK   $name"
}

echo "==> atrac9"
ATRAC9_SRC=$(cd "$ROOT/libs/LibAtrac9-master/C/src" && find . -name '*.c' | sed 's|^\./|libs/LibAtrac9-master/C/src/|' | sort)
# shellcheck disable=SC2086
compile_lib atrac9 "libs/LibAtrac9-master/C/src" "" $ATRAC9_SRC

echo "==> asap"
compile_lib asap "libs/asap" "" libs/asap/asap.c

echo "==> hvl (AHX)"
compile_lib hvl "libs/AHX" "" libs/AHX/hvl_replay.cpp

echo "==> stsound"
compile_lib stsound "libs/STSound/StSoundLibrary libs/STSound/StSoundLibrary/LZH" "" \
  libs/STSound/StSoundLibrary/YmMusic.cpp \
  libs/STSound/StSoundLibrary/Ym2149Ex.cpp \
  libs/STSound/StSoundLibrary/Ymload.cpp \
  libs/STSound/StSoundLibrary/YmUserInterface.cpp \
  libs/STSound/StSoundLibrary/digidrum.cpp \
  libs/STSound/StSoundLibrary/LZH/LzhLib.cpp

echo "==> mdxplay"
compile_lib mdxplay "libs/mdxplay src" "" \
  libs/mdxplay/mdxmain.c \
  libs/mdxplay/mdx2151.c \
  libs/mdxplay/mdx_ym2151.c \
  libs/mdxplay/mdxfile.c \
  libs/mdxplay/mdxmml_ym2151.c \
  libs/mdxplay/pcm8.c \
  libs/mdxplay/pdxfile.c

echo "==> sc68"
SC68_SRC=$(cd "$ROOT" && find libs/sc68/libsc68 libs/sc68/file68 libs/sc68/unice68 -name '*.c' \
  ! -name 'cc68.c' ! -name 'lines68.c' ! -path '*/emu68/lines/*' ! -name '*_table.c' | sort | \
  while IFS= read -r f; do if grep -q '#error' "$f"; then :; else echo "$f"; fi; done)

compile_lib sc68 "libs/sc68 libs/sc68/libsc68 libs/sc68/libsc68/io68 libs/sc68/libsc68/emu68 libs/sc68/libsc68/emu68/lines libs/sc68/libsc68/sc68 libs/sc68/libsc68/src libs/sc68/libsc68/dial68 libs/sc68/file68 libs/sc68/file68/sc68 libs/sc68/file68/src libs/sc68/unice68 libs/sc68/sc68-libc" "-DHAVE_STDINT_H=1 -DHAVE_SYS_TYPES_H=1 -DHAVE_STRING_H=1 -DHAVE_STRINGS_H=1 -DPACKAGE_STRING=\"sc68\" -DPACKAGE_VERSION=\"4.0\" -DPACKAGE_NAME=\"sc68\" -DPACKAGE_BUGREPORT=\"\" -DPACKAGE_URL=\"https://sc68.benjihan.org/\" -include $ROOT/libs/sc68/libsc68/emu68/emu68_private.h -DPACKAGE=\"sc68\" -DCOPYRIGHT_YEAR=\"2016\" -DVERSION=\"4.0\"" $SC68_SRC

echo "==> freeverb"
FREEVERB_SRC=$(cd "$ROOT/libs/freeverb" && find . -maxdepth 1 -name '*.cpp' | sed 's|^\./|libs/freeverb/|' | sort)
# shellcheck disable=SC2086
compile_lib freeverb "libs/freeverb" "" $FREEVERB_SRC

echo "==> done"
touch "$OUT/.extras-complete"

exit 0

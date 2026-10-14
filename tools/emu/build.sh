#!/bin/sh
# SPDX-License-Identifier: GPL-3.0-only
# Build the FM-1 emulator: build/host/emu (clang, SDL2 from Homebrew, CoreMIDI, zlib from the SDK).
#   sh tools/emu/build.sh            (from anywhere)
# The firmware's generated headers (build/gen) must exist: python3 tools/build.py generates them
# (Pillow needs: export DYLD_FALLBACK_LIBRARY_PATH=/opt/homebrew/lib); this script runs the generate
# step itself when build/gen is missing.
set -e
ROOT=$(cd "$(dirname "$0")/../.." && pwd)
cd "$ROOT"
export DYLD_FALLBACK_LIBRARY_PATH=/opt/homebrew/lib${DYLD_FALLBACK_LIBRARY_PATH:+:$DYLD_FALLBACK_LIBRARY_PATH}
for h in ui_fonts.h ui_icons.h ui_keycaps.h ui_palettes.h felucca_tables.h felucca_fm6.h melodee_cz1.h felucca_samples.h; do
    if [ ! -f "build/gen/$h" ]; then
        echo "build/gen/$h missing: running the generate step (tools/build.py)"
        python3 tools/build.py
        break
    fi
done
SDL_CFLAGS=$(/opt/homebrew/bin/sdl2-config --cflags 2>/dev/null || echo "-I/opt/homebrew/include/SDL2 -D_THREAD_SAFE")
SDL_LIBS=$(/opt/homebrew/bin/sdl2-config --libs 2>/dev/null || echo "-L/opt/homebrew/lib -lSDL2")
CC=${CC:-clang}
OUT=build/host
OBJ=build/host/emu_obj
mkdir -p "$OUT" "$OBJ" build/emu
# the version the firmware shows: firmware/src/choralroot.c's FELUCCA_VERSION (the device build's default), or
# CR_VERSION=X.Y as ./build.sh --release X.Y ("ChoralRoot X.Y"); the same rule as tools/emu/web/build_web.sh
if [ -n "${CR_VERSION:-}" ]; then
    case "$CR_VERSION" in
        ChoralRoot*) VER=$CR_VERSION ;;
        *) VER="ChoralRoot $(printf %s "$CR_VERSION" | tr A-Z a-z)" ;;
    esac
else
    VER=$(sed -n 's/^#define FELUCCA_VERSION "\(.*\)".*/\1/p' firmware/src/choralroot.c | head -1)
fi
[ -n "$VER" ] || { echo "build.sh: no FELUCCA_VERSION in firmware/src/choralroot.c"; exit 1; }
# The five units side by side, each through tests/hostcc.sh: compiled again only when its command (the version and
# every flag are in it) or a file it was compiled from has changed (hashes, not dates; build/gen's headers and the
# system's are among them); their diagnostics in the order below. HOSTCC_FORCE=1: everything again.
H="sh tests/hostcc.sh"
# the firmware: one unit, as the host tests build it (warnings off: it is the firmware's own code)
$H $CC -O2 -w -Ibuild/gen -Ifirmware/src "-DFELUCCA_VERSION=\"$VER\"" -c tools/emu/emu_fw.c -o "$OBJ/emu_fw.o" > "$OBJ/emu_fw.log" 2>&1 &
pids=$!
# the emulator
for f in emu keymap emu_img; do
    $H $CC -O2 -Wall -Wextra -Wno-unused-parameter $SDL_CFLAGS -c "tools/emu/$f.c" -o "$OBJ/$f.o" > "$OBJ/$f.log" 2>&1 &
    pids="$pids $!"
done
$H $CC -O2 -Wall -Wno-deprecated-declarations -c tools/emu/emu_midi.c -o "$OBJ/emu_midi.o" $SDL_CFLAGS > "$OBJ/emu_midi.log" 2>&1 &
pids="$pids $!"
st=0
for f in emu_fw emu keymap emu_img emu_midi; do
    p=${pids%% *}; pids=${pids#* }
    wait "$p" || st=$?
    cat "$OBJ/$f.log"
done
[ "$st" -eq 0 ] || exit "$st"
$H $CC -o "$OUT/emu" "$OBJ/emu.o" "$OBJ/keymap.o" "$OBJ/emu_img.o" "$OBJ/emu_midi.o" "$OBJ/emu_fw.o" \
    $SDL_LIBS -lz -lm -framework CoreMIDI -framework CoreFoundation
echo "built $OUT/emu"

#!/bin/sh
# SPDX-License-Identifier: GPL-3.0-only
# ChoralRoot FM-1 in the browser: the emulator's firmware side as a standalone WebAssembly module, and its page.
#   [CR_VERSION=1.0] sh tools/emu/web/build_web.sh
#   -> build/emu-web/{index.html, emu.js, keymap.js, worklet.js, choralroot.wasm}
# Needs emcc (the Emscripten SDK: ~/emsdk/emsdk_env.sh is sourced when emcc is not on the PATH) and the firmware's
# generated headers (build/gen: the generate step of tools/build.py runs when they are missing, as build.sh does).
# CR_VERSION: the version the firmware shows ("1.0" -> "ChoralRoot 1.0", as tools/build.py --release); default
# firmware/src/choralroot.c's FELUCCA_VERSION.
set -e
ROOT=$(cd "$(dirname "$0")/../../.." && pwd)
cd "$ROOT"
if ! command -v emcc >/dev/null 2>&1; then
    for e in "${EMSDK:-}/emsdk_env.sh" "$HOME/emsdk/emsdk_env.sh" "$ROOT/../tools/emsdk/emsdk_env.sh"; do
        [ -f "$e" ] && { . "$e" >/dev/null 2>&1; break; }
    done
fi
command -v emcc >/dev/null 2>&1 || { echo "emcc not found: install the Emscripten SDK (~/emsdk)"; exit 1; }
export DYLD_FALLBACK_LIBRARY_PATH=/opt/homebrew/lib${DYLD_FALLBACK_LIBRARY_PATH:+:$DYLD_FALLBACK_LIBRARY_PATH}
for h in ui_fonts.h ui_icons.h ui_keycaps.h ui_palettes.h felucca_tables.h felucca_fm6.h felucca_samples.h cf_fonts.h; do
    if [ ! -f "build/gen/$h" ]; then
        echo "build/gen/$h missing: running the generate step (tools/build.py)"
        python3 -c 'import sys; sys.path.insert(0, "tools"); import build; build.generate()'
        break
    fi
done
if [ -n "${CR_VERSION:-}" ]; then
    case "$CR_VERSION" in
        CoralFlower*) VER=$CR_VERSION ;;
        *) VER="CoralFlower $(printf %s "$CR_VERSION" | tr A-Z a-z)" ;;
    esac
else
    VER=$(sed -n 's/^#define FELUCCA_VERSION "\(.*\)"/\1/p' firmware/src/coralflower.c | head -1)
fi
OUT=build/emu-web
W=tools/emu/web
mkdir -p "$OUT"
# the firmware unit as build.sh builds it (warnings off: the firmware's own code), single-threaded, no JS runtime;
# through tests/hostcc.sh: compiled again only when the command (the version is in it) or a file it was compiled from
# has changed (hashes, not dates; HOSTCC_FORCE=1: always)
sh tests/hostcc.sh emcc -O2 -w -std=gnu11 -I$W/compat -Ibuild/gen -Ifirmware/src -DEMU_WEB "-DFELUCCA_VERSION=\"$VER\"" \
    --no-entry -sSTANDALONE_WASM -sFILESYSTEM=0 -sSTACK_SIZE=1048576 -sINITIAL_MEMORY=33554432 \
    -o "$OUT/coralflower.wasm" $W/emu_web.c
cp $W/index.html $W/emu.js $W/keymap.js $W/worklet.js assets/fonts/DepartureMono-Regular.otf "$OUT/"
# a new build is a new address: the browser cannot run a cached wasm or script after a rebuild (a plain reload did)
STAMP=$(cat "$OUT/coralflower.wasm" $W/emu.js $W/keymap.js $W/worklet.js | shasum | cut -c1-10)
sed -i '' -e "s/src=\"keymap.js\"/src=\"keymap.js?v=$STAMP\"/" -e "s/src=\"emu.js\"/src=\"emu.js?v=$STAMP\"/" "$OUT/index.html"
sed -i '' -e "s/getBytes(\"coralflower.wasm\")/getBytes(\"coralflower.wasm?v=$STAMP\")/" \
    -e "s/addModule(\"worklet.js\")/addModule(\"worklet.js?v=$STAMP\")/" "$OUT/emu.js"
echo "emu-web: $OUT ($VER)"
for f in coralflower.wasm index.html emu.js keymap.js worklet.js; do
    printf '  %-16s %8d B\n' "$f" "$(wc -c < "$OUT/$f")"
done

#!/bin/sh
# SPDX-License-Identifier: GPL-3.0-only
# CoralFlower: build and run the headless native emulator (tools/emu/cf_headless.c) on a script.
#   sh tools/emu/cf_headless.sh SCRIPT [OUTDIR]     (shots: OUTDIR/*.ppm and *.png)
set -e
ROOT=$(cd "$(dirname "$0")/../.." && pwd)
cd "$ROOT"
OUT=${2:-build/cf-run}
mkdir -p build/host "$OUT"
cc -O2 -g -w -std=gnu11 -Itools/emu/web/compat -Ibuild/gen -Ifirmware/src -DEMU_WEB \
   '-DFELUCCA_VERSION="CoralFlower dev"' -o build/host/cf_headless tools/emu/web/emu_web.c tools/emu/cf_headless.c
build/host/cf_headless "$1" "$OUT"
python3 - "$OUT" <<'PY' 2>/dev/null || true
import sys, glob
from PIL import Image
for f in glob.glob(sys.argv[1] + "/*.ppm"):
    Image.open(f).resize((480, 480), Image.NEAREST).save(f[:-4] + ".png")
PY

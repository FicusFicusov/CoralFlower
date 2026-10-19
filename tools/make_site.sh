#!/bin/sh
# SPDX-License-Identifier: GPL-3.0-only
# CoralFlower's site, put together from its sources:   sh tools/make_site.sh OUT
#   OUT/                 the first page (web/index.html: what CoralFlower is, and its installer), site.css, fonts/,
#                        the installer's modules (web/fm1*.js), the icons, images/ (the README's screens)
#   OUT/play/            the emulator page (tools/emu/web/index.html) with its build (build/emu-web: build_web.sh first)
#   OUT/install/         the old installer address, forwarding to /#install
# OUT/firmware/ (the released packages and releases.json) is not touched: pages.yml fills it from the releases, and a
# local copy keeps what is there. A new site.css is a new address (?v=hash): a browser cannot keep an old one. The
# installer's modules too: an old one kept beside a new page broke the page (an export it did not have yet, 0.3.1).
set -e
ROOT=$(cd "$(dirname "$0")/.." && pwd)
OUT=${1:?usage: sh tools/make_site.sh OUT}
cd "$ROOT"
EMU=build/emu-web
for f in index.html emu.js keymap.js worklet.js coralflower.wasm; do
    [ -f "$EMU/$f" ] || { echo "$EMU/$f missing: run sh tools/emu/web/build_web.sh first"; exit 1; }
done
mkdir -p "$OUT/fonts" "$OUT/images" "$OUT/install" "$OUT/play"
cp web/index.html web/site.css web/fm1pkg.js web/fm1ota.js web/fm1backup.js "$OUT/"
cp web/fonts/*.woff web/fonts/OFL-*.txt "$OUT/fonts/"
cp tools/emu/web/favicon.svg tools/emu/web/favicon-32.png tools/emu/web/apple-touch-icon.png "$OUT/"
cp docs/images/*.png docs/images/*.gif "$OUT/images/"
cp web/install/index.html "$OUT/install/"
cp "$EMU/index.html" "$EMU/emu.js" "$EMU/keymap.js" "$EMU/worklet.js" "$EMU/coralflower.wasm" "$OUT/play/"
STAMP=$(cat web/site.css | shasum | cut -c1-10)
# (sed -i.bak, then the .bak removed: the one form both BSD sed on macOS and GNU sed on the CI runner take)
sed -i.bak "s|href=\"site.css\"|href=\"site.css?v=$STAMP\"|" "$OUT/index.html"
sed -i.bak "s|href=\"../site.css\"|href=\"../site.css?v=$STAMP\"|" "$OUT/play/index.html"
for m in fm1pkg.js fm1ota.js fm1backup.js; do
    MS=$(cat "web/$m" | shasum | cut -c1-10)
    sed -i.bak "s|from \"./$m\"|from \"./$m?v=$MS\"|" "$OUT/index.html"
done
rm -f "$OUT/index.html.bak" "$OUT/play/index.html.bak"
touch "$OUT/.nojekyll"
echo "site: $OUT"

#!/bin/sh
# SPDX-License-Identifier: GPL-3.0-only
# LISTENING FILES for 0.21's chorus (docs/FX.md "The chorus, stereo"): Depth turned in detents through 0.20's block
# (a click a detent) and through 0.21's (the read point kept, Depth and Width glided over 46 ms); the new Width knob
# swept and at 0, 64, 127. The host test's renderer (tests/cr_fx_test.c chorus_listen: cr_fx.c's xc_block itself and
# 0.20's block kept beside it), a held chord beside its chorus at send 105; not the emulator, no sound of the unit.
#   sh tools/chorus_glide_listen.sh [OUTDIR]     (default ../audio-0.21/chorus beside the repository; needs build/gen:
#                                                 ./build.sh or sh tests/run_tests.sh once)
set -e
cd "$(dirname "$0")/.."
O=${1:-../audio-0.21/chorus}
mkdir -p "$O" build/host
cc -std=gnu11 -O2 -w -Ibuild/gen -Ifirmware/src -Itests -o build/host/cr_fx_test tests/cr_fx_test.c -lm
./build/host/cr_fx_test --chorus-listen "$O"
cat > "$O/README.txt" <<'EOF'
ChoralRoot 0.21: the chorus while a knob turns, and its Width (tools/chorus_glide_listen.sh; docs/FX.md "The chorus,
stereo"). 44.1 kHz stereo, a held chord (D3 F#3 A3 D4) beside its chorus, the chorus send at 105 of 127 so that the
chorus is easy to hear. Rendered on the computer by the firmware's own chorus code, not recorded from a unit.

  depth-steps-before-0.20.wav   Depth turned as the FX layer turns it (2 a detent), through 0.20's code:
                                1.5 s .. 4 s   60 -> 100, a detent every 125 ms: a click at each one
                                5 s .. 6 s     100 -> 0, a quick turn (a detent every 20 ms): crackle
                                7 s .. 8.3 s   0 -> 127, a quick turn: crackle
  depth-steps-after-0.21.wav    the same turns through 0.21: no clicks; a quick turn bends the pitch of the chorus
                                for a moment (the delay itself is moving), as a tape chorus would
  width-sweep-127-0-127.wav     0.21's new knob (KNOB 3 of the Chorus page): 1.5 s .. 4.1 s Width 127 -> 0 (the two
                                ears' choruses become one, in the middle), 5 s .. 7.6 s back to 127
  width-sweep-127-0-127-without-the-glide.wav
                                the same sweep had the knob been wired to 0.20's code: the right ear crackles
  width-000.wav                 Width 0: one chorus, the same in both ears (the chorus of 0.18)
  width-064.wav                 Width 64: half way
  width-127.wav                 Width 127, the default: the chorus of 0.19 and 0.20

Headphones show the Width best. Depth 60 and Rate 40 (the defaults) where they are not the thing turned.
EOF
echo "wrote $(ls "$O"/*.wav | wc -l | tr -d ' ') WAVs and README.txt into $O"

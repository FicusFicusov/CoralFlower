#!/bin/sh
# SPDX-License-Identifier: GPL-3.0-only
# What Options > Output = Classic still shares with 0.18, bit for bit (docs/AUDIO-QUALITY.md "Classic is 0.18's"), in
# two parts. Since 0.19's stereo chorus (cr_fx.c xc_block: an effect, in both Output modes) a render with a chorus send
# is no longer 0.18's in either mode; everything else of Classic is.
#
# 1. THE OUTPUT CHAIN, AND EVERYTHING BUT THE CHORUS'S RETURN: every script of tools/emu/scripts through this tree's
#    emulator with the Output held at Classic (EMU_OUTPUT=classic: the settings of a fresh flash say Clean) AND the
#    chorus returned as 0.18 did, one tap to both sides (EMU_CHORUS=mono: fx.c CR_CHORUS_MONO, a test switch of the
#    emulator's build; the unit has only the stereo return), and through a reference emulator built from the 0.18 tree,
#    each on a fresh RAM flash, the two WAVs compared byte for byte. The reference runs ITS OWN copy of the script (an
#    Options entry was added in 0.19: the two scripts that walk to an entry behind it, cr_version and cr_calib, count
#    one step more here); a script the reference tree does not have is not compared.
# 2. CLASSIC AS THE UNIT PLAYS IT, WHERE NOTHING IS SENT TO THE CHORUS: the scripts tools/emu/scripts/classic_dry_*.txt
#    (the chord part's chorus send at 0; FX off), THIS tree's file through both emulators (they use only what 0.18
#    has), this tree's held at Classic with the stereo chorus as built: identical, and not silent.
# So the guarantee is: Classic is 0.18 bit for bit for any sound without a chorus send (part 2, and part 1's scripts
# that have none), and with a chorus send it differs from 0.18 by the chorus's return alone (part 1).
#
#   mkdir -p build/scratch/ref18 && git archive 19b6453 | tar -x -C build/scratch/ref18
#   mkdir -p build/scratch/ref18/build && cp -R build/gen build/scratch/ref18/build/gen     (the generated headers)
#   (cd build/scratch/ref18 && sh tools/emu/build.sh)
#   sh tools/emu/classic_exact.sh build/scratch/ref18
# Three scripts play CZ-1, whose engine changed after 0.18 (abb0b81: no pop when a sounding note is struck again, the
# DCW depth ramped across a block): their mix is no longer 0.18's before any output chain, so they are listed, named in
# the result and not counted as a failure (ENGINE_CHANGED below). For a proof that covers them too, compare with a
# tree that has that fix and Options > Output (f402608 or later), both sides held at Classic:
#   REF_CLASSIC=1 sh tools/emu/classic_exact.sh build/scratch/ref_f402
# (a reference that has the stereo chorus too is held at the mono return with this tree: EMU_CHORUS=mono goes to both;
# 0.18's and f402608's emulators do not know the variable and have only that return.)
# Headless: export DYLD_FALLBACK_LIBRARY_PATH=/opt/homebrew/lib SDL_VIDEODRIVER=dummy SDL_AUDIODRIVER=dummy
# Not part of test_cr.sh (it needs the reference build); never run it beside test_cr.sh (both write build/emu/test).
cd "$(dirname "$0")/../.." || exit 1
RD=${1:?usage: classic_exact.sh REFERENCE_TREE}
REF=$RD/build/host/emu
EMU=build/host/emu
[ -x "$REF" ] || { echo "no $REF: build the reference first (the lines above)"; exit 1; }
O=build/scratch/classic_exact
mkdir -p "$O/new" "$O/ref" "$O/dry" build/emu/test
ENGINE_CHANGED=" cr_cz cr_select perf_g "       # (against a 0.18 tree only)
[ -n "$REF_CLASSIC" ] && ENGINE_CHANGED=""
nonzero() { [ "$(tail -c +45 "$1" | tr -d '\000' | wc -c)" -gt 0 ]; }   # (a WAV that is all zeros proves nothing)
n=0
same=0
diff=0
silent=0
new=0
eng=0
for f in tools/emu/scripts/*.txt; do
    b=$(basename "$f" .txt)
    if [ ! -f "$RD/tools/emu/scripts/$b.txt" ]; then
        case "$b" in classic_dry_*) ;; *) echo "NEW     $b (not in the reference tree: not compared)" ;; esac
        new=$((new + 1))
        continue
    fi
    if [ -n "$REF_CLASSIC" ]; then
        EMU_OUTPUT=classic EMU_CHORUS=mono "$REF" --headless --script "$RD/tools/emu/scripts/$b.txt" --wav "$O/ref/$b.wav" >"$O/ref/$b.log" 2>&1
    else
        EMU_CHORUS=mono "$REF" --headless --script "$RD/tools/emu/scripts/$b.txt" --wav "$O/ref/$b.wav" >"$O/ref/$b.log" 2>&1
    fi
    EMU_OUTPUT=classic EMU_CHORUS=mono "$EMU" --headless --script "$f" --wav "$O/new/$b.wav" >"$O/new/$b.log" 2>&1
    n=$((n + 1))
    if [ ! -s "$O/ref/$b.wav" ] || [ ! -s "$O/new/$b.wav" ]; then
        echo "NO WAV  $b"
        diff=$((diff + 1))
    elif cmp -s "$O/ref/$b.wav" "$O/new/$b.wav"; then
        same=$((same + 1))
        nonzero "$O/new/$b.wav" || silent=$((silent + 1))
    elif [ "${ENGINE_CHANGED#* $b }" != "$ENGINE_CHANGED" ]; then
        echo "ENGINE  $b (CZ-1 changed after 0.18, before the output chain: differs, as expected)"
        eng=$((eng + 1))
    else
        echo "DIFFERS $b"
        diff=$((diff + 1))
    fi
done
echo "classic_exact 1 (the chorus's return held at 0.18's): $n scripts compared, $same WAVs identical to the reference's ($silent of them silent), $diff differ, $eng differ by CZ-1's engine fix (not the output chain); $new new scripts not compared"
# part 2: no chorus send, the stereo chorus as the unit has it
dn=0
dsame=0
for f in tools/emu/scripts/classic_dry_*.txt; do
    b=$(basename "$f" .txt)
    if [ -n "$REF_CLASSIC" ]; then
        EMU_OUTPUT=classic "$REF" --headless --script "$f" --wav "$O/dry/$b.ref.wav" >"$O/dry/$b.ref.log" 2>&1
    else
        "$REF" --headless --script "$f" --wav "$O/dry/$b.ref.wav" >"$O/dry/$b.ref.log" 2>&1
    fi
    EMU_OUTPUT=classic "$EMU" --headless --script "$f" --wav "$O/dry/$b.new.wav" >"$O/dry/$b.new.log" 2>&1
    dn=$((dn + 1))
    if [ -s "$O/dry/$b.new.wav" ] && cmp -s "$O/dry/$b.ref.wav" "$O/dry/$b.new.wav" && nonzero "$O/dry/$b.new.wav"; then
        dsame=$((dsame + 1))
    else
        echo "DIFFERS $b (no chorus send, the stereo chorus built in: must be the reference's, and not silent)"
        diff=$((diff + 1))
    fi
done
echo "classic_exact 2 (no chorus send, the chorus as the unit has it): $dn scripts, $dsame identical to the reference's and not silent"
[ "$diff" -eq 0 ] && echo PASS || { echo FAIL; exit 1; }

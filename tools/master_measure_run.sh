#!/bin/sh
# SPDX-License-Identifier: GPL-3.0-only
# MEASUREMENT ONLY (GitHub issue 3, docs/AUDIO-QUALITY.md): builds tests/master_measure.c and renders the set: four
# sounds (FM6 TINE EP, VA SYNTH BRASS, WHEEL FULL ORGAN, CZ-1 BRASS 1), each as a single note and as the 6-note chord
# with the bass, FX on and off, through both output chains (Classic and Clean) at MASTER 0 / -6 / -12 / -18 / -24 /
# -40 dB; and the transient test (tools/master_transient.py: a kick on a bass, 6 and 12 dB over Clean's threshold)
# through both chains. tools/master_measure_tables.sh prints the tables from it.
#   sh tools/master_measure_run.sh OUTDIR [extra cc flags, e.g. -DCLN_TRIM=2896, -DCLN_LA_K=2u, -DCLN_HOLD=8820u]
# The generated headers (build/gen) must exist: ./build.sh or tools/emu/build.sh makes them.
set -e
cd "$(dirname "$0")/.."
O=${1:?usage: master_measure_run.sh OUTDIR [cc flags]}
shift
mkdir -p build/host "$O"
[ -f build/gen/felucca_tables.h ] || { echo "build/gen missing: run tools/emu/build.sh first"; exit 1; }
cc -std=gnu11 -O2 -w -Ibuild/gen -Ifirmware/src -Itests "$@" -o build/host/master_measure tests/master_measure.c -lm
M=build/host/master_measure
ATT=0,6,12,18,24,40
: > "$O/render.log"
r() {
    n=$1
    shift
    echo "== $n: $*" >> "$O/render.log"
    $M render "$O/$n" --att $ATT "$@" | grep -E "^held|^render|^chain" >> "$O/render.log"
}
for s in "fm6 12 TINE_EP" "va 13 SYNTH_BRASS" "wheel 7 FULL_ORGAN" "cz 14 BRASS_1"; do
    set -- $s
    name=$(echo "$3" | tr _ ' ')
    r "$1-chord6-fx" --eng "$2" --name "$name" --fx 1
    r "$1-chord6-nofx" --eng "$2" --name "$name" --fx 0
    r "$1-single-nofx" --eng "$2" --name "$name" --fx 0 --mods "" --bass 0
    r "$1-single-fx" --eng "$2" --name "$name" --fx 1 --mods "" --bass 0
    r "$1-chord6-nobass-nofx" --eng "$2" --name "$name" --fx 0 --bass 0
done
r fm6-chord4-fx --fx 1 --mods 5,4                       # MAJ + maj7 and the bass: an everyday chord
r fm6-chord6-fx-vel127 --fx 1 --vel 127
r cz-chord6-fx-vel127 --eng 14 --name "BRASS 1" --fx 1 --vel 127
# the exactness check in the other chain and at a low MASTER (the harness's chain against the firmware's own output)
r check-classic --fx 1 --out classic
r check-classic-low --fx 1 --out classic --master 300
r check-clean-low --fx 1 --out clean --master 300
# the transient test: a made signal, not a render (no exactness check: there is no firmware output to compare with)
python3 tools/master_transient.py "$O" >> "$O/render.log"
for k in kick6 kick12 kick12dry; do
    $M chain "$O/$k.raw.i32" "$O/$k" --att 0 >> "$O/render.log"
done
cat "$O/render.log"
if grep -q DIFFERS "$O/render.log"; then echo "FAIL: a chain differs from the firmware's output"; exit 1; fi

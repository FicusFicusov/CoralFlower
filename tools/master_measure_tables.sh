#!/bin/sh
# SPDX-License-Identifier: GPL-3.0-only
# MEASUREMENT ONLY (GitHub issue 3, docs/AUDIO-QUALITY.md): the tables from what tools/master_measure_run.sh rendered
# into OUTDIR: OUTDIR/table-all.txt (every render, chain and MASTER setting), OUTDIR/table-loudness.txt (Classic
# against Clean at full MASTER: level, loudness, gain reduction, distortion), and WAVs of the full-MASTER chords.
#   sh tools/master_measure_tables.sh OUTDIR
set -e
cd "$(dirname "$0")/.."
D=${1:?usage: master_measure_tables.sh OUTDIR}
python3 tools/master_analyze.py --dir "$D" '*.classic.a*.i32' '*.clean.a*.i32' | grep -v "^check-" > "$D/table-all.txt"
: > "$D/table-loudness.txt"
for n in fm6 va wheel cz; do
    for k in single-nofx single-fx chord6-nobass-nofx chord6-nofx chord6-fx; do
        python3 tools/master_analyze.py --pair "$D" "$n-$k" 0 >> "$D/table-loudness.txt"
    done
done
for k in fm6-chord4-fx fm6-chord6-fx-vel127 cz-chord6-fx-vel127 kick6 kick12 kick12dry; do
    python3 tools/master_analyze.py --pair "$D" "$k" 0 >> "$D/table-loudness.txt"
done
python3 tools/master_analyze.py --dir "$D" --wav '*-chord6-fx.classic.a00.i32' '*-chord6-fx.clean.a00.i32' > /dev/null
echo "wrote $D/table-all.txt, $D/table-loudness.txt and the WAVs"

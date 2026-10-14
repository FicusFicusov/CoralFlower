#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-only
"""Listening files for the Clean limiter's hold (docs/AUDIO-QUALITY.md "Round 3"): the same mixes through Clean with
the hold of the first two rounds (40 ms, held again within 0.3 dB) and with 0.19's (200 ms, held again within 1.2 dB,
40 ms after a hit); 24-bit stereo WAVs of the DAC word (x 2: the Q15 sample's full scale is the file's).

  python3 tools/master_listen_hold.py BEFORE NOW OUTDIR

BEFORE, NOW: directories of PREFIX.clean.a00.i32 records (tests/master_measure.c "chain" on the same PREFIX.raw.i32
mixes, built with -DCLN_HOLD=1764u -DCLN_HOLD_HIT=1764u -DCLN_REHOLD=5 -DCLN_CREEP=31 for BEFORE and as the tree is
for NOW). For each PREFIX found in both:
  PREFIX.hold-before.wav, PREFIX.hold-now.wav, and PREFIX.ab.wav: before, half a second of silence, now.
Writes OUTDIR/README.txt with each passage's dist, gain-reduction share and loudness (tools/master_analyze.py).
numpy only."""
import glob
import os
import sys
import numpy as np

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import master_analyze as ma  # noqa: E402
from master_listen import wav24  # noqa: E402

WHAT = {"va-fx": "VA SYNTH BRASS, six notes and the bass, the default sends",
        "va-fx-v127": "the same at velocity 127",
        "wheel-fx": "WHEEL FULL ORGAN, six notes and the bass, the default sends",
        "wheel-fx-v127": "the same at velocity 127",
        "cz-fx": "CZ-1 BRASS 1, six notes and the bass, the default sends",
        "cz-fx-v127": "the same at velocity 127",
        "fm6-fx-v127": "FM6 TINE EP, six notes and the bass, velocity 127",
        "va-wet": "VA, VERY WET: reverb and delay sends 120, Decay 100, Feedback 105",
        "wheel-wet": "WHEEL, very wet",
        "cz-wet": "CZ-1, very wet",
        "cz-wet-v127": "CZ-1, very wet, velocity 127",
        "fm6-wet": "FM6 TINE EP, very wet",
        "arp120": "made: sixteenth notes at 120 BPM over a held bass, 6 dB over the threshold",
        "arp180": "made: the same at 180 BPM",
        "kick6": "made: a kick 6 dB over every half second on a held bass",
        "kick12": "made: the kick 12 dB over",
        "hit12": "made: ONE hit 12 dB over on a steady pad the limiter leaves alone: how the pad comes back",
        "hit12hot": "made: the same with the pad 2 dB over the threshold"}


def main():
    before, now, out = sys.argv[1:4]
    os.makedirs(out, exist_ok=True)
    lines = ["ChoralRoot 0.19: Output Clean's limiter, the hold (tools/master_listen_hold.py; docs/AUDIO-QUALITY.md",
             "\"Round 3\"). The same mix through Clean with the hold as it was (40 ms, held again by a peak within",
             "0.3 dB) and as it is (200 ms, held again within 1.2 dB; a hit more than 3.5 dB above the envelope keeps",
             "the 40 ms). 24-bit, full MASTER. NAME.hold-before.wav, NAME.hold-now.wav, NAME.ab.wav (before, 0.5 s, now).",
             "",
             f"{'NAME':14s} {'before: dist  GR%   LKFS':>26s} {'now: dist  GR%   LKFS':>26s}   what"]
    for p in sorted(glob.glob(os.path.join(before, "*.clean.a00.i32"))):
        name = os.path.basename(p).replace(".clean.a00.i32", "")
        q = os.path.join(now, os.path.basename(p))
        if name not in WHAT or not os.path.exists(q):
            continue
        ra, rb = ma.analyze(p), ma.analyze(q)
        xa = np.fromfile(p, dtype="<i4").reshape(-1, 5)[:, 3:5] / 128.0
        xb = np.fromfile(q, dtype="<i4").reshape(-1, 5)[:, 3:5] / 128.0
        wav24(os.path.join(out, name + ".hold-before.wav"), xa)
        wav24(os.path.join(out, name + ".hold-now.wav"), xb)
        wav24(os.path.join(out, name + ".ab.wav"), np.concatenate([xa, np.zeros((22050, 2)), xb]))
        lines.append(f"{name:14s} {ra['dist']:12.1f} {100 * ra['gr_frac']:5.1f} {ra['lk']:7.2f}"
                     f" {rb['dist']:12.1f} {100 * rb['gr_frac']:5.1f} {rb['lk']:7.2f}   {WHAT[name]}")
    lines += ["",
              "dist: what the chain leaves (the limiter's gain movement faster than 50 ms, mostly), dB under the output;",
              "GR%: the share of the samples with the limiter at work; LKFS: loudness.",
              "What to listen for: on the chords and the wet passages, a faint roughness / flutter on the loudest",
              "moments in 'before' that is steadier in 'now', for 0.1 to 1 LU of level; on kick6, kick12 and hit12,",
              "that nothing is different: the level comes back after a hit as it did."]
    open(os.path.join(out, "README.txt"), "w").write("\n".join(lines) + "\n")
    print("\n".join(lines))


if __name__ == "__main__":
    main()

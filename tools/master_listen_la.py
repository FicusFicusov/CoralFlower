#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-only
"""Listening files for the Clean chain's look-ahead limiter (docs/AUDIO-QUALITY.md "The look-ahead"): the loud chords
and the transient test, through Classic, Clean before the look-ahead and Clean now; 24-bit stereo WAVs of the DAC word
(x 2: the Q15 sample's full scale is the file's), the whole render.

  python3 tools/master_listen_la.py BEFORE NOW OUTDIR [--la64 DIR] [--sloop DIR]

BEFORE, NOW: tools/master_measure_run.sh's renders of the tree before the look-ahead (f402608) and of this one (the
transient test's files kick6 / kick12 in both: tests/master_measure.c "chain"). --la64: the same from a build with
-DCLN_LA_K=2u (1.45 ms). --sloop: files of Sloop's own harness (int32 x 5 a frame, the Q15 sample itself):
sloop26-cr-NAME.a00.i32 = the same mix through Sloop 2.6's master, sloop26-NAME.a00.i32 = the transient test through
it, and Sloop's own passages (sloop26-brass-chord-fx, sloop26-gospel-chord-fx).

For each passage NAME:
  NAME.classic.wav            Output Classic
  NAME.clean-before.wav       Output Clean before the look-ahead (the limiter's gain eased down after the peak)
  NAME.clean-now.wav          Output Clean now (32 samples of look-ahead)
  NAME.before-now.ab.wav      Clean before, half a second of silence, Clean now (their loudness is the same to 0.1 LU)
  NAME.classic-now.ab.wav     Classic, silence, Clean now brought to Classic's loudness
  NAME.clean-la64.wav         (--la64) Clean with 64 samples of look-ahead
  NAME.sloop26-master.wav     (--sloop) the same mix through Sloop 2.6's master, as it comes out
  NAME.sloop26-now.ab.wav     (--sloop) that, silence, Clean now brought to its loudness
Writes OUTDIR/README.txt. numpy only."""
import os
import sys
import numpy as np

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import master_analyze as ma  # noqa: E402
from master_listen import wav24  # noqa: E402

FS = 44100
NAMES = [("va-chord6-fx", "VA SYNTH BRASS, six notes and the bass, FX on"),
         ("wheel-chord6-fx", "WHEEL FULL ORGAN, six notes and the bass, FX on"),
         ("cz-chord6-fx", "CZ-1 BRASS 1, six notes and the bass, FX on"),
         ("cz-chord6-fx-vel127", "CZ-1 BRASS 1, the same at velocity 127"),
         ("kick6", "the transient test: a kick 6 dB over Clean's threshold every half second, on a held bass"),
         ("kick12", "the transient test: the kick 12 dB over")]


def load(path, q15=False):
    d = np.fromfile(path, dtype="<i4").reshape(-1, 5).astype(np.float64)
    return d[:, 3:5] if q15 else d[:, 3:5] / 128.0


def held(x):
    a, b = ma.window()
    return x[a:b]


def lk(x):
    return ma.loudness(held(x))


def pk(x):
    return 20 * np.log10(max(np.abs(x).max(), 1e-9) / 32768.0)


def ab(a, b):
    return np.concatenate([a, np.zeros((FS // 2, 2)), b])


def main():
    args = sys.argv[1:]
    before, now, out = args[0], args[1], args[2]
    la64 = args[args.index("--la64") + 1] if "--la64" in args else None
    sloop = args[args.index("--sloop") + 1] if "--sloop" in args else None
    os.makedirs(out, exist_ok=True)
    L = ["Listening files: Options > Output Clean's limiter with its look-ahead (ChoralRoot 0.19).",
         "Made by tools/master_listen_la.py (docs/AUDIO-QUALITY.md \"The look-ahead\"): the firmware's own code on the Mac,",
         "the DAC word as the unit would send it at full MASTER. 24-bit, 44.1 kHz, stereo; the file's full scale is the",
         "Q15 sample's (the DAC's own is 6 dB above it). Each file is the whole passage: a second of silence, the",
         "attack, 4 s held, the release.",
         "",
         "  NAME.classic.wav          Output Classic (0.18's chain)",
         "  NAME.clean-before.wav     Output Clean before this round: the limiter's gain eased down over 1.5 ms AFTER a peak",
         "  NAME.clean-now.wav        Output Clean now: the output 32 samples (0.73 ms) late, the gain down BEFORE the peak",
         "  NAME.before-now.ab.wav    Clean before, half a second of silence, Clean now (the same loudness: nothing matched)",
         "  NAME.classic-now.ab.wav   Classic, silence, Clean now brought to Classic's loudness",
         "  NAME.clean-la64.wav       Clean with 64 samples of look-ahead (a build with -DCLN_LA_K=2u: not the default)",
         "  NAME.sloop26-master.wav   the SAME mix through Sloop 2.6's master (its limiter, its knee), as it comes out",
         "  NAME.sloop26-now.ab.wav   that, silence, Clean now brought to its loudness",
         "  sloop26-*.wav             Sloop 2.6 playing its own sounds at its own levels (its harness, MASTER full)",
         "",
         "What to listen for. On the chords the two Clean files differ very little: the look-ahead takes away the",
         "overshoot into the knee, which on a held chord was already small. On the kicks the difference is plain: before,",
         "the first 1.5 ms of each hit went through the knee (a tick or a crunch on the hit); now the hit is turned down",
         "as a whole. The limiter's pumping after a hit (the level coming back over a fifth of a second) is the same in",
         "both: it is the hold and the release, which this round did not change.",
         "",
         "Level, loudness (K-weighted, the held 4 s) and peak of each file (dB re the file's full scale):",
         ""]
    for name, what in NAMES:
        c = load(os.path.join(now, f"{name}.classic.a00.i32"))
        b = load(os.path.join(before, f"{name}.clean.a00.i32"))
        n = load(os.path.join(now, f"{name}.clean.a00.i32"))
        files = [("classic", c), ("clean-before", b), ("clean-now", n)]
        wav24(os.path.join(out, f"{name}.before-now.ab.wav"), ab(b, n))
        wav24(os.path.join(out, f"{name}.classic-now.ab.wav"), ab(c, n * 10 ** ((lk(c) - lk(n)) / 20)))
        if la64 and os.path.exists(os.path.join(la64, f"{name}.clean.a00.i32")):
            files.append(("clean-la64", load(os.path.join(la64, f"{name}.clean.a00.i32"))))
        if sloop:
            p = os.path.join(sloop, f"sloop26-cr-{name}.a00.i32")
            if not os.path.exists(p):
                p = os.path.join(sloop, f"sloop26-{name}.a00.i32")
            if os.path.exists(p):
                s = load(p, q15=True)
                files.append(("sloop26-master", s))
                wav24(os.path.join(out, f"{name}.sloop26-now.ab.wav"), ab(s, n * 10 ** ((lk(s) - lk(n)) / 20)))
        L.append(f"{name}: {what}")
        for tag, x in files:
            wav24(os.path.join(out, f"{name}.{tag}.wav"), x)
            L.append(f"    {tag:16s} loudness {lk(x):+6.1f} LKFS, peak {pk(x):+5.1f} dB")
        L.append("")
    if sloop:
        L.append("Sloop 2.6 itself (a comparable sound where it has one; its part level and trims, not ChoralRoot's):")
        for f, what in (("sloop26-brass-chord-fx", "PHASE CZ BRASS, six notes, and ANALOG SUB BASS, FX as the sounds load"),
                        ("sloop26-gospel-chord-fx", "WHEEL GOSPEL, six notes, and ANALOG SUB BASS"),
                        ("sloop26-synbrass-chord-fx", "ANALOG SYN BRASS, six notes, and ANALOG SUB BASS")):
            p = os.path.join(sloop, f + ".a00.i32")
            if os.path.exists(p):
                x = load(p, q15=True)
                wav24(os.path.join(out, f + ".wav"), x)
                L.append(f"    {f + '.wav':34s} {what}: loudness {lk(x):+6.1f} LKFS, peak {pk(x):+5.1f} dB")
        L.append("")
    with open(os.path.join(out, "README.txt"), "w") as fh:
        fh.write("\n".join(L) + "\n")
    print("\n".join(L))


if __name__ == "__main__":
    main()

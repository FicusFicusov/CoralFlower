#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-only
"""MEASUREMENT ONLY (docs/AUDIO-QUALITY.md "The look-ahead"): the transient test signal for the output chain's limiter:
a kick-like hit every 500 ms on a held bass note, as a mix for tests/master_measure.c's "chain" (int32 L R, the mix's
units: x 0.75 is the Clean chain's Q15 sample, whose limiter threshold is 26000; at full MASTER it is Classic's own).

  python3 tools/master_transient.py OUTDIR      writes OUTDIR/kick6.raw.i32, kick12.raw.i32, kick12dry.raw.i32
  build/host/master_measure chain OUTDIR/kick6.raw.i32 OUTDIR/kick6
  python3 tools/master_analyze.py OUTDIR/kick6.clean.a00.i32 OUTDIR/kick6.classic.a00.i32

The bass: 55 Hz and its first five harmonics (1 / k), its peak 3 dB under Clean's threshold. The kick: a sine falling
from 160 to 48 Hz (25 ms), 0.5 ms of attack, a decay of 120 ms, and 3 ms of high-passed noise on the attack; its peak
6 dB (kick6) or 12 dB (kick12) over Clean's threshold; kick12dry is the kick without the bass. The length and the
window are master_measure's render (the hits start where its note-on is, 1060 ms). numpy only."""
import os
import sys
import numpy as np

FS = 44100
N = 289280
ON = int(1060 * FS / 1000)
T_RAW = 26000.0 / 0.75                  # Clean's threshold in the mix's units


def signal(over_db, bass=True):
    t = np.arange(N) / FS
    x = np.zeros(N)
    if bass:
        b = sum(np.sin(2 * np.pi * 55.0 * k * t) / k for k in range(1, 7))
        b *= T_RAW * 10 ** (-3 / 20) / np.abs(b).max()
        env = np.clip((np.arange(N) - ON) / (0.01 * FS), 0, 1) * np.clip((ON + 4 * FS - np.arange(N)) / (0.05 * FS), 0, 1)
        x += b * env
    rng = np.random.default_rng(7)
    n = int(0.6 * FS)
    tk = np.arange(n) / FS
    f = 48.0 + 112.0 * np.exp(-tk / 0.025)
    ph = 2 * np.pi * np.cumsum(f) / FS
    k = np.sin(ph) * np.minimum(tk / 0.0005, 1.0) * np.exp(-tk / 0.12)
    nz = rng.standard_normal(n)
    nz = nz - np.convolve(nz, np.ones(8) / 8, mode="same")          # high-passed
    k += 0.35 * nz * np.exp(-tk / 0.001) * (tk < 0.003)
    k *= T_RAW * 10 ** (over_db / 20) / np.abs(k).max()
    for h in range(8):
        a = ON + h * (FS // 2)
        x[a:a + n] += k[:max(0, min(n, N - a))]
    return x


def main():
    out = sys.argv[1]
    os.makedirs(out, exist_ok=True)
    for name, db, bass in (("kick6", 6, True), ("kick12", 12, True), ("kick12dry", 12, False)):
        x = np.rint(signal(db, bass)).astype("<i4")
        np.stack([x, x], axis=1).tofile(os.path.join(out, name + ".raw.i32"))
        print(f"{name}: peak {20 * np.log10(np.abs(x).max() / T_RAW):+.1f} dB re Clean's threshold")


if __name__ == "__main__":
    main()

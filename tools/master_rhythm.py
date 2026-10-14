#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-only
"""MEASUREMENT ONLY (docs/AUDIO-QUALITY.md "Round 3: the hold"): rhythmic test signals for the Clean chain's limiter,
and what its gain does on them. Made signals, as tools/master_transient.py's (int32 L R in the mix's units, for
tests/master_measure.c's "chain"), the same length and note-on.

  python3 tools/master_rhythm.py OUTDIR          writes OUTDIR/arp120.raw.i32, arp180.raw.i32, hit12.raw.i32,
                                                 hit12hot.raw.i32
  build/host/master_measure chain OUTDIR/hit12.raw.i32 OUTDIR/hit12
  python3 tools/master_rhythm.py --gain OUTDIR/hit12.clean.a00.i32 [..]      the gain's table (below)

  arp120, arp180   a held bass (55 Hz and five harmonics, its peak 3 dB under Clean's threshold) and sixteenth notes
                   at 120 / 180 BPM over it (A minor 7th, two octaves up and down; a pluck: 2 ms of attack, a decay
                   of 150 ms, harmonics 1 / k to 8 kHz), the mix's peak 6 dB over the threshold
  hit12            a steady pad (six notes of two detuned saws each, its peak 4 dB UNDER the threshold: the limiter
                   idle on it) and ONE hit 12 dB over the threshold (master_transient's kick) a second after the
                   note-on: how long the pad stays turned down after it
  hit12hot         the same with the pad 2 dB OVER the threshold (the limiter at work on the pad itself)

--gain prints, for each record: the limiter's gain before the hit (its median over the half second before, dB), the
gain 100 / 250 / 500 / 1000 / 2000 ms after the hit relative to that, the time until it is back within 1 dB and
within 0.5 dB of it for good ("rec1", "rec.5": the recovery), the mean dip over the second after the hit ("dip1s":
the pad's loss of level there, dB; the pad is steady, so that is its loss of loudness in LU), and over the whole held
part the gain's range (max - min of the gain smoothed over 50 ms, dB: "swing", how far the level is moved) and its
standard deviation ("sd"). For the arps the hit columns are of the note at 1 s. numpy only."""
import os
import sys
import numpy as np

FS = 44100
N = 289280
ON = int(1060 * FS / 1000)
HIT = ON + FS                           # the single hit: a second after the note-on
T_RAW = 26000.0 / 0.75                  # Clean's threshold in the mix's units


def held(n=N):
    i = np.arange(n)
    return np.clip((i - ON) / (0.01 * FS), 0, 1) * np.clip((ON + 4 * FS - i) / (0.05 * FS), 0, 1)


def bass():
    t = np.arange(N) / FS
    b = sum(np.sin(2 * np.pi * 55.0 * k * t) / k for k in range(1, 7))
    return b * (T_RAW * 10 ** (-3 / 20) / np.abs(b).max()) * held()


def pluck(f, n):
    t = np.arange(n) / FS
    x = sum(np.sin(2 * np.pi * f * k * t) / k for k in range(1, int(8000 / f) + 1))
    return x * np.minimum(t / 0.002, 1.0) * np.exp(-t / 0.15)


def arp(bpm):
    step = int(FS * 60 / bpm / 4)
    notes = [57, 60, 64, 67, 69, 72, 76, 79, 76, 72, 69, 67, 64, 60]
    x = np.zeros(N)
    n = int(0.6 * FS)
    k = 0
    a = ON
    while a + n < ON + 4 * FS:
        f = 440.0 * 2 ** ((notes[k % len(notes)] - 69) / 12)
        x[a:a + n] += pluck(f, n)
        a += step
        k += 1
    b = bass()
    lo, hi = 0.0, 8.0 * T_RAW                     # the arp's level for a mix 6 dB over the threshold
    for _ in range(40):
        m = (lo + hi) / 2
        if np.abs(b + x * m / np.abs(x).max()).max() > T_RAW * 10 ** (6 / 20):
            hi = m
        else:
            lo = m
    return b + x * lo / np.abs(x).max()


def kick(over_db):
    rng = np.random.default_rng(7)
    n = int(0.6 * FS)
    tk = np.arange(n) / FS
    f = 48.0 + 112.0 * np.exp(-tk / 0.025)
    k = np.sin(2 * np.pi * np.cumsum(f) / FS) * np.minimum(tk / 0.0005, 1.0) * np.exp(-tk / 0.12)
    nz = rng.standard_normal(n)
    nz = nz - np.convolve(nz, np.ones(8) / 8, mode="same")
    k += 0.35 * nz * np.exp(-tk / 0.001) * (tk < 0.003)
    return k * (T_RAW * 10 ** (over_db / 20) / np.abs(k).max())


def pad(db_re_t):
    t = np.arange(N) / FS
    x = np.zeros(N)
    for m in (50, 54, 57, 59, 61, 64):
        f = 440.0 * 2 ** ((m - 69) / 12)
        for det in (0.997, 1.003):
            x += sum(np.sin(2 * np.pi * f * det * k * t + m * k) / k for k in range(1, int(6000 / f) + 1))
    w = slice(ON + FS // 10, ON + 4 * FS)
    return x * (T_RAW * 10 ** (db_re_t / 20) / np.abs(x[w]).max()) * held()


def hit(pad_db):
    x = pad(pad_db)
    k = kick(12)
    x[HIT:HIT + len(k)] += k
    return x


def gain_table(paths):
    print(f"{'record':34s} {'rest':>6s} {'+100':>6s} {'+250':>6s} {'+500':>6s} {'+1000':>6s} {'+2000':>6s} "
          f"{'rec1':>6s} {'rec.5':>6s} {'dip1s':>6s} {'swing':>6s} {'sd':>6s}")
    for p in paths:
        d = np.fromfile(p, dtype="<i4").reshape(-1, 5)
        g = 20 * np.log10(np.maximum(d[:, 2], 1) / 32768.0)
        rest = float(np.median(g[HIT - FS // 2:HIT]))
        at = [float(g[HIT + int(ms * FS / 1000)] - rest) for ms in (100, 250, 500, 1000, 2000)]

        def rec(tol):
            low = np.nonzero(g[HIT:ON + 4 * FS] < rest - tol)[0]
            return 0.0 if not len(low) else (low[-1] + 1) * 1000.0 / FS
        dip = float((g[HIT:HIT + FS] - rest).mean())
        n = int(0.05 * FS) | 1
        h = np.hanning(n)
        h /= h.sum()
        a, b = ON + int(0.1 * FS), ON + 4 * FS
        gs = np.convolve(g[a - n // 2:b + n // 2], h, mode="valid")
        name = os.path.basename(p).replace(".i32", "")
        print(f"{name:34s} {rest:+6.2f} " + " ".join(f"{v:+6.2f}" for v in at)
              + f" {rec(1.0):6.0f} {rec(0.5):6.0f} {dip:+6.2f} {gs.max() - gs.min():6.2f} {gs.std():6.2f}")


def main():
    if len(sys.argv) > 2 and sys.argv[1] == "--gain":
        gain_table(sys.argv[2:])
        return
    out = sys.argv[1]
    os.makedirs(out, exist_ok=True)
    for name, x in (("arp120", arp(120)), ("arp180", arp(180)), ("hit12", hit(-4)), ("hit12hot", hit(2))):
        x = np.rint(x).astype("<i4")
        np.stack([x, x], axis=1).tofile(os.path.join(out, name + ".raw.i32"))
        print(f"{name}: peak {20 * np.log10(np.abs(x).max() / T_RAW):+.1f} dB re Clean's threshold")


if __name__ == "__main__":
    main()

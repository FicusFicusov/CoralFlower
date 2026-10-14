#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-only
"""MEASUREMENT ONLY (GitHub issue 3, docs/AUDIO-QUALITY.md): reads what tests/master_measure.c wrote and prints what
the output chain (fx.c: Classic = 0.18's DC blocker, limiter, soft clip; Clean = 0.19's) did to the held part of the
render.

  python3 tools/master_analyze.py FILE.i32 [FILE.i32 ..]        one table row per PREFIX.<chain>.aNN.i32
  python3 tools/master_analyze.py --wav FILE.i32 ..             also FILE.out.wav and FILE.resid+20dB.wav
  python3 tools/master_analyze.py --pair DIR NAME ATT           Classic against Clean for one render: the level and
                                                                loudness of each and the difference
  options, in this order, before the names: --dir DIR (names and patterns inside DIR), --win A,B (ms after the
  note-on instead of the whole held chord), then --wav

Window: the held chord (note-on + 20 ms .. release), frames from the constants below (master_measure's defaults:
note-on at 1060 ms, 4000 ms held). numpy only.

Columns
  pk, rms      the chain's input (after the DC blocker): peak and RMS in dB relative to the chain's limiter threshold
               (Classic LIM_T 18000, Clean CLN_T 26000; in Clean the input does not depend on MASTER)
  GR%          share of the samples with the limiter's gain below 1
  GRavg/max    the gain reduction in dB: its mean over the window, its deepest
  opk, orms    the output: peak and RMS, dB re full scale of the Q15 sample (32768; the DAC word's own full scale is
               6 dB above that)
  LK           the output's loudness, K-weighted (ITU-R BS.1770's two filters applied in the frequency domain, both
               channels summed, no gating), LKFS re the Q15 full scale
  fix          residual of out - k * in, k one fixed gain (least squares), dB relative to the output: gain riding
               (plain compression) AND distortion AND the arithmetic's noise
  dist         residual of out - k * in * slow(g): the limiter's gain smoothed over 50 ms (what a compressor may do
               inaudibly) taken out: what is left is the gain's audio-rate movement, the clipper's curve and the
               arithmetic (truncations, the word length at a low MASTER); overall and in four bands (dB relative to
               the whole output)
  lim, clip    that residual split: the limiter's fast gain movement alone, the clip stage (and arithmetic) alone.
               clip is the chain's only non-linear part: lim is a gain that moves (a level change, however fast)
  in-band      dist in each band relative to the output's own content in that band
  g>1k         the limiter's gain itself: its spectrum above 1 kHz, dB re a gain of 1 (how much of the gain's
               movement is at audio rate: corners, steps)
Clean's output is CLN_LA samples late (the limiter's look-ahead); master_measure.c writes the input as late, so the
records stay sample for sample.
"""
import sys
import numpy as np

FS = 44100
THRESH = {"classic": 18000.0, "clean": 26000.0}
ON = int(1060 * FS / 1000)
HOLD_MS = 4000
BANDS = [(0, 300), (300, 2000), (2000, 8000), (8000, FS / 2)]


def db(x):
    return 10 * np.log10(max(float(x), 1e-30))


WIN = None                              # --win A,B: ms after the note-on instead of the whole held chord


def window(hold_ms=HOLD_MS):
    if WIN:
        return ON + int(WIN[0] * FS / 1000), ON + int(WIN[1] * FS / 1000)
    return ON + int(0.02 * FS), ON + int(hold_ms * FS / 1000)


def spectrum(x):
    """power spectrum of x (frames, 2), Hann window, the two channels summed"""
    n = len(x)
    w = np.hanning(n)[:, None]
    X = np.fft.rfft(x * w, axis=0)
    return np.fft.rfftfreq(n, 1 / FS), (np.abs(X) ** 2).sum(axis=1)


def bands_power(x):
    f, p = spectrum(x)
    return [p[(f >= lo) & (f < hi)].sum() for lo, hi in BANDS], p.sum(), f, p


def kweight(f):
    """|H|^2 of BS.1770's shelf and high-pass at 44.1 kHz (their analogue prototypes, bilinear at this rate)"""
    def biquad(f0, q, kind, gdb=0.0):
        k = np.tan(np.pi * f0 / FS)
        if kind == "shelf":
            vh = 10 ** (gdb / 20)
            vb = vh ** 0.4996667741545416
            a0 = 1 + k / q + k * k
            b = [(vh + vb * k / q + k * k) / a0, 2 * (k * k - vh) / a0, (vh - vb * k / q + k * k) / a0]
            a = [1.0, 2 * (k * k - 1) / a0, (1 - k / q + k * k) / a0]
        else:
            a0 = 1 + k / q + k * k
            b = [1 / a0, -2 / a0, 1 / a0]
            a = [1.0, 2 * (k * k - 1) / a0, (1 - k / q + k * k) / a0]
        z = np.exp(-2j * np.pi * f / FS)
        return np.abs((b[0] + b[1] * z + b[2] * z * z) / (a[0] + a[1] * z + a[2] * z * z)) ** 2
    return biquad(1681.974450955533, 0.7071752369554196, "shelf", 3.999843853973347) * \
        biquad(38.13547087602444, 0.5003270373238773, "hp")


def loudness(x):
    """LKFS re Q15 full scale (the mean square of each K-weighted channel, summed)"""
    n = len(x)
    X = np.fft.rfft(x, axis=0)
    f = np.fft.rfftfreq(n, 1 / FS)
    p = (np.abs(X) ** 2) * kweight(f)[:, None]
    p[1:-1] *= 2
    ms = p.sum() / (n * n)
    return -0.691 + db(ms / 32768.0 ** 2)


def ls_gain(y, x):
    return float((y * x).sum() / max((x * x).sum(), 1e-30))


def mode_of(path):
    return "clean" if ".clean." in path else "classic"


def analyze(path, hold_ms=HOLD_MS):
    t = THRESH[mode_of(path)]
    d = np.fromfile(path, dtype="<i4").reshape(-1, 5).astype(np.float64)
    a, b = window(hold_ms)
    d = d[a:b]
    x = d[:, 0:2] / 64.0                # the chain's input, nothing lost, after the DC blocker
    g = d[:, 2] / 32768.0
    y = d[:, 3:5] / 128.0               # the DAC word in Q15 units
    lim = x * g[:, None]                # after the limiter, into the clip stage
    r = {}
    r["pk"] = 20 * np.log10(max(np.abs(x).max(), 1e-9) / t)
    r["rms"] = 10 * np.log10(max((x ** 2).mean(), 1e-9) / t ** 2)
    r["gr_frac"] = float((g < 1).mean())
    r["gr_avg"] = float((20 * np.log10(g)).mean())
    r["gr_max"] = float((20 * np.log10(g)).min())
    r["opk"] = 20 * np.log10(max(np.abs(y).max(), 1e-9) / 32768.0)
    r["orms"] = 10 * np.log10(max((y ** 2).mean(), 1e-9) / 32768.0 ** 2)
    r["lk"] = loudness(y)
    yb, yt, f, p = bands_power(y)
    r["sig_bands"] = [db(v / yt) for v in yb]
    r["above"] = [db(p[f >= c].sum() / yt) for c in (2000, 5000, 10000)]
    k = ls_gain(y, x)
    e = y - k * x
    eb, et, _, _ = bands_power(e)
    r["fix"] = db(et / yt)
    n = int(0.05 * FS) | 1
    h = np.hanning(n)
    h /= h.sum()
    gs = np.convolve(np.pad(g, n // 2, mode="edge"), h, mode="valid")
    xs = x * gs[:, None]
    k2 = ls_gain(y, xs)
    e = y - k2 * xs
    eb, et, _, _ = bands_power(e)
    r["dist"] = db(et / yt)
    r["dist_bands"] = [db(v / yt) for v in eb]
    r["dist_inband"] = [db(v / max(s, 1e-30)) for v, s in zip(eb, yb)]
    r["resid"] = e
    k3 = ls_gain(lim, xs)
    _, lt, _, _ = bands_power(lim - k3 * xs)
    _, limt, _, _ = bands_power(lim)
    r["lim_only"] = db(lt / max(limt, 1e-30))
    k4 = ls_gain(y, lim)
    _, st, _, _ = bands_power(y - k4 * lim)
    r["clip_only"] = db(st / yt)
    gw = (g - g.mean()) * np.hanning(len(g))
    gp = np.abs(np.fft.rfft(gw)) ** 2
    gf = np.fft.rfftfreq(len(g), 1 / FS)
    r["g_hf"] = db(gp[gf >= 1000].sum() / ((np.hanning(len(g)) ** 2).sum() * len(g) / 2))
    r["x"], r["y"], r["k"] = x, y, k2
    return r


def wav(path, x):
    x = np.clip(np.rint(x), -32768, 32767).astype("<i2")
    n = len(x)
    with open(path, "wb") as f:
        f.write(b"RIFF" + np.uint32(36 + n * 4).tobytes() + b"WAVEfmt " + np.uint32(16).tobytes())
        f.write(np.uint16(1).tobytes() + np.uint16(2).tobytes() + np.uint32(FS).tobytes() + np.uint32(FS * 4).tobytes())
        f.write(np.uint16(4).tobytes() + np.uint16(16).tobytes() + b"data" + np.uint32(n * 4).tobytes())
        f.write(x.tobytes())


HEAD = (f"{'render':38s} {'pk':>6s} {'rms':>6s} {'GR%':>5s} {'GRavg':>6s} {'GRmax':>6s} {'opk':>6s} {'orms':>6s} "
        f"{'LK':>6s} {'fix':>6s} {'dist':>6s} {'<300':>6s} {'-2k':>6s} {'-8k':>6s} {'>8k':>6s} {'lim':>6s} {'clip':>6s}"
        f"  | out >2k    >5k   >10k | dist in-band: <300    -2k    -8k    >8k |   g>1k")


def row(p):
    r = analyze(p)
    name = p.split("/")[-1].replace(".i32", "")
    return (f"{name:38s} {r['pk']:+6.1f} {r['rms']:+6.1f} {100 * r['gr_frac']:5.1f} {r['gr_avg']:+6.2f} "
            f"{r['gr_max']:+6.2f} {r['opk']:+6.1f} {r['orms']:+6.1f} {r['lk']:+6.1f} {r['fix']:+6.1f} {r['dist']:+6.1f} "
            + " ".join(f"{v:+6.1f}" for v in r["dist_bands"]) + f" {r['lim_only']:+6.1f} {r['clip_only']:+6.1f}  |   "
            + " ".join(f"{v:+6.1f}" for v in r["above"]) + " |              "
            + " ".join(f"{v:+6.1f}" for v in r["dist_inband"]) + f" | {r['g_hf']:+6.1f}"), r


def main():
    import glob
    import os
    args = sys.argv[1:]
    if args and args[0] == "--pair":
        d, name, att = args[1], args[2], int(args[3])
        a = analyze(os.path.join(d, f"{name}.classic.a{att:02d}.i32"))
        b = analyze(os.path.join(d, f"{name}.clean.a{att:02d}.i32"))
        print(f"{name:28s} a{att:02d}  Classic: pk {a['opk']:+6.1f} rms {a['orms']:+6.1f} LK {a['lk']:+6.1f} GR {100 * a['gr_frac']:5.1f} % "
              f"dist {a['dist']:+6.1f} clip {a['clip_only']:+6.1f} | Clean: pk {b['opk']:+6.1f} rms {b['orms']:+6.1f} LK {b['lk']:+6.1f} "
              f"GR {100 * b['gr_frac']:5.1f} % dist {b['dist']:+6.1f} clip {b['clip_only']:+6.1f} | Clean - Classic: rms {b['orms'] - a['orms']:+5.2f} dB, "
              f"loudness {b['lk'] - a['lk']:+5.2f} LU")
        return
    if args and args[0] == "--dir":              # --dir DIR: the names that follow are in DIR, patterns allowed
        base = args[1]
        out = []
        for a in args[2:]:
            if a.startswith("--") or (out and out[-1] == "--win"):
                out.append(a)
            else:
                out += sorted(glob.glob(os.path.join(base, a))) or [os.path.join(base, a)]
        args = out
    if args and args[0] == "--win":
        global WIN
        WIN = [float(v) for v in args[1].split(",")]
        args = args[2:]
    mkwav = False
    if args and args[0] == "--wav":
        mkwav = True
        args = args[1:]
    print(HEAD)
    for p in args:
        line, r = row(p)
        print(line)
        if mkwav:
            base = p.replace(".i32", "")
            wav(base + ".out.wav", r["y"])
            wav(base + ".resid+20dB.wav", r["resid"] * 10)


if __name__ == "__main__":
    main()

#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-only
"""Two builds' renders of the same material, side by side: is the sound the same?

    python3 tools/fx_cmp.py BEFORE_DIR AFTER_DIR [name ...]

For every WAV the two directories share (16-bit, as tests/cr_fx_test.c and tools/emu/fx_review.py write them): each
one's level, the after's against the before's in dB, the residual of (after - before) once the levels are matched (dB
under the before: -inf is sample for sample), and the widest gap between their third-octave spectra from 50 Hz to
16 kHz. A reverb whose lines swing never repeats sample for sample: its residual says little, its spectrum and its
level say whether it is the same room.
"""
import math, os, struct, sys, wave
import numpy as np

def rd(p):
    with wave.open(p, 'rb') as w:
        n, ch, fs = w.getnframes(), w.getnchannels(), w.getframerate()
        d = np.frombuffer(w.readframes(n), dtype='<i2').astype(np.float64)
    return d.reshape(-1, ch), fs

def db(x):
    return 20 * math.log10(x) if x > 0 else float('-inf')

def bands(x, fs):
    """third-octave powers of the mono sum, 50 Hz .. 16 kHz"""
    m = x.sum(axis=1)
    n = 1 << int(math.ceil(math.log2(len(m))))
    s = np.abs(np.fft.rfft(m, n)) ** 2
    f = np.fft.rfftfreq(n, 1.0 / fs)
    out = []
    c = 50.0
    while c <= 16000:
        lo, hi = c / 2 ** (1 / 6), c * 2 ** (1 / 6)
        out.append((c, s[(f >= lo) & (f < hi)].sum()))
        c *= 2 ** (1 / 3)
    return out

def main():
    a_dir, b_dir = sys.argv[1], sys.argv[2]
    names = sys.argv[3:] or sorted(f for f in os.listdir(a_dir) if f.endswith('.wav') and os.path.exists(os.path.join(b_dir, f)))
    print('%-28s %9s %9s %8s %10s %s' % ('file', 'before', 'after', 'level', 'residual', 'widest third-octave gap'))
    for f in names:
        a, fs = rd(os.path.join(a_dir, f))
        b, _ = rd(os.path.join(b_dir, f))
        n = min(len(a), len(b))
        a, b = a[:n], b[:n]
        ra, rb = math.sqrt((a ** 2).mean()), math.sqrt((b ** 2).mean())
        g = ra / rb if rb else 1.0
        res = math.sqrt(((b * g - a) ** 2).mean())
        ba, bb = bands(a, fs), bands(b * g, fs)
        tot = sum(p for _, p in ba)
        gap = max(((abs(10 * math.log10(q / p)), c) for (c, p), (_, q) in zip(ba, bb) if p > tot * 1e-5 and q > 0),
                  default=(0.0, 0.0))                # (bands within 50 dB of the whole)
        print('%-28s %7.1f   %7.1f   %+6.2f dB %7.1f dB   %.2f dB at %.0f Hz' %
              (f, ra, rb, db(rb / ra) if ra else 0.0, db(res / ra) if ra else 0.0, gap[0], gap[1]))

if __name__ == '__main__':
    main()

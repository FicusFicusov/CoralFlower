#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-only
"""The half-band pair of firmware/src/cr_fx.c (docs/FX.md "The half rate"): the coefficients of the two-path
polyphase all-pass filter that takes the delay's and the reverb's sends from 44.1 to 22.05 kHz and their wet back.

  python3 tools/fx_halfband.py            # the search, then the Q13 coefficients cr_fx.c holds and their response

H(z) = (A0(z^2) + z^-1 A1(z^2)) / 2, each path a cascade of first-order all-passes (a + z^-2) / (1 + a z^-2): one
multiply a section, and run at the low rate (the decimator takes the even samples into A0 and the odd ones into A1;
the interpolator's two paths are the even and the odd output). A coordinate search minimises the largest stop-band
gain above fs / 2 - FPASS; the pass band's ripple follows from it (the two paths are power complementary).
"""
import sys

import numpy as np

FS = 44100.0
w = np.linspace(0, np.pi, 8193)
z2 = np.exp(-2j * w)


def response(c0, c1):
    a0 = np.ones_like(z2)
    a1 = np.ones_like(z2)
    for a in c0:
        a0 = a0 * (a + z2) / (1 + a * z2)
    for a in c1:
        a1 = a1 * (a + z2) / (1 + a * z2)
    return 0.5 * (a0 + np.exp(-1j * w) * a1)


def stop(p, n0, ws):
    return np.abs(response(p[:n0], p[n0:]))[w >= ws].max()


def search(n0, n1, fpass, seed=1):
    ws = np.pi * (1 - 2 * fpass / FS)
    rng = np.random.default_rng(seed)
    best = None
    for _ in range(40):
        p = np.sort(rng.uniform(0.02, 0.95, n0 + n1))
        p = np.concatenate([p[0::2][:n0], p[1::2][:n1]])
        c, step = stop(p, n0, ws), 0.1
        while step > 1e-6:
            better = False
            for i in range(len(p)):
                for s in (step, -step):
                    q = p.copy()
                    q[i] = min(0.999, max(0.001, q[i] + s))
                    cq = stop(q, n0, ws)
                    if cq < c:
                        p, c, better = q, cq, True
            for _ in range(24):                 # .. and along random directions (a minimax ridge is not axis-aligned)
                q = np.clip(p + rng.normal(0, step, len(p)), 0.001, 0.999)
                cq = stop(q, n0, ws)
                if cq < c:
                    p, c, better = q, cq, True
            if not better:
                step *= 0.7
        if best is None or c < best[0]:
            best = (c, p.copy())
    return best


def report(q13, n0, fpass):
    q = np.array(q13) / 8192.0
    h = np.abs(response(q[:n0], q[n0:]))
    ws = np.pi * (1 - 2 * fpass / FS)
    db = 20 * np.log10(np.maximum(h, 1e-9))
    f = w * FS / (2 * np.pi)
    f3 = f[np.argmax(db < -3.0)]
    print("  Q13 %s: stop band (above %.0f Hz) %.1f dB, pass band to %.0f Hz %.4f dB, -3 dB at %.0f Hz" %
          (list(q13), FS / 2 - fpass, db[w >= ws].max(), fpass, db[f <= fpass].min(), f3))
    return db, f


if __name__ == "__main__":
    for n0, n1, fp in ((1, 1, 8000), (2, 1, 8500), (2, 2, 9000), (2, 2, 9500)):
        c, p = search(n0, n1, fp)
        q13 = [int(round(x * 8192)) for x in p]
        print("%d + %d sections, pass band %.0f Hz: stop %.1f dB, coefficients %s" % (n0, n1, fp, 20 * np.log10(c), np.round(p, 6)))
        report(q13, n0, fp)
    if len(sys.argv) > 1:
        report([int(x) for x in sys.argv[1:]], (len(sys.argv) - 1 + 1) // 2, 9000)

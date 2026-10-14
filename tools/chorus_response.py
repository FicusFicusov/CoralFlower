#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-only
"""MEASUREMENT ONLY (docs/FX.md "The chorus, stereo"; docs/AUDIO-QUALITY.md "Round 3"): the comb of dry + chorus for
0.18's mono return and 0.19's two taps, computed from the firmware's own numbers (fx.c fx_buses, cr_fx.c xc_block):
a tap reads the send 400 + (1 + sin) / 2 * 6 * DEPTH samples back and returns at the send's gain g = SEND * 258 / 32768
beside the dry sound at 1. Left's tap is 0.18's; right's is the LFO a part of a turn ahead (Width 127: a quarter).

  python3 tools/chorus_response.py [--depth 60] [--csv OUT.csv]

For the send at its default (45) and at full (127), and for: mono (0.18), quadrature (0.19, Width 127), half a turn
apart (Sloop 2.6's):
  worst      the deepest notch and the highest peak of |dry + wet| at any moment of the LFO's turn, 100 Hz .. 10 kHz,
             in the left ear, the right ear and the mono sum (L + R) / 2
  mean       the same of the response averaged over the LFO's turn (power): what a long note's spectrum keeps
  deep       the share of the band (log frequency) and of the LFO's turn in which the mono sum is more than 3 dB down
  corr       the two ears' wet against each other, averaged over the band and the turn (1: the same signal, 0: unrelated)
  swing      how far the mono sum's own delay (the mean of the two taps) still moves, as a part of one tap's: what
             is left of the chorus's motion on one speaker
numpy only."""
import sys
import numpy as np

FS = 44100.0


def taps(ph, depth, turn):
    d = 6.0 * depth
    a = 400.0 + (1.0 + np.sin(ph)) / 2.0 * d
    b = 400.0 + (1.0 + np.sin(ph + 2 * np.pi * turn)) / 2.0 * d
    return a, b


def run(send, depth, turn, csv=None):
    g = send * 258 / 32768.0
    f = np.geomspace(100.0, 10000.0, 6000)
    w = 2 * np.pi * f / FS
    ph = np.linspace(0, 2 * np.pi, 720, endpoint=False)
    a, b = taps(ph[:, None], depth, turn)
    hl = 1.0 + g * np.exp(-1j * w[None, :] * a)
    hr = 1.0 + g * np.exp(-1j * w[None, :] * b)
    hm = (hl + hr) / 2.0
    out = {}
    for name, h in (("L", hl), ("R", hr), ("L+R", hm)):
        p = np.abs(h) ** 2
        mean = p.mean(axis=0)
        out[name] = (10 * np.log10(p.min()), 10 * np.log10(p.max()), 10 * np.log10(mean.min()), 10 * np.log10(mean.max()))
    out["deep"] = float((np.abs(hm) ** 2 < 10 ** (-0.3)).mean())
    out["corr"] = float(np.cos(w[None, :] * (a - b)).mean())
    out["swing"] = float(np.ptp((a + b) / 2.0) / max(np.ptp(a), 1e-9))
    if csv:
        m = 10 * np.log10(np.stack([(np.abs(h) ** 2).mean(axis=0) for h in (hl, hr, hm)]))
        np.savetxt(csv, np.column_stack([f, m.T]), delimiter=",", header="Hz,L,R,mono", comments="")
    return out


def main():
    depth = 60
    csv = None
    a = sys.argv[1:]
    while a:
        if a[0] == "--depth":
            depth = int(a[1])
        elif a[0] == "--csv":
            csv = a[1]
        a = a[2:]
    print(f"dry + chorus, Depth {depth} (the taps 400 .. {400 + 6 * depth} samples back: {400 / 44.1:.1f} .. "
          f"{(400 + 6 * depth) / 44.1:.1f} ms), 100 Hz .. 10 kHz; dB re the dry sound")
    for send in (45, 127):
        print(f"\nSend {send} (the wet at {20 * np.log10(send * 258 / 32768.0):+.1f} dB re the dry)")
        print(f"  {'':22s} {'worst notch / peak':>31s}   {'mean over the LFO turn: min / max':>36s}   deep   corr  swing")
        for name, turn in (("mono (0.18)", 0.0), ("quadrature (0.19)", 0.25), ("half a turn (Sloop)", 0.5)):
            r = run(send, depth, turn, csv if (send == 45 and turn == 0.25) else None)
            for ch in ("L", "R", "L+R"):
                if turn == 0.0 and ch != "L":
                    continue
                v = r[ch]
                tail = f"  {100 * r['deep']:4.0f}%  {r['corr']:+.2f}  {r['swing']:.2f}" if ch in ("L+R",) or turn == 0.0 else ""
                lab = "both ears and the sum" if turn == 0.0 else ch
                print(f"  {name:20s} {lab:>21s}  {v[0]:+7.1f} / {v[1]:+5.1f}      {v[2]:+7.2f} / {v[3]:+5.2f}            {tail}")


if __name__ == "__main__":
    main()

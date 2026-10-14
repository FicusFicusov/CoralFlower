#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-only
"""How many samples the second WAV is behind the first (the same passage through two output chains: Options > Output
Clean's limiter looks ahead, so its output is late by exactly that; docs/AUDIO-QUALITY.md), and each one's peak.

  python3 tools/emu/wavlag.py A.wav B.wav [--max 256]
  -> "lag 32 samples (0.73 ms), correlation 0.998; peak A 14021, B 10513 of 32768"

The lag is where the two left channels' cross-correlation is largest, looked for from -max to +max. 16-bit stereo
WAVs as the emulator writes them. numpy only (tools/emu/test_cr.sh)."""
import sys
import numpy as np


def load(path):
    b = open(path, "rb").read()
    i = b.find(b"data")
    return np.frombuffer(b[i + 8:len(b) - (len(b) - i - 8) % 4], dtype="<i2").reshape(-1, 2).astype(np.float64)


def main():
    a, b = load(sys.argv[1]), load(sys.argv[2])
    mx = int(sys.argv[4]) if len(sys.argv) > 4 and sys.argv[3] == "--max" else 256
    n = min(len(a), len(b)) - 2 * mx
    x = a[mx:mx + n, 0]
    best, lag = -2.0, 0
    for d in range(-mx, mx + 1):
        y = b[mx + d:mx + d + n, 0]
        c = float((x * y).sum() / max(np.sqrt((x * x).sum() * (y * y).sum()), 1e-30))
        if c > best:
            best, lag = c, d
    print(f"lag {lag} samples ({lag / 44.1:.2f} ms), correlation {best:.3f}; peak A {int(np.abs(a).max())}, "
          f"B {int(np.abs(b).max())} of 32768")


if __name__ == "__main__":
    main()

#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-only
"""Three small readings of the emulator's WAVs (16-bit stereo) for tools/emu/test_cr.sh; numpy only.

  python3 tools/emu/wavstat.py stereo A.wav
      -> "side -14.2 dB re mid; left differs from right on 81234 of 176400 frames"
         (the stereo chorus: side = (L - R) / 2, mid = (L + R) / 2, over the whole file; "side -inf": the same word)
  python3 tools/emu/wavstat.py mono A.wav B.wav
      -> "mono sum +0.12 dB re B's; left -0.03 dB"        (the level of A's (L + R) / 2 and of its left against B's)
  python3 tools/emu/wavstat.py level A.wav B.wav T0 T1 [T2 T3]
      -> "level -32.21 dB re B's (T0 .. T1 ms after A's first sound)[; -32.20 dB (T2 .. T3 ms)]"
         (MASTER at power-on: the same script at two pot positions; both windows start at A's first non-zero frame
         of B, the louder one, so the same part of the note is read in both)"""
import sys
import numpy as np

FS = 44100


def load(path):
    b = open(path, "rb").read()
    i = b.find(b"data")
    return np.frombuffer(b[i + 8:len(b) - (len(b) - i - 8) % 4], dtype="<i2").reshape(-1, 2).astype(np.float64)


def db(p):
    return 10 * np.log10(p) if p > 0 else float("-inf")


def main():
    cmd = sys.argv[1]
    a = load(sys.argv[2])
    if cmd == "stereo":
        mid, side = (a[:, 0] + a[:, 1]) / 2, (a[:, 0] - a[:, 1]) / 2
        d = db((side ** 2).sum() / max((mid ** 2).sum(), 1e-30))
        print(f"side {d:.1f} dB re mid; left differs from right on {int((a[:, 0] != a[:, 1]).sum())} of {len(a)} frames")
        return
    b = load(sys.argv[3])
    if cmd == "mono":
        n = min(len(a), len(b))
        ma, mb = (a[:n, 0] + a[:n, 1]) / 2, (b[:n, 0] + b[:n, 1]) / 2
        print(f"mono sum {db((ma ** 2).sum() / max((mb ** 2).sum(), 1e-30)):+.2f} dB re B's; "
              f"left {db((a[:n, 0] ** 2).sum() / max((b[:n, 0] ** 2).sum(), 1e-30)):+.2f} dB")
        return
    if cmd == "level":
        nz = np.nonzero(b[:, 0])[0]
        if not len(nz):
            print("level: B is silent")
            sys.exit(1)
        out = []
        for k in range(4, len(sys.argv) - 1, 2):
            t0, t1 = int(nz[0] + float(sys.argv[k]) * FS / 1000), int(nz[0] + float(sys.argv[k + 1]) * FS / 1000)
            r = db((a[t0:t1, 0] ** 2).sum() / max((b[t0:t1, 0] ** 2).sum(), 1e-30))
            out.append(f"{r:.2f} dB ({sys.argv[k]} .. {sys.argv[k + 1]} ms)")
        print("level " + out[0].replace(" dB (", " dB re B's (", 1) + "".join("; " + o for o in out[1:]))
        return
    print(__doc__)
    sys.exit(2)


if __name__ == "__main__":
    main()

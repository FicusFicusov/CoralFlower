#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-only
"""Pictures of the delay and the reverb (firmware/src/cr_fx.c, docs/FX.md) for judging them without ears.

  python3 tools/fx_plots.py [DIR]         # DIR: build/fx-review (tests/cr_fx_test.c and tools/emu/fx_review.sh fill it)

For every WAV in DIR a PNG beside it: the level of each side against time (dB, 10 ms windows; left blue, right
orange) over a spectrogram (0 .. 12 kHz, 60 dB of range under the file's loudest bin). For the reverb's impulse
responses (ir_reverb_*.wav) also Schroeder's backward integral (white) with the RT60 its -5 .. -35 dB slope gives.
numpy and Pillow only.
"""
import sys
import wave
from pathlib import Path

import numpy as np
from PIL import Image, ImageDraw

W, H1, H2, ML, MT, GAP = 900, 220, 260, 56, 26, 34
BG, GRID, TXT = (22, 22, 26), (60, 60, 68), (216, 216, 220)
LEFT, RIGHT, EDC = (90, 170, 255), (255, 160, 60), (255, 255, 255)


def read(path):
    with wave.open(str(path), "rb") as w:
        n, ch, fs = w.getnframes(), w.getnchannels(), w.getframerate()
        x = np.frombuffer(w.readframes(n), dtype="<i2").astype(np.float64).reshape(-1, ch) / 32768.0
    if ch == 1:
        x = np.repeat(x, 2, axis=1)
    return x, fs


def level_db(x, fs, win):
    n = len(x) // win
    e = (x[:n * win].reshape(n, win) ** 2).mean(axis=1)
    return 10 * np.log10(np.maximum(e, 1e-12))


def spectro(x, fs, cols, rows, fmax=12000.0):
    nfft = 1024
    hop = max(1, (len(x) - nfft) // cols)
    win = np.hanning(nfft)
    img = np.zeros((rows, cols))
    bins = np.minimum((np.arange(rows) * fmax / rows / (fs / nfft)).astype(int), nfft // 2)
    for c in range(cols):
        seg = x[c * hop:c * hop + nfft]
        if len(seg) < nfft:
            break
        img[:, c] = np.abs(np.fft.rfft(seg * win))[bins]
    return 20 * np.log10(np.maximum(img, 1e-9))


def colour(v):                      # 0 .. 1 -> black, purple, orange, white
    stops = np.array([[0, 0, 4], [80, 18, 123], [182, 54, 121], [251, 136, 97], [252, 253, 191]], dtype=float)
    p = np.clip(v, 0, 1) * (len(stops) - 1)
    i = np.minimum(p.astype(int), len(stops) - 2)
    f = (p - i)[..., None]
    return (stops[i] * (1 - f) + stops[i + 1] * f).astype(np.uint8)


def plot(path):
    x, fs = read(path)
    if len(x) < 4096:
        return None
    secs = len(x) / fs
    img = Image.new("RGB", (ML + W + 16, MT + H1 + GAP + H2 + 30), BG)
    d = ImageDraw.Draw(img)
    d.text((ML, 6), path.name, fill=TXT)
    # the level
    win = fs // 100
    lo, hi = -96.0, 0.0
    for db in range(0, -97, -12):
        y = MT + (hi - db) / (hi - lo) * H1
        d.line((ML, y, ML + W, y), fill=GRID)
        d.text((6, y - 6), "%4d dB" % db, fill=TXT)
    step = 1 if secs <= 12 else 5
    for t in range(0, int(secs) + 1, step):
        xx = ML + t / secs * W
        d.line((xx, MT, xx, MT + H1), fill=GRID)
        d.line((xx, MT + H1 + GAP, xx, MT + H1 + GAP + H2), fill=GRID)
        d.text((xx - 6, MT + H1 + GAP + H2 + 6), "%d s" % t, fill=TXT)
    note = ""
    for ch, col in ((0, LEFT), (1, RIGHT)):
        lv = level_db(x[:, ch], fs, win)
        pts = [(ML + (k + 0.5) * win / len(x) * W, MT + (hi - max(lo, v)) / (hi - lo) * H1) for k, v in enumerate(lv)]
        if len(pts) > 1:
            d.line(pts, fill=col, width=1)
    if path.name.startswith("ir_reverb"):
        e = (x ** 2).sum(axis=1)
        edc = 10 * np.log10(np.maximum(np.cumsum(e[::-1])[::-1] / max(e.sum(), 1e-12), 1e-12))
        k = np.arange(0, len(edc), win)
        pts = [(ML + i / len(x) * W, MT + (hi - max(lo, edc[i])) / (hi - lo) * H1) for i in k]
        d.line(pts, fill=EDC, width=2)
        a, b = int(np.argmax(edc < -5)), int(np.argmax(edc < -35))
        if b > a:
            note = "  Schroeder integral (white): RT60 %.2f s from its -5 .. -35 dB" % ((b - a) / fs * 2)
    d.text((ML + 260, 6), "level a side, 10 ms windows (left blue, right orange)" + note, fill=TXT)
    # the spectrogram (both sides summed)
    sp = spectro(x.sum(axis=1), fs, W, H2)
    top = sp.max()
    rgb = colour((sp - (top - 70.0)) / 70.0)[::-1]
    img.paste(Image.fromarray(rgb, "RGB"), (ML, MT + H1 + GAP))
    for f in range(0, 12001, 2000):
        y = MT + H1 + GAP + H2 - f / 12000.0 * H2
        d.text((6, y - 6), "%2d kHz" % (f // 1000), fill=TXT)
    d.text((ML, MT + H1 + 10), "spectrogram, L + R, 70 dB under the loudest bin", fill=TXT)
    out = path.with_suffix(".png")
    img.save(out)
    return out


if __name__ == "__main__":
    root = Path(sys.argv[1] if len(sys.argv) > 1 else "build/fx-review")
    for f in sorted(root.glob("*.wav")):
        o = plot(f)
        if o:
            print("fx_plots:", o)

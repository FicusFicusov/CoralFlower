#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-only
"""Listening files for Options > Output: Classic against Clean (docs/AUDIO-QUALITY.md), from what
tools/master_measure_run.sh rendered: the whole render (attack, held, release and tail), 24-bit stereo WAVs holding the
DAC word itself (x 2: the Q15 sample's full scale is the file's full scale).

  python3 tools/master_listen.py RENDERDIR OUTDIR

For each sound's single note (FX off) and 6-note chord with the bass (FX on):
  NAME.classic.wav          Options > Output Classic at full MASTER, as the unit plays it
  NAME.clean.wav            Options > Output Clean at full MASTER, as the unit plays it
  NAME.clean.matched.wav    Clean brought to Classic's loudness (K-weighted, the held part), to compare the sound and
                            not the level
  NAME.ab.wav               Classic, half a second of silence, Clean matched: one file to loop
and for the two quietest cases at MASTER -24 dB, both raised by 24 dB in the file so that what the low setting leaves
of the sound can be heard:
  NAME.classic.master-24.wav / NAME.clean.master-24.wav
Writes OUTDIR/README.txt with the levels of every file. numpy only."""
import os
import sys
import numpy as np

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import master_analyze as ma  # noqa: E402

FS = 44100


def load(path):
    d = np.fromfile(path, dtype="<i4").reshape(-1, 5).astype(np.float64)
    return d[:, 3:5] / 128.0           # Q15 units


def wav24(path, x):
    """x in Q15 units -> 24-bit: x * 256 (Q15 full scale = the file's)"""
    v = np.clip(np.rint(x * 256.0), -8388608, 8388607).astype("<i4")
    n = len(v)
    raw = np.zeros((n * 2, 3), dtype=np.uint8)
    f = v.reshape(-1)
    raw[:, 0] = f & 255
    raw[:, 1] = (f >> 8) & 255
    raw[:, 2] = (f >> 16) & 255
    with open(path, "wb") as fh:
        fh.write(b"RIFF" + np.uint32(36 + n * 6).tobytes() + b"WAVEfmt " + np.uint32(16).tobytes())
        fh.write(np.uint16(1).tobytes() + np.uint16(2).tobytes() + np.uint32(FS).tobytes() + np.uint32(FS * 6).tobytes())
        fh.write(np.uint16(6).tobytes() + np.uint16(24).tobytes() + b"data" + np.uint32(n * 6).tobytes())
        fh.write(raw.tobytes())


def held(x):
    a, b = ma.window()
    return x[a:b]


def main():
    src, out = sys.argv[1], sys.argv[2]
    os.makedirs(out, exist_ok=True)
    lines = ["Listening files: Options > Output Classic (0.18's output chain) against Clean (0.19's).",
             "Made by tools/master_listen.py from tools/master_measure_run.sh's renders (docs/AUDIO-QUALITY.md):",
             "the firmware's own code on the Mac, the DAC word as the unit would send it. 24-bit, 44.1 kHz, stereo;",
             "the Q15 sample's full scale is the file's full scale. Each: 1 s of silence, the note or chord held 4 s,",
             "the release and the tail.",
             "",
             "single = one note (D4), FX off, no bass. chord6 = MAJ + 6 + M7 + 9 on D4, six notes, with the bass",
             "(DEEP SUB) and the FX on at the power-on sends.",
             "fm6 = TINE EP, va = SYNTH BRASS, wheel = FULL ORGAN, cz = BRASS 1.",
             "",
             "  .classic.wav         Classic at full MASTER, as the unit plays it",
             "  .clean.wav           Clean at full MASTER, as the unit plays it",
             "  .clean.matched.wav   Clean at Classic's loudness (the gain is in the table): the sound, not the level",
             "  .ab.wav              Classic, 0.5 s of silence, Clean matched",
             "  .master-24.wav       MASTER at -24 dB, raised 24 dB in the file: what a low MASTER leaves of the sound",
             "",
             f"{'file':44s} {'peak dBFS':>9s} {'rms dBFS':>9s} {'LKFS':>7s}  note"]

    def note(name, x, what=""):
        h = held(x)
        pk = 20 * np.log10(max(np.abs(x).max(), 1e-9) / 32768.0)
        rms = 10 * np.log10(max((h ** 2).mean(), 1e-9) / 32768.0 ** 2)
        lines.append(f"{name:44s} {pk:+9.1f} {rms:+9.1f} {ma.loudness(h):+7.1f}  {what}")

    for s in ("fm6", "va", "wheel", "cz"):
        for k in ("single-nofx", "chord6-fx"):
            n = f"{s}-{k}"
            c = load(os.path.join(src, f"{n}.classic.a00.i32"))
            d = load(os.path.join(src, f"{n}.clean.a00.i32"))
            g = ma.loudness(held(c)) - ma.loudness(held(d))
            m = d * 10 ** (g / 20)
            wav24(os.path.join(out, f"{n}.classic.wav"), c)
            wav24(os.path.join(out, f"{n}.clean.wav"), d)
            wav24(os.path.join(out, f"{n}.clean.matched.wav"), m)
            wav24(os.path.join(out, f"{n}.ab.wav"), np.concatenate([c, np.zeros((FS // 2, 2)), m]))
            note(f"{n}.classic.wav", c)
            note(f"{n}.clean.wav", d, f"Clean is {-g:+.1f} LU against Classic")
            note(f"{n}.clean.matched.wav", m, f"Clean {g:+.1f} dB" + (" (CLIPS in the file)" if np.abs(m).max() > 32767 else ""))
    lines.append("")
    for n in ("fm6-single-nofx", "va-single-nofx"):
        for ch in ("classic", "clean"):
            x = load(os.path.join(src, f"{n}.{ch}.a24.i32")) * 10 ** (24 / 20)
            wav24(os.path.join(out, f"{n}.{ch}.master-24.wav"), x)
            note(f"{n}.{ch}.master-24.wav", x, "MASTER -24 dB, + 24 dB in the file")
    open(os.path.join(out, "README.txt"), "w").write("\n".join(lines) + "\n")
    print("\n".join(lines))


if __name__ == "__main__":
    main()

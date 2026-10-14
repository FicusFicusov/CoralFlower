#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-only
"""Listening renders of the delay and the reverb (docs/FX.md) on the emulator, headless: nothing is played.

  python3 tools/emu/fx_review.py [--old EMU_OLD]      # WAVs, their pictures and the levels into build/fx-review/

One D major stab (MAJ + D4, 0.4 s) on the power-on sound (FM6 TINE EP) through each setting below, set in the FX
layer as a player would (FX held, SELECT, the knobs). 44.1 kHz stereo WAVs:
  stab_default          the unit as it powers on (the chord part's default sends: reverb 35, chorus 45, delay 25)
  stab_delay_pingpong   the delay alone (send 81): 1/8 at 120 BPM, ping-pong, the defaults
  stab_delay_dotted     .. Stereo, 1/8. left and 3:4 of it right (a dotted eighth against a 9/16), feedback up
  stab_delay_free       .. free: 356 ms, the right side x0.75, ping-pong, the wow up
  stab_reverb_default   the reverb alone (send 83) at its defaults
  stab_reverb_small     .. Size 20, Decay 1.2 s
  stab_reverb_large     .. Size 127, Decay 9.6 s
  stab_reverb_inf       .. Size 127, Decay Inf, Damp 30: the frozen hall, 20 s of it
  stab_both             both, sends up, Decay 4.2 s
With --old (an emulator built from 0.17 / 164bcca: Felucca's mono delay and ROOM): the same stab through the old
effects (old_default, old_delay, old_reverb) and the level of each pair (RMS and peak of the whole file, and the RMS of
the wet alone: 0.6 .. 3 s after the stab began, the note released).
Then tools/fx_plots.py over the directory.
"""
import math
import os
import subprocess
import sys
import wave
from pathlib import Path

import numpy as np

ROOT = Path(__file__).resolve().parents[2]
OUT = ROOT / "build" / "fx-review"
EMU = ROOT / "build" / "host" / "emu"
OPEN = ["wait 300", "btn FX down", "wait 500", "btn FX up", "wait 300"]


def stab(tail_ms):
    return ["key F3 down", "wait 40", "btn D4 down", "wait 400", "btn D4 up", "key F3 up", "wait %d" % tail_ms, "dump"]


def k(knob, n):
    return ["knob %s %+d" % (knob, n), "wait 60"]


# the FX layer opens on Reverb 1; SELECT: Reverb 1, Reverb 2, Chorus, Delay 1, Delay 2, Drive
REV0 = k("KNOB4", -32)                                   # the reverb's send to 0
CHO0 = k("SELECT", 2) + k("KNOB4", -32)                  # Reverb 1 -> Chorus, its send to 0
NEW = {
    "stab_default": stab(7000),
    "stab_delay_pingpong": OPEN + REV0 + CHO0 + k("SELECT", 1) + k("KNOB4", 14) + stab(6000),
    "stab_delay_dotted": OPEN + REV0 + CHO0 + k("SELECT", 1) + k("KNOB4", 14) + k("KNOB1", 2) + k("KNOB2", 1) + k("KNOB3", 8)
                         + k("SELECT", 1) + k("KNOB2", -1) + ["wait 1500"] + stab(8000),
    "stab_delay_free": OPEN + REV0 + CHO0 + k("SELECT", 2) + k("KNOB1", -1) + k("KNOB4", 20) + k("SELECT", -1)
                       + k("KNOB4", 14) + k("KNOB1", 5) + k("KNOB2", 32) + ["wait 1500"] + stab(7000),
    "stab_reverb_default": OPEN + k("KNOB4", 12) + CHO0 + k("SELECT", 1) + k("KNOB4", -32) + stab(7000),
    "stab_reverb_small": OPEN + k("KNOB4", 12) + k("KNOB1", -34) + k("KNOB3", -10) + CHO0 + k("SELECT", 1) + k("KNOB4", -32)
                         + ["wait 1500"] + stab(4000),
    "stab_reverb_large": OPEN + k("KNOB4", 12) + k("KNOB1", 20) + k("KNOB3", 14) + CHO0 + k("SELECT", 1) + k("KNOB4", -32)
                         + ["wait 1500"] + stab(14000),
    "stab_reverb_inf": OPEN + k("KNOB4", 12) + k("KNOB1", 20) + k("KNOB3", 38) + k("KNOB2", -13) + CHO0 + k("SELECT", 1)
                       + k("KNOB4", -32) + ["wait 1500"] + stab(20000),
    "stab_both": OPEN + k("KNOB4", 12) + k("KNOB3", 6) + k("SELECT", 3) + k("KNOB4", 14) + ["wait 800"] + stab(10000),
}
# 0.17's FX layer: SELECT Reverb, Chorus, Delay, Drive; KNOB 4 the amount
OLD = {
    "old_default": stab(7000),
    "old_delay": OPEN + k("KNOB4", -32) + k("SELECT", 1) + k("KNOB4", -32) + k("SELECT", 1) + k("KNOB4", 14) + stab(6000),
    "old_reverb": OPEN + k("KNOB4", 12) + k("SELECT", 1) + k("KNOB4", -32) + k("SELECT", 1) + k("KNOB4", -32) + stab(7000),
}
PAIRS = [("old_default", "stab_default"), ("old_delay", "stab_delay_pingpong"), ("old_reverb", "stab_reverb_default")]


def render(emu, name, lines):
    script = OUT / (name + ".txt")
    script.write_text("# tools/emu/fx_review.py: %s\n" % name + "\n".join(lines) + "\n")
    env = dict(os.environ, SDL_VIDEODRIVER="dummy", SDL_AUDIODRIVER="dummy")
    with open(OUT / (name + ".log"), "wb") as log:
        rc = subprocess.call([str(emu), "--headless", "--script", str(script), "--wav", str(OUT / (name + ".wav"))],
                             stdout=log, stderr=subprocess.STDOUT, env=env, cwd=str(ROOT))
    if rc:
        print("fx_review: %s: exit %d (see %s)" % (name, rc, OUT / (name + ".log")))
    return rc


def level(name):
    with wave.open(str(OUT / (name + ".wav")), "rb") as w:
        x = np.frombuffer(w.readframes(w.getnframes()), dtype="<i2").astype(np.float64).reshape(-1, w.getnchannels()) / 32768.0
    rms = math.sqrt(float((x ** 2).mean()))
    on = int(np.argmax(np.abs(x).max(axis=1) > 0.01))   # the stab's first sample; the wet alone: 0.6 .. 3 s after it
    t = x[on + int(0.6 * 44100):on + 3 * 44100]
    tail = math.sqrt(float((t ** 2).mean())) if len(t) else 0.0
    return (20 * math.log10(max(rms, 1e-9)), 20 * math.log10(max(float(np.abs(x).max()), 1e-9)),
            20 * math.log10(max(tail, 1e-9)))


def main():
    old = None
    if "--old" in sys.argv:
        old = Path(sys.argv[sys.argv.index("--old") + 1])
    OUT.mkdir(parents=True, exist_ok=True)
    bad = 0
    for name, lines in NEW.items():
        bad += render(EMU, name, lines) != 0
    if old:
        for name, lines in OLD.items():
            bad += render(old, name, lines) != 0
    for name in list(NEW) + (list(OLD) if old else []):
        r, p, t = level(name)
        print("fx_review: %-22s rms %6.1f dBFS, peak %6.1f dBFS, the wet alone (0.6 .. 3 s after the stab) %6.1f dBFS" % (name, r, p, t))
    if old:
        for a, b in PAIRS:
            (ra, pa, ta), (rb, pb, tb) = level(a), level(b)
            print("fx_review: %s -> %s: rms %+.1f dB, peak %+.1f dB, the wet alone %+.1f dB" % (a, b, rb - ra, pb - pa, tb - ta))
    subprocess.call([sys.executable, str(ROOT / "tools" / "fx_plots.py"), str(OUT)])
    return bad


if __name__ == "__main__":
    sys.exit(main())

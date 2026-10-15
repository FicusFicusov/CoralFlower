#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-only
"""Stuck notes on fast chord changes (Top, no Lock): the headless emulator plays random chord changes whose keys land
a few ms apart (0..8 ms, as fingers do), then checks after every change that the chord part holds only the chord's
notes, and that nothing sounds once every key is let go.
    python3 tests/stuck_notes.py [SEEDS]        (from the repo root; default 6 seeds)
It found the stuck note of 2026-10-15: a note-on still waiting a block to start (voice.c vsq) was not let go when
the chord moved on (cf_play.c cfp_change), so it started a moment later and held: 7..18 of 121 changes on VA sounds.
Sounds: FM ORGAN (the default, FM6: its own voice allocation) and two VA presets (voice.c's: the waiting notes)."""
import random
import re
import subprocess
import sys
import tempfile
from pathlib import Path

ROOT = Path(__file__).resolve().parent.parent
BTN = [1, 3, 5, 8, 10, 13, 15, 17, 20, 22]                 # the Top layout's chord buttons (Off left out)
KEYS = [0, 2, 4, 6, 7, 9, 11, 12, 14, 16, 18, 19, 21, 23, 24, 26]
PRESETS = [0, 25, 30]                                       # PRESETS turned from FM ORGAN: +25 VA POLY KEYS, +30


def script(seed, preset, steps=120):
    rnd = random.Random(seed)
    out = ["wait 2500", "turn ALGORITHM 1", "wait 100"]     # Top Lock -> Top
    if preset:
        out += [f"turn PRESETS {preset}", "wait 300"]
    held = []
    for _ in range(steps):
        nb, nr = rnd.choice(BTN), rnd.choice(KEYS)
        ev = [(k, "up") for k in held] + [(nb, "down"), (nr, "down")]
        rnd.shuffle(ev)
        for k, d in ev:
            out.append(f"keyq {k} {d}")
            w = rnd.choice([0, 0, 0, 1, 2, 3, 5, 8])
            if w:
                out.append(f"wait {w}")
        held = [nb, nr]
        out += [f"wait {rnd.randint(30, 160)}", "dump"]
    out += [f"keyq {k} up" for k in held] + ["wait 4000", "dump"]
    return "\n".join(out) + "\n"


def check(text):
    """-> (changes checked, changes with a held note outside the chord, voices sounding at the end)"""
    notes, n, bad, voices = None, 0, 0, None
    for line in text.splitlines():
        m = re.match(r"\s+chord: \S+ sounding (\d) single \d buttons \S+ notes([\d ]*)", line)
        if m:
            notes = [int(x) for x in m.group(2).split()] if m.group(1) == "1" else []
        m = re.match(r"\s+part 0 gated:([\d ]*)", line)
        if m and notes is not None:
            n += 1
            bad += any(int(x) not in notes for x in m.group(1).split())
        m = re.match(r"\s+part 0: engine \d+ voices (\d+)", line)
        if m:
            voices = int(m.group(1))
    return n, bad, voices


def main():
    seeds = int(sys.argv[1]) if len(sys.argv) > 1 else 6
    failed = 0
    with tempfile.TemporaryDirectory() as tmp:
        for preset in PRESETS:
            for seed in range(1, seeds + 1):
                p = Path(tmp) / "s.txt"
                p.write_text(script(seed, preset))
                r = subprocess.run(["sh", "tools/emu/cf_headless.sh", str(p)], cwd=ROOT, capture_output=True, text=True)
                n, bad, voices = check(r.stdout + r.stderr)
                ok = n > 100 and bad == 0 and voices == 0
                failed += not ok
                print(f"PRESETS +{preset:2} seed {seed}: {n} changes, {bad} with a stuck note, {voices} voices left "
                      f"{'ok' if ok else 'FAIL'}")
    print("all passed" if not failed else f"{failed} FAILED")
    return 1 if failed else 0


if __name__ == "__main__":
    sys.exit(main())

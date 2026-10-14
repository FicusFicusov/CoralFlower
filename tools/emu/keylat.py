#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-only
"""A key's way to the engine while the screen draws (docs/INTEGRATION.md "Keys while a frame draws"), on the emulator:

    python3 tools/emu/keylat.py [--emu build/host/emu] [--n 40] [--strip-us 2700,7800] SCRIPT...

For each headless script (tools/emu/perf.sh passes its heavy scenarios): a copy with N taps of G5 at pseudo-random
times (a fixed seed: the same every run) inside the longest stretch the script holds a root (its chord sounds and
the screen redraws), run with a redraw's strip taking STRIP_US of the device's clock (EMU_STRIP_US, tools/emu/
README.md "A redraw in simulated time"; 2700: the emulator's worst frame, 16 ms for six strips; 7800: 2.9 times
that, the upper end of the unit's unknown factor), twice: as the firmware is (the keys read between the strips) and
with EMU_PUMP=0 (read between frames only, as before 0.18's key pump). Prints the emulator's "keys:" figures of
both: from the tick that saw the key down to the audio block that started its note, average and worst, in ms (the
emulator's keys have no debounce: the unit adds 1.1 to 2.2 ms; the sound leaves the DAC one half, 2.9 ms, later).
That model moves the clock a strip at a time, so it shows the keys read after each strip, not the reads inside one
(at each glyph, arc and polyline): its figures are an upper bound. The third line is the other measure, of the
script as it is (EMU_UI_GAP=1): the longest stretch of a UI frame in which no key is read, as a device estimate from
the host's instruction count, with the pump and without (the whole frame).
Outputs in build/emu/perf/. Exit 1 if a run printed no "keys:" line."""
import os
import re
import subprocess
import sys


def dur(w, default):
    m = re.match(r"([0-9.]+)(ms|s)?$", w or "")
    if not m:
        return default
    return int(float(m.group(1)) * (1000 if m.group(2) == "s" else 1) + .5)


def window(path):
    """the script's clock as emu.c load_script moves it: the longest time a root is held by `btn NOTE down` ..
    `btn NOTE up` (start, end) in ms"""
    cur, best, down = 0, (0, 0), {}
    for line in open(path):
        w = re.sub(r"(^|\s)#.*", "", line).split()
        if not w:
            continue
        if w[0][0].isdigit():                     # "<ms> command": an absolute time
            cur = int(w[0])
            continue
        cmd, arg = w[0], w[1:] + ["", "", ""]
        if cmd == "btn" and re.match(r"[A-G]#?[0-9]$", arg[0]) and arg[1] in ("down", "up"):
            if arg[1] == "down":
                down[arg[0]] = cur
            elif arg[0] in down and cur - down[arg[0]] > best[1] - best[0]:
                best = (down.pop(arg[0]), cur)
        if cmd == "wait":
            cur += dur(arg[0], 0)
        elif cmd == "frames":
            cur += 15 * int(arg[0])
        elif cmd in ("key", "btn", "press", "tap") and arg[1] not in ("down", "up"):
            cur += dur(arg[1], 60 if cmd == "tap" else 100)
        elif cmd == "midi":                       # "midi F8 xN MS": a train moves the clock
            x = [a for a in arg if a.startswith("x")]
            if x:
                step = arg[arg.index(x[0]) + 1]
                cur += int(int(x[0][1:]) * float(step or 0) + .5)
    return best


def keys_line(out):
    m = re.search(r"^keys: (\d+) note-ons, from the key's scan to the engine avg ([0-9.]+) ms, worst ([0-9.]+) ms", out, re.M)
    return (int(m.group(1)), float(m.group(2)), float(m.group(3))) if m else None


def main():
    a = sys.argv[1:]
    emu, n, strips = "build/host/emu", 40, [2700, 7800]
    while a and a[0].startswith("--"):
        if a[0] == "--emu":
            emu = a[1]
        elif a[0] == "--n":
            n = int(a[1])
        elif a[0] == "--strip-us":
            strips = [int(x) for x in a[1].split(",")]
        else:
            sys.exit(__doc__)
        a = a[2:]
    if not a:
        sys.exit(__doc__)
    os.makedirs("build/emu/perf", exist_ok=True)
    env = dict(os.environ, SDL_VIDEODRIVER="dummy", SDL_AUDIODRIVER="dummy")
    bad = 0
    for path in a:
        t0, t1 = window(path)
        t0, t1 = t0 + 200, t1 - 200               # (clear of the keys the script itself presses)
        seed, taps = 12345, []
        for k in range(n):                        # one tap in each n-th of the window, at a random place in it
            seed = (seed * 1103515245 + 12345) & 0x7FFFFFFF
            slot = (t1 - t0) // n
            taps.append(t0 + k * slot + seed % max(1, slot - 60))
        name = os.path.splitext(os.path.basename(path))[0]
        gen = f"build/emu/perf/{name}_keylat.txt"
        with open(gen, "w") as f:
            f.write(open(path).read())
            f.write(f"\n# tools/emu/keylat.py: {n} taps of G5 at pseudo-random times in {t0} .. {t1} ms\n")
            for t in taps:
                f.write(f"{t} btn G5 down\n{t + 40} btn G5 up\n")
        for us in strips:
            res = []
            for pump in ("1", "0"):
                r = subprocess.run([emu, "--headless", "--script", gen], env=dict(env, EMU_STRIP_US=str(us), EMU_PUMP=pump),
                                   capture_output=True, text=True, errors="replace")
                open(f"build/emu/perf/{name}_keylat_{us}_{'pump' if pump == '1' else 'nopump'}.log", "w").write(r.stdout + r.stderr)
                res.append(keys_line(r.stdout))
            if not res[0] or not res[1]:
                print(f"   keys: {name}: no \"keys:\" line (see build/emu/perf/{name}_keylat_*.log)")
                bad = 1
                continue
            print(f"   keys ({name[-1]}): {res[0][0]} note-ons, {n} of them struck at random times, a strip taking "
                  f"{us / 1000:.1f} ms (a full redraw {6 * us / 1000:.0f} ms): to the engine avg {res[0][1]:.1f} ms, worst "
                  f"{res[0][2]:.1f} ms; read between frames only (as it was) avg {res[1][1]:.1f} ms, worst {res[1][2]:.1f} ms")
        gap = []                                  # the scenario itself: the longest stretch of a frame with no key read
        for pump in ("1", "0"):
            r = subprocess.run([emu, "--headless", "--script", path], env=dict(env, EMU_UI_GAP="1", EMU_PUMP=pump),
                               capture_output=True, text=True, errors="replace")
            m = re.search(r"^ui: the longest stretch of a frame with no key read: \d+ host instructions -> device estimate (\d+) us", r.stdout, re.M)
            gap.append(int(m.group(1)) if m else -1)
        if min(gap) < 0:
            print(f"   keys ({name[-1]}): no \"ui: the longest stretch\" line")
            bad = 1
        else:
            print(f"   keys ({name[-1]}): the longest stretch of a UI frame with no key read (device estimate from the host's "
                  f"instructions): {gap[0] / 1000:.1f} ms; read between frames only (as it was) {gap[1] / 1000:.1f} ms")
    return bad


if __name__ == "__main__":
    sys.exit(main())

#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-only
"""Every run of a tests/cz_pops.c matrix that has a jump, explained (docs/CZ1.md "Pops": the jumps left).

  python3 tools/cz_pops_why.py MATRIX.txt [build/host/cz_pops]

Each such run is rendered again with CZ_WHY at the jump's sample: the tool then prints, for every line of every voice,
the step its wave's definition makes between the two samples' phase positions at that sample's DCW depth and gain
(no state: cz_native_wave alone) and the step the line made. Here: the line with the largest step of the run, whether
the definition gives that step (within 15 %), the wave, the depth and how many phase positions the line moves a sample.
A step the definition gives is the wave's own edge (a phase function squeezed by the DCW depth: the chip's 11-bit
functions do the same); one it does not give would be the engine's (a block's edge, an envelope's step)."""
import os
import re
import subprocess
import sys
from concurrent.futures import ThreadPoolExecutor

LINE = re.compile(r'(\d+) (\d+) "([^"]*)" (\S+) (\S+) (-?\d+) (\d+) (\d+) \|.*?\| jump (\d+) at (\d+) d (-?\d+) ref (-?\d+) ev (-?\d+)')
HEAD = re.compile(r'why \d+ part (\d+) voice (\d+) note (\d+) line (\d+): waves (\d+)/(\d+) window (\d+), depth (\d+) -> (\d+)')
SAMP = re.compile(r'   sample \+0: position +(\d+) wave (\d+) depth +(\d+)')
STEP = re.compile(r"   step -?\d+ -> \d+: the definition's (-?\d+) \(x gain / 2: (-?\d+)\), the line's (-?\d+); positions (\d+) -> (\d+), "
                  r"the definition's largest step between neighbours there (\d+)")
SCEN = ["hold", "retrig", "retrig-gap", "change", "steal", "arp", "restrike", "arp-up"]
CH = {"4": 0, "5": 1, "6": 2, "6wide": 3}


def main():
    if len(sys.argv) < 2:
        print(__doc__)
        return 1
    binary = sys.argv[2] if len(sys.argv) > 2 else "build/host/cz_pops"
    runs = []
    for ln in open(sys.argv[1]):
        m = LINE.match(ln)
        if m and int(m.group(9)):
            runs.append(m.groups())

    def one(r):
        eng, pr, name, scen, chord, root, vel, bass, _, at, d, ref, evd = r
        env = dict(os.environ, CZ_WHY=at)
        out = subprocess.run([binary, "one", eng, pr, str(SCEN.index(scen)), str(CH[chord]), root, vel, bass],
                             capture_output=True, text=True, env=env).stdout.splitlines()
        best, head, samp = None, None, None
        for ln in out:
            h = HEAD.match(ln)
            if h:
                head = h.groups()
            s = SAMP.match(ln)
            if s:
                samp = s.groups()
            m = STEP.match(ln)
            if m and head:
                g = [int(x) for x in m.groups()]
                if best is None or abs(g[2]) > abs(best[0][2]):
                    best = (g, head, samp)
        return r, best

    with ThreadPoolExecutor(8) as ex:
        res = list(ex.map(one, runs))
    own = other = 0
    by = {}
    for r, best in res:
        if best is None:
            other += 1
            print("   not explained (NOISE, or no CZ-1 voice at the sample): %s %s chord %s root %s vel %s bass %s" % (
                r[2], r[3], r[4], r[5], r[6], r[7]))
            continue
        g, head, samp = best
        ok = abs(g[2]) > 0 and abs(g[1] - g[2]) <= 0.15 * abs(g[2]) + 8
        own += ok
        other += not ok
        moved = (g[4] - g[3]) & 2047
        key = (r[2], int(samp[1]) if samp else -1)
        e = by.setdefault(key, dict(n=0, ok=0, depth=[], moved=[], pred=[], made=[], big=[], evd=[], part=set()))
        e["n"] += 1
        e["ok"] += ok
        e["depth"].append(int(samp[2]) if samp else -1)
        e["moved"].append(moved)
        e["pred"].append(abs(g[1]))
        e["made"].append(abs(g[2]))
        e["big"].append(g[5])
        e["evd"].append(int(r[12]))
        e["part"].add("bass" if head[0] == "1" else "chord")
        if not ok:
            print("   NOT the definition's: %s %s chord %s root %s vel %s bass %s at %s: the definition gives %d, the line made %d"
                  % (r[2], r[3], r[4], r[5], r[6], r[7], r[9], g[1], g[2]))
    print("runs with a jump: %d; the largest line step is the wave definition's own in %d, not in %d" % (len(res), own, other))
    print("%-13s %4s %4s %5s %-11s %-11s %-13s %-15s %-15s %s" % ("tone", "runs", "own", "wave", "DCW depth", "pos/sample",
                                                                  "after note-on", "definition", "line", "part"))
    for (name, wave), e in sorted(by.items(), key=lambda kv: -kv[1]["n"]):
        print("%-13s %4d %4d %5d %4d..%-5d %4d..%-5d %5d..%-6d %6d..%-7d %6d..%-7d %s" % (
            name, e["n"], e["ok"], wave, min(e["depth"]), max(e["depth"]), min(e["moved"]), max(e["moved"]), min(e["evd"]),
            max(e["evd"]), min(e["pred"]), max(e["pred"]), min(e["made"]), max(e["made"]), "/".join(sorted(e["part"]))))
    return 0


if __name__ == "__main__":
    sys.exit(main())

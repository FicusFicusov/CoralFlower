#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-only
"""Sort the lines of tests/cz_pops.c "all" (docs/CZ1.md "Pops").

  python3 tools/cz_pops_report.py FILE... [--worst N] [--tones] [--jumps]
  python3 tools/cz_pops_report.py --halves BEFORE AFTER

Prints: the runs, the wraps (a run the sanitizer stopped), the peaks of every stage, and per scenario the runs with a
gain step (a line's gain moving by more than 5 % of full scale between the end of one block and the start of the next:
the engine ramps every gain across a block, so such a step is a discontinuity it made), with a jump in the dry mix
(4 times the signal's own largest step nearby), with a dip, with a hole, with a click in the dry mix and in the output
(wavclicks.py's measure). --worst N lists the N worst runs by gain step and by jump; --tones the tones with a gain
step, per scenario; --jumps the tones with a jump; --halves A B compares the part sums of two matrices (0.19's trim)."""
import re
import sys

PAT = re.compile(
    r'(\d+) (\d+) "([^"]*)" (\S+) (\S+) (-?\d+) (\d+) (\d+) \| part (-?\d+) mix (-?\d+) out (-?\d+) lim (-?\d+) dc (-?\d+)'
    r' \| jump (\d+) at (\d+) d (-?\d+) ref (-?\d+) ev (-?\d+) (\S+) \| ojump (\d+) at (\d+) d (-?\d+) ref (-?\d+)'
    r' \| dip (\d+) at (\d+) hole (\d+) at (\d+) \| kills (\d+) steals (\d+)'
    r' \| line (-?\d+) sum (-?\d+) voice (-?\d+) gstep (-?\d+) (\S+) at (\d+) vjump (-?\d+) dcwstep (-?\d+) (\S+)'
    r'(?: \| click (\d+) at (\d+) x (\d+) oclick (\d+) at (\d+) x (\d+) \| half ([0-9a-f]+) odd (\d+))?( WRAP)?')
KEYS = ("eng pr name scen chord root vel bass part mix out lim dc njump jump_at jump_d jump_ref ev_d ev ojump ojump_at "
        "ojump_d ojump_ref ndip dip_at nhole hole_at kills steals line sum voice gstep gstep_on gstep_at vjump dcwstep "
        "dcwstep_on nclick click_at click_x oclick oclick_at oclick_x half odd wrap").split()
STEP = 1638                                        # 5 % of a gain's full scale (Q15)


def load(paths):
    rows = []
    for p in paths:
        for ln in open(p):
            m = PAT.match(ln)
            if not m:
                continue
            r = dict(zip(KEYS, m.groups()))
            for k in r:
                if k not in ("name", "scen", "chord", "ev", "gstep_on", "dcwstep_on", "wrap", "half"):
                    r[k] = int(r[k] or 0)
            r["wrap"] = bool(r["wrap"])
            rows.append(r)
    return rows


def tag(r):
    return '%2d %-13s %-10s chord %-5s root %3d vel %3d bass %d' % (r["pr"], r["name"], r["scen"], r["chord"], r["root"],
                                                                 r["vel"], r["bass"])


def halves(pa, pb):
    """Two matrices of the same runs, one from a tree before 0.19's trim (a voice * 4), one after (* 2): the hash of
    every sample a CZ-1 voice left in its part's sum, the earlier tree's halved (tests/cz_pops.c cz_probe_out)."""
    a, b = load([pa]), load([pb])
    key = lambda r: (r["pr"], r["scen"], r["chord"], r["root"], r["vel"], r["bass"])
    mb = {key(r): r for r in b}
    same = diff = odd = miss = 0
    for r in a:
        o = mb.get(key(r))
        if o is None or not r["half"] or not o["half"]:
            miss += 1
        elif r["half"] == o["half"]:
            same += 1
        else:
            diff += 1
            if diff <= 5:
                print("   differs: %s" % tag(r))
        odd += r["odd"]
    odd += sum(r["odd"] for r in b)
    print("halves: %d runs in %s, %d in %s: %d with the same hash, %d differ, %d not in both; samples with no exact half %d"
          % (len(a), pa, len(b), pb, same, diff, miss, odd))
    return 0 if not diff and not miss and not odd else 1


def main():
    a = sys.argv[1:]
    worst, tones, jumps, paths = 0, False, False, []
    i = 0
    while i < len(a):
        if a[i] == "--worst":
            worst = int(a[i + 1]); i += 2
        elif a[i] == "--tones":
            tones = True; i += 1
        elif a[i] == "--jumps":
            jumps = True; i += 1
        elif a[i] == "--halves":                   # two files of the same runs: the part's sums, hash for hash
            return halves(a[i + 1], a[i + 2])
        else:
            paths.append(a[i]); i += 1
    rows = load(paths)
    if not rows:
        print(__doc__)
        return 1
    print("runs %d, tones %d, wraps (int32 overflow, the sanitizer) %d" % (len(rows), len({r["pr"] for r in rows}),
                                                                         sum(r["wrap"] for r in rows)))
    for k, what in (("line", "a line (Q15: 32767)"), ("sum", "a voice's lines summed"),
                    ("voice", "a voice into the part (VOICE_FS 24000)"), ("part", "the part after LEVEL (clamp 524287)"),
                    ("mix", "the dry mix (32768 = full scale at MASTER's top)"), ("out", "the output")):
        m = max(rows, key=lambda r: r[k])
        print("peak %-5s %7d  %-52s %s" % (k, m[k], what, tag(m)))
    print("limiter: lowest gain %d / 32768; runs under it %d" % (min(r["lim"] for r in rows),
                                                              sum(r["lim"] < 32768 for r in rows)))
    print("%-11s %6s %10s %10s %6s %6s %6s %12s %6s %6s" % ("scenario", "runs", "gain step", "at note-on", "jump", "dip",
                                                            "hole", "tones w/step", "click", "oclick"))
    for s in sorted({r["scen"] for r in rows}):
        rs = [r for r in rows if r["scen"] == s]
        gs = [r for r in rs if r["gstep"] > STEP]
        print("%-11s %6d %10d %10d %6d %6d %6d %12d %6d %6d" % (
            s, len(rs), len(gs), sum(r["gstep_on"] == "on" for r in gs), sum(r["njump"] > 0 for r in rs),
            sum(r["ndip"] > 0 for r in rs), sum(r["nhole"] > 0 for r in rs), len({r["pr"] for r in gs}),
            sum(r["nclick"] > 0 for r in rs), sum(r["oclick"] > 0 for r in rs)))
    if jumps:
        js = [r for r in rows if r["njump"]]
        print("runs with a jump in the dry mix: %d" % len(js))
        by = {}
        for r in js:
            by.setdefault((r["pr"], r["name"]), []).append(r)
        for (pr, name), rs in sorted(by.items(), key=lambda kv: -len(kv[1])):
            print("   %2d %-13s %3d runs; roots %s; samples after a note-on %d .. %d; step / nearby %d/%d .. %d/%d" % (
                pr, name, len(rs), sorted({r["root"] for r in rs}), min(r["ev_d"] for r in rs),
                max(r["ev_d"] for r in rs), min(rs, key=lambda r: r["jump_d"])["jump_d"],
                min(rs, key=lambda r: r["jump_d"])["jump_ref"], max(rs, key=lambda r: r["jump_d"])["jump_d"],
                max(rs, key=lambda r: r["jump_d"])["jump_ref"]))
    if tones:
        for s in sorted({r["scen"] for r in rows}):
            gs = [r for r in rows if r["scen"] == s and r["gstep"] > STEP]
            best = {}
            for r in gs:
                if r["pr"] not in best or r["gstep"] > best[r["pr"]]["gstep"]:
                    best[r["pr"]] = r
            print("%s: %d tones" % (s, len(best)))
            for pr in sorted(best):
                r = best[pr]
                print("   %s: gain step %5d (%.0f %%), the voice's sample step %d at sample %d" % (
                    tag(r), r["gstep"], r["gstep"] / 327.67, r["vjump"], r["gstep_at"]))
    if worst:
        print("worst %d by gain step:" % worst)
        for r in sorted(rows, key=lambda r: -r["gstep"])[:worst]:
            print("   %s: gain step %d (%s) at sample %d, voice step %d; mix jumps %d (worst %d against %d at %d)" % (
                tag(r), r["gstep"], r["gstep_on"], r["gstep_at"], r["vjump"], r["njump"], r["jump_d"], r["jump_ref"],
                r["jump_at"]))
        print("worst %d by jump in the dry mix:" % worst)
        for r in sorted(rows, key=lambda r: -r["jump_d"])[:worst]:
            print("   %s: step %d against %d nearby at sample %d (%d after a note-%s), %d jumps; gain step %d" % (
                tag(r), r["jump_d"], r["jump_ref"], r["jump_at"], r["ev_d"], r["ev"], r["njump"], r["gstep"]))
        print("worst %d by jump in the output:" % worst)
        for r in sorted(rows, key=lambda r: -r["ojump_d"])[:worst]:
            print("   %s: step %d against %d nearby at sample %d, %d jumps" % (tag(r), r["ojump_d"], r["ojump_ref"],
                                                                          r["ojump_at"], r["ojump"]))
        ds = [r for r in rows if r["ndip"] or r["nhole"]]
        print("dips / holes: %d runs" % len(ds))
        for r in ds[:worst]:
            print("   %s: %d dips (first at %d), %d holes (first at %d)" % (tag(r), r["ndip"], r["dip_at"], r["nhole"],
                                                                         r["hole_at"]))
    return 0


if __name__ == "__main__":
    sys.exit(main())

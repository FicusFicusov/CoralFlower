#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-only
"""What a function costs on the unit, from the build's own disassembly: its size and every loop's body.

    ./build.sh && python3 tools/fx_loops.py build/choralroot.dis xd_run xr_run xf_up xf_down xf_block

A loop is a backward branch: its body's instructions, how many touch memory, how many multiply. Times the loop's
passes a half (128 frames: 128 for the delay, 64 for the reverb at its half rate) and divided by ~205 (the unit's
instructions a microsecond, measured on xf_up and xf_down: straight loops, counted exactly) that is the unit's time:
docs/FX.md "The cost on the unit". A body with a rare branch in it (a saturator's knee) counts the whole of it here:
read the listing for what a pass really runs. The host's instruction count (tests/cr_fx_test.c COST, tools/emu/
perf.sh) is NOT this: another compiler, another machine.
"""
import re, sys

def main():
    dis = open(sys.argv[1]).read().split('\n')
    for fn in sys.argv[2:]:
        try:
            a = dis.index(fn + ':')
        except ValueError:
            print(fn, 'not found')
            continue
        ins = []
        for l in dis[a + 1:]:
            m = re.match(r'\s*([0-9a-f]+):\s+((?:[0-9a-f]{2} )+)\s*(.*)', l)
            if not m:
                if re.match(r'^\w+:$', l):
                    break
                continue
            ins.append((int(m.group(1), 16), m.group(3)))
        addr = [x[0] for x in ins]
        print('%s: %d instructions, %d bytes' % (fn, len(ins), addr[-1] - addr[0]))
        for k, (ad, tx) in enumerate(ins):
            m = re.search(r'goto (-?\d+) <[^:]*: ([0-9a-f]+) >', tx)
            if not m:
                continue
            t = int(m.group(2), 16)
            if t > ad or t not in addr or t == ad:
                continue
            body = ins[addr.index(t):k + 1]
            print('   loop +0x%X .. +0x%X: %3d instructions (%d with memory, %d multiplies)' %
                  (t - addr[0], ad - addr[0], len(body), sum('[' in x[1] for x in body), sum('*' in x[1] for x in body)))

if __name__ == '__main__':
    main()

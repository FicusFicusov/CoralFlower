#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-only
# After Melodee's tools/core1_fault_test.py (PR 21 "fix(dual-core): raise the core supply and survive CPU1 misreads,
# faults and stalls", Kerem Kilic / Melodee, GPL-3.0), adapted to ChoralRoot's SysEx command 75 CORE1_TEST.
"""Inject core 1 failures into a CR_CORE1_TEST=1 build of ChoralRoot and check the FM-1 carries on (docs/DUALCORE.md).

Run on the device by hand (never part of the automated tests), with Options > Dual Core On:
  misread  core 1 reads its sequence as +0x00200000 for 1 ms: refused, counted, never run; the first time core 1
           keeps working; the second time it is given up (one core, c1_fallback 1); the rails never above 14 / 4
  null     a null job at rest read as the next request: refused, never called (no fault)
  stall    a job that never returns: the 8 ms wait holds core 1, "gave up"
  fault    a job that calls address 0: a fault on core 1, caught; core 1 held, "gave up", no reset
  report / power / sysvdd=N / vdc14=N   the counters; the rails; a rail to a level (SYSVDD 6..14, VDC14 0..4)
Each run ends with the audio still running. Giving up is for the session, so each mode restarts the FM-1 first
(RESTART, command 72) and waits out the 30 s boot guard (two crashes within 30 s of boot enter SAFE MODE).
A build without CR_CORE1_TEST answers command 75 with "unsupported": this tool then refuses to run.
"""
import argparse
import os
import sys
import time

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import fm1_install as fi                                # noqa: E402  (the MIDI link, BackupLink)

CMD = 75
OPS = {'report': 0, 'misread': 1, 'null': 2, 'stall': 3, 'fault': 4, 'power': 5}
FIELDS = ('c1', 'jobs', 'rejected', 'faults', 'stalls', 'fallback', 'sysvdd_boot', 'sysvdd', 'vdc14_boot', 'vdc14',
          'fault_cpu', 'fault_dbg', 'fault_pc', 'fault_rets', 'fault_emu', 'misread', 'halves', 'waited_us')
STATE = {0: 'off', 1: 'ok', 2: 'failed', 3: 'gave up'}
RC = {0: 'ok', 1: 'refused (core 1 not running, or a level out of range)', 2: 'the job finished (it should not)'}


class Unsupported(SystemExit):
    pass


def connect(port):
    up = fi.Updater(fi.MidiBackend(), port)     # (every MIDI call of it has a bound: fm1_install.py "MIDI")
    dev = up.find()
    if not dev or dev.id.loader:
        if dev:
            dev.link.close()
        raise SystemExit('no running FM-1 found' + (f' ({up.gave_up} did not return in time)' if up.gave_up else ''))
    return dev


def command(port, op, args=(), timeout=3.0):
    dev = connect(port)
    try:
        r = fi.BackupLink(dev.link).request(CMD, [op, *args], timeout)
    finally:
        dev.link.close()
    if r == [127]:
        raise Unsupported('command 75 unsupported: this is not a CR_CORE1_TEST=1 build (docs/DUALCORE.md)')
    if len(r) < 3 or r[0] != op or len(r) != 3 + 5 * r[2]:
        raise SystemExit(f'unexpected reply to command 75: {r[:8]}...')
    vals = [fi.bk_r32(r, 3 + 5 * k) for k in range(r[2])]
    return {'op': r[0], 'rc': r[1], **dict(zip(FIELDS, vals))}


def show(label, r):
    v = lambda l: 0.93 + 0.03 * l
    w = lambda l: 1.25 + 0.05 * l
    print(f"{label}: rc {r['rc']} ({RC.get(r['rc'], '?')}) c1 {STATE.get(r['c1'], r['c1'])} jobs {r['jobs']} "
          f"rej {r['rejected']} faults {r['faults']} stalls {r['stalls']} fallback {r['fallback']} "
          f"halves {r['halves']} took {r['waited_us']} us")
    print(f"  rails: SYSVDD {r['sysvdd']} ({v(r['sysvdd']):.2f} V, boot {r['sysvdd_boot']}) "
          f"VDC14 {r['vdc14']} ({w(r['vdc14']):.2f} V, boot {r['vdc14_boot']})")
    if r['faults']:
        print(f"  fault: core {r['fault_cpu']} dbg {r['fault_dbg']:08X} emu {r['fault_emu']:08X} "
              f"pc {r['fault_pc']:08X} rets {r['fault_rets']:08X}")


def restart(port, settle):
    dev = connect(port)
    try:
        fi.BackupLink(dev.link).request(fi.BK_RESTART, [], 1.5)
    except fi.InstallError:
        pass
    finally:
        dev.link.close()                    # (at once: the FM-1 leaves the bus 150 ms after its answer)
    time.sleep(3)
    end = time.monotonic() + 30
    while True:
        try:
            r = command(port, 0)
            break
        except Unsupported:
            raise
        except (SystemExit, fi.InstallError, OSError) as e:
            if time.monotonic() > end:
                raise SystemExit(f'the FM-1 did not come back: waited 33 s after RESTART for it to answer command 75 '
                                 f'(the last try: {e})')
            time.sleep(0.5)
    print(f'restarted; waiting {settle:.0f} s for the boot guard')
    time.sleep(settle)
    return command(port, 0)


def run(port, mode, settle, do_restart):
    print(f'== {mode}')
    before = restart(port, settle) if do_restart else command(port, 0)
    show('before', before)
    if before['c1'] != 1:
        raise SystemExit('core 1 is not running ("ok"): Options > Dual Core On, then restart')
    ok = before['sysvdd'] >= 14 and before['vdc14'] >= 4
    if not ok:
        print('  the boot rails are not up (want SYSVDD >= 14, VDC14 >= 4)')
    hit = command(port, OPS[mode])
    show('inject', hit)
    time.sleep(1.0)
    after = command(port, 0)
    show('after ', after)
    running = after['halves'] > hit['halves']
    ok = ok and hit['rc'] == 0 and running
    if mode == 'misread':                   # the first: kept, the rails up; the second: given up
        ok = ok and after['c1'] == 1 and after['rejected'] > 0 and not after['fallback']
        print(f"  first misread: {'counted, core 1 kept' if ok else 'NOT ANSWERED'}")
        second = command(port, OPS[mode])
        time.sleep(1.0)
        after = command(port, 0)
        show('second', after)
        ok = ok and second['rc'] == 0 and after['c1'] == 3 and after['fallback'] == 1 and after['halves'] > second['halves']
    elif mode == 'null':
        ok = ok and after['c1'] == 1 and after['rejected'] > 0 and not after['faults']
    elif mode == 'stall':
        ok = ok and after['c1'] == 3 and after['stalls'] == 1 and after['fallback'] == 1
    elif mode == 'fault':
        ok = ok and after['c1'] == 3 and (after['faults'] > 0 or after['stalls'] > 0) and after['fallback'] == 1
        print('  caught by ' + ('core 1 itself' if after['faults'] and after['fault_cpu'] else
                                'core 0 (the debug unit)' if after['faults'] else 'nobody: the 8 ms wait'))
    print(f"{mode}: {'PASS' if ok else 'FAIL'} (audio {'running' if running else 'STOPPED'})")
    return ok


def main():
    p = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    p.add_argument('modes', nargs='*', default=['misread', 'null', 'stall', 'fault'],
                   choices=[*OPS, *(f'sysvdd={n}' for n in range(6, 15)), *(f'vdc14={n}' for n in range(5))])
    p.add_argument('--port', help='MIDI port name (default: found as fm1_install.py finds it)')
    p.add_argument('--settle', type=float, default=31, help='seconds after a restart before injecting')
    p.add_argument('--no-restart', action='store_true', help='inject into the running session')
    a = p.parse_args()
    show('build', command(a.port, 0))       # (refuses here on a build without CR_CORE1_TEST)
    results = []
    for mode in a.modes:
        if mode in ('report', 'power'):
            show(mode, command(a.port, OPS[mode]))
        elif '=' in mode:
            rail, level = mode.split('=')
            r = command(a.port, 6, (0 if rail == 'sysvdd' else 1, int(level)))
            show(mode, r)
            if r['rc']:
                raise SystemExit(f'{mode}: refused')
        else:
            results.append(run(a.port, mode, a.settle, not a.no_restart))
    if results and not all(results):
        raise SystemExit(1)


if __name__ == '__main__':
    fi.leave(fi.exit_code(main))            # (the exit codes as before; no MIDI worker is left, nothing holds the exit)

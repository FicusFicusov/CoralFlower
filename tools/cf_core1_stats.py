#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-only
"""Bounded passive HOME/voice-work capture. No notes, reset, configuration or flash writes.

rc13: --version 0.7.2-rc13 --baseline (HOME only).
rc14: --version 0.7.2-rc14 (HOME + worker). READY follows the first saved physical sample.
"""
import argparse
import json
import sys
import time
from pathlib import Path
from fm1_install import BackupError, InstallError, MidiBackend, Updater, bk_r32, open_backup
from cf_render_stats import decode as home_decode, interval as home_interval
from cf_usb_audio_stats import decode as usb_decode

NAMES = (
    'ms', 'ticks_per_us', 'hardware', 'core1_state', 'fallback', 'inflight',
    'jobs', 'voices0', 'voices1', 'faults', 'worker_ticks', 'wait_ticks', 'wait_max_ticks',
    'wall_us', 'serial_us', 'audio_halves', 'last_wall_us', 'last_serial_us', 'last_irq_us',
    'budget_us', 'ui_reserve_percent', 'audio_late', 'stack_used', 'rejected', 'core_faults',
    'boot_sysvdd', 'boot_vdc14', 'now_sysvdd', 'now_vdc14',
    'chord_held', 'chord_cap', 'bass_held', 'bass_cap', 'layer_held', 'layer_cap', 'drums_held', 'drums_cap',
)


def decode(data):
    if len(data) != 2 + 5 * len(NAMES) or bytes(data[:2]) != bytes((1, len(NAMES))):
        raise BackupError('unrecognized CoralFlower voice-work schema')
    result = {k: bk_r32(data, 2 + 5*i) for i, k in enumerate(NAMES)}
    if result['hardware'] not in (0, 1) or result['ticks_per_us'] not in (1, 24):
        raise BackupError('invalid voice-work timing source')
    return result


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--port', default='ChoralRoot FM-1')
    parser.add_argument('--version', default='0.7.2-rc14')
    parser.add_argument('--baseline', action='store_true')
    parser.add_argument('--samples', type=int, choices=range(2, 62), default=41)
    parser.add_argument('--out', type=Path, required=True)
    args = parser.parse_args()
    # Refuse overwrite before opening any MIDI client.
    with args.out.open('x') as f:
        f.write('{}\n')
    report = dict(version=None, samples=[], intervals=[])
    link = None

    def save():
        temporary = args.out.with_suffix(args.out.suffix+'.tmp')
        temporary.write_text(json.dumps(report, indent=2)+'\n')
        temporary.replace(args.out)

    try:
        link, info = open_backup(Updater(MidiBackend(), args.port))
        if info[1] != 'coralflower' or args.version not in info[0]:
            raise BackupError('installed version differs from the prepared capture')
        report['version'] = info[0]
        report['usb_before'] = usb_decode(link.request(80, []))
        for i in range(args.samples):
            sample = {'home': home_decode(link.request(84, []))}
            if not args.baseline:
                sample['worker'] = decode(link.request(85, []))
            if any(s['hardware'] != 1 for s in sample.values()):
                raise BackupError('physical timing is unavailable')
            if report['samples']:
                report['intervals'].append(home_interval(report['samples'][-1]['home'], sample['home']))
            report['samples'].append(sample); save()
            if not i:
                print('READY: first physical sample saved. Start the prepared HOME chord scenario.', flush=True)
            if i+1 < args.samples:
                time.sleep(1)
        report['usb_after'] = usb_decode(link.request(80, []))
        return 0
    except (InstallError, OSError, TimeoutError, KeyboardInterrupt) as error:
        report['error'] = str(error)
        print(str(error), file=sys.stderr)
        return 1
    finally:
        if link:
            link.link.close()
        save()


if __name__ == '__main__':
    raise SystemExit(main())

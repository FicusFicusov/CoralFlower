#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-only
"""Actual PCM + voice coverage, threaded ownership/fallback and broken-code controls.

Optional --baseline ROOT compiles the SAME scenario against a pre-change tree.
--mutants proves missing join / shared FM scratch are caught; copies only to tmp.
This checks DSP correctness, not device timing, electrical margin or physical FPS.
"""
import argparse
import hashlib
import json
import re
import shutil
import subprocess
import tempfile
from pathlib import Path

ROOT = Path(__file__).resolve().parent.parent


def build(root, output, diagnostic=1):
    subprocess.run(["cc", "-std=gnu11", "-O2", "-g", "-w", "-pthread", "-DEMU_WEB",
                    f"-DCF_EVENT_DIAG={diagnostic}", "-Itools/emu/web/compat", "-Ibuild/gen", "-Ifirmware/src",
                    "tests/cf_core1_test.c", "-lm", "-o", str(output)], cwd=root, check=True)


def run(exe, mode, preset, out, label=None, must_pass=True):
    stem = out / f"{label or mode}-{preset}"
    proc = subprocess.run([str(exe), mode, str(preset), str(stem.with_suffix('.pcm'))],
                          capture_output=True, text=True, timeout=30)
    stem.with_suffix('.log').write_text(proc.stdout + proc.stderr)
    if must_pass and proc.returncode:
        raise AssertionError(proc.stdout + proc.stderr)
    data = stem.with_suffix('.pcm').read_bytes()
    fields = dict(re.findall(r"\b(frames|coverage|peak|parts)=(\w+)", proc.stdout))
    return dict(mode=label or mode, preset=preset, rc=proc.returncode,
                sha256=hashlib.sha256(data).hexdigest(), bytes=len(data), **fields)


def main():
    ap = argparse.ArgumentParser(description=__doc__)
    ap.add_argument('--baseline', type=Path)
    ap.add_argument('--mutants', action='store_true')
    ap.add_argument('--out', type=Path, default=ROOT/'build/core1-test')
    args = ap.parse_args()
    out = args.out.resolve(); out.mkdir(parents=True, exist_ok=True)
    exe = out/'core1-test'; build(ROOT, exe)
    baseline = None
    if args.baseline:
        root = args.baseline.resolve()
        shutil.copy2(ROOT/'tests/cf_core1_test.c', root/'tests/cf_core1_test.c')
        baseline = out/'baseline-test'; build(root, baseline)
    result = []
    for preset in (0, 25, 30):
        serial = run(exe, 'serial', preset, out); result.append(serial)
        for mode in ('parallel', 'gated', 'refuse', 'nostart'):
            row = run(exe, mode, preset, out); result.append(row)
            for k in ('sha256', 'bytes', 'frames', 'coverage', 'peak', 'parts'):
                assert row[k] == serial[k], (preset, mode, k, row, serial)
        if baseline:
            row = run(baseline, 'serial', preset, out, 'baseline'); result.append(row)
            for k in ('sha256', 'bytes', 'frames', 'coverage', 'peak', 'parts'):
                assert row[k] == serial[k], ('baseline differs', preset, k)
        print(f'PASS preset {preset}: exact PCM, frames, voices; parallel / delayed / refused / failed startup', flush=True)
    for preset in (0, 25, 30):
        result.append(run(exe, 'partial', preset, out))
    print('PASS partially advanced FM/VA voices: held worker, no duplicate render, gain ramp and serial continuation', flush=True)
    if args.mutants:
        with tempfile.TemporaryDirectory(prefix='cf-core1-') as td:
            root = Path(td)
            for name in ('firmware', 'tools/emu', 'tests'):
                shutil.copytree(ROOT/name, root/name)
            (root/'build').mkdir(); (root/'build/gen').symlink_to(ROOT/'build/gen', target_is_directory=True)
            for name, file, before, after, mode in (
                ('missing-join', 'firmware/src/cf_core1.h', 'if (cr_c1_wait()) {', 'if (0) {', 'gated'),
                ('shared-scratch', 'firmware/src/eng_fm6.c', '&fm6_scr[core]', '&fm6_scr[0]', 'parallel'),
            ):
                p = root/file; healthy = p.read_text(); assert before in healthy
                p.write_text(healthy.replace(before, after, 1))
                mutant = out/name; build(root, mutant)
                row = run(mutant, mode, 0, out, name, must_pass=False); result.append(row)
                good = next(r for r in result if r['mode'] == 'serial' and r['preset'] == 0)
                assert row['rc'] or row['sha256'] != good['sha256'], f'oracle missed {name}'
                p.write_text(healthy)
                print(f'PASS negative control: {name} rejected', flush=True)
    (out/'results.json').write_text(json.dumps(result, indent=2)+'\n')


if __name__ == '__main__':
    main()

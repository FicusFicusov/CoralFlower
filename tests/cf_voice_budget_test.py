#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-only
"""Real ISR/admission/MIX_PART with controlled DSP time; no hardware timing claim.

The fixture supplies independent per-core work and IRQ time. Production budget
functions are extracted verbatim, so a stale copy cannot silently keep passing.
--mutants proves the important regressions are detected in build-only copies.
"""
import argparse
import hashlib
import json
import subprocess
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]


def run_variant(out, name, sources):
    folder = out / name
    folder.mkdir(parents=True, exist_ok=True)
    source = sources['voice.c']
    start = source.index('#define VL_HALF_US')
    end = source.index('/* voices the engine may use', start)
    functions = '#if CF_VLOAD\n' + source[start:end]
    start = source.index('#if CF_VLOAD\n', source.index('static int voice_tail_yield'))
    end = source.index('/* the held voice a fifth', start)
    (folder / 'budget_functions.inc').write_text(functions + source[start:end] + '\n#endif\n')
    clock = next(line for line in sources['coralflower.c'].splitlines()
                 if line.startswith('#define VL_NOW()') and 'cf_c1' in line)
    (folder / 'budget_clock.inc').write_text(clock + '\n')
    source = sources['fx.c']
    start = source.index('#if CF_VLOAD                                            /* (voice.c: each part')
    end = source.index('static void mix_block(', start)
    (folder / 'mix_macro.inc').write_text(source[start:end])
    (folder / 'audio.c').write_text(sources['audio.c'])
    (folder / 'test.c').write_text((ROOT / 'tests/cf_voice_budget_test.c').read_text())
    compile_run = subprocess.run(['cc', '-std=gnu11', '-O2', '-Wall', '-Wextra',
                                 '-Wno-unused-function', '-Wno-unused-variable',
                                 str(folder / 'test.c'), '-o', str(folder / 'test')],
                                capture_output=True, text=True, timeout=30)
    (folder / 'compile.log').write_text(compile_run.stdout + compile_run.stderr)
    if compile_run.returncode:
        raise RuntimeError(compile_run.stdout + compile_run.stderr)
    result = subprocess.run([str(folder / 'test')], capture_output=True, text=True, timeout=10)
    (folder / 'result.log').write_text(result.stdout + result.stderr)
    return dict(name=name, returncode=result.returncode, stdout=result.stdout, stderr=result.stderr,
                source_sha256={key: hashlib.sha256(value.encode()).hexdigest()
                               for key, value in sources.items()})


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--out', type=Path, default=ROOT / 'build/voice-budget-test')
    parser.add_argument('--mutants', action='store_true')
    args = parser.parse_args()
    out = args.out.resolve()
    sources = {name: (ROOT / 'firmware/src' / name).read_text()
               for name in ('voice.c', 'audio.c', 'coralflower.c', 'fx.c')}
    results = [run_variant(out, 'production', sources)]
    if args.mutants:
        mutations = (
            ('linear-average-trap', 'if (cr_dualcore_active() && vl_kernel_cost[p]',
             'if (0 && vl_kernel_cost[p]'),
            ('ignore-slower-core', 'main > worker ? main : worker', 'worker'),
            ('retain-refused-discount', '(main_voices > 1u && !worker_voices)', '0'),
            ('learn-mixed-half',
             'discard = (vl_parallel[p].worker_voices && !cr_dualcore_active()) ||\n'
             '                (vl_cost[p] && ((vl_mark[p] ^ m) & 0x8000u)) ||\n'
             '                (vl_parallel[p].invalid && vl_parallel[p].worker_voices);',
             'discard = 0;'),
            ('learn-first-fallback',
             '(vl_parallel[p].worker_voices && !cr_dualcore_active())', '0'),
        )
        for name, original, broken in mutations:
            assert sources['voice.c'].count(original) == 1, f'mutation target changed: {name}'
            mutant = dict(sources)
            mutant['voice.c'] = mutant['voice.c'].replace(original, broken, 1)
            results.append(run_variant(out, name, mutant))
    (out / 'results.json').write_text(json.dumps(results, indent=2) + '\n')
    for result in results:
        print(result['name'], 'rc=' + str(result['returncode']), result['stdout'].splitlines()[-1])
    assert results[0]['returncode'] == 0, results[0]['stderr']
    assert all(result['returncode'] != 0 for result in results[1:]), 'a broken budget escaped its oracle'


if __name__ == '__main__':
    main()

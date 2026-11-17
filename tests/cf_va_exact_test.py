#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-only
"""Exact PCM and complete VA state against a specified pre-change source.

Streams actual render records from two native executables, compares every byte,
and stores per-case SHA256 evidence. Mutants are temporary source copies. Native
CPU time is an observation about this host, never a device/FPS claim.
"""
import argparse
import hashlib
import json
import platform
import statistics
import struct
import subprocess
from pathlib import Path

ROOT = Path(__file__).resolve().parent.parent
GENERAL = "    y = (w.x * x + w.b * ((kd * v1) >> 12) + w.l * v2) >> 10;"
LP = "    return soft_knee(clamp(tsvf_lp(c, x, ic1, ic2), -200000, 200000), 16000);"


def replace_once(source, before, after):
    assert source.count(before) == 1, f"expected exactly one source location: {before}"
    return source.replace(before, after, 1)


def compile_source(source, name, out, instrument=False):
    if instrument:
        source = replace_once(source, GENERAL, "    va_test_output_multiplies += 4;\n" + GENERAL)
        if LP in source:
            source = replace_once(source, LP, "    va_test_lp_samples++;\n" + LP)
    source_path = out / f"{name}.c"
    source_path.write_text(source)
    exe = out / name
    command = ["cc", "-std=gnu11", "-Os", "-g", "-w", "-Ibuild/gen",
               f'-DVA_ENGINE_SOURCE="{source_path}"', "tests/cf_va_exact_test.c", "-o", str(exe)]
    subprocess.run(command, cwd=ROOT, check=True, capture_output=True)
    return exe


def read_exact(pipe, size):
    data = bytearray()
    while len(data) < size:
        chunk = pipe.read(size - len(data))
        if not chunk:
            raise AssertionError(f"render stream truncated: wanted {size}, got {len(data)} bytes")
        data.extend(chunk)
    return data


def compare(before, after, save=None, mode="verify", count_operations=True):
    procs = [subprocess.Popen([str(exe), mode], stdout=subprocess.PIPE, stderr=subprocess.PIPE)
             for exe in (before, after)]
    rows = []
    try:
        headers = [read_exact(p.stdout, 20) for p in procs]
        assert headers[0] == headers[1], "render stream schema changed"
        magic, cases, ticks, pcm_size, state_size = struct.unpack("=5I", headers[0])
        assert magic == 0x56414551 and pcm_size == 256
        assert (cases, ticks) == ((5, 32768) if mode == "cycles" else (123, 1536))
        assert state_size > 2000, "state coverage is unexpectedly empty"
        record_size = pcm_size + state_size + 16
        for case in range(cases):
            data = [read_exact(p.stdout, ticks * record_size) for p in procs]
            ops = [struct.unpack("=2Q", read_exact(p.stdout, 16)) for p in procs]
            if data[0] != data[1]:
                offset = next(i for i, pair in enumerate(zip(*data)) if pair[0] != pair[1])
                tick, in_record = divmod(offset, record_size)
                region = "PCM" if in_record < pcm_size else "state" if in_record < pcm_size + state_size else "coverage"
                return {"matched": False, "case": case, "tick": tick, "region": region,
                        "record_byte": in_record, "before": data[0][offset], "after": data[1][offset]}
            pcm_hashes, state_hashes = [], []
            for variant, raw in zip(("before", "after"), data):
                pcm_hash, state_hash = hashlib.sha256(), hashlib.sha256()
                artifact = (save / f"{variant}-case-{case}.s32le").open("wb") if save and case in (22, 27, 29) else None
                for tick in range(ticks):
                    start = tick * record_size
                    pcm = memoryview(raw)[start:start + pcm_size]
                    state = memoryview(raw)[start + pcm_size:start + pcm_size + state_size]
                    pcm_hash.update(pcm); state_hash.update(state)
                    if artifact:
                        artifact.write(pcm)
                if artifact:
                    artifact.close()
                pcm_hashes.append(pcm_hash.hexdigest()); state_hashes.append(state_hash.hexdigest())
            rendered, audible, sides, finished = struct.unpack_from("=4I", data[0], len(data[0]) - 16)
            assert rendered and audible, f"case {case}: no rendered signal"
            if count_operations:
                assert ops[0][0] == ops[1][0] + 4 * ops[1][1], (case, "operation accounting", ops)
            if mode == "cycles":
                assert finished == 16, f"case {case}: two complete 8-voice releases were not observed"
            rows.append(dict(case=case, ticks=ticks, rendered_voice_blocks=rendered,
                             audible_frames=audible, side_voice_blocks=sides, finished_voices=finished,
                             before_pcm_sha256=pcm_hashes[0], after_pcm_sha256=pcm_hashes[1],
                             before_state_sha256=state_hashes[0], after_state_sha256=state_hashes[1],
                             before_output_multiplies=ops[0][0], after_output_multiplies=ops[1][0],
                             lp_samples=ops[1][1]))
        for p in procs:
            assert not p.stdout.read(1), "unexpected trailing stream bytes"
            assert p.wait(timeout=10) == 0, p.stderr.read().decode()
        assert sum(row["lp_samples"] for row in rows) > 0, "LP optimization was never exercised"
        return dict(matched=True, cases=rows, state_bytes_per_tick=state_size,
                    compared_ticks=cases * ticks, compared_pcm_frames=cases * ticks * 32)
    finally:
        for p in procs:
            if p.poll() is None:
                p.kill()
            p.wait()


def benchmark(before, after, repeats, ticks):
    rows = []
    for case, label in ((20, "MORPH PAD"), (22, "WIDE STRINGS"), (27, "AURORA"),
                        (29, "DREAM BELLS"), (25, "BREATH PAD"), (8, "PWM KEYS")):
        times = {"before": [], "after": []}
        checksums = set()
        for repeat in range(repeats):
            order = (("before", before), ("after", after))
            for variant, exe in order if repeat % 2 == 0 else reversed(order):
                run = subprocess.run([str(exe), "bench", str(case), str(ticks)], check=True,
                                     capture_output=True, text=True, timeout=30)
                fields = dict(field.split("=") for field in run.stdout.split())
                assert int(fields["ticks"]) == ticks and int(fields["voices"]) == 8
                assert int(fields["rendered"]) == ticks * 8 and int(fields["audible"]) > 0
                times[variant].append(float(fields["seconds"]))
                checksums.add(fields["checksum"])
        assert len(checksums) == 1, (case, "benchmark output changed", checksums)
        medians = {variant: statistics.median(values) for variant, values in times.items()}
        rows.append(dict(case=case, label=label, ticks=ticks, repeats=repeats, times=times,
                         rendered_voice_blocks_per_sample=ticks * 8,
                         medians=medians, change_percent=(medians["after"] / medians["before"] - 1) * 100))
    return rows


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--baseline", type=Path, required=True)
    parser.add_argument("--out", type=Path, default=ROOT / "build/quality-opt-20261117/va")
    parser.add_argument("--mutants", action="store_true")
    parser.add_argument("--bench", action="store_true")
    args = parser.parse_args()
    out = args.out.resolve(); out.mkdir(parents=True, exist_ok=True)
    old = args.baseline.read_text(); new = (ROOT / "firmware/src/eng_va.c").read_text()
    result = {"baseline_source_sha256": hashlib.sha256(old.encode()).hexdigest(),
              "candidate_source_sha256": hashlib.sha256(new.encode()).hexdigest(),
              "host": platform.platform(), "compiler": subprocess.check_output(["cc", "--version"], text=True),
              "flags": "-std=gnu11 -Os -g -w", "device_timing": False}
    before = compile_source(old, "before-counted", out, True)
    after = compile_source(new, "after-counted", out, True)
    result["exact"] = compare(before, after, out)
    assert result["exact"]["matched"], result["exact"]
    print(f"PASS exact PCM, state and voice coverage: {len(result['exact']['cases'])} cases", flush=True)
    result["release_cycles"] = compare(before, after, mode="cycles")
    assert result["release_cycles"]["matched"], result["release_cycles"]
    print("PASS two complete releases and fresh onsets, with 2 s silence: 5 representative long-tail presets", flush=True)
    if args.mutants:
        result["mutants"] = {}
        mutants = {
            "lp-output-plus-one": replace_once(new, LP, LP.replace(", 16000);", ", 16000) + 1;")),
            "control-age-plus-one": replace_once(new, "        s->ticks++;", "        s->ticks += 2;"),
        }
        for name, source in mutants.items():
            mutant = compile_source(source, name, out, False)
            caught = compare(before, mutant, count_operations=False)
            assert not caught["matched"], f"oracle missed {name}"
            result["mutants"][name] = caught
            print(f"PASS mutant {name}: case {caught['case']} tick {caught['tick']} {caught['region']} byte {caught['record_byte']}", flush=True)
    if args.bench:
        result["native_benchmark"] = benchmark(compile_source(old, "before-native", out),
                                                compile_source(new, "after-native", out), 9, 100000)
        for row in result["native_benchmark"]:
            print(f"NATIVE {row['label']}: {row['change_percent']:+.2f}% CPU time", flush=True)
    (out / "results.json").write_text(json.dumps(result, indent=2) + "\n")


if __name__ == "__main__":
    main()

#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-only
"""Read passive HOME timing (diagnostic rc13+), without notes/reset/save/arming.

One bounded capture: --samples 21 --interval 1. Stay on HOME, play Dream VA
for several seconds, then FM; intervals crossing a preset/popup are rejected.
The rate is changed-flora LCD submissions, NOT the panel's scanout FPS.
"""
import argparse
import json
import sys
import time
from fm1_install import BackupError, InstallError, MidiBackend, Updater, bk_r32, open_backup
from cf_usb_audio_stats import decode as decode_usb

NAMES = (
    "ms", "ticks", "ticks_per_us", "hardware", "irq_audio_ticks", "irq_timer_ticks",
    "screen", "popup", "sound_chord", "algorithm", "key", "chord_notes", "last_update_ticks",
    "frames", "updates", "gaps", "gap_ticks", "gap_max_ticks", "gap_le20ms", "gap_20_33ms",
    "gap_33_50ms", "gap_50_100ms", "gap_gt100ms", "frame_wall_ticks", "frame_cpu_ticks",
    "compose_wall_ticks", "compose_cpu_ticks", "lcd_wait_wall_ticks", "lcd_wait_cpu_ticks",
    "lcd_wait_calls", "frame_max_ticks", "frame_audio_ticks", "frame_timer_ticks",
    "stamp_calls", "stamp_pair_min_ticks", "scene_epoch",
)


def decode(data):
    if len(data) != 2 + 5 * len(NAMES) or bytes(data[:2]) != bytes((1, len(NAMES))):
        raise BackupError("unrecognized CoralFlower HOME profile schema")
    result = {k: bk_r32(data, 2 + i * 5) for i, k in enumerate(NAMES)}
    if result["ticks_per_us"] not in (1, 24) or result["hardware"] not in (0, 1):
        raise BackupError("invalid HOME profile clock/source")
    return result


def interval(a, b):
    d = lambda k: (b[k] - a[k]) & 0xffffffff
    elapsed = d("ms")
    reject = None
    if not (a["hardware"] == b["hardware"] == 1):
        reject = "physical timing unavailable in emulator"
    elif not 0 < elapsed <= 11000 or a["ticks_per_us"] != b["ticks_per_us"]:
        reject = "restart or unsupported interval"
    elif any(a[k] != b[k] for k in ("screen", "sound_chord", "algorithm", "key")):
        reject = "scene changed across snapshots"
    elif a["scene_epoch"] != b["scene_epoch"]:
        reject = "HOME eligibility/scene changed between snapshots"
    elif a["screen"] != 0 or a["popup"] or b["popup"]:
        reject = "HOME without popup required"
    elif not d("frames"):
        reject = "no completed eligible HOME frame"
    if reject:
        return {"valid": False, "reason": reject, "elapsed_ms": elapsed}
    tick = a["ticks_per_us"]
    span = d("ticks")
    if abs(span / tick - elapsed * 1000) > 3000:
        return {"valid": False, "reason": "inconsistent clock span", "elapsed_ms": elapsed}
    frames = d("frames")
    cpu, compose, wait = d("frame_cpu_ticks"), d("compose_cpu_ticks"), d("lcd_wait_cpu_ticks")
    wall = d("frame_wall_ticks")
    audio, timer = d("frame_audio_ticks"), d("frame_timer_ticks")
    if cpu < compose + wait or wall != cpu + audio + timer:
        return {"valid": False, "reason": "timing conservation failed", "elapsed_ms": elapsed}
    return {
        "valid": True, "elapsed_ms": elapsed, "sound_chord": b["sound_chord"],
        "algorithm": b["algorithm"], "key": b["key"], "home_frames": frames,
        "changed_flora_submissions": d("updates"),
        "flora_submissions_per_s": d("updates") * tick * 1e6 / span,
        "submission_gaps": {k: d(k) for k in NAMES[18:23]},
        "mean_submission_gap_ms": d("gap_ticks") / tick / 1000 / d("gaps") if d("gaps") else None,
        "mean_frame_ms": wall / tick / 1000 / frames,
        "mean_compose_cpu_ms": compose / tick / 1000 / frames,
        "mean_present_cpu_ms": (cpu - compose - wait) / tick / 1000 / frames,
        "mean_lcd_poll_cpu_ms": wait / tick / 1000 / frames,
        "mean_lcd_wait_wall_ms": d("lcd_wait_wall_ticks") / tick / 1000 / frames,
        "mean_frame_audio_ms": audio / tick / 1000 / frames,
        "mean_frame_timer5_ms": timer / tick / 1000 / frames,
        "audio_irq_fraction": d("irq_audio_ticks") / span,
        "timer5_irq_fraction": d("irq_timer_ticks") / span,
        # A calibrated tuple-read footprint, NOT a bound on the whole observer.
        "stamp_calls": d("stamp_calls"),
        "stamp_pair_min_us": b["stamp_pair_min_ticks"] / tick,
    }


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--port", help="MIDI port name filter")
    parser.add_argument("--samples", type=int, choices=range(2, 32), default=21)
    parser.add_argument("--interval", type=float, default=1)
    args = parser.parse_args()
    if not .25 <= args.interval <= 10:
        parser.error("interval must be between 0.25 and 10 seconds")
    link = None
    result = {"firmware": None, "samples": [], "intervals": []}
    try:
        link, info = open_backup(Updater(MidiBackend(), args.port))
        result["firmware"] = info[0]
        if info[1] != "coralflower":
            raise BackupError("connected device is not CoralFlower")
        result["usb_before"] = decode_usb(link.request(80, []))
        for i in range(args.samples):
            if i:
                time.sleep(args.interval)
            sample = decode(link.request(84, []))
            if result["samples"]:
                result["intervals"].append(interval(result["samples"][-1], sample))
            result["samples"].append(sample)
        result["usb_after"] = decode_usb(link.request(80, []))
        return 0
    except (InstallError, TimeoutError) as error:
        result["error"] = str(error)
        print(str(error), file=sys.stderr)
        return 1
    finally:
        if link:
            link.link.close()
        print(json.dumps(result, indent=2))


if __name__ == "__main__":
    raise SystemExit(main())

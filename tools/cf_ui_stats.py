#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-only
"""Read passive UI timing/input counters from a CoralFlower diagnostic rc11+.

Command83 only reads RAM, including maxima retained after a temporary stall.
It neither arms ARP tracing nor clears counters, saves, resets or sends notes.
Use --samples 2 --interval 1 for two bounded snapshots during a repeat. Ordinary
releases/older diagnostic builds do not implement this optional command.
"""
import argparse
import json
import sys
import time

from fm1_install import BackupError, InstallError, MidiBackend, Updater, bk_r32, open_backup

PHASES = ("none", "housekeeping", "sounds", "note_input", "sequence",
          "controls", "settings", "frame", "draw", "idle")
NAMES = (
    "ms", "hardware_counters", "ui_frames", "input_calls", "input_gap_max_ms",
    "button_calls", "encoder_calls", "last_control", "control_delta", "control_ms",
    "phase", "phase_age_ms",
    "max_housekeeping_ms", "max_sounds_ms", "max_note_input_ms", "max_sequence_ms",
    "max_controls_ms", "max_settings_ms", "max_frame_ms", "max_draw_ms", "max_idle_ms",
    "screen", "page", "pattern", "track", "held_flags", "transport_flags", "clock_last_ms",
    "physical_keys", "external_keys", "input_buttons", "input_button_edges", "input_note_keys",
    "input_note_edges", "scan_frames", "scan_ticks", "scan_age_us", "scan_gap_max_us",
    "lcd_timeouts", "lcd_tiles_sent", "raw_rows_0", "raw_rows_1", "raw_rows_2",
    "encoder_steps_0", "encoder_steps_1", "encoder_steps_2", "encoder_steps_3",
    "encoder_steps_4", "encoder_steps_5", "encoder_steps_6", "audio_halves",
)


def decode(data):
    if len(data) != 2 + 5 * len(NAMES) or bytes(data[:2]) != bytes((1, len(NAMES))):
        raise BackupError("unrecognized CoralFlower UI diagnostic schema")
    values = {name: bk_r32(data, 2 + 5 * i) for i, name in enumerate(NAMES)}
    for name in ("control_delta", *(f"encoder_steps_{i}" for i in range(7))):
        if values[name] & 0x80000000:
            values[name] -= 0x100000000
    phase = values["phase"]
    values["phase_name"] = PHASES[phase] if phase < len(PHASES) else "unknown"
    return values


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--port", help="MIDI port name filter")
    parser.add_argument("--samples", type=int, choices=range(1, 6), default=1)
    parser.add_argument("--interval", type=float, default=1.0, help="seconds between snapshots (0.1–10)")
    args = parser.parse_args()
    if not 0.1 <= args.interval <= 10:
        parser.error("--interval must be between 0.1 and 10 seconds")
    link = None
    samples = []
    firmware = None
    try:
        link, info = open_backup(Updater(MidiBackend(), args.port))
        firmware = info[0]
        if info[1] != "coralflower":
            raise BackupError("the connected device is not CoralFlower")
        for i in range(args.samples):
            if i:
                time.sleep(args.interval)
            samples.append(decode(link.request(83, [])))
        print(json.dumps({"firmware": info[0], "samples": samples}, indent=2))
        return 0
    except (InstallError, TimeoutError) as error:
        # A second read timing out must not discard the first live snapshot.
        print(json.dumps({"firmware": firmware, "samples": samples, "error": str(error)}, indent=2))
        print(str(error), file=sys.stderr)
        return 1
    finally:
        if link:
            link.link.close()


if __name__ == "__main__":
    raise SystemExit(main())

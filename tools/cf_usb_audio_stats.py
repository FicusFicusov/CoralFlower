#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-only
"""Read CoralFlower USB/audio counters over MIDI without changing the device.

Run before and after a DAW recording; compare counter deltas. Requires python-rtmidi,
as tools/fm1_install.py does. No update, restore, reset or MIDI notes are sent.
"""
import argparse
import json
import sys

from fm1_install import BackupError, InstallError, MidiBackend, Updater, bk_r32, open_backup

NAMES = (
    "ms", "sample_rate", "channels", "stream_open", "host_not_reading", "suspended", "buffer_frames",
    "underruns", "overruns", "packets", "missed_usb_frames", "max_packet_gap_us", "max_service_gap_us",
    "max_service_us", "resample_avg_us", "resample_max_us", "stream_opens", "rate_changes", "audio_halves",
    "audio_last_us", "audio_max_us", "audio_late", "audio_stale", "second_core_state", "flash_erases",
    "flash_audible_holes", "host_holds",
)


def decode(data):
    if len(data) < 2 or data[0] != 1 or data[1] != len(NAMES) or len(data) != 2 + 5 * data[1]:
        raise BackupError("unrecognized CoralFlower USB audio diagnostic schema")
    return {name: bk_r32(data, 2 + 5 * i) for i, name in enumerate(NAMES)}


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--port", help="MIDI port name filter")
    args = parser.parse_args()
    link = None
    try:
        link, info = open_backup(Updater(MidiBackend(), args.port))
        if info[1] != "coralflower":
            raise BackupError("the connected device is not CoralFlower")
        values = decode(link.request(80, []))
        print(json.dumps({"firmware": info[0], **values}, indent=2))
        return 0
    except (InstallError, TimeoutError) as error:
        print(str(error), file=sys.stderr)
        return 1
    finally:
        if link:
            link.link.close()


if __name__ == "__main__":
    sys.exit(main())

#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-only
"""Read the diagnostic ARP capture (INFO/82 only), or align it with a raw WAV.

The device captures ten seconds after the last actual ARP MODE change. Read it
afterwards; this command never arms, clears, saves, plays, or updates anything.
Offline: --trace capture.json --wav recording.wav (requires numpy).
"""
import argparse
import json
import struct
import sys
from pathlib import Path

from fm1_install import BackupError, InstallError, MidiBackend, Updater, bk_r32, bk_u32, open_backup

META = ("generation", "active", "ticks_per_us", "trigger_ms", "trigger_ticks", "duration_ticks", "offset",
        "main_count", "audio_count", "usb_count", "main_dropped", "audio_dropped", "usb_dropped",
        "main_max_emit_ticks", "audio_max_emit_ticks", "usb_max_emit_ticks",
        "flash_windows", "last_flash_at", "last_flash_ticks")
LANES = ("main", "audio", "usb")
KINDS = (None, "mode", "config", "save_begin", "save_end", "flash_begin", "flash_end", "flash_slow",
         "shed", "shed_request", "audio_late", "block_late", "audio_state", "voices", "arp_note", "pcm",
         "usb_arm_gap", "usb_service_gap", "usb_ring", "usb_stream", "context", "flash_window")
CAPACITY = (64, 128, 64)


def decode_page(data):
    if (len(data) < 4 + 5 * len(META) or data[:1] != [1] or data[1] >= 3 or
            data[2] != len(META) or any(not 0 <= x < 128 for x in data)):
        raise BackupError("unrecognized event-trace schema")
    lane = data[1]
    m = dict(zip(META, (bk_r32(data, 3 + 5 * i) for i in range(len(META)))))
    at = 3 + 5 * len(META)
    count = data[at]
    if count > 8 or len(data) != at + 1 + 20 * count:
        raise BackupError("invalid event-trace page length")
    if (m["ticks_per_us"] not in (1, 24) or m["active"] > 1 or
            m["duration_ticks"] != 10_000_000 * m["ticks_per_us"] or
            any(m[f"{name}_count"] > cap for name, cap in zip(LANES, CAPACITY)) or
            m["offset"] + count > m[f"{LANES[lane]}_count"]):
        raise BackupError("invalid event-trace metadata")
    # Each u32 uses five MIDI bytes, but the fifth has only four significant bits.
    starts = [3 + 5 * i for i in range(len(META))]
    starts += [at + 1 + 5 * i for i in range(count * 4)]
    if any(data[i + 4] > 15 for i in starts):
        raise BackupError("noncanonical event-trace u32")
    events = []
    for i in range(count):
        ticks, tag, a, b = (bk_r32(data, at + 1 + 20 * i + 5 * j) for j in range(4))
        kind = tag & 65535
        if not 0 < kind < len(KINDS) or ticks >= m["duration_ticks"]:
            raise BackupError("invalid event kind or timestamp")
        events.append(dict(lane=LANES[lane], index=m["offset"] + i, kind=KINDS[kind],
                           detail=tag >> 16, ticks=ticks, seconds=ticks / m["ticks_per_us"] / 1e6, a=a, b=b))
    return lane, m, events


def read_trace(link):
    baseline = None
    events = []
    for lane, name in enumerate(LANES):
        offset = 0
        while True:
            got_lane, meta, page = decode_page(link.request(82, [lane, *bk_u32(offset)]))
            if got_lane != lane or meta["offset"] != offset:
                raise BackupError("event-trace reply cursor differs from request")
            meta.pop("offset")
            if not meta["generation"]:
                raise BackupError("no ARP MODE change captured since boot")
            if meta["active"]:
                raise BackupError("capture is still running; read after ten seconds from the last MODE change")
            if baseline is None:
                baseline = meta
            elif meta != baseline:
                raise BackupError("capture changed while reading; discard these mixed pages and read again")
            events.extend(page)
            offset += len(page)
            if offset == meta[f"{name}_count"]:
                break
            if not page:
                raise BackupError("event-trace page omitted published records")
    if not any(e["kind"] == "mode" for e in events) or not any(e["kind"] == "config" for e in events):
        raise BackupError("capture lacks the MODE gesture or its audio-side configuration")
    omitted = sum(baseline[f"{name}_dropped"] for name in LANES)
    return dict(schema=1, metadata=baseline, capture_status="partial_overflow" if omitted else "complete_window",
                events=sorted(events, key=lambda e: (e["ticks"], LANES.index(e["lane"]), e["index"])),
                limits=["Diagnostic evidence, not a continuity PASS.",
                        "block_late uses measured block completion and an estimated DMA boundary.",
                        "flash_window is the most recent IRQ-off window, not proof that it caused an anomaly.",
                        "PCM anchors identify armed packet data; only a WAV match establishes host receipt."])


def wav_pcm16(path):
    """Read PCM16 or losslessly reduced PCM24 stereo, including WAVE_EXTENSIBLE."""
    import numpy as np
    raw = Path(path).read_bytes()
    if raw[:4] != b"RIFF" or raw[8:12] != b"WAVE":
        raise ValueError("expected a RIFF WAVE recording")
    fmt = pcm = None
    pos = 12
    while pos + 8 <= len(raw):
        ident, n = raw[pos:pos+4], struct.unpack_from("<I", raw, pos+4)[0]
        end = pos + 8 + n
        if end > len(raw):
            raise ValueError("truncated WAV chunk")
        if ident == b"fmt ":
            fmt = raw[pos+8:end]
        elif ident == b"data":
            if pcm is not None:
                raise ValueError("multiple WAV data chunks are unsupported")
            pcm = raw[pos+8:end]
        pos = end + (n & 1)
    if fmt is None or len(fmt) < 16 or pcm is None:
        raise ValueError("WAV lacks fmt or data")
    code, channels, rate, _, block, bits = struct.unpack_from("<HHIIHH", fmt)
    if code == 0xFFFE and len(fmt) >= 40:
        if fmt[24:40] != bytes.fromhex("0100000000001000800000aa00389b71"):
            raise ValueError("WAVE_EXTENSIBLE is not PCM")
        code = 1
    if code != 1 or channels != 2 or rate not in (44100, 48000) or bits not in (16, 24):
        raise ValueError("expected raw stereo PCM16/24 at 44100 or 48000 Hz")
    if block != channels * bits // 8 or len(pcm) % block:
        raise ValueError("invalid WAV frame alignment")
    samples = np.frombuffer(pcm, dtype=np.uint8)
    if bits == 24:
        samples = samples.reshape(-1, 3)
        if np.any(samples[:, 0]):
            raise ValueError("PCM24 is not exact PCM16; processing/dither prevents exact packet matching")
        samples = samples[:, 1:].copy().reshape(-1)
    return samples, rate


def match_wav(trace, path):
    import numpy as np
    data, rate = wav_pcm16(path)
    n = len(data) // 4 - 15
    if n <= 0:
        raise ValueError("recording has fewer than 16 stereo frames")
    hashes = np.full(n, 2166136261, dtype=np.uint32)
    for byte in range(64):
        hashes ^= data[byte:byte + 4 * n:4]
        hashes *= np.uint32(16777619)
    anchors = []
    for e in trace["events"]:
        if e["kind"] != "pcm":
            continue
        matches = np.flatnonzero(hashes == e["b"])
        anchors.append(dict(device_seconds=e["seconds"], device_frame=e["a"], hash=e["b"],
                            matches=len(matches), wav_frames=[int(x) for x in matches[:8]]))
    unique = [a for a in anchors if a["matches"] == 1]
    offsets = sorted({a["wav_frames"][0] - a["device_frame"] for a in unique})
    result = dict(path=str(Path(path).resolve()), sample_rate=rate, frames=len(data)//4, anchors=anchors,
                  unique_anchors=len(unique), frame_offsets=offsets,
                  aligned=len(unique) >= 2 and len(offsets) == 1,
                  limit="32-bit prefix hashes: multiple consistent unique anchors corroborate alignment; "
                        "ambiguous/unmatched anchors do not establish a packet loss. Events use wall time; "
                        "producer-to-USB buffering still separates a note event from its audible samples.")
    if result["aligned"]:
        # Interpolate on-device wall time between received packet anchors. Retain
        # the original events and expose this only as an estimated WAV time.
        x = [a["device_seconds"] for a in unique]
        y = [a["wav_frames"][0] / rate for a in unique]
        result["event_times"] = [dict(lane=e["lane"], index=e["index"], kind=e["kind"],
                                      wav_seconds_estimate=float(np.interp(e["seconds"], x, y)))
                                 for e in trace["events"] if x[0] <= e["seconds"] <= x[-1]]
    return result


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--port", help="MIDI port name filter")
    parser.add_argument("--trace", type=Path, help="read a saved JSON capture instead of the device")
    parser.add_argument("--wav", type=Path, help="match raw host PCM, offline")
    args = parser.parse_args()
    link = None
    try:
        if args.trace:
            trace = json.loads(args.trace.read_text())
        else:
            link, info = open_backup(Updater(MidiBackend(), args.port))
            if info[1] != "coralflower":
                raise BackupError("the connected device is not CoralFlower")
            trace = {"firmware": info[0], **read_trace(link)}
        if args.wav:
            trace["wav_match"] = match_wav(trace, args.wav)
        print(json.dumps(trace, indent=2))
        return 2 if trace["capture_status"] != "complete_window" or (args.wav and not trace["wav_match"]["aligned"]) else 0
    except (InstallError, TimeoutError, ValueError, OSError, KeyError, ImportError) as error:
        print(str(error), file=sys.stderr)
        return 1
    finally:
        if link:
            link.link.close()


if __name__ == "__main__":
    sys.exit(main())

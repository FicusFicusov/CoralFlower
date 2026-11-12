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

META_V1 = ("generation", "active", "ticks_per_us", "trigger_ms", "trigger_ticks", "duration_ticks", "offset",
        "main_count", "audio_count", "usb_count", "main_dropped", "audio_dropped", "usb_dropped",
        "main_max_emit_ticks", "audio_max_emit_ticks", "usb_max_emit_ticks",
        "flash_windows", "last_flash_at", "last_flash_ticks")
META = META_V1 + ("summary_count", "counter_saturation", "max_audio_hook_ticks")
LANES = ("main", "audio", "usb", "summary")
KINDS = (None, "mode", "config", "save_begin", "save_end", "flash_begin", "flash_end", "flash_slow",
         "shed", "shed_request", "audio_late", "block_late", "audio_state", "voices", "arp_note", "pcm",
         "usb_arm_gap", "usb_service_gap", "usb_ring", "usb_stream", "context", "flash_window", "part",
         "counts", "shed_counts", "voice_totals", "peak", "part_peak", "peak_irq")
CAPACITY = (44, 115, 80, 350)


def decode_page(data):
    schema = data[0] if data else 0
    fields = META_V1 if schema == 1 else META
    lanes = 3 if schema == 1 else 4
    caps = (64, 128, 64) if schema == 1 else CAPACITY
    if (schema not in (1, 2) or len(data) < 4 + 5 * len(fields) or data[1] >= lanes or
            data[2] != len(fields) or any(not 0 <= x < 128 for x in data)):
        raise BackupError("unrecognized event-trace schema")
    lane = data[1]
    m = dict(zip(fields, (bk_r32(data, 3 + 5 * i) for i in range(len(fields)))))
    m["schema"] = schema
    at = 3 + 5 * len(fields)
    count = data[at]
    if count > 8 or len(data) != at + 1 + 20 * count:
        raise BackupError("invalid event-trace page length")
    if (m["ticks_per_us"] not in (1, 24) or m["active"] > 1 or
            m["duration_ticks"] != 10_000_000 * m["ticks_per_us"] or
            any(m[f"{name}_count"] > cap for name, cap in zip(LANES[:lanes], caps)) or
            m["offset"] + count > m[f"{LANES[lane]}_count"] or
            (schema == 2 and (m["counter_saturation"] > 7 or (m["active"] and count)))):
        raise BackupError("invalid event-trace metadata")
    starts = [3 + 5 * i for i in range(len(fields))]
    starts += [at + 1 + 5 * i for i in range(count * 4)]
    if any(data[i + 4] > 15 for i in starts):
        raise BackupError("noncanonical event-trace u32")
    events = []
    for i in range(count):
        ticks, tag, a, b = (bk_r32(data, at + 1 + 20 * i + 5 * j) for j in range(4))
        kind = tag & 65535
        if (not 0 < kind < (22 if schema == 1 else len(KINDS)) or ticks >= m["duration_ticks"] or
                (schema == 2 and ((lane == 3) != (kind >= 23)))):
            raise BackupError("invalid event kind or timestamp")
        e = dict(lane=LANES[lane], index=m["offset"] + i, kind=KINDS[kind],
                 detail=tag >> 16, ticks=ticks, seconds=ticks / m["ticks_per_us"] / 1e6, a=a, b=b)
        if kind == 8 and (tag >> 16 & 15):
            e.update(logical_gate=(a >> 8) & 255, note=a & 255, stage=(a >> 16) & 255)
            if schema == 2:
                detail = tag >> 16
                one_shot = bool(detail & (1 << 11))
                e.update(reason=detail & 15, part=(detail >> 4) & 7, slot=(detail >> 7) & 15,
                         engine=a >> 24, one_shot=one_shot, own_envelope=bool(detail & (1 << 12)),
                         voice_mode=(detail >> 13) & 3,
                         voice_class="one_shot" if one_shot else "melodic_gated" if e["logical_gate"] else "released",
                         level_kind="engine_level" if detail & (1 << 15) else "generic_envelope")
            else:
                e.update(voice_class="unclassified", identity_limit="rc5 did not record part or engine; gate=1 does not prove a held melodic note")
        if kind == 27:
            e.update(part=e["detail"] & 255, active_voices=b & 31, logical_gated=(b >> 5) & 31,
                     voice_mode=(b >> 10) & 3, one_shot=bool(b & (1 << 12)),
                     own_envelope=bool(b & (1 << 13)), engine=b >> 14)
        events.append(e)
    return lane, m, events


def check_summaries(meta, events):
    """The counts are useful only if every retained detail is accounted for.

    Schema 2 deliberately summarizes saturated intervals. Never relabel the
    selected examples as an exact timeline or a counter overflow as complete.
    """
    counts, retained = {}, {}
    for e in events:
        if e["lane"] == "summary":
            continue
        key = (int(e["seconds"]), e["lane"], e["kind"])
        retained[key] = retained.get(key, 0) + 1
    omissions = dict.fromkeys(LANES[:3], 0)
    for e in events:
        if e["kind"] != "counts":
            continue
        kind, lane = e["detail"] & 255, e["detail"] >> 8
        if not 1 <= kind <= 22 or lane >= 3 or not e["a"]:
            raise BackupError("invalid summary counter")
        key = (int(e["seconds"]), LANES[lane], KINDS[kind])
        if key in counts or e["a"] < e["b"] or e["a"] - e["b"] != retained.get(key, 0):
            raise BackupError("summary counter does not account for retained details")
        counts[key] = e["a"]
        omissions[LANES[lane]] += e["b"]
    if set(retained) - set(counts):
        raise BackupError("retained event lacks a summary counter")
    if not meta["counter_saturation"] and any(omissions[n] != meta[f"{n}_dropped"] for n in LANES[:3]):
        raise BackupError("summary omissions differ from metadata")
    for kind in ("voice_totals", "peak", "peak_irq"):
        bins = [int(e["seconds"]) for e in events if e["kind"] == kind]
        if sorted(bins) != list(range(10)):
            raise BackupError("summary does not cover all ten seconds")
    selected = sum((e["a"] & 65535) + (e["a"] >> 16) + (e["b"] & 65535) + (e["b"] >> 16)
                   for e in events if e["kind"] == "shed_counts")
    kills = sum(e["a"] for e in events if e["kind"] == "voice_totals")
    consistent = kills >= selected
    return dict(exact_timeline_complete=not sum(omissions.values()), action_counts_consistent=consistent,
                aggregate_counts_complete=not meta["counter_saturation"] and consistent,
                detailed_events_omitted=omissions, interval_seconds=1)


def read_trace(link):
    baseline = None
    events = []
    for lane, name in enumerate(LANES):
        if lane == 3 and baseline["schema"] == 1:
            break
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
    omitted = sum(baseline[f"{name}_dropped"] for name in LANES[:3])
    if baseline["schema"] == 2:
        coverage = check_summaries(baseline, events)
        status = ("partial_counter_saturation" if baseline["counter_saturation"] else
                  "partial_action_counts" if not coverage["action_counts_consistent"] else
                  "summarized_window" if omitted else "complete_window")
    else:
        coverage = dict(exact_timeline_complete=not omitted, aggregate_counts_complete=not omitted)
        status = "partial_overflow" if omitted else "complete_window"
    return dict(schema=baseline["schema"], metadata=baseline, capture_status=status, coverage=coverage,
                events=sorted(events, key=lambda e: (e["ticks"], LANES.index(e["lane"]), e["index"])),
                limits=["Diagnostic evidence, not a continuity PASS.",
                        "Summaries count every occurrence until an explicit counter saturation; omitted details have no exact timeline.",
                        "block_late uses measured block completion and an estimated DMA boundary.",
                        "part_peak describes voices after the measured half; a gate on a one-shot is not a held key.",
                        "voice_totals follow explicit voice_kill/voice_steals operations; engine-owned allocation and direct stage changes may bypass these existing counters.",
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

#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-only
"""Exercise the reader with real C-protocol pages, plus explicit broken inputs.

Usage: python3 tests/cf_event_trace_test.py trace.pages capture.wav [output.json]
Requires numpy for exact raw PCM matching. Does not open a MIDI device.
"""
import copy
import json
from pathlib import Path
import struct
import sys
import tempfile

sys.path.insert(0, str(Path(__file__).resolve().parents[1] / "tools"))
from cf_event_trace import META, decode_page, match_wav, read_trace
from fm1_install import BackupError, bk_u32


class Pages:
    def __init__(self, pages):
        self.pages = copy.deepcopy(pages)
        self.requests = []

    def request(self, cmd, args):
        self.requests.append((cmd, args))
        assert cmd == 82, "only read-only diagnostic command allowed"
        if not self.pages:
            raise AssertionError("reader requested an unexpected page")
        return self.pages.pop(0)


def change(page, field, value):
    at = 3 + 5 * META.index(field)
    page[at:at + 5] = bk_u32(value)


def must_fail(fn, text):
    try:
        fn()
    except (BackupError, ValueError) as error:
        assert text in str(error), (text, str(error))
    else:
        raise AssertionError(f"broken input was accepted: {text}")


def main():
    data = Path(sys.argv[1]).read_bytes()
    pages, pos = [], 0
    while pos < len(data):
        n = struct.unpack_from("<I", data, pos)[0]
        pos += 4
        assert pos + n <= len(data), "truncated test evidence"
        pages.append(list(data[pos:pos + n]))
        pos += n
    assert pages, "C test emitted no protocol pages"
    link = Pages(pages)
    trace = read_trace(link)
    assert not link.pages and trace["capture_status"] == "complete_window"
    kinds = [e["kind"] for e in trace["events"]]
    assert kinds.count("mode") == 1 and kinds.count("save_begin") == kinds.count("save_end") == 1
    begin = next(e["seconds"] for e in trace["events"] if e["kind"] == "save_begin")
    assert .5 <= begin <= .6, f"actual 500ms autosave not observed: {begin}"
    match = match_wav(trace, sys.argv[2])
    assert match["aligned"] and match["unique_anchors"] >= 30, match
    assert len(match["event_times"]) >= 30, "events not mapped to raw recording"
    trace["wav_match"] = match
    if len(sys.argv) > 3:
        Path(sys.argv[3]).write_text(json.dumps(trace, indent=2) + "\n")
    broken = copy.deepcopy(pages)
    change(broken[1], "generation", 42)
    must_fail(lambda: read_trace(Pages(broken)), "capture changed")
    broken = copy.deepcopy(pages)
    change(broken[0], "active", 1)
    must_fail(lambda: read_trace(Pages(broken)), "still running")
    broken = copy.deepcopy(pages)
    change(broken[0], "generation", 0)
    must_fail(lambda: read_trace(Pages(broken)), "no ARP MODE")
    broken = copy.deepcopy(pages)
    for page in broken:
        change(page, "audio_dropped", 7)
    assert read_trace(Pages(broken))["capture_status"] == "partial_overflow"
    broken = pages[0][:-1]
    must_fail(lambda: decode_page(broken), "length")
    broken = copy.deepcopy(pages[0])
    broken[7] = 16
    must_fail(lambda: decode_page(broken), "invalid number")
    wrong_pcm = copy.deepcopy(trace)
    for e in wrong_pcm["events"]:
        if e["kind"] == "pcm":
            e["b"] ^= 0x69BA341D
    assert not match_wav(wrong_pcm, sys.argv[2])["aligned"], "corrupted packet fingerprints falsely align"
    # An exact PCM24 copy must match; adding low-bit dither must be rejected.
    import numpy as np
    from cf_event_trace import wav_pcm16
    pcm, rate = wav_pcm16(sys.argv[2])
    out = np.zeros((len(pcm)//2, 3), dtype=np.uint8)
    out[:, 1:] = pcm.reshape(-1, 2)
    with tempfile.TemporaryDirectory() as folder:
        path = Path(folder) / "pcm24.wav"
        fmt = struct.pack("<HHIIHH", 1, 2, rate, rate*6, 6, 24)
        def write():
            body = b"WAVEfmt " + struct.pack("<I", len(fmt)) + fmt + b"data" + struct.pack("<I", out.size) + out.tobytes()
            path.write_bytes(b"RIFF" + struct.pack("<I", len(body)) + body)
        write()
        assert match_wav(trace, path)["aligned"], "raw PCM24 changed alignment"
        out[200, 0] = 1
        write()
        must_fail(lambda: match_wav(trace, path), "not exact PCM16")
    print(f"TRACE READER PASS pages={len(pages)} events={len(trace['events'])} "
          f"anchors={match['unique_anchors']} autosave={begin:.3f}s; malformed/mixed/overflow/corrupt-PCM controls caught")


if __name__ == "__main__":
    main()

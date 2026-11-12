#!/usr/bin/env python3
"""Decode the actual C handler's wire response, and reject bad schema/length."""
from pathlib import Path
import sys
sys.path.insert(0, str(Path(__file__).resolve().parents[1] / "tools"))
from cf_ui_stats import decode, BackupError

raw = Path(sys.argv[1]).read_bytes()
values = decode(raw)
assert decode(list(raw)) == values  # BackupLink.request returns a list.
assert values["max_sounds_ms"] == 1375
assert values["max_draw_ms"] == 53
assert values["control_delta"] == -3
assert values["encoder_steps_2"] == -2
assert values["hardware_counters"] == 0
assert values["phase_name"] == "idle"
for broken in (raw[:-1], raw + b"\0", bytes([2]) + raw[1:], raw[:1] + bytes([50]) + raw[2:]):
    try:
        decode(broken)
    except BackupError:
        pass
    else:
        raise AssertionError("bad wire snapshot was silently accepted")
print("UI_DIAG DECODE PASS: actual C reply, signed values, retained delay, schema and length")

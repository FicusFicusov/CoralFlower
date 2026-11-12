#!/usr/bin/env python3
"""An ordinary release cannot accidentally carry the temporary RAM trace."""
import os
from pathlib import Path
import sys
from unittest.mock import patch
sys.path.insert(0, str(Path(__file__).resolve().parents[1] / "tools"))
from build import check, diagnostic_define

with patch.dict(os.environ, {}, clear=True):
    assert diagnostic_define("CoralFlower 0.7.2") == "-DCF_EVENT_DIAG=0"
    os.environ["CF_EVENT_DIAG"] = "1"
    assert diagnostic_define("CoralFlower 0.7.2-rc6") == "-DCF_EVENT_DIAG=1"
    for version in ("CoralFlower 0.7.2", "CoralFlower 0.7.2-beta"):
        try:
            diagnostic_define(version)
        except SystemExit as e:
            assert "only allowed" in str(e)
        else:
            raise AssertionError(f"stable/non-rc trace unexpectedly enabled: {version}")
    os.environ["CF_EVENT_DIAG"] = "yes"
    try:
        diagnostic_define(None)
    except SystemExit as e:
        assert "0 or 1" in str(e)
    else:
        raise AssertionError("unrecognized build flag accepted")
print("DIAGNOSTIC BUILD GATE PASS: default off, explicit rc on, stable/invalid rejected")

# Actual instructions from the rejected first rc6 target. A source-only check
# missed this compiler-generated table; the packaging gate must reject it.
symbols = """02001000 g F .text 00000004 _start
01c02ee8 l F .ram_text 000002b2 cf_diag_event
01c08000 g .bss 00000000 _bss_end
01c20000 g .pool 00000000 _pool_start
01c20000 g .pool 00000000 _pool_end
"""
entry = bytes(3808) + bytes.fromhex("04818000")
xip = " 1c03100:    c6 ff b0 9e 05 02 \tr6 = 33922736 <sq_lane_label.H+0x1C : 2059eb0 >"
timer = " 1c03188:    c6 ff 04 08 01 00 \tr6 = 67588 <build/coralflower_size.ll+0x10804 : 10804 >"
errors, _ = check(entry, symbols, xip, xip)
assert any("cf_diag_event references XIP" in e for e in errors), errors
errors, _ = check(entry, symbols, timer, timer)
assert not errors, errors
print("DIAGNOSTIC TARGET GATE PASS: real XIP table rejected, direct TIMER4 accepted")

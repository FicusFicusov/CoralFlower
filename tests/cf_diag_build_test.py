#!/usr/bin/env python3
"""An ordinary release cannot accidentally carry the temporary RAM trace."""
import os
from pathlib import Path
import sys
from unittest.mock import patch
sys.path.insert(0, str(Path(__file__).resolve().parents[1] / "tools"))
from build import diagnostic_define

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

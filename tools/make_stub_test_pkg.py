#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-only
# Copyright (C) 2026 ChoralRoot FM-1 contributors (a fork of Felucca)
"""Test packages for the boot stub (docs/BOOT-SAFETY.md "As built": the device test plan and the host tests), made
from a build's outputs:

  tools/make_stub_test_pkg.py MODE [--app build/choralroot.bin] [--ota build/loader/ota.bin] [-o OUT.fwsc]

  bad-marker   FOR THE DEVICE, with the Transporter at hand. The build as it is, but the marker's body CRC is wrong
               (the marker's own CRC and every directory CRC hold, sector 0x4000 is the build's). It installs like
               any package; at the restart the stub refuses the body and hands over to the staged loader: the FM-1
               shows three short LED flashes and comes back as ota-FM-1 (update mode). Installing the good package
               from there is the rescue path end to end. Installing bad-marker a second time from update mode ends in
               the ROM's USB boot instead (one long flash, the "WL82 UBOOT1.00" disk): the rescue count.
  other-body   host tests only (tests/ldr_test.c): the same stub, another body (every byte changed), a valid marker.
               NEVER install it: the stub would boot it.
  old-stub     host tests only: another stub (the version field one lower, a byte of its padding changed) and another
               body, a valid marker: a unit that runs an earlier stub. NEVER install it.
  no-stub      host tests only: the app without the stub, in the layout of 0.16 and Felucca (no marker).
  no-stub-other  host tests only: the same with every byte changed (another firmware of that layout).
"""
import argparse
import struct
import sys
import zlib
from pathlib import Path

SRC = Path(__file__).resolve().parents[1]
sys.path.insert(0, str(SRC / "tools"))
import fm1pkg_make as P  # noqa: E402


def bad_marker(body):
    m = struct.pack("<IIII", P.MARK_MAGIC, P.MARK_FORMAT, len(body), zlib.crc32(body) ^ 0x00010000)
    return m + struct.pack("<I", zlib.crc32(m))


def main():
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("mode", choices=("bad-marker", "other-body", "old-stub", "no-stub", "no-stub-other"))
    ap.add_argument("--app", type=Path, default=SRC / "build" / "choralroot.bin")
    ap.add_argument("--ota", type=Path, default=SRC / "build" / "loader" / "ota.bin")
    ap.add_argument("-o", "--out", type=Path)
    ap.add_argument("--product", default="FM-1_920")
    ap.add_argument("--sdk", type=Path, help="JieLi AC79 SDK checkout (default: $AC79_SDK, else tools/sdk)")
    a = ap.parse_args()
    P.SDK = a.sdk or (None if P.os.environ.get("AC79_SDK") else SRC / "tools" / "sdk")
    app, ota = a.app.read_bytes(), a.ota.read_bytes()
    if not P.has_stub(app):
        raise SystemExit(f"{a.app}: no boot stub in it (a build before the stub?)")
    out = a.out or SRC / "build" / ("" if a.mode == "bad-marker" else "host") / f"stub-test-{a.mode}.fwsc"
    out.parent.mkdir(parents=True, exist_ok=True)
    if a.mode == "bad-marker":
        flash = P.flash_image(app, P.KEY, bad_marker)
        good = P.flash_image(app, P.KEY)
        diff = [s for s in range(0, len(good), 0x1000) if flash[s:s + 0x1000] != good[s:s + 0x1000]]
        if diff != [0x91000]:
            raise SystemExit(f"bad-marker: sectors {[hex(s) for s in diff]} differ from the build, expected the marker's only")
        note = "the stub will refuse it: update mode after the restart (docs/BOOT-SAFETY.md, the device test plan)"
    elif a.mode in ("other-body", "old-stub"):
        stub = bytearray(app[:P.STUB_LEN])
        if a.mode == "old-stub":
            struct.pack_into("<H", stub, 8 + len(P.STUB_SIG), P.stub_version(app) - 1)
            stub[-1] ^= 0xFF
        flash = P.flash_image(bytes(stub) + bytes(b ^ 0x5A for b in app[P.STUB_LEN:]), P.KEY)
        note = "HOST TESTS ONLY, never install: a valid marker over a body that is not code"
    else:
        body = app[P.STUB_LEN:]
        flash = P.flash_image(body if a.mode == "no-stub" else bytes(b ^ 0x5A for b in body), P.KEY)
        note = "host tests only: the 0.16 layout (linked for 0x02001000, it would not run at 0x02000120)"
    out.write_bytes(P.ufw(flash, ota, a.product))
    print(f"{out}: {a.mode}, identity {a.product}; {note}")


if __name__ == "__main__":
    main()

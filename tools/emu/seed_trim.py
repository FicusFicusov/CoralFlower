#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-only
"""The parts' stored trims (css_part_t.trim) in an emulator flash file's session object: a session as an older
firmware wrote it, whose trim table had rows this one has not (0.18's CZ-1 BRASS 1 -10, STRINGS 2 -6, PIPE ORGAN 1
-12; docs/CZ1.md "Level"; tools/emu/test_persist.sh).

  python3 tools/emu/seed_trim.py FLASH CHORD BASS   set both parts' trims (0.5 dB steps, signed) in every valid copy;
                                                    prints the copies changed
  python3 tools/emu/seed_trim.py FLASH              print "CHORD BASS" of the newest valid copy (or "none")
The object: sectors 0xB4000 / 0xB5000, storage.c's 32-byte header (magic "FELU", type, slot, seq, len, the payload's
CRC-32 at +16, the header's at +28), the payload at +256: cr_session_t (cr_settings.h): magic, version, size, the
FNV-1a check over [12, size) at +8, part[0] at +196, part[1] at +580, a part's trim its byte 4."""
import struct
import sys
import zlib

SECT = (0xB4000, 0xB5000)
TRIM = (196 + 4, 580 + 4)


def fnv(b):
    h = 2166136261
    for x in b:
        h = ((h ^ x) * 16777619) & 0xFFFFFFFF
    return h


def copies(b):
    for o in SECT:
        magic, _typ, _slot, seq, ln = struct.unpack_from("<IHHII", b, o)
        pay = o + 256
        if magic != 0x554C4546 or ln < 1024:
            continue
        if struct.unpack_from("<I", b, o + 16)[0] != (zlib.crc32(bytes(b[pay:pay + ln])) & 0xFFFFFFFF):
            continue
        size = struct.unpack_from("<H", b, pay + 6)[0]
        if size < 1024 or size > ln or struct.unpack_from("<I", b, pay + 8)[0] != fnv(b[pay + 12:pay + size]):
            continue
        yield o, pay, seq, ln, size


def main():
    path = sys.argv[1]
    b = bytearray(open(path, "rb").read())
    if len(sys.argv) < 4:
        best = None
        for _o, pay, seq, _ln, _size in copies(b):
            if best is None or seq > best[0]:
                best = (seq, struct.unpack_from("<b", b, pay + TRIM[0])[0], struct.unpack_from("<b", b, pay + TRIM[1])[0])
        print("%d %d" % best[1:] if best else "none")
        return
    n = 0
    for o, pay, _seq, ln, size in list(copies(b)):
        for k in (0, 1):
            if b[pay + TRIM[k] - 4]:                      # (valid: a sound is stored)
                struct.pack_into("<b", b, pay + TRIM[k], int(sys.argv[2 + k]))
        struct.pack_into("<I", b, pay + 8, fnv(b[pay + 12:pay + size]))
        struct.pack_into("<I", b, o + 16, zlib.crc32(bytes(b[pay:pay + ln])) & 0xFFFFFFFF)
        struct.pack_into("<I", b, o + 28, zlib.crc32(bytes(b[o:o + 28])) & 0xFFFFFFFF)
        n += 1
    open(path, "wb").write(b)
    print(n)


if __name__ == "__main__":
    main()

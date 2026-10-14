#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-only
"""Put a ChoralRoot settings record (cr_settings.h: 192 bytes, "CRS1", as a backup's ChoralRoot block gives it) into an
emulator flash file, so a headless run starts from a unit's own settings.

    python3 tools/emu/seed_settings.py FLASH HEX

FLASH must already hold a settings object (run the emulator once on it with a script that changes a setting and waits
past the save delay: scripts/persist_set.txt). Both copies of the object (storage.c: 0xFC000 / 0xFD000, a 32-byte
commit record, the payload at +256) get the record in place of their ChoralRoot block; the payload's CRC and the
commit record's are recomputed. The record's own checksum is not touched: cr_settings_import() checks it."""
import struct
import sys
import zlib

ST_MAGIC = 0x554C4546
SETTINGS = 0xFC000
SECTOR = 4096
PAYLOAD = 256


def main():
    if len(sys.argv) != 3:
        sys.exit(__doc__)
    rec = bytes.fromhex(sys.argv[2])
    if len(rec) != 192 or rec[:4] != b"CRS1":
        sys.exit("seed_settings: the record must be 192 bytes starting CRS1")
    with open(sys.argv[1], "rb") as f:
        b = bytearray(f.read())
    n = 0
    for copy in range(2):
        o = SETTINGS + copy * SECTOR
        magic, _typ, _slot, _seq, ln = struct.unpack_from("<IHHII", b, o)
        if magic != ST_MAGIC or ln > SECTOR - PAYLOAD:
            continue
        pay = o + PAYLOAD
        i = b.find(b"CRS1", pay, pay + ln)
        if i < 0 or i + 192 > pay + ln:
            continue
        b[i:i + 192] = rec
        struct.pack_into("<I", b, o + 16, zlib.crc32(bytes(b[pay:pay + ln])) & 0xFFFFFFFF)
        struct.pack_into("<I", b, o + 28, zlib.crc32(bytes(b[o:o + 28])) & 0xFFFFFFFF)
        n += 1
    if not n:
        sys.exit("seed_settings: no settings object in " + sys.argv[1])
    with open(sys.argv[1], "wb") as f:
        f.write(b)
    print("seed_settings: %d cop%s patched" % (n, "y" if n == 1 else "ies"))


if __name__ == "__main__":
    main()

#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-only
"""Felucca's SPEAKER setting (settings.lowcut: 0 off, 1 LOWCUT, 2 BASS+) in an emulator flash file's settings object
(docs/AUDIO-QUALITY.md "Left behind"; tools/emu/test_persist.sh).

  python3 tools/emu/seed_lowcut.py FLASH VALUE [--per4]   set it in both copies (--per4: and make the object what
                                                          Felucca 1.0 writes: PER4, without ChoralRoot's block);
                                                          prints the copies changed
  python3 tools/emu/seed_lowcut.py FLASH                  print the value in the newest valid copy (or "none")
The object: sectors 0xFC000 / 0xFD000, a 32-byte header (magic "FELU", type, slot, seq, len, the payload's CRC-32 at
+16, the header's at +28), the payload at +256: magic PER5 / PER4, palette, lowcut (u32 at +8), ..."""
import struct
import sys
import zlib

SECT = (0xFC000, 0xFD000)


def main():
    path = sys.argv[1]
    b = bytearray(open(path, "rb").read())
    if len(sys.argv) < 3:
        best = None
        for o in SECT:
            magic, typ, slot, seq, ln = struct.unpack_from("<IHHII", b, o)
            pay = o + 256
            if magic != 0x554C4546 or b[pay:pay + 4] not in (b"5REP", b"4REP"):
                continue
            if struct.unpack_from("<I", b, o + 16)[0] != (zlib.crc32(bytes(b[pay:pay + ln])) & 0xFFFFFFFF):
                continue
            if best is None or seq > best[0]:
                best = (seq, struct.unpack_from("<I", b, pay + 8)[0])
        print(best[1] if best else "none")
        return
    val = int(sys.argv[2])
    per4 = "--per4" in sys.argv[3:]
    n = 0
    for o in SECT:
        magic, typ, slot, seq, ln = struct.unpack_from("<IHHII", b, o)
        pay = o + 256
        if magic != 0x554C4546 or b[pay:pay + 4] != b"5REP":
            continue
        if per4:
            b[pay:pay + 4] = b"4REP"
            b[pay + ln - 192:pay + ln] = b"\xff" * 192
            ln -= 192
            struct.pack_into("<I", b, o + 12, ln)
        struct.pack_into("<I", b, pay + 8, val)
        struct.pack_into("<I", b, o + 16, zlib.crc32(bytes(b[pay:pay + ln])) & 0xFFFFFFFF)
        struct.pack_into("<I", b, o + 28, zlib.crc32(bytes(b[o:o + 28])) & 0xFFFFFFFF)
        n += 1
    open(path, "wb").write(b)
    print(n)


if __name__ == "__main__":
    main()

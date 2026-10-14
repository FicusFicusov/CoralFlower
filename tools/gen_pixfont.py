#!/usr/bin/env python3
"""Generate the pixel fonts of CoralFlower's UI as a C header (1-bit glyphs, no anti-aliasing).

    python3 tools/gen_pixfont.py OUT.h

Two faces:
  UI     Departure Mono (OFL, assets/fonts) rendered at 11 px without anti-aliasing: the screens' text. The screens
         draw it at 1x and at 2x (the big chord name), so the text keeps the plant's chunky pixels.
  MICRO  a hand-drawn 3x5 face (5x5 for M, m, W, w) for the key caps: note names, degrees, chord labels. Bytes
         0x10..0x19 are small raised digits (3x4) for added degrees, 0xB0 the diminished ring, 0xF8 the
         half-diminished sign; '#' and 'b' draw as compact sharp and flat signs.

Each glyph: an advance and rows of bits (bit 7 = the leftmost pixel). No Raqm, no kerning: identical on every machine.
"""
import pathlib
import sys

from PIL import Image, ImageDraw, ImageFont

ROOT = pathlib.Path(__file__).resolve().parent.parent
UI_FONT = ROOT / "assets" / "fonts" / "DepartureMono-Regular.otf"
UI_PX = 11

# extra code points of the UI face, stored at these byte values
UI_EXTRA = {0xB0: "\u00b0", 0xF8: "\u00f8", 0x82: "\u2192", 0x83: "\u2190", 0x84: "\u2191", 0x85: "\u2193",
            0x86: "\u00b7"}
# glyphs the font does not have, drawn here in its style: the sharp and the flat (rows 0..13, cap height 3..10)
UI_DRAWN = {
    0x80: ["", "", "", "..#.#..", "..#.#..", ".#####.", "..#.#..", "..#.#..", ".#####.", "..#.#..", "..#.#..", "", "", ""],
    0x81: ["", "", ".#.....", ".#.....", ".#.....", ".#.##..", ".##..#.", ".#...#.", ".#..#..", ".#.#...", ".##....", "", "", ""],
}

MICRO = {
    " ": ["...", "...", "...", "...", "..."],
    "0": ["###", "#.#", "#.#", "#.#", "###"],
    "1": [".#.", "##.", ".#.", ".#.", "###"],
    "2": ["##.", "..#", ".#.", "#..", "###"],
    "3": ["##.", "..#", ".#.", "..#", "##."],
    "4": ["#.#", "#.#", "###", "..#", "..#"],
    "5": ["###", "#..", "##.", "..#", "##."],
    "6": [".##", "#..", "###", "#.#", "###"],
    "7": ["###", "..#", ".#.", ".#.", ".#."],
    "8": ["###", "#.#", "###", "#.#", "###"],
    "9": ["###", "#.#", "###", "..#", "##."],
    "A": [".#.", "#.#", "###", "#.#", "#.#"],
    "B": ["##.", "#.#", "##.", "#.#", "##."],
    "C": [".##", "#..", "#..", "#..", ".##"],
    "D": ["##.", "#.#", "#.#", "#.#", "##."],
    "E": ["###", "#..", "##.", "#..", "###"],
    "F": ["###", "#..", "##.", "#..", "#.."],
    "G": [".##", "#..", "#.#", "#.#", ".##"],
    "I": ["###", ".#.", ".#.", ".#.", "###"],
    "V": ["#.#", "#.#", "#.#", "#.#", ".#."],
    "X": ["#.#", "#.#", ".#.", "#.#", "#.#"],
    "O": ["###", "#.#", "#.#", "#.#", "###"],
    "N": ["#.#", "###", "###", "#.#", "#.#"],
    "P": ["##.", "#.#", "##.", "#..", "#.."],
    "R": ["##.", "#.#", "##.", "#.#", "#.#"],
    "S": [".##", "#..", ".#.", "..#", "##."],
    "T": ["###", ".#.", ".#.", ".#.", ".#."],
    "L": ["#..", "#..", "#..", "#..", "###"],
    "H": ["#.#", "#.#", "###", "#.#", "#.#"],
    "K": ["#.#", "#.#", "##.", "#.#", "#.#"],
    "U": ["#.#", "#.#", "#.#", "#.#", "###"],
    "Y": ["#.#", "#.#", ".#.", ".#.", ".#."],
    "Z": ["###", "..#", ".#.", "#..", "###"],
    "J": ["..#", "..#", "..#", "#.#", ".#."],
    "Q": ["###", "#.#", "#.#", "###", "..#"],
    "M": ["#...#", "##.##", "#.#.#", "#...#", "#...#"],
    "W": ["#...#", "#...#", "#.#.#", "##.##", "#...#"],
    "m": [".....", "##.#.", "#.#.#", "#.#.#", "#.#.#"],
    "w": [".....", "#...#", "#.#.#", "#.#.#", ".#.#."],
    "i": [".#.", "...", ".#.", ".#.", ".#."],
    "v": ["...", "...", "#.#", "#.#", ".#."],
    "x": ["...", "...", "#.#", ".#.", "#.#"],
    "s": ["...", ".##", "#..", ".#.", "##."],
    "d": ["..#", "..#", ".##", "#.#", ".##"],
    "a": ["...", "##.", ".##", "#.#", ".##"],
    "u": ["...", "...", "#.#", "#.#", ".##"],
    "g": ["...", ".##", "#.#", ".##", "##."],
    "o": ["...", "...", "###", "#.#", "###"],
    "n": ["...", "...", "##.", "#.#", "#.#"],
    "f": [".#", "#.", "##", "#.", "#."],
    "b": ["#..", "#..", "##.", "#.#", "##."],
    "#": ["#.#", "###", "#.#", "###", "#.#"],
    "+": ["...", ".#.", "###", ".#.", "..."],
    "-": ["...", "...", "###", "...", "..."],
    "/": ["..#", "..#", ".#.", "#..", "#.."],
    "(": [".#", "#.", "#.", "#.", ".#"],
    ")": ["#.", ".#", ".#", ".#", "#."],
    ".": [".", ".", ".", ".", "#"],
    ":": [".", "#", ".", "#", "."],
    "\xb0": ["###", "#.#", "###", "...", "..."],
    "\xf8": [".#.#", "#.#.", "#.##", ".#.#", "#.#."],
    "?": ["##.", "..#", ".#.", "...", ".#."],
}
SMALL = {  # small raised digits, 3x4 (top-aligned in the 5-row cell)
    "0": ["###", "#.#", "#.#", "###"],
    "1": [".#.", "##.", ".#.", ".#."],
    "2": ["##.", "..#", ".#.", "###"],
    "3": ["###", ".##", "..#", "###"],
    "4": ["#.#", "###", "..#", "..#"],
    "5": ["###", "##.", "..#", "##."],
    "6": ["#..", "###", "#.#", "###"],
    "7": ["###", "..#", ".#.", ".#."],
    "8": ["###", "###", "#.#", "###"],
    "9": ["###", "#.#", "###", "..#"],
}


def rows_bits(rows):
    out = []
    for r in rows:
        v = 0
        for i, ch in enumerate(r):
            if ch == "#":
                v |= 0x80 >> i
        out.append(v)
    return out


def micro_glyphs():
    g = {}
    for ch, rows in MICRO.items():
        g[ord(ch)] = (len(rows[0]) + 1, rows_bits(rows + ["." * len(rows[0])] * (5 - len(rows))))
    for ch, rows in SMALL.items():
        g[0x10 + int(ch)] = (len(rows[0]) + 1, rows_bits(rows + ["..."]))
    return g


def ui_glyphs():
    font = ImageFont.truetype(str(UI_FONT), UI_PX, layout_engine=ImageFont.Layout.BASIC)
    asc, desc = font.getmetrics()
    h = asc + desc
    glyphs = {}
    chars = {c: chr(c) for c in range(32, 127)}
    chars.update(UI_EXTRA)
    for code, ch in chars.items():
        adv = int(round(font.getlength(ch)))
        if adv <= 0:
            continue
        im = Image.new("1", (adv + 4, h), 0)
        d = ImageDraw.Draw(im)
        d.fontmode = "1"
        d.text((0, 0), ch, font=font, fill=1)
        rows = []
        for y in range(h):
            v = 0
            for x in range(min(8, adv + 4)):
                if im.getpixel((x, y)):
                    v |= 0x80 >> x
            rows.append(v)
        glyphs[code] = (adv, rows)
    for code, rows in UI_DRAWN.items():
        glyphs[code] = (7, rows_bits([r if r else "......." for r in rows][:h]))
    return glyphs, h, asc


def emit(f, name, glyphs, h, base):
    codes = sorted(glyphs)
    f.write(f"/* {name}: {len(codes)} glyphs, {h} rows, baseline {base} */\n")
    f.write(f"static const uint8_t {name}_BITS[] = {{\n")
    offs = {}
    pos = 0
    for c in codes:
        adv, rows = glyphs[c]
        offs[c] = pos
        f.write("    " + ", ".join(f"0x{v:02X}" for v in rows) + f",  /* {c:#04x} {chr(c)!r} */\n")
        pos += len(rows)
    f.write("};\n")
    f.write(f"static const cf_glyph_t {name}_GLYPHS[256] = {{\n")
    for c in codes:
        adv, rows = glyphs[c]
        f.write(f"    [{c:#04x}] = {{{offs[c]}, {adv}}},\n")
    f.write("};\n")
    f.write(f"static const cf_font_t {name} = {{{name}_BITS, {name}_GLYPHS, {h}, {base}}};\n\n")


def main():
    out = pathlib.Path(sys.argv[1] if len(sys.argv) > 1 else ROOT / "build" / "gen" / "cf_fonts.h")
    ui, h, asc = ui_glyphs()
    micro = micro_glyphs()
    out.parent.mkdir(parents=True, exist_ok=True)
    with out.open("w") as f:
        f.write("/* Generated by tools/gen_pixfont.py - do not edit. Departure Mono: SIL OFL 1.1 "
                "(LICENSES/OFL-DepartureMono.txt). */\n")
        f.write("#ifndef CF_FONTS_H\n#define CF_FONTS_H\n#include <stdint.h>\n")
        f.write("typedef struct { uint16_t off; uint8_t adv; } cf_glyph_t;   /* adv 0: no glyph */\n")
        f.write("typedef struct { const uint8_t *bits; const cf_glyph_t *g; uint8_t h, base; } cf_font_t;\n\n")
        emit(f, "CF_FONT_UI", ui, h, asc)
        emit(f, "CF_FONT_MICRO", micro, 5, 5)
        f.write("#endif\n")
    print(f"pixfont: UI {len(ui)} glyphs {h} rows, MICRO {len(micro)} glyphs -> {out}")


if __name__ == "__main__":
    main()

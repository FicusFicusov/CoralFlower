#!/usr/bin/env python3
"""Turn flora_preview frames (.idx palette indices) into a contact sheet PNG and an animated GIF.

    python3 tools/flora_sheet.py FRAMES_DIR W H [--every N] [--scale S]

The palette is read from firmware/src/flora/flora_pal.h so the preview always matches the firmware.
"""
import argparse
import glob
import pathlib
import re

from PIL import Image

ROOT = pathlib.Path(__file__).resolve().parent.parent


def load_palette():
    text = (ROOT / "firmware" / "src" / "flora" / "flora_pal.h").read_text()
    body = text[text.index("uint32_t fl_pal_rgb"):]
    body = body[body.index("{") + 1:body.index("};")]
    vals = [int(v, 16) for v in re.findall(r"0x([0-9A-Fa-f]{6})", body)]
    vals += [0] * (56 - len(vals))  # 48..55: the UI theme (black here)
    back = text[text.index("uint32_t fl_back_rgb"):]
    back = back[back.index("{") + 1:back.index("};")]
    vals += [int(v, 16) for v in re.findall(r"0x([0-9A-Fa-f]{6})", back)]
    pal = []
    for v in vals:
        pal += [(v >> 16) & 255, (v >> 8) & 255, v & 255]
    return pal + [0] * (768 - len(pal))


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("frames")
    ap.add_argument("w", type=int)
    ap.add_argument("h", type=int)
    ap.add_argument("--every", type=int, default=10)
    ap.add_argument("--scale", type=int, default=2)
    ap.add_argument("--cols", type=int, default=6)
    ap.add_argument("--gif-scale", type=int, default=2)
    args = ap.parse_args()
    pal = load_palette()
    files = sorted(glob.glob(f"{args.frames}/frame_*.idx"))
    frames = []
    for f in files:
        im = Image.frombytes("P", (args.w, args.h), pathlib.Path(f).read_bytes())
        im.putpalette(pal)
        frames.append(im)
    pick = frames[::args.every]
    s = args.scale
    rows = (len(pick) + args.cols - 1) // args.cols
    sheet = Image.new("RGB", (args.cols * args.w * s + (args.cols - 1) * 4, rows * args.h * s + (rows - 1) * 4),
                      (40, 40, 40))
    for i, im in enumerate(pick):
        sheet.paste(im.convert("RGB").resize((args.w * s, args.h * s), Image.NEAREST),
                    ((i % args.cols) * (args.w * s + 4), (i // args.cols) * (args.h * s + 4)))
    out = pathlib.Path(args.frames)
    sheet.save(out / "sheet.png")
    g = args.gif_scale
    gif = [im.resize((args.w * g, args.h * g), Image.NEAREST) for im in frames]
    gif[0].save(out / "anim.gif", save_all=True, append_images=gif[1:], duration=50, loop=0, optimize=False)
    print("sheet", sheet.size, "frames", len(frames))


if __name__ == "__main__":
    main()

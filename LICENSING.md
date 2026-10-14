# CoralFlower licensing

CoralFlower is free software under the GNU General Public License, version 3 only (`GPL-3.0-only`; the full text is
in `LICENSE`). That covers the firmware, the tools, the web pages and the tests in this tree. A few bundled or ported
parts keep their own licences. They are listed below and in `docs/LICENSING-choralroot.md`, and their licence texts
are in `LICENSES/`.

The firmware built from this tree is GPL-3.0-only as a whole. If you distribute it, or firmware derived from it, you
must also give your recipients its complete corresponding source under the same licence.

## Where CoralFlower comes from

CoralFlower is a fork of **ChoralRoot FM-1** by Quixotic7 (GPL-3.0-only, <https://github.com/Quixotic7/ChoralRootFM1>),
which is itself a fork of **Felucca** by Leo Kuroshita (Hügelton Instruments, <https://github.com/hugelton/Felucca>). It keeps their platform: the hardware layer, the
update loader and the installer protocol, the sound engines (FM6, VA), the effects, the user sound stores. The
inherited parts, their authors and their licences are listed in `docs/LICENSING-choralroot.md`. That covers Melodee
by Kerem Kilic, MSFA and Dexed, SLOOP, the JieLi AC79 SDK files, the fonts and the rest. The copyright lines in the
source files are kept as they are.

## CoralFlower's own work

| What | Licence | Where |
| --- | --- | --- |
| The chord instrument: the chord model, the player, the arpeggiator, the looper, the effects chain and its screens, the UI and the screens | GPL-3.0-only | `firmware/src/cf_*.c`, `firmware/src/cf_*.h` |
| The organism (the plant and the coral) and its renderer | GPL-3.0-only | `firmware/src/flora/` |
| CoralFlower's VA presets and FM pad voices (F25..F30) | GPL-3.0-only | `firmware/src/eng_va.c`, `firmware/src/eng_fm6_rom.h` |
| The browser emulator page and the web installer page | GPL-3.0-only | `tools/emu/web/`, `web/` |

The plant's anatomy follows the model of PlantStudio by Paul D. Fernhout and Cynthia F. Kurtz (Kurtz-Fernhout
Software, <https://github.com/pdfernhout/PlantStudio>). That covers the phytomers, the phyllotaxis, apical dominance
and one orientation rule per plant. This is a design reference only: no PlantStudio code is copied.

## Third-party material added by CoralFlower

| What | Licence | Where |
| --- | --- | --- |
| Departure Mono font by Helena Zhang (<https://departuremono.com>): the screens' pixel font (rasterised at build time by `tools/gen_pixfont.py`) and the web pages' font | SIL OFL 1.1 | `assets/fonts/DepartureMono-Regular.otf`, `LICENSES/OFL-DepartureMono.txt` |

## JieLi SDK files in the packages (Apache-2.0)

A `.fwsc` package made by `tools/build.py` holds three unmodified files from the JieLi AC79 SDK:
`uboot.boot`, `cfg_tool.bin` and `eq_cfg_hw.bin`. They are vendored in `tools/sdk/` with the SDK's licence. They are
licensed under the Apache License 2.0 (`LICENSES/Apache-2.0.txt`), not under the GPL, and their copyright stays with
JieLi Technology. Every release carries the licence texts next to the package.

## No vendor material

No M-VAVE material is part of CoralFlower. The firmware links no vendor code and holds no vendor data. The packages
hold only CoralFlower and the three SDK files above.

## Trademarks

"M-VAVE" and "FM-1" are trademarks of their respective owners. "Orchid" is a product of Telepathic Instruments.
CoralFlower is independent firmware that runs on FM-1 hardware. It is not affiliated with, endorsed by or supported
by any of them.

## Contributions and contact

Contributions are welcome under GPL-3.0-only. Contact: iambemoli@gmail.com, Telegram channel
<https://t.me/ficusficusov>.

# GRAIN: a granular layer on CoralFlower's master bus

Whatever the chord part, the bass part and the sequencer play is recorded into a short ring (mono, 22.05 kHz, 0.74 s) and
cut into grains that play back across the stereo field. The grains are mixed with the dry sound by MIX and then go
through the effects chain (cf_fx.c), so the reverb and the delay act on the grains. Freeze stops the recording: the grains
then play a held moment of the music, so a chord, a pattern or a loop can be turned into a texture and played over.

Where: GLO, three pages after Reset (SELECT turns the pages). The values are not saved: a power cycle returns the
defaults (MIX 0, which is off).

| Page | K1 | K2 | K3 | K4 |
| --- | --- | --- | --- | --- |
| Grain | MIX (0 = off) | POSITION (0 newest .. 100 oldest) | SIZE (10 .. 250 ms) | DENSITY (1 .. 120 /s) |
| Pitch | PITCH (-24 .. +24 st) | RANDOM (scatter, octave jumps) | SPREAD (stereo) | DIR (Fwd, Rev, Either) |
| Hold | FREEZE (Live / Frozen) | | | |

Files: `firmware/src/cf_gran.c` (the engine; `cf_gran_run` is called first in `cf_fx_chain`), `cf_granui.c` (the knobs),
`cf_views.c` (`view_grain`), `cf_screens.c` and `cf_ui.c` (GLO now has 8 pages), `tools/emu/emu_firmware.h`.

Design notes
- Integers only, and no 64-bit division (the device's toolchain has none, see cf_arp.c).
- The ring and the state are in `.pool` (about 33 KB). `CG_LOG2` (cf_gran.c) sets the ring: 14 is 16384 samples; 13 halves
  the memory and the time. Check the POOL line of the build report before and after changing it.
- A grain never reads what is not yet written, frozen or live: it starts at least its own span behind the head and is
  shortened when pitch and size do not fit the ring (a 250 ms grain up to +12 semitones, 150 ms at +24).
- Nothing is processed while MIX is 0; the ring still records so that raising MIX has sound to play at once.
- Not yet measured on a unit: the audio load with 8 grains on top of a full chord, bass and effects. Watch the load
  meter in the emulator and on the unit; `CG_NG` (cf_gran.c) is the knob for it.

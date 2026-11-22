# Drum edit, sampled kits, drums from MIDI in

## Drum edit

Each drum has eight settings of its own on top of whatever kit plays it. They work on every kit (the eight DRUM engine
kits, the X kits, the S kits), they are one set for the instrument, and they are kept in flash with the rhythm machine's
record.

Where: the rhythm machine's page and a pattern's DRUMS track. **ENV** opens page 1, again page 2, again closes; its
light is lit while it is open.

| Page | K1 | K2 | K3 | K4 |
| --- | --- | --- | --- | --- |
| 1 shape | LEVEL (-24 .. +12 dB) | PITCH (-12 .. +12 st) | DECAY (-40 .. +40) | BEND (-20 .. +20) |
| 2 colour | TONE (-40 dark .. +40 bright) | DRIVE (0 .. 40) | CRUSH (0 .. 40) | ATTACK (0 .. 40 ms) |

- The drum: on the rhythm machine SELECT picks it while the edit is open (a message names it); in a DRUMS track it is the
  drum picked, and follows it. The three tom lanes are one tom at three pitches: one edit.
- "Kit" (or "Off") is the kit's own value. All at the kit's own changes nothing, to the bit (`tests/drum_edit_test.c`
  compares).
- SEL on the rhythm machine, the edit open: every drum's edits cleared.
- DECAY on an S kit only shortens (a recording cannot ring longer). BEND does nothing on the eight DRUM engine kits.
  LEVEL above about +2 dB is at its top there.

Code: `firmware/src/de.c` (the values, the chain after the voice), `eng_drum.c` (where each engine reads them),
`cf_dmui.c` (the knobs, the record), `cf_sequi.c` and `cf_ui.c` (ENV in both places).

## Sampled kits (S 808, S 909, S Linn)

A third drum engine, `firmware/src/drum_smp.c`: nine one-shots a kit, 4-bit IMA ADPCM in flash, pitched for the toms.
The repository has no recordings: see `assets/drum-kits/README.md` for how to build the kits from your own. Without
them the firmware and the kit lists are as before.

## Drums and the LAYER from MIDI in

- Notes on the DRUMS channel (GLO > Channels) play the drums: General MIDI's drum notes, with velocity, in the kit the
  rhythm machine shows. With NOTES IN at FM-1 keys or Roots this needs IN CH set to one channel (All takes every note
  for the keys); with Direct it always works unless DRUMS shares the chords' or the bass's channel.
- NOTES IN Direct: notes on the LAYER's channel play the LAYER's sound.

# CoralFlower — interface specification

Source of truth: the owner's Figma mock-ups (file `U8n5SHMw8sFT2Df5Bte3UN`, page `fm-1`), one section per
FM-1 button, translated from Russian. Notes in *italics* are our interpretation; everything else is the
owner's requirement. All on-device text is English and uses real music and synth terminology.

## 0. The hardware, as CoralFlower uses it

```
 ┌──────────────────────────────────────────────────────────────────────────┐
 │ (MASTER) (SELECT)   ┌────────┐   (K1)   (K2)   (K3)   (K4)                │
 │ (PRESETS)(ALGO)     │ 240x240│   [FX ][SEL][ENV][LFO][EDIT][GLO]          │
 │ [OCT-][OCT+]        │  TFT   │   [HOME][SAVE][ARP][SEQ][PLAY][REC]        │
 │  upper row: 11 keys  F# G# A# | C# D# | F# G# A# | C# D# | F#  (OP1…POLY,+1) │
 │  lower row: 16 keys  F G A B C D E F G A B C D E F G                       │
 └──────────────────────────────────────────────────────────────────────────┘
```

### The left-hand controls (same on every screen unless the screen says otherwise)

| Control | Default role | Role on multi-page screens (EDIT, GLO, ARP) | Role on FX |
| --- | --- | --- | --- |
| MASTER (pot) | Master volume | Master volume | Master volume |
| SELECT (encoder) | **Bass sound** (bass preset) | **Page** | **Effect select** |
| PRESETS (encoder) | **Chord sound** (chord preset) | Chord sound | **Page** |
| ALGORITHM (encoder) | **Chord algorithm** (input mode 1–4) | Chord algorithm | **Move effect in the chain** |

Pages are changed **only** with an encoder, never with the keys (the owner explicitly rejects ChoralRoot's
page selection on the keyboard).

KNOB 1–4 always belong to the open screen; each screen defines them.

## 1. HOME — play mode (opens at power-on)

The essential performance settings.

| Knob | Parameter | Values |
| --- | --- | --- |
| K1 | **Quantize** | Off / Basic / Full — default **Full** |
| K2 | **Key** (tonic) | C … B; disabled (greyed) when Quantize is Off |
| K3 | **Scale** | Major, Natural Minor, then Major Pentatonic, Minor Pentatonic, then Dorian, Mixolydian, Lydian, Phrygian, Locrian…, then less common scales |
| K4 | **Glide** (ms) | Glide between chords and notes — "very important", must work across whole chords like Vital's poly glide |

- **Quantize Basic** works like ChoralRoot's Key switch: roots snap to the scale.
- **Quantize Full** also snaps the individual chord tones to the scale (diatonic chords).
- Enharmonic spelling must follow the key: D# in E major, E♭ in B♭ major, etc.
- OCT− / OCT+ shift octaves as usual (−3 … 3). Octave 0 sounds an octave under the keys' printed pitch (the owner,
  2026-10-15: at the printed pitch the chords were too high); both buttons together go back to 0.

### Root keys per chord algorithm

*Algorithm 1 (chord buttons on the upper row):* the 16 lower keys play roots.
- Quantize Off: chromatic, one semitone per key, the tonic on lower key 5
  (G# A A# B **C** C# D D# E F F# G G# A A# B).
- Quantize Basic/Full: one scale degree per key, the tonic on lower key 5
  (C major: F G A B **C** D E F G A B C D E F G, labelled IV V VI VII **I** II III …).

*Algorithm 2 (chord buttons on the left):* the remaining 8 upper and 12 lower keys play roots as a
piano keyboard (lower: C D E F G A B C D E F G; upper: C# D# | F# G# A# | C# D# | F#).
- Quantize Off: notes as printed.
- Quantize Basic/Full: each key snaps to the nearest scale tone (C major: black keys snap to C D | F G A …;
  E major: white keys become C# D# E F# G# A B …).
- Every root key shows its note name and its scale degree (Roman numeral).

## 2. SEL — chord editor

| Knob | Parameter |
| --- | --- |
| K1 | **Chord type** of the selected chord button |
| K2 | **Added degree(s)** (add) — more than one can be combined; offer popular combinations |
| K3 | **Inversion** |
| K4 | **Bass degree** — which chord degree the bass plays (counted in the chord, ignoring inversion) — needs a clear visualiser |

Chord types: the main ones (major, minor, the suses, diminished) plus **none** (single degrees only, so a
button can hold just added degrees).

### The four chord algorithms (ALGORITHM encoder), in this order (the Lock variants first, 2026-10-15)
1. **Top + Lock** — chord buttons on the upper row, latching. The default.
2. **Top** — the same, held.
3. **Side + Lock** — chord buttons on the left, latching.
4. **Side** — the same, held.

**Algorithm 1 — Top.** The 11 upper keys are chord buttons, default assignment (the owner's order, 2026-10-15):
`M6  M7  M9 | s2  s4 | m6  m7  m9 | M  m | Off`. While `Off` is active single notes play, as many as keys are held.
`Off` is a form, not a place: the button that holds it is Off, and SEL leaves it alone. All others can be reassigned
in SEL. A settings record still in the first builds' order (`Off  M  m | s2  s4 | M6  M7  M9 | m6  m7 | m9`) moves to
the new one; a changed one stays as it is. The flow is like FoMni's chord selection, but here a chord *form*
is chosen.

**Algorithm 2 — Side.** Quality buttons on upper keys 1–3: `M  m  d`; extension buttons on lower keys
1–4: `s2  s4  7  9`. All remaining keys play roots. Flow like FoMni's, but chord form *and* degrees are
chosen.

**Combining.** Buttons pressed together add their degrees: M6 + M9 → M6/9. Sus buttons combine with major
or minor too, and so do single degrees. *(We treat sus pressed alone as a suspended chord, and sus pressed
with M or m as an added 2nd/4th that keeps the 3rd: Madd2, m(add4).)*

**Lock variants (3 and 4 of the list).** Several compatible buttons pressed together latch together, not
just one. When compatible and incompatible buttons are held at once, the larger compatible group wins:
holding M, M6, M9 and m7 applies the first three → M6/9.

**Label dictionary** (labels are short because space is tight): `m` minor, `M` major, `d` diminished,
`s` sus (s2, s4), a trailing number is the added degree (MajAdd9 → `M9`; if the number does not fit, draw it
smaller). *Find a consistent way to combine this with s2/s4.*

## 3. ENV — envelope (ADSR) editor

| K1 | K2 | K3 | K4 |
| --- | --- | --- | --- |
| Attack | Decay | Sustain | Release |

Ideally a mode switch: **1. Chords and bass separately**, **2. Together** — with a consistent solution.

## 4. LFO — bass editor

| K1 | K2 | K3 | K4 |
| --- | --- | --- | --- |
| Bass On/Off | Bass Level | Bass Octave ± | Octaver On/Off |

## 5. EDIT — preset (sound) editor

The same as the stock FM-1 OS: per-operator and per-page parameters (operators selected like the stock
EDIT mode). SELECT = page.

## 6. GLO — global settings

- Take what is useful from ChoralRoot's Options, and add more (look at the Orchid's settings too).
- **Group the settings into sections** — never one endless list.
- Pages only on the SELECT encoder; never shown on or navigated with the keys.
- Add a choice of **accent and secondary colour themes** matching the colours of the M-VAVE synth line,
  plus a few popular extra palettes.

## 7. FX — effects

- The effect set and its parameters: copy the stock FM-1 firmware (Filter, Reverb, Delay, Distortion,
  Chorus, Phaser; reorderable chain).
- SELECT = effect select, PRESETS = settings page, ALGORITHM = move the effect in the chain;
  K1–K4 = the parameters of the effect/page.
- Screen, its UX and layout: ours to design.

## 8. SAVE

Look at the existing mechanics or invent something more logical and better from scratch. Consistent with the
rest of the interface and, above all, convenient.

## 9. ARP — arpeggiator

Copy the stock FM-1 arpeggiator (pages via SELECT, K1–K4 per page). Improve where it helps.

## 10. SEQ — metronome

| K1 | K2 | K3 | K4 |
| --- | --- | --- | --- |
| Metronome Level | Metronome On/Off | Tempo | Time Signature (beats) |

- Level defaults to 0.
- While this screen is open the metronome plays; while recording it plays at the set level.

## 11. PLAY/STOP and REC — looper

Work like ChoralRoot's looper. With no loop recorded, PLAY/STOP only starts and stops the metronome:
PLAY starts it from beat 1, STOP stops it.

## 12. Cross-cutting requirements

- **Battery** indicator.
- **On-screen key labels** like FiMba-1 and FoMni — but beautiful.
- **Parameter display for the four knobs** (placement free).
- **A classy, animated, conceptual visual.** Inspired by JIANT FM, but with the chords at the centre: the
  complexity of the chords drives the complexity of the botany/corals. Every settings screen has something
  of its own. The whole interface is dark.
- **Boot splash** referring to corals/botany.
- **Visual feedback** through the key LEDs and the menus.
- Everything in **English**, with correct music and synth terms.
- Improve anything that can be improved; consistency of interface and control, and above all convenience.

## 13. Visual direction (from the owner's references)

- Palette: a bright "TLP"-style 31-colour pixel-game palette (ten 4-step ramps: fire, two skin tones, lime,
  green-teal, blue, magenta, rose, warm grey, cool grey) on a warm near-black background. Gradients along
  lines are welcome.
- Plants: PlantStudio (1998) look — chunky pixels, flat-shaded leaves and petals, alive and animated even
  when nobody plays; optional fake isometric 3D.
- Sound and chord complexity must visibly shape the plant/coral; the style must stay consistent everywhere.

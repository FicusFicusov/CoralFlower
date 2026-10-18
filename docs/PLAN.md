# CoralFlower — plan and architecture

Last updated 2026-10-14. The interface is specified in [SPEC.md](SPEC.md); research notes behind each decision
are summarised at the end.

## Decisions

| Area | Decision | Why |
| --- | --- | --- |
| Base | Fork **ChoralRoot FM-1 v0.21** (GPL-3.0-only, itself a fork of Felucca) for the platform, sound engines and FX. | Its platform has the safest install path of all FM-1 firmwares (boot stub, safe mode, rescue), a working browser emulator and a measured audio budget. |
| What we keep frozen | `hal/`, `crt0.S`, `app.ld`, `stub/`, `loader/`, `ota*.c`, `storage*.c`, `audio.c`, `lcd.c`, `usb*.c`, `midi_uart.c`, `main.c` boot order, `fm1pkg_make.py`. | Byte-identical stub, loader and package format are what keep installs from bricking units. |
| What we replace | The whole app layer: ChoralRoot's UI (`cr_ui.c`, `cr_draw.c`, `cr_edit.c`, settings, shims) **and** its chord engine. | The owner's chord model (assignable chord forms, four input algorithms, Quantize Off/Basic/Full with 20+ scales, bass degree, chord glide) does not fit `cr_engine`'s fixed Orchid model. A smaller engine written for the spec is cleaner than bending a 1,600-line one. |
| Rendering | An **indexed 240×240 framebuffer** (57.6 KB, the same POOL room ChoralRoot's canvas used), a 64-colour palette, per-tile change detection, only changed tiles sent over SPI. | The panel's SPI link caps full-screen redraws at ~13 fps; the plant animates a region, the rest of the screen stays still and costs nothing. Palette indices make themes a table swap. |
| Look | Pixel art throughout: the plant at 2× pixels, a pixel font for text, the "TLP" ramps, dark warm background. | One consistent style: the owner's references are a pixel-game palette and PlantStudio's raster plants. |
| Fonts | Pixel fonts rendered without anti-aliasing (no Raqm needed): a UI face at 1×, the same at 2× for big values, a micro face for key caps. | Crisp at 240 px; the Mac's Pillow lacks Raqm, so ChoralRoot's AA font pipeline would differ between Mac and CI. |
| Engines | FM6 (DX7-compatible, Melodee's) and the VA first; then an **S1** engine (Synth1-style, fixed point) with a local `.sy1` converter; optionally JIANT's LOFI FLOAT. No mono-only engines. | The chip has no FPU in use; OpenSynth1 cannot be ported as is, but an S1 voice costs about a VA voice. |
| Presets | Only redistributable sounds in the repo: the FM-1 family's GPL voices, MIT and CC0 banks after listening, AZur Studio (CC0) and OpenSynth1's bank (MIT) for S1, plus our own. Third-party Synth1 and DX7 banks are converted **locally** by the owner and never committed. | Most famous banks have no licence or forbid redistribution. |
| Glide | Chord glide pairs voices of the old and new chord by pitch order (lowest to lowest…), with a small table when sizes differ. | None of Vital, Surge, Dexed or Ableton matches voices by pitch; this is an improvement, not a copy. |
| Identity | Package identity `FM-1_930`, INFO text `CoralFlower x.y`, own USB strings; a free pid.codes PID before the first release. | `FM-1_920` and others are taken; installers decide by the INFO text. |
| Build | Device firmware on GitHub Actions (ubuntu, JieLi toolchain native). Host tests, the browser emulator and the generators on the Mac. | No Docker on this Mac; the JieLi toolchain is Linux x86-64 only. |

## Milestones (big to small)

1. **Skeleton.** CoralFlower tree from ChoralRoot; the new unit `coralflower.c`; the indexed framebuffer and
   pixel fonts; the browser emulator boots CoralFlower and shows the plant on HOME. *Exit: the emulator page runs,
   keys make sound through a minimal chord path.*
2. **Chords.** Music theory module (scales, chord forms, names, spelling, numerals); the four chord algorithms with
   combining and Lock; Quantize Off/Basic/Full, Key, Scale; inversion and bass degree; the bass part; chord glide.
   Host tests for all of it. *Exit: every rule of SPEC §1–2 has a passing test.*
3. **Screens.** HOME, SEL, ENV, LFO (bass), FX, ARP, SEQ (metronome), GLO (grouped settings, colour themes), SAVE,
   EDIT (FM operator editor), PLAY/REC (looper); top bar with battery, the key-label strip, the knob row, popups,
   LED feedback, boot splash. Each screen gets its own plant "lens".
4. **Sound.** Curated FM6 and VA presets; the S1 engine and converter; FX chain matching the stock FM-1 set.
5. **Ship.** Device build on CI, the web installer page, the emulator on GitHub Pages, README and manual,
   publishing under the owner's new GitHub account. The site (2026-10-15): one page with the installer at `/`, the
   emulator at `/play/`, `/install/` forwarding to `/#install`; a shared design system (`web/site.css`); built by
   `tools/make_site.sh`. Its structure, design system and QA report: `docs/site/`.

## Risks to verify on a real unit

- SPI at 20 MHz (would raise the full-screen rate from ~13 to ~21 fps).
- CPU headroom with an FM6 chord, bass, FX and the plant animating (target: UI frame under 30 ms).
- Dual core stays off by default (SRAM misreads on some units).

## Status (2026-10-14, end of the first session)

Milestone 1 (skeleton) works in the emulator:

- `firmware/src/coralflower.c` builds (host syntax check clean); the browser emulator (`tools/emu/web/build_web.sh`)
  and a native headless runner (`tools/emu/cf_headless.sh SCRIPT`) run it.
- Music theory (`cf_theory.c`, 99 checks) and the chord model with all four algorithms, Lock, combining, Quantize
  Off/On/Full, auto inversion and bass degree (`cf_chord.c`, 35 checks): `tests/`.
- The chord player with pitch-matched chord glide, the bass part with octaver, the metronome click
  (`cf_play.c`, `cf_out.c`); FM6 gained a per-voice glide hook (`eng_fm6.c fm6_glide`).
- The organism (`flora/`): a flowering plant on the Top algorithms, a branching coral on the Side ones (owner's call,
  2026-10-14: the two mixed looked chaotic). Branches by pitch class, organs by the tone's role, shape by the chord's
  quality. The plant's anatomy follows PlantStudio's model (2026-10-15, the owner: "the anatomy is unnatural", from
  its source, github.com/pdfernhout/PlantStudio): a node per side shoot, a leaf at each node turning 137.5 degrees,
  the shoot in the leaf's axil leaving the stem at 40 degrees and arcing up, no shoot at the top node (a small leaf
  there) and a terminal stalk that lifts the crown clear, the first leaves low (the owner: "too much bare stem"),
  lower shoots longer and opening first; every flower, bud and fruit faces
  up (the seventh is an upright cup, the sixth's berries stand up: a head that hangs reads as wilted); one colour family per organism (green plant with blooms by the chord's root; coral by the key, polyps by
  the root). Pollen and bubbles rise out of the flowers and polyps; a note's light sparkles out of them.
- Backdrops behind the organism (GLO > Display > BACKDROP): Scene (the plant: dusk, moon, stars, hills; the coral:
  deep water with drifting out-of-focus lights), Ripples (a ring of sound per chord), None. The keys and knob cells
  take the scene's tint (violet by the plant, blue by the coral); the sounding root's keys a lighter step of it with
  the accent in the letters (no dark-red fill). One interface colour (GLO > Display > COLOR, Lime by default)
  serves both the accent and the secondary roles. Colours: palette entries 56..63.
- ENV shows what is heard: times in ms and s (FM6: the engine's own envelope code run on C4), the sustain in %.
  ENV "chord + bass" links the parts: the bass follows the chord's envelope on every turn and sound change.
- The bass part is monophonic: a new bass note cuts the old one's release tail (cf_play.c cfp_bass_mono).
- RETRIG (GLO > Play, four ways, the owner's; on the screen Wait, Slide, Notes, Chord: Move shows as Slide, Strike as
  Notes since 2026-10-15). Wait (the default): with held keys a chord button changes nothing that
  sounds, in every algorithm (only its light, and Lock's latch); the next root key takes it (cf_chord_t.keep).
  Move (the behaviour before Wait, kept at the owner's wish): a button moves the sounding voices to the new chord
  with no new attack, only the tones it adds start. In both, another root under held ones moves the voices (at the
  Glide time, 0: at once). Strike: a change strikes its new notes (the first behaviour). Chord (2026-10-15, the owner): a change strikes the whole
  new chord, the shared tones too, and with a Glide time each note slides from the last chord's.
- Keys pressed at once: what changed in one scan goes as releases, then chord buttons, then roots (cf_ui_pump; in key
  order a root struck with the old buttons and the last chord slipped in). A button pressed or let go within 50 ms
  of a root going down still makes that root's chord, in Wait too (cf_play.c CFP_GATHER_MS).
- Chord buttons: a major and a minor button never join (the type held most wins, a tie goes to the last pressed);
  sus and single degrees join either. Only the buttons in the chord light; one held but left out shows plain.
- Settings records grow at the end of cf_set_t: an older, shorter record loads and the new fields keep defaults.
- The bass sits an octave under the root as played (OCTAVE 0; the owner moved it up from two) (its degree's nearest note to that): it goes the way the keys go;
  it used to fold into C2..B2 by pitch class, and B up to C fell eleven semitones.
- Settings are kept in flash (cf_settings.c): the whole cf_set_t with the effects chain, written 1.5 s after the
  last change once nothing sounds (an erase silences the audio ~45 ms), in OBJ_PROJECT0 + 3 (ChoralRoot's CZ store,
  unused here). The sounds come back by their place in the sound row.
- FX (cf_fx.c, cf_fxui.c): the stock set in a chain the player orders on the master bus: Filter (TPT state-variable,
  LP/BP/HP), Reverb (Room/Hall/Plate on cr_fx.c's FDN), Delay (cr_fx.c's, ping-pong), Distortion, Chorus (cr_fx.c's),
  Phaser (6 all-passes). SELECT picks, ALGORITHM moves, K1 on/off (the owner's order), K2-K4 values. Tails ring out
  when switched off (a reverb's within ~2 s even at DECAY 100).
- The reverb is a fixed-point Dattorro plate at 22.05 kHz (cf_fx.c cv_*: ~35 KB of int16 lines, ~1-2 % CPU), with a
  200 Hz low cut and an 8 kHz high cut on its input; Room / Hall / Plate set size, pre-delay, damping, bandwidth.
  Tuned for clarity after the owner found it muddy: DECAY is an RT60 per type (Hall 0.8-7 s, ~2 s at 45 %), light
  damping, -4 dB under ~400 Hz and +4 dB above ~1 kHz into it, a 300 Hz low cut inside the tank, 10-40 ms pre-delay,
  a small swing (3 samples: 12 made a sparse tail swell and dip by up to 10 dB).
- A settings save waits for a second of silence at the effects' output (cf_fx.c cfx_quiet): a flash erase stops the
  audio ~45 ms on the unit, and saving under a reverb tail cut holes into it.
- The master is fx.c's Clean output chain (a held-gain safety limiter at -2 dBFS, then MASTER), at a 0 dB trim: the
  Classic limiter (-5 dB, 0.15 s release) pumped under a big reverb.
- Screens' heads (cf_views.c): the top bar names the screen; the title under it is what is in front of you there,
  never that name again (Global: the page; Sound: the section, FM "OP 1"; Bass: the sound; ENV: the target). The
  screen's icon at the title's end. Paged screens: the pager under the title (arrows round marks, the page a long
  green one; FM's page name after it); the picture slides in on a turn. The plant is a faint veil behind (fb_veil).
- Central pictures (cf_views.c): Global Display (themes, backdrops), MIDI (lanes, channels, notes running), Play
  (two chords: glide, loudness with RETRIG, the lit keys); Bass (its wave by level / octave / octaver, the keys);
  VA editor (OSC wave and the mix, FILTER response with FENV and drive, ENV shape and where it goes, LFO wave and
  where it goes, LFO sync, unison spread, MOD slot as source > amount > target); FM editor (the operator's DX7
  envelope, rates or levels marked; Tuning: its wave against the note's, ratio or fixed Hz; Sens & Level: hard and
  soft press; Scaling / Curves: the level along the keys, the break point, RATE SCL; Pitch envelope; Voice: the
  algorithm's real graph from FM6_ALG with feedback; LFO: the wave and the pitch / loudness it moves, delay, sync).
  The operator pages show the algorithm small at the right, the operator lit.
- HOME's chord name: the root big, the rest small at the top, the degree after; caps on the key's line (C MAJ).
- Hints: in the top bar, right of the screen's name (the owner's choice), JIANT's style: the control's name on a light
  chip and what it does (cf_draw.c draw_hints / draw_top_hints; pairs that do not fit drop from the last). GLO, FX,
  ENV, EDIT and SEL use them; nothing else prints control hints.
- Screens: HOME (Quantize, Key, Scale, Glide), SEL (chord editor), ENV, LFO (bass), SEQ (metronome), GLO
  (Display, MIDI, Play pages), FX, EDIT, ARP, SAVE as working knob screens.
- CHORD (SEL): the title is the button as its key shows it ("M7"), its chord in words under it; the picture: three
  octaves of piano keys on the key's tonic, the button's notes lit as voiced (INV), each one's degree over it and
  name under it, the bass tone (BASS) marked; an arrow over that button in the key row, "press a chord button below".
- ARP (cf_arp.c, in the audio ISR): the stock FM-1's arpeggiator. The ARP button opens its screen as any screen's
  does; K1 ON/OFF switches it (the owner's: four knobs a page, ON/OFF first as an effect's); it plays on elsewhere
  (ARP lit, an ARP chip on HOME's top bar); off at power-on. Pattern page: ON/OFF, MODE (Up, Down, Inclusive,
  Exclusive, Random, Order = the chord's tones from the root, Repeat, Strum up, Strum down), OCTAVE 1-4, RATE (1/1 ..
  1/32 with triplets; Strum: 5-250 ms between its notes). Feel page: TEMPO (the metronome's), GATE 20-100 %, SWING
  50-75 % (a pair's first long), LATCH. While the metronome's clock runs (PLAY, a loop, SEQ open, MIDI clock in) its
  steps lock to the beat (cf_grid: 1/24 beats; a chord starts at once, then on the grid's places; one a third of a
  step early is not played twice); free-running otherwise. The screen says "on the beat".
- MIDI clock in (GLO > MIDI > CLOCK IN, cf_out.c): 0xF8 pulses run the beat (a pulse = 1/24, the tempo followed by
  cr_midi.c's follower and shown on the knobs, TEMPO dimmed), so the metronome, the arpeggiator and the loop (4 ticks
  a pulse) follow; 0xFA / 0xFB start from beat 1 (the loop too), 0xFC stops. The MIDI picture says CLOCK IN / NO CLOCK
  with a beat light. (The headless runner: `clock BPM MS`, `midi start|stop`.)
- A root that goes down with no chord button (no Off, no latch) takes the buttons pressed after it at once, in Wait
  too (cf_chord_t.bare: a single note, its chord picked after it; the owner's exception).
- Power-on sound (GLO > Display > BOOT SND, on by default): always the same, whatever the sounds and effects are. A
  voice of its own after the effects and before MASTER (cf_fx.c cf_boot_mix): glass bells (two sines 5 cents apart,
  a quick phase-modulation shimmer, a 40-cent slide up as each comes in) strum a wide Cmaj9#11 upward over a soft sub
  C, an echo of it, then a shower down from above. Peak about -14 dBFS at full MASTER, below a held chord.
- Controls named in a caption look as in the top bar: a chip (cf_draw.c draw_chip_text, "hold [SAVE] to keep it"). Strum is the Orchid's and ChoralRoot's: each
  new chord played once, its notes RATE ms apart, held until the chord ends. A chord changed while it runs takes its
  place in the pattern; from silence it starts on its first note at once; the bass holds under it (latched with it).
  The picture: sixteen steps as a little roll (GATE long, SWING late), the step playing lit; Strum: the fan. The
  settings are kept (cf_set_t.arp), on / off is not.
- Two lists of sounds (cf_sound.c cf_ent_part): PRESETS turns the chord part through the chord sounds, SELECT the
  bass part through the basses; an engine's INIT is in both. The factory presets by name (CF_BASS, CF_DROP: taken
  out at the owner's word, 2026-10-15: TINE EP, BELL, DX MARIMBA, DRAWBARS, GLASS PAD, KALIMBA, FLUTE, TUBULAR of the
  FM), a user sound by the part it was saved from (a bass is saved MONO). The FM names lose their "FM " (the screens
  put the engine before the name: FM6_PLAIN_NAMES). A new unit starts on VA POLY KEYS and VA DEEP SUB.
- More sounds (2026-10-15, the owner: "the VA presets are alive, more of them, and living FM pads"): 12 VA presets
  (eng_va.c, appended: BREATH PAD, TAPE STRINGS, AURORA, GLOW PLUCK, DREAM BELLS, HAZE, STAR KEYS, PWM PAD; basses FAT,
  ACID, WOOD, REESE; DRIFT KEYS and SUNRISE made and taken out again at the owner's word, SUNRISE too close to BREATH
  PAD) and 6 FM pads (eng_fm6_rom.h FM6_XROM, PTCH F25..F30 after the bank so
  nothing saved moves: HALO, SPARKLE, VOX, BLOOM, WAVE, ICE PAD; their release 1.5..1.8 s to -20 dB, shortened at the
  owner's word), each moving by itself (LFOs on shapes, filter,
  spread, the FM modulators' AMD). Levels matched: a held 6-note chord at -14..-19 dB RMS (tests/cr_va_test.c,
  ChoralRoot's, with the RMS added; the FM pads measured in the emulator); ACID BASS trimmed -4 dB (cr_bank.c CB_TRIM).
- SAVE (cf_sound.c cf_snd_save / cf_snd_delete on ChoralRoot's 32 user slots, upreset.c with va_store.c and
  fm6_ustore.c: the engine patch is kept with the sound): SELECT the part (Chord / Bass; the edited one opens),
  K1 SAVE AS Replace (a user sound over itself; a factory one as a copy in its place) or New (the engine's next
  place, its name with a 2), K2 / K3 the name's letter and what it is, K4 ACTION Save or Delete (your sounds only:
  a copy gives the factory sound back, an added one leaves the row). SAVE held 1 s does it (a bar fills), a tap goes
  HOME. The picture: where it lands in the row (the one before, it, the one after), what the save does, the slots
  free. After a save or a delete both parts' places are taken again (the row moves) and the settings follow.
- Looper (cf_loop.c, in the audio ISR; ChoralRoot's, made over): it records the keys (roots and chord buttons, down
  and up) on a 96-a-beat clock at the tempo, in layers, and plays them through a chord model of its own (the
  algorithm it was played in; the key, scale, forms and sounds of now), its notes on the same parts as the keys' (a
  note both sound goes with the last of them; with ARP on and no key held, the loop's chords are arpeggiated). REC: arm
  (Free: the first chord starts the take; LENGTH 1-8 bars: a bar counted in, then exactly that, and it plays by
  itself); while taking: the take ends, the loop plays at once (Free: rounded to the nearest beat). MODE New (REC
  over a loop: a new take, the old one going when it starts) or Overdub (a layer on top). PLAY: play / stop, the
  metronome's clock with it from beat 1; no loop: the metronome alone. REC held 1 s: the last layer undone (the last
  one: the loop cleared). PLAY held: the LOOPER screen (LENGTH, MODE, QUANTIZE 1/8 1/16 at record, COUNT-IN; the
  timeline: bars, a row a layer, the chords as blocks with their roots, the playhead; the count-in counting down).
  REC lit armed, blinking taking; PLAY with the beat, mid when a loop waits; HOME's top bar a LOOP / REC chip with
  bar.beat. SAVE > Loop keeps it in flash (OBJ_CFLOOP 0xB6000, a storage.c A/B pair; loaded at power-on, stopped),
  ACTION Delete clears it there and in memory. Not ChoralRoot's yet: ten slots, Replace / Advance / Step modes, the
  loop editor, Rate; layers sound as one chord model (two layers' roots held at once: the later wins, as on keys).
- The panel gets only the 16 x 16 tiles whose hash changed (cf_fb.c cf_present). The hash (FNV on 4-byte words)
  rotates after each word since 2026-10-15: with the multiply alone a change in a word's high byte (a tile's right
  column) only reached the hash's top byte, two rows' changes could cancel, and that column stayed stale on the
  panel (the looper's timeline showed it: 195 frames of one test, 0 now; the emulator compares panel and frame).
- Memory (an estimate: the unit built for a 32-bit ARM, its assembly stubbed, 2026-10-15): RAM about 54 KB of 96
  (.bss 42 KB, .data.tables 12 KB), POOL 304 KB of 344 (88 %), code and constants about 440 KB of 560. The device
  build (CI) gives the real figures.
- The intro (the seed growing, the name), the update gesture (OCT- + OCT+ + HOME 5 s), pixel fonts, themes.

Next: `cf_backup.c` (the installer's INFO and backup, the loop's object in it), the metronome's picture (SEQ), each
screen's own plant view, the looper's slots / step entry if wanted, then the device build on CI (and a .gitignore for
build/ when the project goes on git).

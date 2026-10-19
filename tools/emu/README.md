# FM-1 emulator

CoralFlower's firmware built for the computer. `emu_fw.c` is the firmware side: the sources `emu_firmware.h` lists
(the `cf_*` units, the DSP through `tests/hostsim.c`'s sound side), the HAL of `emu_hal_fw.h` and the `emu_fw_*`
hooks of `emu_hooks.h`. Three programs run it:

| program | files | what for |
|---|---|---|
| the browser emulator | `web/`, built into `build/emu-web` | the site's `/play/`: CoralFlower with sound, no unit needed |
| the headless runner | `cf_headless.c`, `cf_headless.sh` | scripts of presses with screenshots, dumps and a WAV; `tests/stuck_notes.py` |
| the window app | `emu.c` and the files below, `build.sh` | ChoralRoot's, inherited (below); needs SDL2 |

```sh
sh tools/emu/web/build_web.sh                 # -> build/emu-web/{index.html, emu.js, keymap.js, worklet.js, coralflower.wasm}
node tools/emu/web/test_web_emu.mjs           # the browser build's checks, against cf_headless.sh: PASS / FAIL
sh tools/emu/cf_headless.sh SCRIPT [OUTDIR]   # a script natively: OUTDIR/*.ppm (and .png with Pillow), out.wav
node tools/emu/web/cf_run.mjs SCRIPT [OUTDIR] # a script on the wasm
```

## The headless runner

`cf_headless.c` builds `web/emu_web.c` with `cc` (no SDL, no window) and plays a script through its `web_*` exports:
the browser's unit, so the same input at the same millisecond gives the same samples. Its header holds the script
grammar: `wait`, `key` / `keyq`, `btn`, `hold`, `turn`, `clock`, `midi`, `shot`, `dump`, `wav on|off`. `CF_BOOT_MS`
(default 1600) runs before the script (0: the script sees the intro); `CF_FLASH=PATH` reads a flash image before
power-on and writes it back after the script, so a second run sees what the first one saved. `cf_headless.sh`
builds it each time, as "CoralFlower dev" (a few seconds).

## Browser build

`tools/emu/web/` builds the same firmware side (`emu_fw.c` through `emu_firmware.h`) into a standalone WebAssembly
module played from a web page (no Emscripten JS runtime, no COOP/COEP headers: it runs on GitHub Pages).
`tools/make_site.sh` puts the page in the site's `/play/`, and the page takes its look from the site's `../site.css`.

Needs `emcc` (the Emscripten SDK; `~/emsdk/emsdk_env.sh` is sourced when it is not on the PATH); `CR_VERSION=1.0`
sets the version shown (else the newest release in `web/releases.json`). `emu_web.c` runs the device clock exactly
as the headless loop (tick, frame / idle, the audio blocks due, per millisecond) inside an AudioWorklet
(`worklet.js`), so a script's input gives `cf_headless`'s samples bit for bit. The CPU lock is a no-op
(`web/compat/os/lock.h`: one thread); the flash file is the page's IndexedDB copy (loaded before power-on, saved a
second after the last erase / program). The page (`index.html`, `emu.js`): the LCD, the panel with its LEDs
(pointer, multi-touch, right-click latches, wheel / drag on the knobs), the computer keyboard of `web/keymap.js` (a
tracker's piano: its own map, not `keymap.c`'s), Web MIDI in and out, Clear saved data, a load meter.

`test_web_emu.mjs` (`--no-native` and `--no-page` skip its second and third parts):
1. the wasm in node plays the test's own script (Top + Lock, the default layout: M + C4, then M7 + C4) and checks the
   LEDs, the sound, the chord's name on the screen, the dump's notes, the MIDI out, the settings saved by
   themselves and read back by a second module booted from the flash image;
2. the same script through `cf_headless.sh`, compared sample for sample and dump for dump;
3. the site from `make_site.sh` in `build/emu-web-test/site`, its `/play/?demo` in headless Chromium or Chrome
   (`CHROME=path`): the page's status line must say Ready, then a screenshot.

## ChoralRoot's window app (inherited)

Everything below describes ChoralRoot's SDL emulator as CoralFlower inherited it. `build.sh` still builds it, with
CoralFlower's firmware and version, where SDL2 is installed (Homebrew; not on every Mac). Its scripts are gone
(`tools/emu/scripts/`, `test_cr.sh`), so `test.sh`, `perf.sh` and `keylat.py` have nothing to run, and the screens,
layers, button roles and dump lines named below are ChoralRoot's: CoralFlower's are in docs/SPEC.md. The docs it
cites (docs/INTEGRATION.md, LOOPER.md, SETTINGS.md and the rest) stayed in ChoralRoot's repository.

The window app: the 240x240 LCD, the panel with its LEDs, the firmware's audio callback at 44.1 kHz and CoreMIDI
ports, around the firmware side above.

```sh
sh tools/emu/build.sh          # -> build/host/emu  (clang, SDL2 from /opt/homebrew, CoreMIDI, zlib)
build/host/emu --help          # options and the key map
```

`build.sh` runs the generate step (`tools/build.py`'s) when `build/gen` is missing; it sets
`DYLD_FALLBACK_LIBRARY_PATH=/opt/homebrew/lib` for Pillow. It compiles its five units side by side through
`tests/hostcc.sh`: a unit is compiled again only when its command (the version and every flag) or a file it was
compiled from has changed (hashes, not dates), so a second `build.sh` takes a fraction of a second;
`HOSTCC_FORCE=1 sh tools/emu/build.sh` compiles everything. `web/build_web.sh` does the same for the wasm module.

## Files

| file | what |
|---|---|
| `emu.c` | SDL window, panel drawing, mouse, the script runner, headless mode, audio callback, timing |
| `keymap.c` | the computer-key map (change it there; `--help` and the panel hints follow) |
| `emu_hooks.h` | the boundary: `emu_hal` (keys, buttons, knobs, LEDs, LCD) and the `emu_fw_*` hooks |
| `emu_fw.c`, `emu_firmware.h`, `emu_hal_fw.h` | the firmware side: its sources, the HAL, power-on, frame, audio ISR |
| `emu_midi.c` | CoreMIDI: source "ChoralRoot FM-1" (out), destination "ChoralRoot FM-1 In" (in); there is no USB audio (the firmware is built with `FELUCCA_UAC` 0 through `tests/hostsim.c`: Options > USB Record / USB Playback / USB Level are kept and saved, USB Level = Fixed applies, nothing streams; docs/USB-AUDIO.md) |
| `emu_img.c` | LCD to PNG / PPM |
| `perf.sh`, `scripts/perf_*.txt`, `wavclicks.py` | the worst cases for the CPU and the pops (docs/INTEGRATION.md, Performance): the audio block's cost and its device estimate, the UI frame's, voices stolen, flash erases, and the WAV checked for jumps, silent holes and clicks |
| `keylat.py`, `scripts/cr_keylat.txt` | a key's way to the engine while the screen draws (below, "A redraw in simulated time"; docs/INTEGRATION.md "Keys while a frame draws"): `keylat.py` strikes 40 keys at random times in a script and prints the latency with the keys read inside the redraw and between frames only (perf.sh's "keys" lines) |
| `seed_settings.py` | puts a unit's ChoralRoot settings record (192 bytes of hex, a backup's ChoralRoot block) into a `--flash` file that already holds a settings object, so a headless run starts from the unit's own settings (`python3 tools/emu/seed_settings.py FLASH HEX`; test_cr.sh "the arp screen from the owner's own settings record") |
| `scripts/`, `test.sh`, `test_cr.sh` | headless scripts and their checks (`sel.txt`, `knobs.txt`, `acceptance.txt`: Felucca's UI, no longer run) |

## Windowed mode: never in front by default

`build/host/emu` starts as a **background app**: `SDL_HINT_MAC_BACKGROUND_APP=1` (no Dock icon, no
activation, it never takes the keyboard focus from the app you are using), and its window opens at the
**bottom-right corner of the display the mouse pointer is on**. Click the window to play it with the keyboard.

`--front` turns that off: a normal app with a Dock icon, the window centred on that display and in front.

**Layout: the panel first.** The window shows the FM-1 panel alone, sized to about 80 % of the display's
usable height, with the live LCD on the panel's own screen. **`` ` ``** (backtick) shows / hides the big LCD view above the
panel (a square as wide as the panel; `--lcd` starts with it shown, `--scale N` then makes the starting
window 240*N points wide). The window is **resizable** and can go to any display: everything is redrawn
to fit, keeping the aspect ratio (letterboxed); the panel is re-rasterised at the new size, so it stays
sharp (in play only the keys, buttons and knobs whose look changed are redrawn and uploaded, each box from the
cached plate: well under a millisecond a press), and the scale snaps to an integer multiple of the 240x240 screen when one is close (nearest
neighbour; linear otherwise). `--display N` picks the display, `--pos X,Y` the top-left corner (global
points), `--size W,H` the size in points. Nothing is remembered between runs.

The panel is drawn in
the geometry of `ChoralRootFM1Designer/index.html` (BODY, SCREEN, KEYS, BUTTONS, ENCODERS) with the live
LEDs: key and button LEDs lit / mid / dim / faint from `fm1_led` / `fm1_led_dim2` / `fm1_led_dim` / `fm1_led_faint` (four levels; `expect led KEY on|mid|dim|faint|off`; the dump prints `leds mid:`, `leds faint:` and, per key, `key leds:` 0 off 1 faint 2 dim 3 mid 4 lit), REC red, PLAY orange plus its green LED.
Keys and buttons are clickable (right-click / ctrl-click latches one down), the knobs turn with the mouse
wheel over them or a vertical drag; a click selects a knob for Up / Down. At exit it prints the audio block
render time (avg / max us), the device callback gaps and the UI frame time.

Other options: `--flash PATH` / `--no-flash` / `--save-on-exit` (the flash file: settings, user sounds, loops), `--boot-fail N` / `--reset-reason poweron|wdt|soft|other` / `--boot-stage N` (the boot guard as a crashed run left it, `firmware/src/cr_bootguard.h`: `--boot-fail 1 --reset-reason wdt` starts in SAFE MODE, `--boot-fail 3 --reset-reason wdt` exits 3 as UBOOT), `--demo` (ignored by ChoralRoot), `--quit-after S`, `--midi-log`, `--no-midi`, `--no-audio`,
`--shot PATH` (the LCD at exit). A flash erase plays as on the device: 45 ms of silence with the audio ISR not run
(`emu_hal_fw.h` `st_erase`; the dump's `flash erases with the audio stalled` line counts them).

**PA2** (`emu_hooks.h` `emu_hal.pa2`, `firmware/hal/fm1_lcd_hw.h`). On the unit PA2 is not the backlight alone: low,
the backlight and the key matrix / 595 chain (keys, buttons, knobs, LEDs) are alive; high, all of them are dead (seen
2026-10-10: the screen off of 0.14 / 0.15 raised it for the backlight, and no key sounded or woke the unit). The
emulator carries that fact: while `emu_hal.pa2` is set the panel (and every shot) is black, no LED lights, the 1 ms
tick reads no key, button or knob, and the dump's `pa2:` line says `HIGH` and counts the milliseconds. The firmware
never drives PA2 high any more (the screen off is a black frame with the backlight on, so a shot of it shows the
pixels the firmware wrote); a HAL stand-in in `emu_hal_fw.h` for code that ever does must set `emu_hal.pa2`, and
`test_cr.sh`'s screen-off checks then fail as they do on the 0.14 screen off. `scripts/cr_pa2.txt` checks the
model itself with the script's `pa2 high`.

## Key map (physical US-layout positions; `keymap.c`)

Firmware key index = MIDI note - 53 (0 = F3 .. 26 = G5).

| computer keys | FM-1 |
|---|---|
| `A S D F G H J K L ; ' ]` | white note keys C4 D4 E4 F4 G4 A4 B4 C5 D5 E5 F5 G5 (keys 7 9 11 12 14 16 18 19 21 23 24 26; Ableton Live's layout) |
| `W E T Y U O P` | black note keys C#4 D#4 F#4 G#4 A#4 C#5 D#5 (keys 8 10 13 15 17 20 22); `R` and `I` (the gaps) are unmapped, F#5 (key 25) is mouse only |
| `F1 F2 F3 F4` | F#3 G#3 A#3 C#4 (keys 1 3 5 8) |
| `2 3 4 5` | F3 G3 A3 C4 (keys 0 2 4 7) |
| Tab | B3 (key 6: ChoralRoot's LOCK, the chord mod key) |
| `Z` `X` (Ableton's), also Esc / Return | OCT- / OCT+ (Esc backs out of menus) |
| End | OCT- and OCT+ together (panic) |
| F5 F6 F7 F8 F9 F10 | the top button row: FX SEL ENV LFO EDIT GLO (ChoralRoot: FX KEY BASS LATCH EDIT OPT; in the editor: OSC FILT ENV LFO EDIT SHIFT) |
| `7 8 9 0 - =` | the bottom button row: HOME SAVE ARP SEQ PLAY REC (ChoralRoot: HOME SAVE PERF METRO LOOP REC; in the editor: HOME SAVE PERF MOD FX MIX) |
| Shift + Up / Down | fine steps in the editor (the firmware's SHIFT: GLO held); outside it, OPT's second function (OPT + KNOB 1 = split point, OPT + SELECT = metronome level) |
| Page Down / Page Up | select the next / previous knob of SELECT, KNOB1 .. KNOB4 (wraps; the selected knob has a ring) |
| Up / Down | turn the selected knob one detent clockwise / counter-clockwise (repeats while held; MASTER: 32 of 1023) |
| mouse wheel over a knob | turn that knob (MASTER, PRESETS, ALGORITHM: the mouse only; a click selects a knob for Up / Down) |
| `` ` `` (backtick) | show / hide the big LCD view above the panel (window only) |
| PrintScreen (F13 on a Mac keyboard) | LCD screenshot: `build/emu/shot_NNN.png` |
| Insert | record every LCD frame to `build/emu/rec/NNNN.ppm` (again to stop) |
| F12 | print the input state (`fm1_in`, the LEDs, the track, the song) |

No computer key is mapped twice: the emulator checks the table at start-up and exits 2 on a duplicate.
On a Mac keyboard the F keys may need `fn`. Cmd shortcuts stay the system's; losing the focus releases
every held key (no stuck notes).

## Headless mode (`build/host/emu --headless`)

```sh
build/host/emu --headless --script FILE [--frames N] [--shot build/emu/x.ppm] [--wav out.wav]
```

No window and no audio device: `SDL_Init(0)` with `SDL_VIDEODRIVER=dummy` and `SDL_AUDIODRIVER=dummy` set.
Time is simulated (the 1 ms timer, a UI frame every 15 ms, the 128-frame audio blocks as they fall due), so
a run is deterministic: the same script gives the same LCD and the same audio, bit for bit. It runs N
frames of 15 ms (`--frames N`; without it, to the end of the script, else 600 frames), then prints the
peak / rms, the block render time, the host instructions per audio block and UI frame with the device estimate
(`EMU_CPU_LOG=PCT` lists every block above PCT % of the device's budget; `EMU_STAGES=1` adds the audio ISR's stages,
`cpu: stages ...` (docs/DUALCORE.md); the second core is off unless the flash's settings say Options > Dual Core On
(`cr_dualcore_on.txt` on a `--flash` file, then relaunch with it); then `EMU_C1=nostart` / `EMU_C1=hang`: it does not
answer at power-on / never finishes a job (the stub otherwise runs the bass part's job inline); `EMU_OUTPUT=classic`
holds Options > Output at Classic whatever the settings say, `EMU_CHORUS=mono` returns the chorus as 0.18 did (one tap
to both sides: the unit has only the stereo return), `EMU_MASTER=ADC` puts the MASTER pot at ADC (0..1023) at
power-on (docs/AUDIO-QUALITY.md "Round 3"); `EMU_UI_LOG=N` every UI frame above N
million host instructions; `EMU_TRACE_LEDS=1` prints the LED bytes and the buttons off / dim / lit after every UI
frame they (or the held controls) change, and in the window each panel redraw and its time; `EMU_PANEL_CHECK=1`
compares every partial panel redraw with a whole one, pixel for pixel, with its index, time, device estimate and the screen it drew: kind, view, name / item /
title, animation clock, ring, message, strips blitted), **the number of non-silent blocks and
non-zero samples**, and exits
1 if an `expect` failed (2 on a script error). `--frames` alone implies `--headless`.

### Script grammar

One command a line; `#` at the start of a word begins a comment (so `F#4` is a name). The script has its
own clock in ms from power-on; commands run in order and only `wait` (and the timed presses) move it.

| command | meaning |
|---|---|
| `wait MS` | move the clock on: `200`, `200ms`, `1.5s` (`frames N` = N x 15 ms) |
| `key KEY down` / `key KEY up` | a computer key of the map, as if typed: `key S down` (D4), `key RETURN up`, `key UP down` |
| `key KEY [MS]` | press KEY for MS (default 100) ms; the clock moves on by MS |
| `btn NAME [down\|up\|MS]` | a panel control by name: `FX SEL ENV LFO EDIT GLO HOME SAVE ARP SEQ PLAY REC OCT- OCT+` or a note key `F3`..`G5` (`D#4`); default a 100 ms tap |
| `knob NAME +-N` | turn `SELECT ALGORITHM PRESETS KNOB1..KNOB4` N detents (+ clockwise); `MASTER`: N x 16 of 1023 |
| `master ADC` | set the MASTER pot, 0..1023 |
| `shot NAME` | the LCD to `build/emu/NAME.png` and `.ppm`; a NAME with `/` or ending `.ppm` / `.png` is a path |
| `expect led NAME on\|dim\|off` | check a key's or button's LED now (`on` = lit); `GREEN` is PLAY's green LED |
| `expect sound` / `expect silence` | non-zero samples since the previous `expect sound\|silence` (or power-on) |
| `probe N` | (window) the next N presented frames: each button cap's level read back from the frame, with its LED state (`probe: present ... FX=247/2 SEL=121/1 ...`) |
| `dump` | print the input state (as F12); its `cr: key` line ends with the KEY layer's keys (`kkeys` 0 pick, 1 play); the `lock:` line gives LOCK, the latched chord keys (bits DIM 01 MIN 02 MAJ 04 SUS 08, 6 10, m7 20, M7 40, 9 80) and the LOCK layer's two styles; its `loop:` line also gives the record mode, the PLAY menu's keys, the step cursor and the events a Replace layer hid (`mode keys step hidden`); `loop3:` the loop editor's page, its cursor step and that step's ratchet / condition / voicing / chance (-1: none), its notes' part of a step, the pass of the loop (mod 12), the steps that have a property and the loop voices in use (`epage cursor ratchet cond voicing chance frac pass props loopv`); `loop4:` the cursor step's events as root/qx in the array's order with their count, and the chord the loop struck last since it started (`step-events 72/20 count 1 hit 62/30`; `hit -`: none), and every UI frame's line (`EMU_UI_LOG`) has the editor's playing name (`now '...'`); `lcd:` says whether the UI's screen is off (a black frame) and counts the LCD writes; `pa2:` the PA2 line (below) |
| `pa2 high` / `pa2 low` | drive the PA2 line by hand (the firmware never raises it): high = the panel black, no LED lit, no key, button or knob read, as on the unit |
| `strike KEY [MS]` | (headless) a note key pressed at the worst moment for its latency: as the next UI frame begins, just past its scan; let go MS (default 100) later. Prints the strips that frame's redraw composed (`strike: G4 down at 1805 ms as a frame began: a redraw of 6 strips, over at 1821 ms`). With `EMU_STRIP_US` (below) |
| `rec` | start / stop recording the LCD frames (as Insert) |
| `quit` | stop here |

Older forms still work: `press KEY [MS]`, `tap`, `hold` / `down`, `release` / `up`, `turn KNOB N`, and an
absolute time in front of a command (`1200 down A`). For `key`/`press`/`hold` a computer key name wins,
for `btn`/`knob`/`expect` a panel name; `note:F3` always means the note F3.

Example (`scripts/key_a.txt`):

```
wait 500
shot build/emu/test/home.ppm   # the power-on screen
btn D4 down                     # D4 (computer key S)
wait 150
expect led D4 on
wait 150
btn D4 up
wait 100
expect sound
```

### A redraw in simulated time

By default a UI frame takes none of the simulated time: the tick, the frame, the audio blocks due, a millisecond at
a time. So a key never goes down "while the screen draws", and the firmware's key reads inside a redraw
(`firmware/src/cr_ui.c cr_ui_pump`, docs/INTEGRATION.md "Keys while a frame draws") change nothing: every script's
LCD and WAV are what they were. Three switches, headless only, for measuring that path:

| environment | what |
|---|---|
| `EMU_STRIP_US=N` | every strip a redraw composes takes N us of the device's clock (2700: six strips = 16 ms, the emulator's worst frame; 7800: 47 ms). The milliseconds that pass run inside the frame as the device's interrupts would: the timer's tick, the script (a key goes down mid-frame), the audio blocks due. The main loop's own scan waits for the frame, as on the device; the next frame starts 15 ms after this one began, or at once if it took longer. A strip at a time: the model shows the keys read after each strip, not the reads inside one |
| `EMU_PUMP=0` | the keys are not read inside a redraw: the firmware as it was before 0.18's key pump, the reference |
| `EMU_UI_GAP=1` | prints `ui: the longest stretch of a frame with no key read` (host instructions and the device estimate): from a frame's start to the first key read in its redraw, between two reads, from the last to the frame's end. A run apart: each mark is a system call, counted in the frame's instructions |

Every headless run that played a key prints `keys: N note-ons, from the key's scan to the engine avg .. ms, worst ..
ms (to the main loop's post: worst .. ms); M key scans inside redraws`: the firmware's own counters (the unit's 78
PEAK `key_*`), from the tick that saw the key down to the audio block that started its note. The emulator's keys
have no debounce (the unit adds 1.1 to 2.2 ms), and the sound leaves the DAC one half (2.9 ms) later.

```sh
EMU_STRIP_US=2700 build/host/emu --headless --script tools/emu/scripts/cr_keylat.txt             # worst 4 ms
EMU_STRIP_US=2700 EMU_PUMP=0 build/host/emu --headless --script tools/emu/scripts/cr_keylat.txt  # worst 19 ms
python3 tools/emu/keylat.py tools/emu/scripts/perf_d.txt     # 40 keys at random times, both ways, two strip times
```

### test.sh

`sh tools/emu/test.sh` rebuilds when a source is newer, then checks in headless mode: D4 held 300 ms
lights key D4's LED and gives non-silent blocks; the power-on screen is written; a second run gives the same
LCD and WAV; then `test_cr.sh`: MAJ + D4 (D major: sound, the D4 F#4 A4 LEDs, the chord screen), KNOB 1
(voicing), KEY tap / hold (Key Mode, select-key), PERF hold (the Perform picker), PRESETS (another sound),
ALGORITHM (the bass on part 1), End (PANIC), idle (the stripes), Options and the other layers' screens, no
stuck notes (every part silent 2 s after the release). Outputs and logs go to `build/emu/test/`.

## Realtime smoke test without a window

The windowed code path (timer thread, SDL audio callback, renderer) also runs under SDL's dummy drivers,
which open no window and no audio device:

```sh
SDL_VIDEODRIVER=dummy SDL_AUDIODRIVER=dummy build/host/emu --no-midi --demo --quit-after 3
```

## Not done / stubbed

- The flash is a file (`FELUCCA_FLASH 1` on `emu_hal_fw.h`'s NOR image; docs/SETTINGS.md "The emulator's flash"):
  settings, user sounds and loops persist across runs on the same `--flash` file (windowed runs:
  `build/emu/flash.bin` unless another is given). A headless run without `--flash`, and any run with `--no-flash`,
  is RAM only: nothing persists.
- The CPU meter reads ~0 %: the Mac renders a block in a few tens of us against the device's budget (headless runs
  print a device estimate from the host's instruction count instead: above, and `perf.sh`).
- ChoralRoot's roles of the printed buttons: F6 (SEL) = KEY, F9 (EDIT) = EDIT, F5 FX, F7 (ENV) BASS, F8 (LFO)
  LATCH, F10 (GLO) OPT, `9` (ARP) PERF, `0` (SEQ) METRO, `-` (PLAY) LOOP, `=` REC; B3 (Tab) = LOCK.
  A layer button held past the hold time (400 ms by default, Options > Hold Time) locks its layer open: scripts
  close it with `btn OCT-` (or `btn HOME`). SAVE has no layer since 0.18: held, it is the global save (`scripts/cr_save_hold.txt`).
- Sound editing: F9 (EDIT) tap = the sound editor (F7 held + F9 the bass's), held = the engine picker;
  `8` (SAVE) = the save dialog (Overwrite / Save as new), then naming (the white roots type, D#4 a space, F#4
  deletes, OCT- (`Z`) cancels, OCT+ (`X`) saves). User sounds go to the flash file (above).
- ChoralRoot's looper and metronome run as on the device (`cr_loop.c` ticked with the engine, the click mixed in
  `emu_firmware.h`; docs/LOOPER.md); the loop slots go to the flash file (above).

# CoralFlower

**A chord instrument for the M-VAVE FM-1.** Hold a root on the lower keys, add colour with the upper keys, and the
FM-1 plays the whole chord: in key, smoothly voiced, with a bass underneath. While you play, a pixel flower grows
from your chords.

<p align="center"><img src="docs/images/grow.gif" width="480" alt="A pink pixel flower growing and changing as chords are played"></p>

<p align="center">
  <a href="https://ficusficusov.github.io/CoralFlower/"><b>Try it in your browser</b></a> ·
  <a href="https://ficusficusov.github.io/CoralFlower/install/"><b>Install it on your FM-1</b></a> ·
  <a href="https://github.com/FicusFicusov/CoralFlower/releases"><b>Releases</b></a>
</p>

The browser emulator runs the real firmware code, compiled to WebAssembly, with sound. Play it with the mouse or the
computer keyboard before you install anything.

## What it does

- **Chords with two fingers.** One finger picks the root on the lower keys; the other picks the chord type on the upper
  keys: major, minor, sus2, sus4, 6, 7 or 9. Hold more upper keys to combine types. Quantize keeps every chord in your
  key, in any of 26 scales. Chord glide moves each voice smoothly to its place in the next chord.
- **Four ways to play.** In the Top layout the upper row picks the chord type and the lower row the root. In the Side
  layout a few keys on the left pick the type and the rest play roots. Each layout has a Lock mode that keeps the chord
  type, so one finger plays the whole chord.
- **A bass part** under every chord, with its own sound, octave and an octaver (a second note an octave away).
- **An arpeggiator** with nine modes, including a strum like the Orchid chord synth's. It syncs to the metronome or to
  an external MIDI clock.
- **A looper** for chord progressions, with count-in, overdub, undo and timing quantize. Your loop stays saved when you
  switch off.
- **Six effects** in a chain you can reorder: filter, drive, chorus, phaser, delay and reverb.
- **About 60 sounds** from the virtual-analog (VA) and FM synth engines, many of them slowly breathing and drifting.
  Chord sounds and bass sounds have separate lists. Every sound has a full editor that draws what you change, and you
  can save your own.
- **A plant that grows from your music.** A flower grows from your chords in the Top layout, a coral in the Side
  layout. Every note of the chord grows its own part: a bloom, a bud, a cluster of berries. The plant's structure is
  modelled on [PlantStudio](https://github.com/pdfernhout/PlantStudio), a botanical plant designer.
- **MIDI over USB:** chord and bass notes out on their own channels, clock in.

<table>
  <tr>
    <td><img src="docs/images/home.png" width="260" alt="HOME: the chord name above a pink flower"></td>
    <td><img src="docs/images/coral.png" width="260" alt="The Side layout: a pink coral"></td>
    <td><img src="docs/images/chord.png" width="260" alt="CHORD: a chord button's notes on a piano"></td>
  </tr>
  <tr>
    <td><img src="docs/images/fm-voice.png" width="260" alt="The FM editor: the algorithm's operators"></td>
    <td><img src="docs/images/arp.png" width="260" alt="ARPEGGIO: the pattern as a little piano roll"></td>
    <td><img src="docs/images/looper.png" width="260" alt="LOOPER: a take being recorded"></td>
  </tr>
</table>

## Install

1. Open <https://ficusficusov.github.io/CoralFlower/install/> in **Chrome or Edge** on a computer. These browsers can
   talk to the FM-1 over USB.
2. Connect the FM-1 straight to the computer with a USB cable, not through a hub.
3. Press **Connect**, then **Install**. Keep the page open and the cable plugged in for about a minute. The FM-1
   restarts as CoralFlower.

The installer offers to back up your sounds first. To go back to the stock firmware, download FM-1 V15 from
[m-vave.com](https://www.m-vave.com) and install it the same way. Installing firmware is at your own risk.

## Build it yourself

- **The browser emulator:** install the [Emscripten SDK](https://emscripten.org), then run
  `sh tools/emu/web/build_web.sh`. The page is built into `build/emu-web/`.
- **The headless runner:** `sh tools/emu/cf_headless.sh SCRIPT` plays a script of key presses, takes screenshots and
  records audio. It is useful for tests.
- **The firmware package:** `./build.sh --release X.Y` needs the toolchain of JieLi (the maker of the FM-1's chip),
  which runs on Linux x86-64 only
  (`tools/get_toolchain.sh` fetches it). The release workflow (`.github/workflows/release.yml`) builds the package on
  GitHub.

## Credits and licence

CoralFlower is free software under the GPL-3.0 (see `LICENSE` and `LICENSING.md`). It is built on
[ChoralRoot FM-1](https://github.com/Quixotic7/ChoralRootFM1) by Quixotic7 and on
[Felucca](https://github.com/hugelton/Felucca) by Leo Kuroshita (Hügelton Instruments), with the FM engine from
[Melodee](https://github.com/keremimo/melodee) by Kerem Kilic. The screens use the
[Departure Mono](https://departuremono.com) font by Helena Zhang. Thank you to everyone whose work this builds on.

M-VAVE and FM-1 are trademarks of their respective owners. Orchid is a product of Telepathic Instruments. CoralFlower
is not affiliated with M-VAVE or Telepathic Instruments.

## Feedback and support

This is a young project, so what your ears and hands tell me matters most. Tell me what feels good, what feels off,
and which sounds or features you would like next:

- [GitHub issues](https://github.com/FicusFicusov/CoralFlower/issues)
- the Telegram channel [t.me/ficusficusov](https://t.me/ficusficusov)
- email: iambemoli@gmail.com

CoralFlower is free, open source and ad-free. If you would like to support its development, the **Support** button on
the [browser emulator](https://ficusficusov.github.io/CoralFlower/) lists crypto wallets.

#!/bin/sh
# SPDX-License-Identifier: GPL-3.0-only
# LISTENING FILES for 0.19's stereo chorus (docs/FX.md "The chorus, stereo"): the same note and chord through the
# emulator with the chorus as the unit has it (two taps, left and right) and as 0.18 returned it (one tap to both
# sides: EMU_CHORUS=mono, the emulator's test switch), at the default send and at a high one, and the mono sums.
#   sh tools/chorus_listen.sh [OUTDIR]        (default ../audio-0.19/chorus beside the repository; builds nothing:
#                                              build/host/emu must be this tree's, sh tools/emu/build.sh)
# Headless: export DYLD_FALLBACK_LIBRARY_PATH=/opt/homebrew/lib SDL_VIDEODRIVER=dummy SDL_AUDIODRIVER=dummy
set -e
cd "$(dirname "$0")/.."
O=${1:-../audio-0.19/chorus}
W=build/scratch/chorus_listen
EMU=build/host/emu
[ -x "$EMU" ] || { echo "no $EMU: sh tools/emu/build.sh first"; exit 1; }
mkdir -p "$O" "$W" build/emu/test
export SDL_VIDEODRIVER=dummy SDL_AUDIODRIVER=dummy

# script NAME SOUND SENDS WHAT: SOUND tine | va (OPT + PRESETS +2: the VA, LUSH PAD); SENDS default (chorus 45, the
# delay and the reverb off) | high (chorus 105) | all (the unit's defaults: reverb 35, chorus 45, delay 25); WHAT note |
# chord (MAJ + 6 + M7 + 9 held, D4: six notes)
script() {
    f="$W/$1.txt"
    {
        echo "master 1023"
        echo "wait 300"
        if [ "$2" = va ]; then
            printf 'btn GLO down\nwait 100\nknob PRESETS +2\nwait 100\nbtn GLO up\nwait 300\n'
        fi
        if [ "$3" != all ]; then
            printf 'btn FX down\nwait 500\nknob KNOB3 -5\nwait 100\nknob KNOB4 -6\nwait 100\n'
            [ "$3" = high ] && printf 'knob KNOB2 +10\nwait 100\n'
            printf 'btn FX up\nwait 300\nbtn OCT-\nwait 300\n'
        fi
        if [ "$4" = chord ]; then
            printf 'key F3 down\nkey 2 down\nkey 4 down\nkey 5 down\nwait 40\n'
        fi
        printf 'btn D4 down\nwait 4000\ndump\nbtn D4 up\n'
        [ "$4" = chord ] && printf 'key F3 up\nkey 2 up\nkey 4 up\nkey 5 up\n'
        printf 'wait 2500\nquit\n'
    } > "$f"
}
: > "$O/render.log"
for snd in tine va; do
    for what in note chord; do
        for sends in default high all; do
            n="$snd-$what-send-$sends"
            script "$n" "$snd" "$sends" "$what"
            "$EMU" --headless --script "$W/$n.txt" --wav "$O/$n.stereo.wav" > "$W/$n.log" 2>&1
            EMU_CHORUS=mono "$EMU" --headless --script "$W/$n.txt" --wav "$O/$n.mono018.wav" > "$W/$n.mono.log" 2>&1
            {
                echo "== $n"
                LC_ALL=C grep -a "part 0:" "$W/$n.log" | head -1
                LC_ALL=C grep -a "session fx" "$W/$n.log" | head -1 | sed 's/ par .*//'
                echo "   0.19 stereo: $(python3 tools/emu/wavstat.py stereo "$O/$n.stereo.wav")"
                echo "   0.18's return: $(python3 tools/emu/wavstat.py stereo "$O/$n.mono018.wav")"
                echo "   0.19 against 0.18's: $(python3 tools/emu/wavstat.py mono "$O/$n.stereo.wav" "$O/$n.mono018.wav")"
            } >> "$O/render.log"
        done
    done
done
# the mono sums, and one file with the three after each other
python3 - "$O" <<'EOF'
import glob, os, sys
import numpy as np
o = sys.argv[1]
def load(p):
    b = open(p, "rb").read()
    i = b.find(b"data")
    return b[:i + 8], np.frombuffer(b[i + 8:len(b) - (len(b) - i - 8) % 4], dtype="<i2").reshape(-1, 2)
def save(p, x):
    x = np.ascontiguousarray(x.astype("<i2"))
    with open(p, "wb") as f:
        f.write(b"RIFF" + np.uint32(36 + x.nbytes).tobytes() + b"WAVEfmt " + np.uint32(16).tobytes())
        f.write(np.uint16(1).tobytes() + np.uint16(2).tobytes() + np.uint32(44100).tobytes() + np.uint32(176400).tobytes())
        f.write(np.uint16(4).tobytes() + np.uint16(16).tobytes() + b"data" + np.uint32(x.nbytes).tobytes())
        f.write(x.tobytes())
for p in sorted(glob.glob(os.path.join(o, "*.stereo.wav"))):
    _, s = load(p)
    _, m = load(p.replace(".stereo.wav", ".mono018.wav"))
    ms = ((s[:, 0].astype(np.int32) + s[:, 1]) // 2).astype(np.int16)
    sm = np.stack([ms, ms], axis=1)
    save(p.replace(".stereo.wav", ".stereo.monosum.wav"), sm)
    mm = ((m[:, 0].astype(np.int32) + m[:, 1]) // 2).astype(np.int16)
    save(p.replace(".stereo.wav", ".mono018.monosum.wav"), np.stack([mm, mm], axis=1))
    gap = np.zeros((22050, 2), dtype=np.int16)
    a = int(0.9 * 44100)                    # from just before the note, 5.5 s of each
    n = int(5.5 * 44100)
    save(p.replace(".stereo.wav", ".abc.wav"), np.concatenate([m[a:a + n], gap, s[a:a + n], gap, sm[a:a + n]]))
EOF
cat > "$O/README.txt" <<'EOF'
ChoralRoot 0.19: the chorus in stereo. Listening files (tools/chorus_listen.sh; docs/FX.md "The chorus, stereo").

The emulator's output (Options > Output Clean, MASTER at its top), the same script each time:
  NAME.mono018.wav          the chorus returned as 0.18 did: ONE tap, the same to both sides (EMU_CHORUS=mono)
  NAME.stereo.wav           0.19: TWO taps on the same line, left and right, the right one's LFO a quarter turn ahead
  NAME.stereo.monosum.wav   0.19's left and right added ((L + R) / 2 on both sides): what one speaker gets
  NAME.mono018.monosum.wav  the same of 0.18's (with "send-default" and "send-high" it is the mono018 file itself:
                            only the chorus sounds, and that was mono; with "send-all" the delay and reverb are in it)
  NAME.abc.wav              5.5 s of each after each other, half a second between: 0.18, 0.19 stereo, 0.19's mono sum

NAME = SOUND-WHAT-send-SENDS
  SOUND   tine: FM6 TINE EP (the power-on sound, nearly a sine at D4: the chorus is most of what moves)
          va:   VA LUSH PAD (a brighter one: the comb is easier to hear)
  WHAT    note: D4 alone, held 4 s        chord: MAJ + 6 + M7 + 9 held + D4, six notes, held 4 s
  SENDS   default: the chorus send at its default (45 of 127, the wet 9 dB under the dry), the delay and the reverb
                   sends at 0 so that only the chorus is heard beside the dry sound
          high:    the chorus send at 105 (the wet 1.6 dB under the dry), the others at 0
          all:     as a new unit plays: reverb 35, chorus 45, delay 25

What to listen for, on headphones first:
  - mono018 against stereo: 0.18's chorus sits in the middle of the head and its "swirl" is one comb moving in both
    ears at once; 0.19's is spread and each ear has its own. Left alone is 0.18's tap (the same sound to -45 dB).
  - stereo.monosum against mono018: what a single speaker gets. The deepest notch is the same (it is reached at the
    two moments of the LFO's turn where the taps cross); everywhere else the sum's comb is shallower, and the chorus
    still moves (71 % of the delay's swing is left in the sum). "high" shows it most. The sum is also a little
    quieter than 0.18's (0.0 to 0.4 dB at the default send, 0.8 to 1.5 dB at the high one: two different taps add
    to less than one tap twice).
render.log: the sound and the sends of each render, the side's level against the mid's, the levels against 0.18's.
EOF
echo "wrote $(ls "$O"/*.wav | wc -l | tr -d ' ') WAVs, README.txt and render.log into $O"

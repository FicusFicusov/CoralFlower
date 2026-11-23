# Sampled drum kits

The S kits (S 808, S 909, S Linn: `firmware/src/drum_smp.c`) play recorded one-shots. **No recordings are in this
repository**: put your own here and build the data header, and the kits appear in every kit list. Without it the
firmware builds as before, with no S kits.

```
assets/drum-kits/S808/   bd.wav sd.wav cp.wav hh.wav oh.wav mt.wav rim.wav bell.wav cr.wav
assets/drum-kits/S909/   (the same nine)
assets/drum-kits/SLINN/  (the same nine)

python3 tools/gen_drum_samples.py      # writes firmware/src/drum_smp_data.h (needs ffmpeg and numpy)
./build.sh
```

Any WAV ffmpeg reads will do. Each sound is cut to its cap (0.2 .. 1.2 s), faded out, levelled and stored as 4-bit IMA
ADPCM: kick and tom at 22.05 kHz, the rest at 32 kHz. Three kits of drum machine hits come to about 160 KB of flash.
`mt` is one tom: the low, mid and high tom lanes play it at three pitches. `bell` is the cowbell's lane (a ride works).

The wavs and the generated header are in `.gitignore`: use recordings you have the right to use, and do not commit
ones you may not redistribute.

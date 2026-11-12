#!/usr/bin/env python3
"""Starting a WAV recording must not stop the script's ongoing MIDI Clock."""
from pathlib import Path
import re
import subprocess
import tempfile
import wave
import numpy as np

root = Path(__file__).resolve().parents[1]
script = """wait 2200
btn GLO
turn SELECT 1
turn K1 1
turn K2 1
btn HOME
btn ARP
turn K1 1
turn K2 3
turn K3 1
turn K4 2
turn SELECT 1
turn K1 20
turn K2 -2
turn SELECT -1
clockon 120
midi start
key 7 down
key 11 down
key 14 down
wait 300
wav on
turn K2 1
wait 11000
dump
key 7 up
key 11 up
key 14 up
wait 4000
dump
wav off
"""
with tempfile.TemporaryDirectory() as tmp:
    folder = Path(tmp)
    (folder / "script.txt").write_text(script)
    result = subprocess.run([str(root / "build/host/cf_headless"), str(folder / "script.txt"), tmp],
                            cwd=root, text=True, capture_output=True, timeout=60, check=True)
    counts = [int(x) for x in re.findall(r"notes on (\d+)", result.stdout)]
    assert len(counts) == 2 and counts[0] > 75 and counts[1] == counts[0], f"Clock+WAV onsets: {counts}"
    assert re.findall(r"part 0: engine \d+ voices (\d+)", result.stdout)[-1] == "0", "stuck voice after release"
    with wave.open(str(folder / "out.wav")) as wav:
        rate = wav.getframerate()
        samples = np.frombuffer(wav.readframes(wav.getnframes()), dtype="<i2").reshape(-1, 2).astype(float)
    assert len(samples) > rate * 14, "recording ended before the scenario"
    rms = [float(np.sqrt(np.mean(samples[i*rate:(i+1)*rate]**2))) for i in range(11)]
    assert min(rms) > 10, f"ARP missing in a recorded second: {rms}"
    print(f"HEADLESS CLOCK+WAV PASS: {counts[0]} onsets, sound in all 11 seconds, release stops notes")

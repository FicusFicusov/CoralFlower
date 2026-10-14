#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-only
"""CZ-1's pops on the unit: are they late audio? (docs/CZ1.md "Pops", docs/FX.md "What the unit says")

The host render shows what the engine computes; it cannot show a half that was rendered late. The unit counts those
itself (SysEx 78 PEAK, `fm1_install.py --debug`: worst cases since the last read, cleared by reading). This tool plays
the heaviest CZ-1 material for a fixed time and judges the counters, or judges them after you played by hand.

  python3 tools/cz_pop_check.py                 print the plan, check that a ChoralRoot answers (--info). Sends no
                                                note, reads no counter
  python3 tools/cz_pop_check.py --read          .. then read the counters (which clears them) and judge: after a
                                                session played by hand on the unit
  python3 tools/cz_pop_check.py --play          .. select the sounds, clear the counters, play over MIDI for
                                                --seconds (default 45), read and judge

--play needs Options > MIDI Perform and MIDI Bass set to the channels given here (--channel, default 1;
--bass-channel, default 2; a channel that is Off is ignored by the unit and nothing sounds: the tool cannot tell, so
listen). It selects CZ-1 05 STRINGS 2 on the chord part and CZ-1 13 SYNTH.BASS on the bass part (--tone, --bass-tone:
their positions in the CZ-1 pool; --no-select leaves the unit's sounds as they are) with `fm1_install.py --select`,
which changes the sounds that play, not what is saved. The effects are the unit's: for "without FX" switch the FX tap
off on the unit first; for the heaviest case leave both sends up.

What it plays (MIDI notes straight to the parts: no chord engine, no arp of the unit's; velocity --velocity, 110):
  1. eight notes held (all of CZ-1's voices) with the bass, a third of the time: the steady cost
  2. six-note chords with common tones changed every 400 ms with the bass, a third: the voices struck again
  3. eight notes struck every 60 ms in turn, a third: attacks in every half (the DCW ramps: the dearest blocks)

The verdict (cause "late audio" against "the engine's arithmetic"):
  stale_halves > 0     halves in which the DAC played something old: audible drop-outs or clicks, and they are timing.
                       The cause is the stage `pk_late_stage` names; if it is part0, CZ-1's render
  pk_late > 0 only     halves finished after their deadline that the DAC did not reach: not audible by themselves
  pk_part0_us          the chord part's worst half: about 630 us was predicted for eight CZ-1 voices (docs/CZ1.md),
                       about 700 with every DCW moving; above 1450 (half of the 2902 us budget) it is far from the
                       count and worth a report; above 2000 a late half is a matter of what else runs
  flash_holes > 0      a flash erase fell into the sound (a save): a 45 ms hole that is neither
  all of them clean    what you heard in this run, if anything, was not late audio

Exit status: 0 clean, 1 late audio found, 2 could not run (no FM-1, not ChoralRoot, no MIDI). Never run it while
another tool talks to the FM-1."""
import argparse
import io
import os
import re
import sys
import time

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import fm1_install as I  # noqa: E402

BUDGET_US = 2902
PREDICTED_US = 630          # eight CZ-1 voices, docs/CZ1.md "The render's cost on the unit"
WARN_US = BUDGET_US // 2
HIGH_US = 2000


def tool(args, port):
    """fm1_install.py ARGS -> (exit status, its output)"""
    out = io.StringIO()
    try:
        rc = I.main(list(args) + (["--port", port] if port else []), out=out)
    except SystemExit as e:                      # (argparse, or the tool's own exit)
        rc = e.code if isinstance(e.code, int) else 2
    except I.InstallError as e:
        out.write(f"{e}\n")
        rc = 2
    return rc, out.getvalue()


def plan(a):
    third = a.seconds / 3
    lines = [f"plan ({'--play' if a.play else '--read' if a.read else 'nothing is sent: no --play, no --read'}):",
             "  1. fm1_install.py --info: go on only if the FM-1 answers as ChoralRoot"]
    if a.play:
        if a.select:
            lines += [f"  2. fm1_install.py --select 0 {a.engine} {a.tone} and --select 1 {a.engine} {a.bass_tone}: the "
                      f"chord part to CZ-1 position {a.tone}, the bass part to CZ-1 position {a.bass_tone} (live, not saved)"]
        else:
            lines += ["  2. (--no-select: the unit's sounds as they are)"]
        lines += ["  3. fm1_install.py --debug: read the counters once to clear them",
                  f"  4. MIDI notes on channel {a.channel} (chord part) and {a.bass_channel} (bass part), velocity "
                  f"{a.velocity}, {a.seconds:.0f} s:",
                  f"       {third:.0f} s  eight notes held with the bass",
                  f"       {third:.0f} s  six-note chords with common tones, changed every 400 ms, with the bass",
                  f"       {third:.0f} s  eight notes struck in turn every 60 ms",
                  "     then every note off (and CC 123 on both channels)",
                  "  5. fm1_install.py --debug: read the counters and judge"]
    elif a.read:
        lines += ["  2. fm1_install.py --debug: read the counters (this clears them) and judge"]
    else:
        lines += ["  2. stop"]
    return "\n".join(lines)


def midi_out(port):
    try:
        import rtmidi
    except ImportError:
        raise I.InstallError("other", "python-rtmidi is missing: python3 -m pip install python-rtmidi")
    mo = rtmidi.MidiOut()
    names = mo.get_ports()
    pick = [k for k, n in enumerate(names) if (port and port.lower() in n.lower()) or (not port and I.PORT_RE.search(n))]
    if len(pick) != 1:
        raise I.InstallError("other", f"MIDI out: {'no' if not pick else 'more than one'} FM-1 port among {names} "
                                      f"(--port NAME picks one)")
    mo.open_port(pick[0])
    return mo, names[pick[0]]


def play(a, say):
    mo, name = midi_out(a.port)
    say(f"playing on '{name}'")
    ch, bch, vel = a.channel - 1, a.bass_channel - 1, a.velocity
    held = set()

    def on(c, n):
        mo.send_message([0x90 | c, n, vel])
        held.add((c, n))

    def off(c, n):
        mo.send_message([0x80 | c, n, 0])
        held.discard((c, n))

    def all_off():
        for c, n in sorted(held):
            mo.send_message([0x80 | c, n, 0])
        held.clear()
        for c in {ch, bch}:
            mo.send_message([0xB0 | c, 123, 0])

    third = a.seconds / 3
    try:
        # 1. eight notes held: D3 A3 D4 F#4 A4 B4 C#5 E5, the bass D2
        say(f"  1/3 eight notes held, {third:.0f} s")
        for n in (50, 57, 62, 66, 69, 71, 73, 76):
            on(ch, n)
        on(bch, 38)
        time.sleep(third)
        all_off()
        time.sleep(0.3)
        # 2. six-note chords with common tones (D, A/C#, G, Bm colours of perf_g's voicing), changed every 400 ms
        say(f"  2/3 chord changes every 400 ms, {third:.0f} s")
        chords = [((62, 66, 69, 71, 73, 76), 38), ((61, 64, 69, 71, 73, 76), 33), ((59, 62, 66, 69, 74, 78), 35),
                  ((62, 67, 69, 71, 74, 78), 31)]
        end, k, cur = time.monotonic() + third, 0, None
        while time.monotonic() < end:
            notes, bass = chords[k % len(chords)]
            if cur:
                for n in cur[0]:
                    off(ch, n)
                off(bch, cur[1])
            for n in notes:
                on(ch, n)
            on(bch, bass)
            cur = (notes, bass)
            k += 1
            time.sleep(0.4)
        all_off()
        time.sleep(0.3)
        # 3. eight notes in turn every 60 ms, each held until its next turn (480 ms): an attack in every half or so
        say(f"  3/3 eight notes in turn every 60 ms, {third:.0f} s")
        ring = (50, 57, 62, 66, 69, 73, 76, 81)
        end, k = time.monotonic() + third, 0
        on(bch, 38)
        while time.monotonic() < end:
            n = ring[k % 8]
            if (ch, n) in held:
                off(ch, n)
            on(ch, n)
            k += 1
            time.sleep(0.06)
    finally:
        all_off()
        time.sleep(0.5)
        mo.close_port()
        del mo


def judge(text, say):
    d = {}
    for ln in text.splitlines():
        m = re.match(r"^([a-z0-9_]+) (\d+)\b", ln)
        if m:
            d[m.group(1)] = int(m.group(2))
    if "stale_halves" not in d or not d.get("pk_halves"):
        say("no PEAK counters in the answer (a ChoralRoot before 0.18, or no half counted): nothing to judge")
        return 2
    secs = d["pk_halves"] * 128 / 44100
    stage = I.LATE_STAGE[d["pk_late_stage"]] if d.get("pk_late_stage", 0) < len(I.LATE_STAGE) else "?"
    rows = [("stale_halves", d["stale_halves"], "0", d["stale_halves"] == 0,
             "halves in which the DAC played something old: the audible ones"),
            ("pk_late", d["pk_late"], "0", d["pk_late"] == 0, "halves finished after their deadline"),
            ("pk_block_us", d["pk_block_us"], f"< {BUDGET_US}", d["pk_block_us"] < BUDGET_US, "the longest half"),
            ("pk_wall_us", d.get("pk_wall_us", 0), f"< {BUDGET_US}", d.get("pk_wall_us", 0) < BUDGET_US,
             "the longest half by the clock, TIMER5 in it"),
            ("pk_part0_us", d["pk_part0_us"], f"< {WARN_US} (predicted ~{PREDICTED_US})", d["pk_part0_us"] < WARN_US,
             "the chord part's (CZ-1's) worst half"),
            ("pk_part1_us", d["pk_part1_us"], f"< {WARN_US}", d["pk_part1_us"] < WARN_US, "the bass part's worst half"),
            ("pk_fx_us", d["pk_fx_us"], f"< {WARN_US}", d["pk_fx_us"] < WARN_US, "the mix and the effects"),
            ("pk_lag_us", d.get("pk_lag_us", 0), f"< {BUDGET_US}", d.get("pk_lag_us", 0) < BUDGET_US,
             "the furthest the ISR started behind its boundary"),
            ("irq_gap_us", d.get("irq_gap_us", 0), "< 1000", d.get("irq_gap_us", 0) < 1000,
             "the longest stretch with the interrupts masked (+100)"),
            ("flash_holes", d.get("flash_holes", 0), "0", d.get("flash_holes", 0) == 0,
             "flash erases into a sounding output, since power-on")]
    say(f"counters over {d['pk_halves']} halves ({secs:.1f} s):")
    for name, v, want, ok, what in rows:
        say(f"  {'ok  ' if ok else 'OVER'} {name:13s} {v:6d}   want {want:28s} {what}")
    if d["stale_halves"]:
        say(f"VERDICT: late audio. {d['stale_halves']} halves played something old in {secs:.1f} s: that is audible, and "
            f"it is timing, not the engine's arithmetic. The latest late half's largest stage was {stage} "
            f"({d.get('pk_late_us', 0)} us)" + (": CZ-1's render." if stage == "part0" else "."))
        return 1
    if d["pk_late"] or d["pk_block_us"] >= BUDGET_US:
        say(f"VERDICT: {d['pk_late']} halves finished late (the latest: stage {stage}), but the DAC played none of them "
            f"old (stale_halves 0): not audible in this run, and close to it. pk_part0_us {d['pk_part0_us']}.")
        return 1
    if d["pk_part0_us"] >= HIGH_US:
        say(f"VERDICT: no half was late, but the chord part took {d['pk_part0_us']} us of {BUDGET_US} in its worst half "
            f"({PREDICTED_US} predicted): with the effects or a redraw on top a late half is near. Report this number.")
        return 0
    if d.get("flash_holes"):
        say("VERDICT: no half was late in this stretch. flash_holes counts erases into the sound since power-on (a "
            "save): a 45 ms hole each, neither the engine nor the render's time.")
        return 0
    say(f"VERDICT: no late audio. Every half of these {secs:.1f} s was on time (the worst {d['pk_block_us']} us of "
        f"{BUDGET_US}, the chord part {d['pk_part0_us']}): a pop heard in this stretch was computed, not late.")
    return 0


def main(argv=None, out=sys.stdout):
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("--play", action="store_true", help="select the sounds, play over MIDI, then read and judge")
    ap.add_argument("--read", action="store_true", help="only read the counters and judge (after playing by hand)")
    ap.add_argument("--seconds", type=float, default=45.0, help="how long --play plays (default 45; 6..600)")
    ap.add_argument("--channel", type=int, default=1, help="the chord part's MIDI channel, Options > MIDI Perform (1)")
    ap.add_argument("--bass-channel", type=int, default=2, help="the bass part's, Options > MIDI Bass (2)")
    ap.add_argument("--velocity", type=int, default=110)
    ap.add_argument("--engine", type=int, default=14, help="the engine --play selects (14: CZ-1)")
    ap.add_argument("--tone", type=int, default=5, help="the chord part's position in the engine's pool (5: STRINGS 2)")
    ap.add_argument("--bass-tone", type=int, default=13, help="the bass part's position (13: SYNTH.BASS)")
    ap.add_argument("--no-select", dest="select", action="store_false", help="play the unit's sounds as they are")
    ap.add_argument("--port", metavar="NAME", help="MIDI port to use (part of its name)")
    a = ap.parse_args(argv)

    def say(s):
        print(s, file=out, flush=True)

    if a.play and a.read:
        ap.error("--play reads the counters itself: not with --read")
    if not (6 <= a.seconds <= 600 and 1 <= a.channel <= 16 and 1 <= a.bass_channel <= 16 and 1 <= a.velocity <= 127):
        ap.error("--seconds 6..600, channels 1..16, --velocity 1..127")
    say(plan(a))
    rc, text = tool(["--info"], a.port)
    say(text.rstrip())
    m = re.search(r"version: (.*)$", text, re.M)
    if rc or not m or not re.match(r"ChoralRoot\b", m.group(1).strip()):
        say("REFUSED: --info does not report a ChoralRoot version: nothing sent.")
        return 2
    say(f"ChoralRoot answers: {m.group(1).strip()}")
    if not a.play and not a.read:
        say("nothing sent, no counter read (no --play, no --read)")
        return 0
    try:
        if a.play:
            if a.select:
                for part, pos in ((0, a.tone), (1, a.bass_tone)):
                    rc, text = tool(["--select", str(part), str(a.engine), str(pos)], a.port)
                    say(text.rstrip())
                    if rc:
                        say("REFUSED: the sound could not be selected: nothing played (--no-select plays the unit's own)")
                        return 2
                    time.sleep(0.5)
            rc, _ = tool(["--debug"], a.port)        # (clears PEAK)
            if rc:
                say("REFUSED: --debug failed before playing")
                return 2
            time.sleep(0.5)
            play(a, say)
        rc, text = tool(["--debug"], a.port)
        if rc:
            say(text.rstrip())
            say("the counters could not be read")
            return 2
        for ln in text.splitlines():                 # the tool's own summary lines, as --debug prints them
            if ln.startswith(("worst half", "fx stage", "the deadline", "interrupts:", "flash since")):
                say(ln)
        return judge(text, say)
    except I.InstallError as e:
        say(f"error: {e}")
        return 2
    except KeyboardInterrupt:
        say("interrupted: the notes were released")
        return 2


if __name__ == "__main__":
    sys.exit(main())

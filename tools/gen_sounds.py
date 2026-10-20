#!/usr/bin/env python3
"""Generates Tabless's sound palette into design/sounds/: done, undo and refuse
(APP-177) and the three meeting chimes (APP-178).

Clear and light, like the notification sounds of other desktop apps (dogfood
2026-10-20: the first, low and muffled set was almost inaudible). Each note is
a small bell: a pure sine with a quiet 2nd and a fainter 3rd partial that die
away first, a soft raised-cosine attack (no click) and a quick exponential
decay. Every fundamental sits between ~590 and ~1320 Hz, where the ear is
most sensitive, so a sound is heard at a low volume without being shrill.
Everything sits in A:

    done    two notes going up    E5 (659 Hz) -> A5 (880 Hz)
    undo    the same going down   A5 -> E5
    refuse  one short, lower note D5 (587 Hz), duller (no 3rd partial)

Each of those is under 150 ms. A meeting is the exception — it must not be
missed — so it gets a short melody, under a second, in the same bell timbre.
As the meeting comes closer:

    meet-chords   two chords, E5+A5 then A5+C#6        (15 min before by default)
    meet-rise     E5, A5, C#6, E6: four notes up        (10 min)
    meet-call     E6, A5, B5, E6: down and back, "hey"  (5 min)

Every file starts and ends on exact zero, so nothing clicks. Each sound is
normalized on its own (its peak lands on PEAK_DBFS times its LEVEL), so a
short tick is as present as a chord; the app scales them by the volume
setting. 16-bit PCM, mono, 44.1 kHz, deterministic: re-running the script
writes the same bytes.

    python tools/gen_sounds.py            # writes the committed files
    python tools/gen_sounds.py out_dir    # somewhere else
"""

import math
import struct
import sys
import wave
from pathlib import Path

RATE = 44100
PEAK_DBFS = -1.5
# A raised-cosine fade over the last few ms puts the tail on exact zero; the
# exponential decay is already near -70 dB there, so it changes nothing audible.
TAIL_FADE_S = 0.004

D5, E5, A5, B5, CS6, E6 = 587.33, 659.26, 880.0, 987.77, 1108.73, 1318.51

# (frequency multiple, level, how long it rings as a share of the note).
BELL = [(1.0, 1.0, 1.0), (2.0, 0.22, 0.45), (3.0, 0.06, 0.25)]
DULL = [(1.0, 1.0, 1.0), (2.0, 0.15, 0.40)]

# (start s, frequency Hz, length s, relative level, partials, attack s)
SOUNDS = {
    "done": [
        (0.000, E5, 0.080, 0.85, BELL, 0.005),
        (0.055, A5, 0.090, 1.00, BELL, 0.005),
    ],
    "undo": [
        (0.000, A5, 0.080, 1.00, BELL, 0.005),
        (0.055, E5, 0.090, 0.85, BELL, 0.005),
    ],
    "refuse": [
        (0.000, D5, 0.120, 1.00, DULL, 0.006),
    ],
}
# How loud each file is against the loudest: the ticks a little under the
# chimes, the last chime the loudest of the three.
LEVEL = {
    "done": 0.80,
    "undo": 0.70,
    "refuse": 0.70,
    "meet-chords": 0.85,
    "meet-rise": 0.92,
    "meet-call": 1.00,
}

CHIME_ATTACK = 0.006
# (start s, frequency Hz, length s) per bell note.
CHIMES = {
    "meet-chords": [(0.00, E5, 0.60), (0.00, A5, 0.60), (0.24, A5, 0.66), (0.24, CS6, 0.66)],
    "meet-rise": [(0.00, E5, 0.30), (0.13, A5, 0.30), (0.26, CS6, 0.30), (0.39, E6, 0.50)],
    "meet-call": [(0.00, E6, 0.26), (0.15, A5, 0.26), (0.30, B5, 0.26), (0.45, E6, 0.48)],
}


def decay(peak, t, start, length):
    """Exponential from `peak` at `start` down to 3e-4 at `length`."""
    floor = 3e-4
    return peak * (floor / peak) ** (max(0.0, t - start) / (length - start))


def bell(freq, length, level, partials, attack):
    n = int(round(RATE * length))
    out = []
    for i in range(n):
        t = i / RATE
        s = 0.0
        for mul, amp, ring in partials:
            ring_s = length * ring
            if t < ring_s:
                s += decay(amp, t, 0.0, ring_s) * math.sin(2 * math.pi * freq * mul * t)
        # Raised-cosine attack: starts on zero with zero slope, so no click.
        if t < attack:
            env = level * (0.5 - 0.5 * math.cos(math.pi * t / attack))
        else:
            env = decay(level, t, attack, length)
        remaining = length - t
        if remaining < TAIL_FADE_S:
            env *= 0.5 - 0.5 * math.cos(math.pi * max(0.0, remaining) / TAIL_FADE_S)
        out.append(s * env)
    return out


def mix(notes):
    end = max(start + length for start, _, length, _, _, _ in notes)
    buf = [0.0] * int(round(RATE * end))
    for start, freq, length, level, partials, attack in notes:
        offset = int(round(RATE * start))
        for i, v in enumerate(bell(freq, length, level, partials, attack)):
            if offset + i < len(buf):
                buf[offset + i] += v
    return buf


def main():
    root = Path(__file__).resolve().parent.parent
    out_dir = Path(sys.argv[1]) if len(sys.argv) > 1 else root / "design" / "sounds"
    out_dir.mkdir(parents=True, exist_ok=True)
    rendered = {name: mix(spec) for name, spec in SOUNDS.items()}
    rendered.update(
        {name: mix([(s, f, ln, 1.0, BELL, CHIME_ATTACK) for s, f, ln in notes]) for name, notes in CHIMES.items()})
    for name, buf in rendered.items():
        peak = max(abs(v) for v in buf) or 1.0
        gain = (10 ** (PEAK_DBFS / 20)) * LEVEL[name] / peak
        samples = [max(-32767, min(32767, int(round(v * gain * 32767)))) for v in buf]
        # The tail fade lands on zero; rounding keeps it there.
        samples[0] = 0
        samples[-1] = 0
        out = out_dir / f"{name}.wav"
        with wave.open(str(out), "wb") as w:
            w.setnchannels(1)
            w.setsampwidth(2)
            w.setframerate(RATE)
            w.writeframes(struct.pack("<%dh" % len(samples), *samples))
        print(f"{out}: {out.stat().st_size} bytes, {len(samples)} samples, {len(samples) / RATE * 1000:.0f} ms")


if __name__ == "__main__":
    main()

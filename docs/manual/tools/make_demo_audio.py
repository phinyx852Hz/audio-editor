#!/usr/bin/env python3
"""Writes the demo song used for the manual's screenshots: 32 bars at 120 BPM, stereo, 48 kHz, 24-bit.

    python3 make_demo_audio.py OUT_DIR

Makes demo-song.wav (drums, bass, chords and a lead in A minor, with a quiet intro and a
louder chorus so the meters and waveform have something to show), demo-voice.wav (a sung-like
vowel line with a click and a hum to repair) and demo-loop.wav (a two-bar break).
Everything is synthesised here, so the screenshots carry no one else's audio.
"""
import sys
import wave
from pathlib import Path

import numpy as np

RATE = 48000
BPM = 120.0
BEAT = 60.0 / BPM
rng = np.random.default_rng(1618)


def env(n, attack, release):
    a = max(1, int(attack * RATE))
    r = max(1, int(release * RATE))
    e = np.ones(n)
    e[:a] = np.linspace(0, 1, a)
    if r < n:
        e[-r:] *= np.exp(-np.linspace(0, 6, r))
    return e


def note_hz(midi):
    return 440.0 * 2 ** ((midi - 69) / 12.0)


def saw(freq, n, detune=0.0):
    t = np.arange(n) / RATE
    out = np.zeros(n)
    for k in range(1, 24):
        if freq * k > RATE / 2.2:
            break
        out += np.sin(2 * np.pi * freq * (1 + detune) * k * t) / k
    return out * 0.6


def add(buf, start, sig, pan=0.0, gain=1.0):
    i = int(start * RATE)
    j = min(len(buf), i + len(sig))
    if j <= i:
        return
    left = np.cos((pan + 1) * np.pi / 4)
    right = np.sin((pan + 1) * np.pi / 4)
    buf[i:j, 0] += sig[: j - i] * left * gain
    buf[i:j, 1] += sig[: j - i] * right * gain


def kick(n=int(0.35 * RATE)):
    t = np.arange(n) / RATE
    f = 45 + 110 * np.exp(-t * 30)
    return np.sin(2 * np.pi * np.cumsum(f) / RATE) * np.exp(-t * 9)


def snare(n=int(0.25 * RATE)):
    t = np.arange(n) / RATE
    return (rng.standard_normal(n) * 0.6 + np.sin(2 * np.pi * 190 * t) * 0.5) * np.exp(-t * 18)


def hat(n=int(0.06 * RATE)):
    t = np.arange(n) / RATE
    x = rng.standard_normal(n)
    x = np.diff(np.concatenate([[0], x]))
    return x * np.exp(-t * 70) * 0.5


def song(bars=32, parts=("chords", "bass", "drums", "lead"), normalise=True):
    total = bars * 4 * BEAT + 2.0
    buf = np.zeros((int(total * RATE), 2))
    chords = [[57, 60, 64], [53, 57, 60], [48, 52, 55], [55, 59, 62]]   # Am F C G
    bass = [45, 41, 36, 43]
    lead = [76, 74, 72, 71, 72, 74, 76, 79, 77, 76, 74, 72, 71, 72, 69, 69]
    for bar in range(bars):
        t0 = bar * 4 * BEAT
        section = 0 if bar < 4 else (1 if bar < 12 else (2 if bar < 20 else (1 if bar < 28 else 3)))
        level = [0.25, 0.6, 1.0, 0.6][section]
        c = chords[bar % 4]
        for m in (c if "chords" in parts else []):
            n = int(4 * BEAT * RATE)
            sig = (saw(note_hz(m), n, 0.003) + saw(note_hz(m), n, -0.004)) * env(n, 0.3, 1.2)
            add(buf, t0, sig, pan=-0.4 if m == c[0] else 0.4, gain=0.07 * level + 0.03)
        if section >= 1:
            for b in (range(8) if "bass" in parts else []):
                n = int(0.45 * BEAT * RATE)
                sig = np.sin(2 * np.pi * note_hz(bass[bar % 4]) * np.arange(n) / RATE) * env(n, 0.005, 0.2)
                sig += 0.3 * np.sin(2 * np.pi * 2 * note_hz(bass[bar % 4]) * np.arange(n) / RATE) * env(n, 0.005, 0.1)
                add(buf, t0 + b * 0.5 * BEAT, sig, gain=0.35 * level)
            for b in (range(4) if "drums" in parts else []):
                add(buf, t0 + b * BEAT, kick(), gain=0.8 * level)
                if b % 2:
                    add(buf, t0 + b * BEAT, snare(), pan=0.05, gain=0.45 * level)
            for h in (range(8) if "drums" in parts else []):
                add(buf, t0 + h * 0.5 * BEAT, hat(), pan=0.5, gain=0.35 * level)
        if section == 2 and "lead" in parts:
            for k in range(4):
                m = lead[(bar * 4 + k) % len(lead)]
                n = int(0.95 * BEAT * RATE)
                t = np.arange(n) / RATE
                vib = 1 + 0.004 * np.sin(2 * np.pi * 5.5 * t)
                sig = np.sin(2 * np.pi * np.cumsum(note_hz(m) * vib) / RATE)
                sig += 0.3 * np.sin(4 * np.pi * np.cumsum(note_hz(m) * vib) / RATE)
                add(buf, t0 + k * BEAT, sig * env(n, 0.02, 0.3), pan=-0.2, gain=0.18)
    if not normalise:
        return buf
    peak = np.max(np.abs(buf))
    return buf / peak * 0.89


def voice(seconds=12.0):
    n = int(seconds * RATE)
    t = np.arange(n) / RATE
    notes = [57, 60, 62, 64, 62, 60, 59, 57]
    f = np.zeros(n)
    for i, m in enumerate(notes):
        a, b = int(i * n / len(notes)), int((i + 1) * n / len(notes))
        f[a:b] = note_hz(m)
    f = np.convolve(f, np.ones(2400) / 2400, mode="same")
    f *= 1 + 0.006 * np.sin(2 * np.pi * 5.2 * t)
    ph = 2 * np.pi * np.cumsum(f) / RATE
    sig = np.zeros(n)
    for k in range(1, 30):
        formant = np.exp(-((k * f - 700) / 300) ** 2) + 0.6 * np.exp(-((k * f - 1200) / 250) ** 2) + 0.3 * np.exp(-((k * f - 2600) / 400) ** 2)
        sig += np.sin(k * ph) * formant / k ** 0.3
    sig *= 0.5 * (0.6 + 0.4 * np.sin(2 * np.pi * 0.25 * t) ** 2)
    sig += 0.02 * np.sin(2 * np.pi * 60 * t) + 0.01 * np.sin(2 * np.pi * 180 * t)   # hum to remove
    sig += 0.004 * rng.standard_normal(n)                                        # hiss
    for c in (2.3, 5.1, 8.7):                                                    # clicks to heal
        i = int(c * RATE)
        sig[i:i + 30] += np.hanning(30) * 0.9
    sig /= np.max(np.abs(sig)) / 0.8
    return np.stack([sig, sig], axis=1)


def loop():
    bars = 2
    n = int(bars * 4 * BEAT * RATE)
    buf = np.zeros((n, 2))
    pattern = "K.h.S.hKK.h.S.h.K.hKS.h.K.h.S.hS"
    step = BEAT / 4
    for i, ch in enumerate(pattern):
        t = i * step
        if ch == "K":
            add(buf, t, kick(), gain=0.9)
        elif ch == "S":
            add(buf, t, snare(), gain=0.6)
        add(buf, t, hat(), pan=0.4, gain=0.25 if ch == "." else 0.4)
    return buf / np.max(np.abs(buf)) * 0.9


def write(path, data):
    data = np.clip(data, -1, 1)
    ints = (data * (2 ** 23 - 1)).astype(np.int32)
    raw = bytearray()
    for frame in ints:
        for s in frame:
            raw += int(s).to_bytes(4, "little", signed=True)[:3]
    with wave.open(str(path), "wb") as w:
        w.setnchannels(data.shape[1])
        w.setsampwidth(3)
        w.setframerate(RATE)
        w.writeframes(bytes(raw))


def main():
    out = Path(sys.argv[1] if len(sys.argv) > 1 else ".")
    out.mkdir(parents=True, exist_ok=True)
    write(out / "demo-song.wav", song())
    write(out / "demo-voice.wav", voice())
    write(out / "demo-loop.wav", loop())
    # Stems for the mixer, on one common scale, and a 7.1 file built from them.
    full = song(normalise=False)
    scale = 0.89 / np.max(np.abs(full))
    stems = {name: song(parts=(name,), normalise=False) * scale for name in ("chords", "bass", "drums", "lead")}
    for name, data in stems.items():
        write(out / f"stem-{name}.wav", data)
    # 7.1 in WAV order: L R C LFE Ls Rs Lb Rb
    n = len(full)
    s71 = np.zeros((n, 8))
    s71[:, 0], s71[:, 1] = stems["chords"][:, 0], stems["chords"][:, 1]
    s71[:, 2] = stems["lead"].mean(axis=1)
    s71[:, 3] = stems["bass"].mean(axis=1) * 0.7
    s71[:, 4], s71[:, 5] = stems["drums"][:, 0], stems["drums"][:, 1]
    s71[:, 6], s71[:, 7] = stems["chords"][:, 1] * 0.5, stems["chords"][:, 0] * 0.5
    write(out / "demo-7.1.wav", s71)
    print("written to", out)


if __name__ == "__main__":
    main()

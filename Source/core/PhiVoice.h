// SPDX-License-Identifier: AGPL-3.0-only
#pragma once

// The sound PHI plays as its splash opens: a small phase distortion voice (in the
// manner of Casio's CZ synths) with a new patch every start, through a granular
// reverb with a long tail. A stand-in for the Φ synth until it is bundled. Plain
// C++, so the core tests can check it. No allocation after prepare().
#include <array>
#include <cstdint>
#include <vector>

namespace phi
{
    // A point in the patch space, every value from 0 to 1. Patch n of the sequence is
    // frac (seed + n α), with α = 1/g, 1/g², ... for g the root of x^(d+1) = x + 1:
    // the golden sequence (g = φ when d = 1) spread over d dimensions at once. Each
    // start lands in the biggest corner of the space the earlier starts left, so the
    // sounds keep changing without clumping or repeating.
    struct PdPatch
    {
        static constexpr int dimensions = 16;
        std::array<double, dimensions> values {};

        static PdPatch at (std::int64_t n, double seed);
    };

    // Phase distortion: a cosine read with its phase bent, so a sine turns into a
    // saw, a square, a pulse or a resonant sweep as the bend (the DCW envelope)
    // opens and closes. Two lines, each with its own wave, the second tuned to a
    // golden or harmonic ratio of the first.
    enum class PdWave { saw, square, pulse, resonantSaw, resonantTriangle, count };

    // One phase-distorted cycle at phase 0..1 with distortion 0..1 (0 is a sine).
    // `minWidth` keeps the steepest part of the bend at least that much of a cycle wide.
    double pdWave (PdWave, double phase, double distortion, double minWidth = 0.002);

    // The granular reverb: the input and its echoes are written into a 4 second
    // buffer, and up to 48 Hann-windowed grains, 70 to 190 ms long, read it back from
    // up to 0.9 s ago at slightly drifting pitches, panned across the stereo field.
    // Part of their sum feeds back into the buffer, so the cloud thins out slowly:
    // `tail` is about how long it takes to fall 60 dB.
    class GranularReverb
    {
    public:
        void prepare (double sampleRate);
        void reset();
        void setTail (double seconds);   // 0.5 to 8; default 2
        void setMix (double wet);        // 0 to 1; default 0.5
        // Adds the reverb to left and right in place.
        void process (float* left, float* right, int frames);

    private:
        struct Grain
        {
            bool active = false;
            double position = 0.0, step = 1.0;   // where it reads in the buffer, and how fast
            int length = 0, age = 0;
            float gainLeft = 0.0f, gainRight = 0.0f;
        };
        float random();   // 0..1
        void spawn();

        double rate = 48000.0, tail = 2.0, wet = 0.5;
        float feedback = 0.4f, damping = 0.0f, lowLeft = 0.0f, lowRight = 0.0f;
        std::vector<float> bufferLeft, bufferRight;
        int write = 0, size = 0;
        std::array<Grain, 48> grains {};
        std::vector<float> window;
        int untilNext = 0, spacing = 1;
        std::uint32_t seed = 1618033989u;
    };

    // The whole start-up sound: a chord of one to three notes on the PD voice, held
    // for 1 to φ seconds, through the reverb. Renders about 4.5 s and then stays silent.
    class StartupSound
    {
    public:
        // Patch `launch` of the sequence; `seed` (0..1) makes one install's sequence its own.
        StartupSound (std::int64_t launch, double seed);

        void prepare (double sampleRate);
        // Audio thread: adds to left and right; false once the sound has ended.
        bool render (float* left, float* right, int frames);

        double lengthSeconds() const { return holdSeconds + releaseSeconds + reverbSeconds; }
        const PdPatch& patch() const { return settings; }
        int numNotes() const { return notes; }
        double noteHz (int i) const { return voice[(size_t) i].hz; }

    private:
        struct Envelope
        {
            double attack = 0.01, decay = 0.5, sustain = 0.6, release = 0.5;   // seconds; sustain 0..1
            double level = 0.0;
            int stage = 0;   // 0 attack, 1 decay, 2 sustain, 3 release, 4 done
            double next (double rate, bool held);
        };
        struct Line
        {
            PdWave wave = PdWave::saw;
            double ratio = 1.0, level = 1.0, depth = 1.0;   // depth: how far the DCW opens
        };
        struct Voice
        {
            double hz = 220.0, pan = 0.0;
            std::array<double, 2> phase {};
            Envelope amp, dcw;
        };

        PdPatch settings;
        std::array<Line, 2> line {};
        bool ring = false;
        int notes = 1;
        std::array<Voice, 3> voice {};
        double holdSeconds = 1.2, releaseSeconds = 0.8, reverbSeconds = 2.5;
        double vibratoHz = 5.0, vibratoDepth = 0.0, vibratoPhase = 0.0;
        double pitchDrop = 0.0, gain = 0.25;
        double dcCoefficient = 0.001, dcLeft = 0.0, dcRight = 0.0;
        double rate = 48000.0;
        std::int64_t frame = 0, total = 0, fade = 1;
        GranularReverb reverb;
        std::vector<float> scratchLeft, scratchRight;
    };
}

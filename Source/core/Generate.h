// SPDX-License-Identifier: AGPL-3.0-only
#pragma once

#include <cstdint>
#include <vector>

// Test and sound-design signals: the four classic oscillator waves (with an
// optional sweep) and coloured noise. Plain C++.
namespace gen
{
    enum class Wave { sine, square, sawtooth, triangle };

    struct Tone
    {
        Wave wave = Wave::sine;
        double startHz = 440.0;
        double endHz = 440.0;        // differs from startHz: a sweep, gliding evenly in pitch (exponentially)
        double peakDb = -6.0;        // the wave's peak level in dBFS
        bool bandLimited = true;     // no harmonics above half the sample rate (off: the raw wave, which aliases)
        double pulseWidth = 0.5;     // square only: the part of each cycle that is high
    };

    // `length` samples of the tone at `sampleRate`.
    std::vector<float> tone (const Tone&, double sampleRate, std::int64_t length);

    // White: equal energy per hertz. Pink: -3 dB per octave (equal per octave).
    // Brown (red): -6 dB per octave. Blue: +3 dB. Violet: +6 dB.
    enum class Noise { white, pink, brown, blue, violet };

    // `length` samples with the given peak level, from `seed` (the same seed gives the same noise).
    std::vector<float> noise (Noise, double peakDb, double sampleRate, std::int64_t length, std::uint32_t seed);
}

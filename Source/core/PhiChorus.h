// SPDX-License-Identifier: AGPL-3.0-only
#pragma once

// Φ Chorus: 2, 3, 5 or 8 voices (Fibonacci numbers), each a copy of the sound
// on a short delay swept by its own LFO. The voices' rates are a golden step
// (√φ) apart around Rate, so they never line up again; their LFOs start a
// golden angle (137.5°) apart, and the voices are spread across the stereo
// field by the golden angle too, so no two sit in the same place. Each voice's
// delay is a little longer than the one before, by φ^(1/n). The defaults are
// golden as well: 0.618 Hz, 2.618 ms (φ²) deep, 13 ms delay, 38.2 % mix.
// Plain C++ (no JUCE), no allocation after prepare().
#include <array>
#include <atomic>
#include <vector>

namespace phi
{
    class Chorus
    {
    public:
        static constexpr int maxVoices = 8;
        static constexpr std::array<int, 4> voiceCounts { 2, 3, 5, 8 };
        static constexpr double maxDelayMs = 34.0 * 1.25 + 10.0;   // the longest delay (34 ms × φ^(3.5/8)) plus the deepest sweep

        struct Settings
        {
            double rateHz = 0.618;
            double depthMs = 2.618;
            double delayMs = 13.0;
            int voices = 5;
            double width = 1.0;   // 0 = all voices centred, 1 = golden-angle spread
            double mix = 0.382;
        };

        // Voice v of n: its LFO rate, its delay at rest and its place left (-1) to right (1).
        static double voiceRate (const Settings&, int voice);
        static double voiceDelayMs (const Settings&, int voice);
        static double voicePan (const Settings&, int voice);

        void prepare (double sampleRate, int numChannels);
        void set (const Settings&);
        void reset();
        void process (float* const* channels, int numChannels, int numFrames);

        // For the window, from any thread: each voice's delay now, in ms.
        double delayNowMs (int voice) const { return shownDelay[(size_t) voice].load(); }

    private:
        double rate = 48000.0;
        Settings settings;
        std::vector<float> line;   // the input (mono), a ring
        int writePos = 0;
        std::array<double, maxVoices> phases {};
        std::array<std::atomic<double>, maxVoices> shownDelay {};
    };
}

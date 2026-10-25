// SPDX-License-Identifier: AGPL-3.0-only
#pragma once

// Φ Phaser: a chain of first-order allpass stages, a Fibonacci number of them
// (2, 3, 5, 8 or 13), tuned φ apart around a centre instead of all together, so
// the notches they cut when mixed with the dry sound are spread unevenly, like
// the Φ EQ's bands. Two LFOs at rate and rate × φ sweep them together, a motion
// that never repeats, and the right side runs a golden angle (137.5°) behind
// the left. Plain C++ like the other Φ cores (Phi.h); no allocation after prepare().
#include <array>
#include <vector>

namespace phi
{
    class Phaser
    {
    public:
        static constexpr int numStageChoices = 5;
        static constexpr std::array<int, numStageChoices> stageChoices { 2, 3, 5, 8, 13 };
        static constexpr int maxStages = 13;
        static constexpr double sweepOctaves = 3.0;   // at full depth the stages move ±3 octaves

        struct Settings
        {
            int stages = 5;             // one of stageChoices
            double rateHz = 0.382;      // the slower LFO; the other runs φ times faster
            double depth = 0.618;       // 0..1 of sweepOctaves
            double centreHz = 618.0;    // where the middle stage sits with the LFOs at rest
            double spread = 1.0;        // 0: every stage at the centre (a classic phaser); 1: φ apart
            double feedback = 0.382;    // -0.9..0.9
            double stereo = 1.0;        // 0: both sides together; 1: the right a golden angle behind
            double mix = 0.5;           // 0..1; 0.5 cuts the deepest notches
        };

        // Where stage `k` of `stages` sits for an LFO value from -1 to 1.
        static double stageHz (const Settings&, int k, double lfo);
        // The two LFOs together at `seconds` (and `phase`, in turns), from -1 to 1.
        static double lfo (double seconds, double rateHz, double phase);
        // The response at `hz` with the LFOs at rest, in dB (for tests and drawing).
        static double responseDb (const Settings&, double hz, double sampleRate);

        void prepare (double sampleRate, int numChannels);
        void set (const Settings&);
        void reset();
        void process (float* const* channels, int numChannels, int numFrames);

    private:
        struct Channel
        {
            std::array<float, maxStages> x1 {}, y1 {}, a {};
            float last = 0.0f;   // the chain's output, fed back
        };
        void update (int channel, double lfoValue);
        static double lfoAt (double slowTurns, double fastTurns, double phase);

        double rate = 48000.0, slowTurns = 0.0, fastTurns = 0.0;
        Settings settings;
        std::vector<Channel> state;
        int sinceUpdate = 0;
        float smoothedMix = 0.5f, mixStep = 0.001f;
    };
}

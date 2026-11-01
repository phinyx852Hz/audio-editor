// SPDX-License-Identifier: AGPL-3.0-only
#pragma once

// Φ Repeat: a tempo-synced beat repeat. At every Interval (moved by Offset) it
// may, with some Chance, catch a slice of the sound one Grid long and play it
// over and over for the length of the Gate, each repetition lower in pitch and
// quieter if asked. Variation changes the grid at random as it goes; a band
// filter thins the repeats; and the dry sound is kept under them (Mix), cut
// while they play (Insert) or never heard (Gate). Held Repeat repeats now.
//
// It is PHI's own code, after the controls of the classic beat-repeat effects;
// Golden chance swaps the dice for the Fibonacci word (as Φ Rhythm plays it),
// so the repeats fall in a pattern that never repeats yet never clumps.
// Plain C++ like the other Φ cores (Phi.h); no allocation after prepare().
#include <array>
#include <cstdint>
#include <vector>

namespace phi
{
    class Repeat
    {
    public:
        static constexpr int numIntervals = 8;   // 1/32 to 4 bars
        static constexpr std::array<double, numIntervals> intervalBeats { 0.125, 0.25, 0.5, 1.0, 2.0, 4.0, 8.0, 16.0 };
        static constexpr int numGrids = 7;       // 1/2 to 1/128
        static constexpr std::array<double, numGrids> gridBeats { 2.0, 1.0, 0.5, 0.25, 0.125, 0.0625, 0.03125 };
        static constexpr double maxSliceSeconds = 6.0;   // a 1/2 note at 20 BPM
        static constexpr double minPitch = -48.0;

        enum class Variation { trigger, quarter, eighth, sixteenth, everyRepeat };
        enum class Mix { mix, insert, gate };

        struct Settings
        {
            double bpm = 120.0;
            int interval = 5;            // index of intervalBeats: 1 bar
            double offsetSixteenths = 8; // 0..15, where in the interval it may start
            int grid = 3;                // index of gridBeats: 1/16
            bool triplets = false;       // the grid × 2/3
            double gateSixteenths = 8;   // 1..64: how long the repeating lasts
            double chance = 1.0;         // 0..1
            bool golden = false;         // the Fibonacci word decides instead of the dice
            double variation = 0.0;      // 0..1: how far the grid may wander (up to four halvings)
            Variation variationMode = Variation::trigger;
            bool repeat = false;         // held: repeat from now on, whatever the chance
            double pitch = 0.0;          // semitones, -48..0, by playing the slice slower
            double pitchDecay = 0.0;     // 0..1: up to 3 semitones lower each repetition
            double decay = 0.0;          // 0..1: the repeats fade to this much less by the gate's end
            bool filter = false;
            double filterHz = 1000.0;
            double filterOctaves = 2.0;  // the band's width
            Mix mix = Mix::mix;
            double volumeDb = 0.0;       // the repeats' level
        };

        // A grid in beats, after the triplet switch.
        static double gridLength (int grid, bool triplets);

        void prepare (double sampleRate, int numChannels);
        void set (const Settings&);
        void reset();   // back to beat 0, nothing repeating
        // When the host knows where it is (in beats), the repeats follow it.
        void setPosition (double beats);
        void process (float* const* channels, int numChannels, int numFrames);

        // For the display: the sixteenth playing, and whether it is repeating.
        std::int64_t currentSixteenth() const { return sixteenth; }
        bool repeating() const { return active; }
        double currentGrid() const { return repLength; }   // in beats, while repeating

    private:
        void start();
        void nextRepetition();
        double pickGrid();
        double random01();
        void updateFilter();
        float slice (int channel, double offset) const;

        double rate = 48000.0;
        Settings settings;
        int channels = 2;

        // The slice, caught as it is heard (`write` frames of it so far).
        std::vector<float> ring;
        int ringFrames = 0, write = 0;

        double beats = 0.0, lastIntervalBeats = -1.0, lastOffset = -1.0;
        std::int64_t sixteenth = 0, lastInterval = -1, lastVariationStep = -1, triggers = 0;

        bool active = false;
        double gateEnd = 0.0;             // in beats
        double gateStart = 0.0;
        double repStart = 0.0, repLength = 0.25;   // the repetition playing, in beats
        int repetition = 0;
        double readSpeed = 1.0;           // samples of the slice per sample out
        double readPosition = 0.0;        // from the slice's start, in samples

        float wetGain = 0.0f;             // fades the repeats in and out
        float dryGain = 1.0f;
        float fadeStep = 0.01f;
        int edgeSamples = 64;             // fades at each repetition's start and end

        struct Band { double b0 = 1, b1 = 0, b2 = 0, a1 = 0, a2 = 0; };
        struct BandState { double z1 = 0, z2 = 0; };
        Band highPass, lowPass;
        std::vector<BandState> hpState, lpState;

        std::uint64_t seed = 0x9E3779B97F4A7C15ull;
    };
}

// SPDX-License-Identifier: AGPL-3.0-only
#pragma once

// The signal side of a Prism (a device that holds several rays of plugins in
// the Device Chain): how one input is divided among the rays, and how the rays
// come back together. Plain C++, so the core tests can check that a Prism with
// nothing in it gives back exactly what went in.
#include <array>
#include <vector>

namespace prism
{
    enum class Split
    {
        parallel,    // every ray hears the whole signal; the rays are added together
        bands,       // ray 1 is the lowest band, ray 2 the next... (Linkwitz-Riley crossovers)
        midSide,     // ray 1 hears the mid, ray 2 the side
        leftRight    // ray 1 hears the left, ray 2 the right
    };

    inline constexpr int numSplits = 4;
    inline constexpr int maxRays = 8;
    inline constexpr int maxBands = 4;
    inline constexpr int maxChannels = 16;

    const char* splitName (Split);
    // How many of `numRays` rays a split feeds; the rest hear silence and are left out.
    int raysHeard (Split, int numRays);

    // One Linkwitz-Riley crossover network for up to four bands. The bands add back to an
    // allpass of the input (the same level at every frequency), whatever the crossovers are.
    class Crossover
    {
    public:
        void prepare (double sampleRate, int numChannels);
        // `crossoverHz` holds numBands - 1 frequencies, in any order (they are sorted).
        void set (int numBands, const double* crossoverHz);
        int bands() const { return numBands; }
        const double* frequencies() const { return hz.data(); }
        void reset();
        // Fills `out[band][channel]` with each band of `in`.
        void process (const float* const* in, int numChannels, int numFrames, float* const* const* out);

    private:
        struct Biquad
        {
            double b0 = 1, b1 = 0, b2 = 0, a1 = 0, a2 = 0, z1 = 0, z2 = 0;
            double run (double x)
            {
                const auto y = b0 * x + z1;
                z1 = b1 * x - a1 * y + z2;
                z2 = b2 * x - a2 * y;
                return y;
            }
        };
        // Per channel: two lowpasses and two highpasses at each crossover, and the allpasses
        // each band goes through for the crossovers above it, so all bands stay in phase.
        struct Stage
        {
            std::array<std::array<Biquad, 2>, maxBands - 1> low, high;
            std::array<std::array<Biquad, maxBands - 1>, maxBands - 1> allpass;   // [band][crossover]
        };
        void design();

        double rate = 48000.0;
        int channels = 2, numBands = 1;
        std::array<double, maxBands - 1> hz { 200.0, 2000.0, 8000.0 };
        std::array<double, maxBands - 1> designed { 0.0, 0.0, 0.0 };
        std::vector<Stage> stages;   // per channel
    };

    // Divides a block among the rays and joins them again.
    class Splitter
    {
    public:
        void prepare (double sampleRate, int numChannels);
        void setBands (int numBands, const double* crossoverHz) { crossover.set (numBands, crossoverHz); }
        void reset() { crossover.reset(); }

        // `rays[ray][channel]` are filled with what each ray hears.
        void split (Split, const float* const* in, int numChannels, int numFrames, float* const* const* rays, int numRays);
        // Adds the rays back into `out`, each at its gain (0 leaves it out).
        static void join (Split, const float* const* const* rays, const float* gains, int numRays,
                          float* const* out, int numChannels, int numFrames);

    private:
        Crossover crossover;
    };

    // A delay of whole samples, so rays whose plugins have less latency line up with the slowest one.
    class Delay
    {
    public:
        void prepare (int numChannels, int maxSamples);
        void reset();
        void process (float* const* io, int numChannels, int numFrames, int delaySamples);
        int maximum() const { return length; }

    private:
        std::vector<std::vector<float>> ring;
        int length = 0, write = 0;
    };
}

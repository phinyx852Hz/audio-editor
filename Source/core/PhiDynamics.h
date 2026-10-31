// SPDX-License-Identifier: AGPL-3.0-only
#pragma once

// The Φ dynamics: Φ Compressor, Φ Multiband and Φ Limiter, the signal side of the
// Φ effects that control level (engine/PhiEffects.cpp). Plain C++ like core/Phi.h,
// no allocation after prepare().
//
// What makes them golden:
// - Ratios are powers of φ: φ^(n/2), 1.27, 1.62 (φ), 2.06, 2.62 (φ²), 3.33, 4.24 (φ³)...
// - The release has two stages, a fast one at release ÷ φ and a slow one at
//   release × φ, mixed 1/φ and 1/φ² (which add up to exactly 1). Short peaks let go
//   quickly; long passages recover slowly, without pumping.
// - The defaults are Fibonacci times: 13 ms attack and 233 ms release, which are
//   φ⁶ apart (13 × φ⁶ = 233.2).
// - Φ Multiband splits at 80 Hz × φⁿ (209 Hz, 887 Hz and 3.76 kHz to start, φ³
//   apart), and each band's times are a golden step slower below and faster above.
#include <array>
#include <atomic>
#include <vector>

namespace phi
{
    // A ratio from its golden step: φ^(step / 2), step 1 to maxRatioStep.
    inline constexpr int maxRatioStep = 12;   // φ⁶, 17.9:1, as good as limiting
    double ratioOf (int step);

    // Gain reduction smoothed with an attack and the golden two-stage release, in dB
    // of reduction (0 or more).
    class GoldenRelease
    {
    public:
        void set (double sampleRate, double attackMs, double releaseMs);
        void reset() { fast = slow = 0.0; }
        double next (double reductionDb);
        double current() const { return fast * inverseRatio + slow * (1.0 - inverseRatio); }

    private:
        static constexpr double inverseRatio = 0.6180339887498949;
        double attackCoef = 1.0, fastCoef = 1.0, slowCoef = 1.0, fast = 0.0, slow = 0.0;
    };

    // The last few seconds of gain reduction, for a window's picture, written from the
    // audio thread and read from any other: historySize points, each the deepest
    // reduction over its stretch of time.
    class ReductionHistory
    {
    public:
        static constexpr int historySize = 233;     // a Fibonacci number of points
        static constexpr double seconds = 3.0;

        void prepare (double sampleRate);
        void push (double reductionDb);
        // Oldest first, in dB of reduction (0 or more).
        void read (float* out) const;

    private:
        std::array<std::atomic<float>, historySize> points {};
        std::atomic<int> newest { 0 };
        int samplesPerPoint = 618, count = 0;
        float deepest = 0.0f;
    };

    // The true peak of a sample stream, 4x oversampled as ITU-R BS.1770 measures it
    // (a 96-tap Hann-windowed sinc; the same as dsp::TruePeakDetector, kept here so the
    // Φ cores need nothing outside the standard library). Late by 12 samples.
    class TruePeak
    {
    public:
        static constexpr int delay = 12;
        TruePeak();
        float process (float x);   // the largest |value| of the 4 interpolated points
        void reset();

    private:
        static constexpr int tapsPerPhase = 24;
        std::array<std::array<float, tapsPerPhase>, 4> phases {};
        std::array<float, tapsPerPhase> history {};
        int writeIndex = 0;
    };

    //==========================================================================
    // Φ Compressor: a feed-forward compressor, all channels linked, with a soft knee.
    class Compressor
    {
    public:
        struct Settings
        {
            double thresholdDb = -18.0;
            int ratioStep = 2;            // φ: 1.618:1
            double kneeDb = 6.854;        // φ⁴ dB
            double attackMs = 13.0;
            double releaseMs = 233.0;
            double makeupDb = 0.0;
            double mix = 1.0;             // 0..1: the share compressed, the rest dry (parallel)
        };

        // The level out for a level in, both in dB, as the settings shape it (the picture).
        static double curveDb (const Settings&, double inDb);

        void prepare (double sampleRate, int numChannels);
        void set (const Settings&);
        void reset();
        void process (float* const* channels, int numChannels, int numFrames);
        double reductionDb() const { return shownReduction.load(); }   // now, 0 or more
        const ReductionHistory& history() const { return past; }

    private:
        double rate = 48000.0;
        Settings settings;
        GoldenRelease envelope;
        ReductionHistory past;
        std::atomic<double> shownReduction { 0.0 };
    };

    //==========================================================================
    // Φ Multiband: four bands split by Linkwitz-Riley crossovers (24 dB/oct, the bands
    // sum back flat; below each crossover, all-passes keep the bands in phase), each
    // compressed on its own, all channels linked.
    class Multiband
    {
    public:
        static constexpr int numBands = 4;
        static constexpr int minCrossoverStep = -3, maxCrossoverStep = 11;   // 18.9 Hz to 15.9 kHz
        static double crossoverHz (int step);   // 80 Hz × φ^step

        struct Band
        {
            double thresholdDb = -18.0;
            double gainDb = 0.0;
            bool operator== (const Band&) const = default;
        };
        struct Settings
        {
            std::array<Band, numBands> bands {};
            std::array<int, numBands - 1> crossoverSteps { 2, 5, 8 };   // 209 Hz, 887 Hz, 3.76 kHz
            int ratioStep = 2;
            double kneeDb = 6.854;
            double attackMs = 13.0;     // the middle of the bands; a golden step slower per band below
            double releaseMs = 233.0;
            double mix = 1.0;
            bool operator== (const Settings&) const = default;
        };

        // The crossovers in order, in Hz.
        static std::array<double, numBands - 1> crossovers (const Settings&);
        // A band's attack and release, scaled by φ^(1.5 - band): ×2.06 for the lowest, ÷2.06 for the highest.
        static double timeScale (int band);

        void prepare (double sampleRate, int numChannels);
        void set (const Settings&);
        void reset();
        void process (float* const* channels, int numChannels, int numFrames);
        double reductionDb (int band) const { return shownReduction[(size_t) band].load(); }

    private:
        struct Coefs { double b0 = 1, b1 = 0, b2 = 0, a1 = 0, a2 = 0; };
        struct State
        {
            double z1 = 0, z2 = 0;
            double run (const Coefs& c, double x)
            {
                const auto y = c.b0 * x + z1;
                z1 = c.b1 * x - c.a1 * y + z2;
                z2 = c.b2 * x - c.a2 * y;
                return y;
            }
        };
        // Per crossover: a low-pass, a high-pass (each run twice) and an all-pass.
        struct Crossover { Coefs low, high, all; };
        // Per channel: the states of every filter in the tree.
        struct Channel
        {
            std::array<std::array<State, 2>, numBands - 1> low {}, high {};
            std::array<State, 3> all {};   // band 0 at crossovers 1 and 2, band 1 at crossover 2
        };
        void design();

        double rate = 48000.0;
        Settings settings;
        std::array<Crossover, numBands - 1> splits {};
        std::vector<Channel> channelState;
        std::vector<std::array<float, numBands>> scratch;   // per channel, one sample of each band
        std::array<GoldenRelease, numBands> envelopes {};
        std::array<std::atomic<double>, numBands> shownReduction {};
    };

    //==========================================================================
    // Φ Limiter: a look-ahead brickwall limiter that keeps the true peak (4x
    // oversampled) at or below the ceiling, with the golden two-stage release. Its look
    // ahead is φ² ms. All channels share one gain. The audio comes out latency() late.
    class Limiter
    {
    public:
        static constexpr double lookaheadMs = 2.618;   // φ²

        struct Settings
        {
            double driveDb = 0.0;       // into the limiter
            double ceilingDb = -1.0;    // dBTP
            double releaseMs = 89.0;    // a Fibonacci number
        };

        void prepare (double sampleRate, int numChannels);
        void set (const Settings&);
        void reset();
        int latency() const { return window + detectorDelay; }
        void process (float* const* channels, int numChannels, int numFrames);
        double reductionDb() const { return shownReduction.load(); }
        const ReductionHistory& history() const { return past; }

    private:
        float nextGain (float required);

        static constexpr int detectorDelay = TruePeak::delay;
        double rate = 48000.0;
        Settings settings;
        std::vector<TruePeak> detectors;
        std::vector<std::vector<float>> delay;
        std::vector<std::pair<long long, float>> minQueue;
        std::vector<float> box;
        int window = 126, hold = 130, writePos = 0, queueHead = 0, queueTail = 0, boxPos = 0;
        long long counter = 0;
        double boxSum = 0.0;
        float ceiling = 0.89f, drive = 1.0f, fastCoef = 0.0f, slowCoef = 0.0f, fast = 1.0f, slow = 1.0f;
        ReductionHistory past;
        std::atomic<double> shownReduction { 0.0 };
    };
}

// SPDX-License-Identifier: AGPL-3.0-only
#pragma once

// The signal side of the Φ effects: built-in effects of PHI whose times,
// frequencies, levels and patterns all come from the golden ratio φ and the
// Fibonacci numbers. Plain C++, so the core tests can check them; the effects
// themselves are in engine/PhiEffects.cpp. No allocation after prepare().
#include "Fft.h"

#include <array>
#include <complex>
#include <cstdint>
#include <memory>
#include <vector>

namespace phi
{
    inline constexpr double ratio = 1.6180339887498949;     // φ = (1 + √5) / 2
    inline constexpr double inverse = 0.6180339887498949;   // 1 / φ = φ - 1

    // φⁿ for any whole n (φ⁻¹, φ⁻², ...).
    double power (int n);
    // The nth Fibonacci number: 0, 1, 1, 2, 3, 5, 8... (n up to 92 fits).
    std::uint64_t fibonacci (int n);

    // The golden sequence: frac (seed + n / φ). Each value lands in the
    // biggest gap the earlier ones left, so a run of them is spread more
    // evenly than random numbers, yet never repeats. 0 <= value < 1.
    double golden (std::int64_t n, double seed = 0.0);

    // A Sturmian rhythm: step n is a hit when ceil ((n + 1) d - shift) differs
    // from ceil (n d - shift). With d = 1/φ this is the Fibonacci word
    // (1 1 0 1 1 0 1 0 1 1 0 1 1 0 1 0 ...): a pattern that never repeats but
    // never clumps, with hits on a share d of the steps and one on step 0.
    bool hit (std::int64_t step, double density, double shift = 0.0);

    // The densities Φ Rhythm offers: 1/φ (61.8 %), 1/φ² (38.2 %), 1/φ³ (23.6 %), 1/φ⁴ (14.6 %).
    inline constexpr int numDensities = 4;
    double density (int index);

    //==========================================================================
    // Φ EQ: a Super Low shelf at 80 Hz, ten bells at 80 Hz × φⁿ (n = 1..10:
    // 129 Hz to 9.8 kHz) and a Super High shelf at 80 Hz × φ¹¹ (15.9 kHz).
    // Neighbouring bands are φ apart, and each bell is as wide as that step:
    // Q = φ^1.5 (2.06).
    class Eq
    {
    public:
        static constexpr int numBands = 12;   // Super Low, 10 bells, Super High
        static constexpr double baseHz = 80.0;
        static double bandHz (int band);      // band 0 is Super Low, 11 Super High

        void prepare (double sampleRate, int numChannels);
        // Gains in dB per band; recalculates only the bands that changed.
        void set (const std::array<double, numBands>& gainsDb);
        void reset();
        void process (float* const* channels, int numChannels, int numFrames);
        // The response at `hz`, in dB (for tests and drawing).
        double responseDb (double hz) const;

    private:
        struct Biquad { double b0 = 1, b1 = 0, b2 = 0, a1 = 0, a2 = 0; };
        void design (int band);

        double rate = 48000.0;
        std::array<Biquad, numBands> coefs {};
        std::array<double, numBands> gains {};
        std::vector<std::array<std::array<double, 2>, numBands>> state;   // per channel and band: z1, z2
    };

    //==========================================================================
    // Φ Echo: taps at time × φ⁰, φ¹, φ²..., each 1/φ (or `decay`) quieter than
    // the one before, panned around the stereo field by the golden angle so no
    // two land in the same place. Feedback sends the last tap back in.
    class Echo
    {
    public:
        static constexpr int maxTaps = 6;
        static constexpr double maxTimeSeconds = 0.6;
        static constexpr double maxSpanSeconds = 6.7;   // maxTime × φ⁵, the last tap at most

        struct Settings
        {
            double timeMs = 250.0;   // the first tap
            int taps = 5;
            double decay = inverse;  // the level of each tap against the one before
            double spread = 1.0;     // 0 = all taps centred, 1 = golden-angle panning
            double feedback = 0.0;   // 0..0.9
            double mix = 0.35;
        };

        static double tapSeconds (const Settings&, int tap);

        void prepare (double sampleRate, int numChannels);
        void set (const Settings&);
        void reset();
        void process (float* const* channels, int numChannels, int numFrames);

    private:
        double rate = 48000.0;
        Settings settings;
        std::vector<float> ring;   // the mid of the input, and the feedback
        int channels = 2;
        size_t pos = 0;
        std::array<size_t, maxTaps> delays {};
        std::array<float, maxTaps> gainL {}, gainR {};
        float smoothedMix = 0.35f;
    };

    //==========================================================================
    // Φ Space: a reverb from eight delay lines whose lengths step by √φ, so no
    // two of them share a period and the tail stays smooth instead of ringing.
    // A feedback delay network (Hadamard mixing) with damping in the loop.
    class Space
    {
    public:
        static constexpr int numLines = 8;

        struct Settings
        {
            double size = 0.5;        // 0..1: the shortest line runs from 7 ms to 34 ms
            double decaySeconds = 2.0; // RT60
            double damping = 0.4;     // 0..1, how quickly the highs die
            double preDelayMs = 13.0;
            double mix = 0.25;
        };

        void prepare (double sampleRate);
        void set (const Settings&);
        void reset();
        void process (float* const* channels, int numChannels, int numFrames);
        int lineLength (int line) const { return lengths[(size_t) line]; }

    private:
        double rate = 48000.0;
        Settings settings;
        std::array<std::vector<float>, numLines> lines;
        std::array<int, numLines> lengths {}, positions {};
        std::array<double, numLines> feedback {}, lowpass {};
        std::vector<float> pre;
        int prePos = 0, preLength = 1;
        double dampCoef = 0.0;
        float smoothedMix = 0.25f;
    };

    //==========================================================================
    // Φ Drift: movement that never repeats. Three LFOs run at rate, rate × φ and
    // rate × φ², so their sum never comes back to the same shape. Chorus moves
    // a short delay with it (left and right a golden angle apart); Tremolo
    // moves the level; Pan moves the image.
    class Drift
    {
    public:
        enum class Mode { chorus, tremolo, pan };
        static constexpr double maxDelayMs = 30.0;

        struct Settings
        {
            Mode mode = Mode::chorus;
            double rateHz = 0.618;
            double depth = 0.5;   // 0..1
            double mix = 0.5;     // chorus only; tremolo and pan are fully wet
        };

        // The modulation at time `seconds` for a rate, from -1 to 1, offset by `phase` turns.
        static double lfo (double seconds, double rateHz, double phase);

        void prepare (double sampleRate, int numChannels);
        void set (const Settings&);
        void reset();
        void process (float* const* channels, int numChannels, int numFrames);

    private:
        double rate = 48000.0;
        Settings settings;
        std::vector<std::vector<float>> ring;
        size_t pos = 0;
        double time = 0.0;
    };

    //==========================================================================
    // Φ Rhythm: a gate that opens on the hits of a golden rhythm (hit() above)
    // at a step length set by tempo and division, with a short ramp so it does
    // not click. Depth is how far a closed step is turned down.
    class Rhythm
    {
    public:
        struct Settings
        {
            double bpm = 120.0;
            int stepsPerBeat = 4;     // 2 = eighths, 4 = sixteenths, 8 = thirty-seconds
            int densityIndex = 0;     // see density()
            double shift = 0.0;       // 0..1, rotates the pattern
            double depth = 1.0;       // 0..1
            double smoothMs = 3.0;
        };

        void prepare (double sampleRate);
        void set (const Settings&);
        void reset();                  // back to step 0
        // When the host knows where it is (in beats), the pattern follows it.
        void setPosition (double beats);
        void process (float* const* channels, int numChannels, int numFrames);
        std::int64_t currentStep() const;

    private:
        double rate = 48000.0;
        Settings settings;
        double beats = 0.0;
        float gain = 1.0f, smoothCoef = 0.0f;
    };

    //==========================================================================
    // A biquad for the effects below (Audio EQ Cookbook).
    struct Biquad
    {
        double b0 = 1, b1 = 0, b2 = 0, a1 = 0, a2 = 0;
        static Biquad bandPass (double hz, double q, double sampleRate);   // 0 dB at the centre
        static Biquad bell (double hz, double q, double gainDb, double sampleRate);
        static Biquad lowPass (double hz, double q, double sampleRate);
    };

    struct BiquadState
    {
        double z1 = 0, z2 = 0;
        float run (const Biquad& c, float x)
        {
            const auto y = c.b0 * x + z1;
            z1 = c.b1 * x - c.a1 * y + z2;
            z2 = c.b2 * x - c.a2 * y;
            return (float) y;
        }
    };

    //==========================================================================
    // Φ Resonator: very narrow band-passes at root × φ⁰, φ¹, φ²... picked out
    // of the sound (or of white noise mixed in), the root in the centre and the
    // others panned left, right, left... Frequencies that are not on any scale
    // yet sound related. After a technique in a tutorial on golden-ratio sound
    // design: white noise through band-passes at 220, 356, 576, 932, 1507 Hz.
    class Resonator
    {
    public:
        static constexpr int maxBands = 8;

        struct Settings
        {
            double rootHz = 220.0;
            int bands = 5;
            double q = 30.0;        // 2..100
            double width = 1.0;     // 0 = all centred, 1 = hard left and right
            double noise = 0.0;     // 0..1, white noise added before the band-passes
            double mix = 1.0;
        };

        static double bandHz (const Settings&, int band);

        void prepare (double sampleRate, int numChannels);
        void set (const Settings&);
        void reset();
        void process (float* const* channels, int numChannels, int numFrames);

    private:
        double rate = 48000.0;
        Settings settings;
        int used = 0;   // bands below Nyquist
        std::array<Biquad, maxBands> filters {};
        std::array<float, maxBands> gainL {}, gainR {};
        std::array<BiquadState, maxBands> state {};   // the bands run on the mid
        float makeup = 1.0f;
        std::uint32_t noiseState = 0x9e3779b9u;
    };

    //==========================================================================
    // Φ Stack: copies of the sound pitched up by φ, φ², φ³... (each +8.33
    // semitones on the last), panned left, right, left... with the original
    // in the centre, then a low-pass to soften the top. Notes that are not on
    // the piano, yet sound as one. Each copy comes from a granular pitch shifter
    // (two crossfaded taps sweeping a delay line), which suits pads and textures.
    class Stack
    {
    public:
        static constexpr int maxCopies = 4;
        static constexpr double grainMs = 60.0;

        struct Settings
        {
            int copies = 4;
            double decay = inverse;     // the level of each copy against the one below
            double width = 1.0;
            double toneHz = 8000.0;     // the low-pass after the stack
            double mix = 1.0;           // 0 = the original only
        };

        void prepare (double sampleRate, int numChannels);
        void set (const Settings&);
        void reset();
        void process (float* const* channels, int numChannels, int numFrames);

    private:
        double rate = 48000.0;
        Settings settings;
        std::vector<float> ring;   // the mid of the input
        size_t pos = 0, grain = 2880;
        std::array<double, maxCopies> phase {};
        std::array<float, maxCopies> gainL {}, gainR {};
        Biquad tone;
        std::array<BiquadState, 2> toneState {};
        float smoothedMix = 1.0f;
    };

    //==========================================================================
    // Φ Tame: the "golden EQ" from the same tutorial. A cut at the
    // fundamental × φ ÷ 2 × 10 (58.9 Hz → 476.5 Hz) softens drums and loops
    // like gentle compression while leaving their dynamics alone. The
    // fundamental is either set by hand or found as the strongest peak between
    // 25 and 250 Hz, followed smoothly.
    class Tame
    {
    public:
        static constexpr double multiplier = ratio * 5.0;   // φ ÷ 2 × 10
        static constexpr double lowestHz = 25.0, highestHz = 250.0;

        struct Settings
        {
            bool automatic = true;
            double fundamentalHz = 55.0;   // used when not automatic
            double depthDb = -6.0;         // -18..0
            double q = ratio;              // the width of the cut
        };

        static double cutHz (double fundamentalHz) { return fundamentalHz * multiplier; }

        void prepare (double sampleRate, int numChannels);
        void set (const Settings&);
        void reset();
        void process (float* const* channels, int numChannels, int numFrames);
        double fundamental() const { return found; }   // what the cut follows now

    private:
        void analyse();
        void design();

        static constexpr int order = 13;   // 8192-point analysis
        double rate = 48000.0;
        Settings settings;
        std::unique_ptr<SpectrumAnalyzer> analyzer;
        std::vector<float> ring, block, magnitudes;
        size_t pos = 0;
        int sinceAnalysis = 0;
        double found = 55.0, designedHz = 0.0, designedDepth = 1.0;
        Biquad cut;
        std::vector<BiquadState> state;
        int samplesSinceDesign = 0;
    };

    //==========================================================================
    // Φ De-resonator: turns down resonances while they ring, in the manner of
    // dynamic resonance suppressors. Short spectra (75 % overlapped Hann
    // frames) are compared bin by bin with their own smoothed curve, averaged
    // over one golden step (about 0.69 octave, narrower or wider with
    // Sharpness); a bin standing above it is turned down by Depth of its
    // excess. Cuts open again φ times slower than they close. The same gains
    // go on every channel. Latency: one frame (2048 samples at 48 kHz).
    class Deresonator
    {
    public:
        struct Settings
        {
            double depth = 0.5;        // 0..1, the share of a peak's excess taken away
            double sharpness = 0.5;    // 0..1: 0 hears broad bumps, 1 only narrow rings
            double speed = 0.5;        // 0..1: attack from 50 ms down to 5 ms
            double lowHz = 80.0, highHz = 18000.0;   // where it works
            double mix = 1.0;
            bool delta = false;        // play only what it takes away
        };

        void prepare (double sampleRate, int numChannels);
        void set (const Settings&);
        void reset();
        void process (float* const* channels, int numChannels, int numFrames);
        int latency() const { return size; }
        // The deepest cut of the last frame, in dB (0 or below).
        float deepestCutDb() const { return deepest; }

    private:
        void frame();
        void layout();

        double rate = 48000.0;
        Settings settings;
        int size = 2048, hop = 512, bins = 1025, fill = 0;
        std::unique_ptr<Fft> fft;
        std::vector<float> window;
        std::vector<std::vector<float>> input, output, ready;
        std::vector<std::vector<std::complex<float>>> spectra;
        std::vector<double> power, levelDb, prefix;
        std::vector<float> envelopeDb, gainDb;
        std::vector<int> spanLow, spanHigh;
        double attack = 0.5, release = 0.5;
        float deepest = 0.0f;
    };
}


// SPDX-License-Identifier: AGPL-3.0-only
#pragma once

// Φ Modulation: four tools around one idea, motion that is worked out rather than
// rolled, and that never falls into a short loop. Each is a page of one Φ effect
// (engine/PhiEffects.cpp), and the last three take their sequences from the first.
//
// - Flow: the sequence engine. A phase that steps by 1/ratio of a turn (1/φ by
//   default), so it never comes back to where it was, read as four control signals
//   at rate × ratio⁰, ¹, ², ³. Periodic rounds everything to simple fractions for a
//   pattern that repeats, Fibonacci holds each value for the steps of the Fibonacci
//   word, Hybrid adds bounded noise that is still the same every time it plays.
// - Space: a spectral remap. The sound's partials are found in short spectra and
//   moved so that each octave above (or below) the Anchor becomes a ratio, φ by
//   default: 220, 440, 880 Hz become 220, 356, 576 Hz. Blend goes from untouched
//   (harmonic) to fully remapped (inharmonic); Flow can move it.
// - Grain: granular texture from the last seconds of the input. Where each grain
//   starts, how long it lasts, its pitch and when the next one comes all follow
//   their own Flow stream, so a patch sounds the same every time it is played.
// - Matrix: four effects (an echo, a band-pass, a drive and a pitch shifter, each
//   behind its own delay, the delays ratio apart) whose routing changes on every
//   event: Flow picks three of the four and chains them in a loop. Feedback stays
//   below one round the loop, and every node's input is soft-clipped, so it cannot
//   run away.
//
// Plain C++ (no JUCE), no allocation after prepare().
#include "Phi.h"

#include <array>
#include <atomic>
#include <cstdint>
#include <memory>
#include <string>
#include <vector>

namespace phi
{
    //==========================================================================
    class Flow
    {
    public:
        enum class Mode { periodic, irrational, fibonacci, hybrid };
        enum class Shape { smooth, stepped, wave };
        static constexpr int numModes = 4, numShapes = 3;
        static constexpr int numOutputs = 4;
        static constexpr int numStreams = 8;   // outputs 0-3, and four more for Grain and Matrix

        struct Settings
        {
            Mode mode = Mode::irrational;
            Shape shape = Shape::smooth;
            double ratio = phi::ratio;   // 1.05 to 4: φ by default, never hard-wired
            double rateHz = 0.1;         // events a second on output 0; output k runs ratio^k faster
            double jitter = 0.25;        // Hybrid: the most the noise moves a value, 0..1
            double seed = 0.0;           // 0..1: where every stream starts
            double smoothMs = 50.0;      // a slew on the outputs
        };

        // The ratio as the mode uses it: Periodic rounds it to a quarter (φ: 1.5).
        static double ratioOf (const Settings&);
        // How far stream s steps each event, in turns (0..1): 1/ratio for stream 0, and
        // for the others 1/ratio plus a different quadratic irrational (√7, √13, ...),
        // so the streams never move in lockstep. Periodic rounds it to eighths.
        static double stepOf (const Settings&, int stream);
        // Output k's events a second: rate × ratio^k.
        static double outputRate (const Settings&, int output);
        // The value of event n on a stream, 0 <= value < 1. The same every time.
        static double event (const Settings&, int stream, std::int64_t n);
        // How many times stream s's value has moved by event n (Fibonacci holds it).
        static std::int64_t moves (const Settings&, int stream, std::int64_t n);
        // The signal at `position` events along a stream, 0..1, before the slew.
        static double at (const Settings&, int stream, double position);

        void prepare (double sampleRate);
        void set (const Settings&);
        void reset();                   // back to the start: the same patch plays the same
        void advance (int numFrames);   // moves the outputs on, once a block
        void locate (double seconds);   // to where it is `seconds` from the start, following the host
        float output (int k) const { return values[(size_t) k]; }
        double position (int k) const { return positions[(size_t) k]; }

    private:
        double rate = 48000.0;
        Settings settings;
        std::array<double, numOutputs> positions {};
        std::array<float, numOutputs> values {};
        bool started = false;
    };

    //==========================================================================
    class SpectralSpace
    {
    public:
        static constexpr int order = 11;   // 2048-point frames, a quarter apart
        static constexpr int maxPeaks = 256;

        struct Settings
        {
            double ratio = phi::ratio;   // what an octave above the anchor becomes (2 leaves it)
            double anchorHz = 220.0;     // the partial that stays put
            double blend = 1.0;          // 0 = harmonic (untouched) to 1 = fully remapped
            double density = 1.0;        // 0..1: the share of partials kept, strongest first
            double tiltDb = 0.0;         // dB an octave (of the new spectrum) above the anchor
            double mix = 1.0;
        };

        // Where a partial at `hz` goes: anchor × (hz / anchor)^e, e = 1 + blend × (log2 ratio - 1).
        static double mappedHz (const Settings&, double hz);

        void prepare (double sampleRate, int numChannels);
        void set (const Settings&);
        void reset();
        void process (float* const* channels, int numChannels, int numFrames);
        int latency() const { return size; }

        // For the window, from any thread: the level heard and the level made (dB full
        // scale) at pictureSize frequencies from 20 Hz to 20 kHz, and the partials found.
        static constexpr int pictureSize = 128;
        static double pictureHz (int point);
        void picture (float* inDb, float* outDb) const;
        int partialsFound() const { return shownPeaks.load(); }

    private:
        void frame (int channel);

        double rate = 48000.0;
        Settings settings;
        int size = 2048, hop = 512, bins = 1025, sinceFrame = 0, pos = 0;
        std::unique_ptr<Fft> fft;
        std::vector<float> window;
        std::vector<std::vector<float>> input, output;          // per channel, rings of `size`
        std::vector<std::complex<float>> spectrum, made;
        std::vector<float> magnitude;
        std::vector<int> peaks, bounds;
        std::vector<std::vector<int>> lastPeaks;                // per channel: last frame's peaks (bins)
        std::vector<std::vector<double>> lastTurn;              // and the phase each had turned, by bin
        std::vector<double> turn;
        std::array<std::atomic<float>, pictureSize> shownIn {}, shownOut {};
        std::atomic<int> shownPeaks { 0 };
    };

    //==========================================================================
    class Grain
    {
    public:
        static constexpr double bufferSeconds = 6.0;
        static constexpr int maxGrains = 32;

        struct Settings
        {
            double density = 13.0;       // grains a second, 1 to 89
            double sizeMs = 89.0;        // a grain lasts size / ratio to size × ratio
            double spreadSeconds = 1.0;  // how far back grains may start, 0 to 5
            double pitch = 0.0;          // semitones either way a grain may be pitched
            double width = 1.0;          // 0 = centred, 1 = golden-angle spread
            double flow = 0.0;           // 0..1: how far Flow's outputs move the start and size
            double mix = 0.382;
            bool freeze = false;         // stop listening: the grains keep playing what was heard
        };

        // Grain n's timing and colour (each 0..1, from its own stream): the gap before
        // it, where it starts back, how long it lasts and its pitch.
        struct Plan
        {
            double gapSeconds = 0.0, back = 0.0, sizeMs = 0.0, semitones = 0.0, pan = 0.0;
        };
        static Plan plan (const Settings&, const Flow::Settings&, std::int64_t n);

        void prepare (double sampleRate, int numChannels);
        void set (const Settings&, const Flow::Settings&);
        void reset();
        // `flowStart` and `flowSize` (0..1): Flow outputs 0 and 3 now.
        void process (float* const* channels, int numChannels, int numFrames, float flowStart, float flowSize);

        // For the window: the grains playing, as (how far back, 0..1 of spread; level, 0..1).
        void picture (std::vector<float>& taps) const;

    private:
        struct Voice
        {
            bool on = false;
            double read = 0.0, speed = 1.0;   // where in the ring, in samples, and how fast it moves
            int age = 0, length = 1;
            float gainL = 0.0f, gainR = 0.0f;
        };
        void start (float flowStart, float flowSize);

        double rate = 48000.0;
        Settings settings;
        Flow::Settings flow;
        std::vector<float> ring;                // the mid of the input
        int ringSize = 1, writePos = 0;
        std::int64_t next = 0;                  // the next grain's number
        double untilNext = 0.0;                 // samples
        std::array<Voice, maxGrains> voices {};
        std::array<std::atomic<float>, maxGrains * 2> shown {};
        float smoothedMix = 0.382f;
    };

    //==========================================================================
    class Matrix
    {
    public:
        static constexpr int numNodes = 4;   // echo, filter, drive, pitch
        static constexpr double maxDelaySeconds = 5.0;
        static const char* nodeName (int node);

        struct Settings
        {
            double eventsHz = 0.382;   // how often the routing changes, 0.05 to 8
            double timeMs = 233.0;     // node k's delay is time × ratio^k (at most 5 s)
            double feedback = 0.618;   // round the loop, 0 to 0.95
            double toneHz = 1000.0;    // the band-pass's centre
            double driveDb = 12.0;
            double glideMs = 89.0;     // how long a change of routing takes
            double width = 1.0;
            double flow = 0.5;         // how far Flow output 2 sweeps the band-pass, 0..1
            double mix = 0.236;
        };

        // The route of event n: three different nodes, chained first to last and back.
        static std::array<int, 3> route (const Flow::Settings&, std::int64_t n);
        static double delaySeconds (const Settings&, const Flow::Settings&, int node);

        void prepare (double sampleRate, int numChannels);
        void set (const Settings&, const Flow::Settings&);
        void reset();
        // `flowTone` (0..1): Flow output 2 now.
        void process (float* const* channels, int numChannels, int numFrames, float flowTone);
        void locate (double seconds);   // to the event of that time, following the host

        // For the window, from any thread.
        std::array<int, 3> currentRoute() const { return { shownRoute[0].load(), shownRoute[1].load(), shownRoute[2].load() }; }
        std::int64_t currentEvent() const { return shownEvent.load(); }
        float level (int node) const { return shownLevel[(size_t) node].load(); }

    private:
        void design (float flowTone);
        void target (bool snap);

        double rate = 48000.0;
        Settings settings;
        Flow::Settings flow;
        std::array<std::vector<float>, numNodes> lines;
        int lineSize = 1, writePos = 0;
        std::array<double, numNodes> delay {}, delayTarget {};   // samples, glided
        std::array<float, numNodes> send {}, sendTarget {}, out {}, outTarget {}, panL {}, panR {};
        std::array<std::array<float, numNodes>, numNodes> gain {}, gainTarget {};   // gain[to][from]
        float glide = 0.001f;
        double eventPosition = 0.0;
        std::int64_t event = -1;
        // The band-pass (a TPT state-variable filter) and the drive.
        double g = 0.1, k = 0.6;
        double ic1 = 0.0, ic2 = 0.0;
        float drive = 4.0f;
        // The pitch shifter: two taps crossfaded across a short line.
        std::vector<float> shiftLine;
        int shiftPos = 0;
        double shiftPhase = 0.0, shiftStep = 0.0;
        int shiftWindow = 2880;
        std::array<std::atomic<float>, numNodes> shownLevel {};
        std::array<float, numNodes> meter {};
        std::atomic<std::int64_t> shownEvent { 0 };
        std::array<std::atomic<int>, 3> shownRoute {};
        float smoothedMix = 0.236f;
    };
}

// SPDX-License-Identifier: AGPL-3.0-only
#pragma once

// Effects that play from what they hear (owner, 2026-11-01: "let's try them all ... where I and
// every user will need it explained in the manual"): sounds made by the audio coming in, for a
// mixer return's Prism or any chain. A send carries sound, not notes, so these take their notes,
// rhythm and dynamics from the sound itself:
//
//   Vocoder   the sound shapes a carrier (oscillators or noise) band by band;
//   Strings   sympathetic strings tuned to a chord ring when the sound excites them;
//   Grains    a live granular sampler that sprays grains of the last few seconds;
//   Double    a pitch tracker plays an oscillator along with a monophonic line;
//   Trigger   each hit in the sound fires a drum voice, as loud as the hit.
//
// Plain C++ (the Φ effects in engine/PhiEffects.cpp wrap them), tested in tests/CoreTests.cpp.
#include <array>
#include <cstdint>
#include <vector>

namespace phi
{
    // The pitch of a monophonic sound, by YIN (de Cheveigné and Kawahara, 2002) on the sound
    // taken down to a quarter of its rate. Feeds a sample at a time; the estimate follows
    // every 64 samples of the reduced rate.
    class PitchTracker
    {
    public:
        static constexpr double lowestHz = 50.0, highestHz = 1200.0;

        void prepare (double sampleRate);
        void reset();
        void push (float sample);
        double hz() const { return frequency; }        // 0 while there is no clear pitch
        double confidence() const { return clarity; }  // 0..1

    private:
        void analyse();

        double rate = 48000.0, reducedRate = 12000.0;
        int factor = 4, phase = 0, sinceAnalysis = 0;
        double accumulator = 0.0;
        std::array<double, 2> lowpass {};
        std::vector<float> ring;   // the reduced sound
        int write = 0;
        std::vector<double> difference;
        double frequency = 0.0, clarity = 0.0;
    };

    // A two-pole band-pass (constant 0 dB peak gain), used by the vocoder's bands.
    struct BandPass
    {
        double b0 = 0, b2 = 0, a1 = 0, a2 = 0, x1 = 0, x2 = 0, y1 = 0, y2 = 0;
        void design (double hz, double q, double rate);
        float run (float x)
        {
            const auto y = b0 * x + b2 * x2 - a1 * y1 - a2 * y2;
            x2 = x1; x1 = x; y2 = y1; y1 = y;
            return (float) y;
        }
        void clear() { x1 = x2 = y1 = y2 = 0.0; }
    };

    //==========================================================================
    class Vocoder
    {
    public:
        static constexpr int maxBands = 32;
        enum class Carrier { saw, square, noise, sawAndNoise };
        enum class Chord { single, octaves, fifth, major, minor, sus4 };

        struct Settings
        {
            int bands = 16;               // 4..32, from 80 Hz to 12 kHz
            Carrier carrier = Carrier::saw;
            Chord chord = Chord::single;
            int note = 48;                // the carrier's root, as a MIDI note (C3)
            bool follow = false;          // the carrier plays the sound's own pitch (and the chord on it)
            double formant = 0.0;         // semitones the carrier's bands sit above (+) or below the sound's
            double releaseMs = 40.0;      // how fast a band closes after the sound leaves it
            double sibilance = 0.3;       // 0..1: the sound's own highs above 5 kHz let through, for consonants
            double mix = 1.0;
        };

        static int chordNotes (Chord, std::array<int, 4>& offsets);   // how many, and their semitones above the root

        void prepare (double sampleRate);
        void set (const Settings&);
        void reset();
        void process (float* const* channels, int numChannels, int numFrames);

    private:
        void design();

        double rate = 48000.0;
        Settings settings;
        bool designed = false;
        Settings designedFor;
        std::array<BandPass, maxBands> analysis, synthesis, synthesis2;
        std::array<float, maxBands> envelope {};
        BandPass air;   // the sibilance: a band-pass high up
        std::array<double, 4> phases {};
        double glideHz = 0.0;
        std::uint32_t noise = 0x9e3779b9u;
        PitchTracker tracker;
        double attack = 0.0, release = 0.0;
    };

    //==========================================================================
    // Sympathetic strings: Karplus-Strong style feedback combs, a pair a note of a chord, that
    // ring when the sound has energy near them.
    class Strings
    {
    public:
        static constexpr int maxStrings = 12;
        enum class Chord { unison, octaves, fifths, major, minor, sus4, major7, minor7, phi };

        struct Settings
        {
            int note = 45;                // the lowest string, a MIDI note (A2)
            Chord chord = Chord::fifths;
            int strings = 6;              // 1..12, up the chord over the octaves
            double decaySeconds = 3.0;    // how long a string rings, to -60 dB
            double brightness = 0.6;      // 0..1: the strings' damping, dark to bright
            double width = 0.8;           // 0..1: the strings spread left and right
            double mix = 0.5;
        };

        static double stringHz (const Settings&, int string);

        void prepare (double sampleRate);
        void set (const Settings&);
        void reset();
        void process (float* const* channels, int numChannels, int numFrames);

    private:
        struct String
        {
            std::vector<float> line;
            int length = 1, write = 0;
            double fraction = 0.0;   // the delay's part below a sample, read by linear interpolation
            float feedback = 0.0f, damp = 0.0f, state = 0.0f, panL = 0.7f, panR = 0.7f;
        };
        void tune();

        double rate = 48000.0;
        Settings settings;
        std::array<String, maxStrings> strings;
        int used = 0;
        float dcIn = 0.0f, dcOut = 0.0f;
    };

    //==========================================================================
    // A live granular sampler: the last few seconds kept in a loop, and grains of it played back
    // at a density, size and pitch, from a spot behind the present (or a frozen moment).
    class Grains
    {
    public:
        static constexpr double bufferSeconds = 6.0;
        static constexpr int maxGrains = 64;

        struct Settings
        {
            double density = 20.0;        // grains a second, 1..100
            double sizeMs = 120.0;        // 10..1000
            double positionMs = 300.0;    // how far behind the present they start, 0..5000
            double jitter = 0.5;          // 0..1: the spread of their start around that spot
            double pitch = 0.0;           // semitones, -24..24
            double pitchJitter = 0.0;     // semitones of random detune, 0..12
            double spread = 0.7;          // 0..1: grains placed across the stereo field
            bool reverse = false;
            bool freeze = false;          // stop recording: the loop keeps what it has
            double mix = 0.5;
        };

        void prepare (double sampleRate);
        void set (const Settings& s) { settings = s; }
        void reset();
        void process (float* const* channels, int numChannels, int numFrames);
        int activeGrains() const;

    private:
        struct Grain
        {
            bool on = false;
            double position = 0.0, step = 1.0;   // in the loop, in samples
            int age = 0, length = 0;
            float panL = 0.7f, panR = 0.7f;
        };
        double random();   // 0..1

        double rate = 48000.0;
        Settings settings;
        std::array<std::vector<float>, 2> loop;
        int write = 0, filled = 0;
        std::array<Grain, maxGrains> grains;
        double untilNext = 0.0;
        std::uint32_t seed = 0x2545f491u;
    };

    //==========================================================================
    // A pitch-tracking double: an oscillator follows a monophonic line, an interval away, as
    // loud as the line, gliding between its notes.
    class Double
    {
    public:
        enum class Wave { sine, triangle, saw, square, phi };   // phi: partials at φⁿ, each 1/φ quieter

        struct Settings
        {
            Wave wave = Wave::saw;
            double interval = -12.0;      // semitones from the line, -24..24
            bool quantize = true;         // to the nearest semitone (equal temperament)
            double glideMs = 30.0;
            double threshold = 0.5;       // 0..1: how sure the tracker must be to play
            double tone = 0.6;            // 0..1: a low-pass, dark to open
            double mix = 0.5;
        };

        void prepare (double sampleRate);
        void set (const Settings& s) { settings = s; }
        void reset();
        void process (float* const* channels, int numChannels, int numFrames);
        double playingHz() const { return currentHz; }

    private:
        double rate = 48000.0;
        Settings settings;
        PitchTracker tracker;
        double phase = 0.0, currentHz = 0.0, targetHz = 0.0, level = 0.0, gate = 0.0, lowState = 0.0;
        std::array<double, 5> phiPhases {};
    };

    //==========================================================================
    // An audio-triggered drum voice: each hit in the sound (a jump of its fast envelope over its
    // slow one, above a threshold) fires a synthesised drum, as loud as the hit.
    class Trigger
    {
    public:
        enum class Sound { kick, snare, clap, hat, tom, rim, bell };

        struct Settings
        {
            Sound sound = Sound::kick;
            double thresholdDb = -30.0;   // a hit must be louder than this
            double sensitivity = 0.5;     // 0..1: how sharp a jump counts as a hit
            double holdMs = 60.0;         // the shortest time between two hits
            double tune = 0.0;            // semitones
            double decay = 0.5;           // 0..1, short to long
            double dynamics = 0.8;        // 0..1: how much the hit's loudness sets the drum's
            double mix = 1.0;             // 1: only the drum; 0: only the sound
        };

        void prepare (double sampleRate);
        void set (const Settings& s) { settings = s; }
        void reset();
        void process (float* const* channels, int numChannels, int numFrames);
        int hits() const { return count; }   // since prepare or reset, for the display and tests

    private:
        void fire (float velocity);
        float voice();

        double rate = 48000.0;
        Settings settings;
        double fast = 0.0, slow = 0.0;
        int holdLeft = 0, count = 0;
        // The drum playing.
        bool playing = false;
        double t = 0.0, phase = 0.0, velocity = 1.0;
        std::array<double, 4> bellPhases {};
        double noiseState = 0.0, noiseState2 = 0.0;
        std::uint32_t seed = 0x6d2b79f5u;
    };
}

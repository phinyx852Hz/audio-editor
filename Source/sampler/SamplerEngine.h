// SPDX-License-Identifier: AGPL-3.0-only
#pragma once

// The Sampler's sound engine: one sample played across the keyboard, Classic
// (loops while a key is held, then releases) or 1-Shot (plays through, or fades
// when the key is let go in Gate). Each voice has its own filter, glide and loop
// direction; one voice is monophonic, with legato and a stack of held keys.
// Plain C++, no JUCE or Qt: notes may arrive from any thread (MIDI, the window),
// render() runs on the audio thread. The sample is given at the rate render()
// runs at.
#include "../core/AudioData.h"

#include <array>
#include <atomic>
#include <memory>
#include <mutex>
#include <vector>

namespace sampler
{
    enum class Mode { classic, oneShot };

    // Classic's loop: Continuous goes round for as long as the voice sounds, Alternate
    // goes back and forth, Once goes round one time and plays on to the end, and the two
    // Until Release modes loop while the key is held, then play on to the end.
    enum class LoopMode { continuous, alternate, once, untilRelease, alternateUntilRelease };
    enum class FilterType { lowPass, highPass, bandPass, notch };
    // Standard plays clean; Vintage is 12 bits at 26 kHz as early samplers were, and the
    // two Turntable settings go further, as a record sampled at 45 or 78 rpm and tuned down.
    enum class Quality { standard, vintage, turntable45, turntable78 };

    // A pitch or filter envelope: attack, decay, sustain and release, each segment bowed by
    // Curve, moving the pitch (semitones) or the cutoff (octaves) by Amount at its top.
    struct Envelope
    {
        bool on = false;
        double attack = 0.0, decay = 0.3, sustain = 0.0, release = 0.2;   // seconds; sustain 0..1
        double curve = 0.0;                 // -1 (slow, then fast) .. 1 (fast, then slow)
        double amount = 0.0;                // pitch: semitones; filter: octaves; either sign
        double velocity = 0.0;              // 0..1: how much less soft notes move it
    };

    // The LFOs are shared by every voice, as Cubase's: one value at a time, started again by
    // a note when Retrigger says so. Shape changes each wave: harmonics on the sine, a
    // trapezoid from the triangle, ramp down to triangle to ramp up on the saw, the pulse's
    // width, a pause before the ramp, the log's bend, and glides between the random steps.
    enum class Wave { sine, triangle, saw, pulse, ramp, log, sampleHold, sampleHold2 };
    enum class Retrigger { off, firstNote, eachNote };

    struct Lfo
    {
        Wave wave = Wave::sine;
        double shape = 0.5;                 // 0..1
        double rate = 4.0;                  // Hz, when not synced
        bool sync = false;                  // to Settings::tempo
        double beats = 1.0;                 // synced: beats a cycle (0.25 a sixteenth note)
        Retrigger retrigger = Retrigger::off;
        double phase = 0.0;                 // 0..1: where a cycle starts again
        bool randomPhase = false;
        double modWheel = 0.0;              // 0..1: 1 leaves it silent until the mod wheel goes up
        double toPitch = 0.0;               // semitones
        double toFilter = 0.0;              // octaves
        double toVolume = 0.0;              // 0..1
        double toPan = 0.0;                 // 0..1
    };

    struct Settings
    {
        Mode mode = Mode::classic;
        Frame start = 0, end = -1;          // what is played; end -1: to the end of the sample
        bool loop = true;                   // Classic: loops while held
        LoopMode loopMode = LoopMode::continuous;
        Frame loopStart = 0, loopEnd = -1;  // within [start, end); -1: the end
        double loopCrossfade = 0.0;         // seconds blended across the loop's seam (forward loops)
        bool reverse = false;               // plays the sample backwards; the markers count from its end
        int rootNote = 60;                  // the key that plays the sample as recorded
        bool fixedPitch = false;            // every key plays the sample at the root's pitch
        int keyLow = 0, keyHigh = 127;      // only these keys play
        double transpose = 0.0;             // semitones
        double detune = 0.0;                // cents
        double glide = 0.0;                 // seconds to slide from the last note to the new one; 0: none
        bool fingered = false;              // glide only between notes played legato
        bool legato = false;                // one voice: a new key held over another changes pitch without restarting
        double gainDb = 0.0;
        double pan = 0.0;                   // -1 (left) .. 1 (right)
        double velocity = 0.6;              // 0: every note at full level, 1: level follows velocity
        double attack = 0.002, decay = 0.3, sustain = 1.0, release = 0.15;   // seconds; sustain 0..1
        double fadeIn = 0.0, fadeOut = 0.01;  // 1-Shot: seconds
        bool gate = false;                  // 1-Shot: letting go fades out over Fade out (else it plays through)
        bool filter = false;
        FilterType filterType = FilterType::lowPass;
        bool steep = false;                 // 24 dB an octave instead of 12
        double cutoff = 5000.0;             // Hz
        double resonance = 0.2;             // 0..1
        double drive = 0.0;                 // dB of saturation before the filter
        double keyFollow = 0.0;             // 0..1: 1 moves the cutoff an octave for each octave above the root
        double filterVelocity = 0.0;        // 0..1: soft notes close the cutoff by up to four octaves
        Quality quality = Quality::standard;
        Envelope pitchEnvelope, filterEnvelope;
        std::array<Lfo, 2> lfo {};
        double tempo = 120.0;               // what synced LFOs count beats at
        int voices = 8;                     // 1..32; 1 is monophonic
        double bendRange = 2.0;             // semitones for a full pitch bend
    };

    struct Sample
    {
        std::vector<float> left, right;     // equal length; mono sounds have both the same
        Frame length() const { return (Frame) left.size(); }
    };
    using SamplePtr = std::shared_ptr<const Sample>;
    SamplePtr makeSample (const AudioData&);   // the first two channels (mono doubled)

    class Engine
    {
    public:
        static constexpr int maxVoices = 32;

        Engine() { for (auto& h : heads) h.store (-1.0); }

        void prepare (double sampleRate);
        void setSample (SamplePtr);         // stops what plays
        void setSettings (const Settings&);
        Settings settings() const;
        SamplePtr sample() const;

        void noteOn (int note, float velocity);   // velocity 0..1; 0 is a note-off
        void noteOff (int note);
        void allNotesOff();                  // releases everything
        void panic();                        // silences everything at once
        void pitchBend (double amount);      // -1..1
        void sustainPedal (bool down);
        void modWheel (double amount);       // 0..1

        void render (float* left, float* right, int frames);   // adds to what is there

        // For the window: where the voices are in the sample (-1: free) and which keys sound.
        std::array<double, maxVoices> playheads() const;
        bool isSounding (int note) const;

    private:
        struct Stage   // where a pitch or filter envelope is
        {
            int stage = 0;               // 0 attack, 1 decay, 2 sustain, 3 release, 4 done
            double time = 0.0, from = 0.0, level = 0.0;
        };
        struct Voice
        {
            bool active = false, held = false, sustained = false;
            int note = 0;
            float velocity = 1.0f;
            double position = 0.0;
            bool forward = true;         // Alternate loops turn round
            int loops = 0;               // times round the loop so far
            int stage = 0;               // 0 attack, 1 decay, 2 sustain, 3 release
            double level = 0.0;
            double pitch = 0.0;          // the note sounding, in semitones, while it glides
            double glideStep = 0.0;      // semitones a frame
            Frame glideLeft = 0;
            double filter[2][2][2] {};   // [stage][channel][state]: two state-variable filters
            float kept[2] {};            // Vintage: the value kept between its samples
            double holdPhase = 1.0;
            Stage envelope[2] {};        // pitch, filter
            unsigned age = 0;
        };
        void start (Voice&, int note, float velocity, double fromPitch);
        void glideTo (Voice&, int note, double fromPitch);
        void releaseVoice (Voice&);
        void push (int note);
        void pop (int note);
        float sampleAt (const std::vector<float>&, double position) const;
        void renderBlock (float* left, float* right, int frames);
        void startRelease (Voice&);
        void retrigger (bool firstNote);

        mutable std::mutex lock;
        double rate = 48000.0;
        Settings current;
        SamplePtr data;
        std::array<Voice, maxVoices> voice {};
        unsigned clock = 0;
        double bend = 0.0;
        bool pedal = false;
        double lastPitch = -1.0;                 // where the last note was, for glide; -1: none yet
        std::array<int, 128> stack {};           // held keys, oldest first (for one voice)
        int stacked = 0;
        double wheel = 0.0;
        struct LfoState { double phase = 0.0, value = 0.0, held = 0.0, previous = 0.0; bool high = false; };
        std::array<LfoState, 2> lfos {};
        uint32_t noise = 0x9E3779B9u;            // the random steps' generator
        std::array<std::atomic<double>, maxVoices> heads {};
        std::array<std::atomic<bool>, 128> sounding {};
    };
}

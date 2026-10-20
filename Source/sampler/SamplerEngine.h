// SPDX-License-Identifier: AGPL-3.0-only
#pragma once

// The Sampler's sound engine: one sample played across the keyboard, Classic
// (loops while a key is held, then releases) or 1-Shot (plays through, ignoring
// note-off). Plain C++, no JUCE or Qt: notes may arrive from any thread (MIDI,
// the window), render() runs on the audio thread. The sample is given at the
// rate render() runs at.
#include "../core/AudioData.h"

#include <array>
#include <atomic>
#include <memory>
#include <mutex>
#include <vector>

namespace sampler
{
    enum class Mode { classic, oneShot };

    struct Settings
    {
        Mode mode = Mode::classic;
        Frame start = 0, end = -1;          // what is played; end -1: to the end of the sample
        bool loop = true;                   // Classic: loops while held
        Frame loopStart = 0, loopEnd = -1;  // within [start, end); -1: the end
        int rootNote = 60;                  // the key that plays the sample as recorded
        double transpose = 0.0;             // semitones
        double detune = 0.0;                // cents
        double gainDb = 0.0;
        double pan = 0.0;                   // -1 (left) .. 1 (right)
        double velocity = 0.6;              // 0: every note at full level, 1: level follows velocity
        double attack = 0.002, decay = 0.3, sustain = 1.0, release = 0.15;   // seconds; sustain 0..1
        double fadeIn = 0.0, fadeOut = 0.01;  // 1-Shot: seconds
        int voices = 8;                     // 1..32
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

        void render (float* left, float* right, int frames);   // adds to what is there

        // For the window: where the voices are in the sample (-1: free) and which keys sound.
        std::array<double, maxVoices> playheads() const;
        bool isSounding (int note) const;

    private:
        struct Voice
        {
            bool active = false, held = false, sustained = false;
            int note = 0;
            float velocity = 1.0f;
            double position = 0.0;
            int stage = 0;               // 0 attack, 1 decay, 2 sustain, 3 release
            double level = 0.0;
            unsigned age = 0;
        };
        void start (Voice&, int note, float velocity);
        void releaseVoice (Voice&);
        float sampleAt (const std::vector<float>&, double position) const;

        mutable std::mutex lock;
        double rate = 48000.0;
        Settings current;
        SamplePtr data;
        std::array<Voice, maxVoices> voice {};
        unsigned clock = 0;
        double bend = 0.0;
        bool pedal = false;
        std::array<std::atomic<double>, maxVoices> heads {};
        std::array<std::atomic<bool>, 128> sounding {};
    };
}

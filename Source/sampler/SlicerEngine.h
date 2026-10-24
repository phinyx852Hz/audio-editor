// SPDX-License-Identifier: AGPL-3.0-only
#pragma once

// The Slicer's sound engine: one sample cut into up to 64 slices, each on a pad
// with its own level, pan, pitch, direction, play mode, envelope and choke group
// (after Ninjas2 by Clearly Broken Software, written for PHI). Plain C++: pads
// may be hit from any thread, render() runs on the audio thread. The sample is
// given at the rate render() runs at.
#include "SamplerEngine.h"

#include <array>
#include <atomic>
#include <mutex>
#include <utility>
#include <vector>

namespace slicer
{
    constexpr int numPads = 64;     // four banks of 16
    constexpr int padsPerBank = 16;

    enum class Play { oneShot, gate };   // gate: stops when the pad is let go

    struct Pad
    {
        bool used = false;
        Frame start = 0, end = 0;   // the slice
        double gainDb = 0.0;
        double pan = 0.0;           // -1..1
        double pitch = 0.0;         // semitones
        bool reverse = false;
        Play play = Play::oneShot;
        double attack = 0.0005;     // seconds
        double release = 0.03;      // seconds, after letting go (gate) or at the end of the slice
        int choke = 0;              // 0: none; pads in the same group stop each other (open and closed hats)
    };


    // Slices from cut points (sorted, inside the sound); slivers under minGap join the next.
    std::vector<std::pair<Frame, Frame>> slicesFromCuts (std::vector<Frame> cuts, Frame length, Frame minGap);
    std::vector<std::pair<Frame, Frame>> equalSlices (Frame length, int count);

    // MIDI notes to pads. Push: its 8x8 grid in User mode (notes 36 to 99, bottom row
    // first) as four 4x4 banks, A bottom left, B bottom right, C top left, D top right.
    // Chromatic: 16 notes a bank from 36 (C1 in Live's naming), as Live's drum racks.
    enum class Layout { push, chromatic };
    int padForNote (int note, Layout);   // -1: no pad
    int noteForPad (int pad, Layout);

    struct Kit
    {
        std::array<Pad, numPads> pads {};
        double velocity = 0.6;      // 0: every hit at full level, 1: level follows velocity
        int voices = 16;            // 1..32
        Layout layout = Layout::push;
    };

    // The kit with the first slices on the pads in order (the rest empty), keeping each pad's settings.
    void assign (Kit&, const std::vector<std::pair<Frame, Frame>>& slices);

    class Engine
    {
    public:
        static constexpr int maxVoices = 32;

        void prepare (double sampleRate);
        void setSample (sampler::SamplePtr);
        void setKit (const Kit&);
        Kit kit() const;
        sampler::SamplePtr sample() const;

        void hit (int pad, float velocity);   // velocity 0..1
        void letGo (int pad);
        void noteOn (int note, float velocity);   // MIDI, through the kit's layout
        void noteOff (int note);
        int lastHit() const { return last.load (std::memory_order_relaxed); }   // -1: none yet
        void panic();
        void render (float* left, float* right, int frames);   // adds to what is there

        bool isSounding (int pad) const;
        std::vector<double> playheads() const;   // positions of the playing voices

    private:
        struct Voice
        {
            bool active = false, held = false, releasing = false;
            int pad = 0;
            float velocity = 1.0f;
            double position = 0.0, level = 0.0, fastRelease = 0.0;
            unsigned age = 0;
        };

        mutable std::mutex lock;
        double rate = 48000.0;
        Kit current;
        sampler::SamplePtr data;
        std::array<Voice, maxVoices> voice {};
        unsigned clock = 0;
        std::atomic<int> last { -1 };
        std::array<std::atomic<bool>, numPads> sounding {};
        std::array<std::atomic<double>, maxVoices> heads {};
    };
}

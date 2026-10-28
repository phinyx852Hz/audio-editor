// SPDX-License-Identifier: AGPL-3.0-only
#pragma once

// The Slicer's sound engine: a drum machine of 128 pads in eight banks of 16 (as
// Cubase's Drum Machine), each pad up to four layers deep. A layer plays a slice of
// the kit's one sample or a sound of its own, with its own level, tune, fades and
// direction; the pad has level, pan, pitch, play mode, envelope, choke group, mute,
// solo and the MIDI note it answers (after Ninjas2 by Clearly Broken Software and
// Ableton's Drum Rack, written for PHI). Plain C++: pads may be hit from any thread,
// render() runs on the audio thread. Sounds are given at the rate render() runs at.
#include "SamplerEngine.h"

#include <array>
#include <atomic>
#include <cstdint>
#include <mutex>
#include <string>
#include <utility>
#include <vector>

namespace slicer
{
    constexpr int numPads = 128;    // eight banks of 16
    constexpr int padsPerBank = 16;
    constexpr int numBanks = numPads / padsPerBank;
    constexpr int maxLayers = 4;
    constexpr int numColours = 16;
    constexpr int numChokes = 16;

    enum class Play { oneShot, gate };   // gate: stops when the pad is let go

    struct Layer
    {
        bool used = false;
        sampler::SamplePtr sound;   // a sound of its own; empty: a slice of the kit's sample
        std::string path;           // where its own sound came from, if a file
        Frame start = 0, end = 0;   // the part of the sound it plays
        double gainDb = 0.0;        // on top of the pad's
        double pitch = 0.0;         // semitones, on top of the pad's
        double fadeIn = 0.0, fadeOut = 0.0;   // seconds
        bool reverse = false;
    };

    struct Pad
    {
        std::array<Layer, maxLayers> layers {};   // the first is the pad's slice or sound
        std::string name;           // empty: the sound's name, or none
        int colour = 0;             // 0: none, 1..numColours
        bool mute = false, solo = false;
        int receive = -1;           // the MIDI note it answers; -1: its note in the kit's layout
        double gainDb = 0.0;
        double pan = 0.0;           // -1..1
        double pitch = 0.0;         // semitones
        Play play = Play::oneShot;
        double attack = 0.0005;     // seconds
        double release = 0.03;      // seconds, after letting go (gate) or at the end of the sound
        int choke = 0;              // 0: none, 1..numChokes; pads in the same group stop each other (open and closed hats)

        bool used() const;          // any layer in use
        int layerCount() const;
        bool isSlice() const { return layers[0].used && layers[0].sound == nullptr; }
    };

    // Slices from cut points (sorted, inside the sound); slivers under minGap join the next.
    std::vector<std::pair<Frame, Frame>> slicesFromCuts (std::vector<Frame> cuts, Frame length, Frame minGap);
    std::vector<std::pair<Frame, Frame>> equalSlices (Frame length, int count);

    // MIDI notes to pads. Push: its 8x8 grid in User mode (notes 36 to 99, bottom row first)
    // as four 4x4 banks, A bottom left, B bottom right, C top left, D top right; `page` 1
    // puts banks E to H there instead, and the other four banks take the notes left over
    // (100 to 127, then 0 to 35). Chromatic: pad n is note n, 16 notes a bank, as in Cubase
    // (C1, note 36, is pad 5 of bank C).
    enum class Layout { push, chromatic };
    int padForNote (int note, Layout, int page = 0);   // -1: no pad
    int noteForPad (int pad, Layout, int page = 0);

    struct Kit
    {
        std::array<Pad, numPads> pads {};
        double velocity = 0.6;      // 0: every hit at full level, 1: level follows velocity
        int voices = 16;            // 1..64, every layer of a hit takes one
        Layout layout = Layout::push;
        int pushPage = 0;           // which half of the pads Push's grid plays: 0 A to D, 1 E to H

        int receiveNote (int pad) const;   // the note a pad answers
    };

    // The slices on the pads in order, keeping each pad's settings. Pads with a sound of
    // their own are skipped and keep it; the other pads past the slices are emptied.
    void assign (Kit&, const std::vector<std::pair<Frame, Frame>>& slices);
    // A sound of its own on a pad's layer, played whole.
    void placeSound (Kit&, int pad, int layer, sampler::SamplePtr, const std::string& path);
    // Swaps two pads: what they hold and their settings, but not the notes they answer.
    void swapPads (Kit&, int a, int b);

    class Engine
    {
    public:
        static constexpr int maxVoices = 64;

        Engine()
        {
            for (auto& h : heads)
                h.store (-1.0);
            for (auto& p : headPads)
                p.store (-1);
        }

        void prepare (double sampleRate);
        double sampleRate() const;
        void setSample (sampler::SamplePtr);
        void setKit (const Kit&);
        Kit kit() const;
        sampler::SamplePtr sample() const;

        void hit (int pad, float velocity);   // velocity 0..1
        void letGo (int pad);
        void noteOn (int note, float velocity);   // MIDI: every pad that answers the note
        void noteOff (int note);
        int lastHit() const { return last.load (std::memory_order_relaxed); }   // -1: none yet
        void panic();
        void render (float* left, float* right, int frames);   // adds to what is there

        bool isSounding (int pad) const;
        // Positions of the playing voices: on the kit's sample (pad -1), or on a pad's own first layer.
        std::vector<double> playheads (int pad = -1) const;

    private:
        struct Voice
        {
            bool active = false, held = false, releasing = false;
            int pad = 0, layer = 0;
            float velocity = 1.0f;
            double position = 0.0, level = 0.0, fastRelease = 0.0;
            unsigned age = 0;
        };

        void rebuildNotes();

        mutable std::mutex lock;
        double rate = 48000.0;
        Kit current;
        // The pads each MIDI note plays (fixed size: notes arrive on the audio thread from the mixer).
        std::array<std::array<std::uint8_t, numPads>, 128> padsForNote {};
        std::array<int, 128> padsForNoteCount {};
        sampler::SamplePtr data;
        std::array<Voice, maxVoices> voice {};
        unsigned clock = 0;
        std::atomic<int> last { -1 };
        std::array<std::atomic<bool>, numPads> sounding {};
        std::array<std::atomic<double>, maxVoices> heads {};
        std::array<std::atomic<int>, maxVoices> headPads {};   // -1: on the kit's sample
    };
}

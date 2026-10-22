// SPDX-License-Identifier: AGPL-3.0-only
#pragma once

// The patchbay: one routing graph for everything PHINYX plays live. Every source
// (the editor's playback, the Sampler, the Slicer, the patchbay's own synths, the
// mixer's tracks, the Mix Bus and the FX buses) has a stereo output, and every
// destination (the mixer's tracks, the Mix Bus, the FX buses and the Main Out) has a
// stereo input. A patch connects an output to an input with a gain; an input sums
// everything patched into it. Patches that would feed a signal back into itself are
// refused, so the graph always plays in one pass. The patchbay window shows 8 of the
// sources against 8 of the destinations; the rest keep their patches.
// Plain C++, no JUCE or Qt: the UI changes the graph from its thread, render() runs on
// the audio thread.
#include "../mixer/MixerEngine.h"

#include <array>
#include <atomic>
#include <memory>
#include <mutex>
#include <string>
#include <vector>

namespace sampler { class Engine; }
namespace slicer { class Engine; }

namespace patchbay
{
    // An effect on an FX bus: stereo in, stereo out, in place. Plugins come from the
    // engine (AudioEngine::loadEffectPlugin).
    class Effect
    {
    public:
        virtual ~Effect() = default;
        virtual std::string name() const = 0;
        virtual std::string pluginId() const { return {}; }
        virtual void prepare (double sampleRate, int maxBlock) = 0;
        virtual void process (float* left, float* right, int frames) = 0;   // audio thread
        virtual void reset() {}
        virtual bool hasWindow() const { return false; }
        virtual void showWindow() {}
        virtual std::string state() const { return {}; }
        virtual void setState (const std::string&) {}
    };
    using EffectPtr = std::shared_ptr<Effect>;

    constexpr int maxSynths = 4, maxBuses = 4;

    // The patch points, numbered 0..numNodes-1.
    enum class Kind { editor, sampler, slicer, synth, track, mixBus, fxBus, mainOut };
    namespace node
    {
        constexpr int editor = 0, sampler = 1, slicer = 2;
        constexpr int synth = 3;                                // synth 1 .. maxSynths
        constexpr int track = synth + maxSynths;                // mixer track 1 .. 8
        constexpr int mixBus = track + mixer::maxTracks;
        constexpr int fxBus = mixBus + 1;                       // FX bus 1 .. maxBuses
        constexpr int mainOut = fxBus + maxBuses;
    }
    constexpr int numNodes = node::mainOut + 1;

    Kind kindOf (int node);
    int indexOf (int node);              // which synth, track or bus (0-based); 0 otherwise
    bool hasOutput (int node);           // a source: everything but the Main Out
    bool hasInput (int node);            // a destination: tracks, Mix Bus, FX buses, Main Out
    std::string defaultName (int node);  // "Track 3", "FX Bus 1"...

    struct Link
    {
        int from = 0, to = 0;
        float gainDb = 0.0f;
    };

    enum class Refusal { none, self, noPort, loop };

    class PatchBay final : private mixer::Patch
    {
    public:
        static constexpr int block = 256;
        static constexpr float minDb = -60.0f, maxDb = 12.0f;   // below minDb is silence

        PatchBay (sampler::Engine&, slicer::Engine&, mixer::Engine*);
        ~PatchBay() override;

        void prepare (double sampleRate);
        double sampleRate() const { return rate.load(); }

        // The graph. UI thread.
        bool linked (int from, int to) const;
        float gainDb (int from, int to) const;
        Refusal check (int from, int to) const;   // why from -> to can't be patched (none: it can)
        Refusal connect (int from, int to, float gainDb = 0.0f);
        void disconnect (int from, int to);
        void setGainDb (int from, int to, float db);   // any thread; no replan, no lock
        std::vector<Link> links() const;
        void setLinks (const std::vector<Link>&);      // the ones that make a loop are left out
        static std::vector<Link> defaultLinks();       // every source to where it is heard without a patchbay
        void resetLinks() { setLinks (defaultLinks()); }
        bool isDefault() const;
        std::vector<int> destinationsOf (int from) const;
        std::vector<int> sourcesOf (int to) const;

        // What sits in the synth and FX bus slots. Null clears. Prepared here. UI thread.
        void setSynth (int index, mixer::InstrumentPtr);
        mixer::InstrumentPtr synth (int index) const;
        void setEffect (int bus, EffectPtr);
        EffectPtr effect (int bus) const;
        // A MIDI keyboard plays a synth (status with its channel). Any thread.
        void liveMidi (int synthIndex, uint8_t status, uint8_t data1, uint8_t data2);
        void panic();   // every synth silent, every effect reset

        // The loudest sample out of a node since the last call (linear). Any thread.
        float takePeak (int node);

        // Audio thread: left and right hold the editor's sound; on return they hold the Main Out.
        void render (float* left, float* right, int frames);

    private:
        struct Plan
        {
            std::vector<int> order;                                  // every node, sources before what they feed
            std::array<std::vector<int>, numNodes> inputs;           // what is patched into each node
            std::array<int, mixer::maxTracks> tracks {};             // the tracks in that order
            std::array<mixer::InstrumentPtr, maxSynths> synths;
            std::array<EffectPtr, maxBuses> effects;
        };

        // mixer::Patch
        int trackAt (int k) const override;
        void trackInput (int track, int offset, float* left, float* right, int frames) override;
        void trackOutput (int track, int offset, const float* left, const float* right, int frames) override;

        bool reaches (int from, int to) const;   // a path of patches from -> ... -> to
        void replan();                           // publishes the graph to the audio thread
        void renderChunk (float* left, float* right, int frames);
        void ensure (int node);
        void sumInputs (int node, float* left, float* right);
        float* bufL (int n) { return buffers.data() + (size_t) n * 2 * block; }
        float* bufR (int n) { return buffers.data() + ((size_t) n * 2 + 1) * block; }

        sampler::Engine& samplerEngine;
        slicer::Engine& slicerEngine;
        mixer::Engine* mix;
        std::atomic<double> rate { 48000.0 };

        // UI side: the truth, published as plans.
        std::array<std::array<bool, numNodes>, numNodes> edges {};
        std::array<mixer::InstrumentPtr, maxSynths> synthSlots;
        std::array<EffectPtr, maxBuses> effectSlots;
        std::array<std::atomic<float>, numNodes * numNodes> gains;

        // Audio side.
        std::mutex lock;                   // the plan: held by render and by swaps
        std::unique_ptr<Plan> plan;
        std::array<float, numNodes * numNodes> lastGain {};
        float lastMaster = 1.0f;
        bool fresh = true;                 // nothing to ramp from yet
        std::vector<float> buffers, trackInL, trackInR, scratchL, scratchR;
        std::array<bool, numNodes> done {};
        std::array<bool, mixer::maxTracks> trackInputDone {};
        int chunk = 0;                     // frames in the chunk being rendered
        bool rendering = false;            // inside renderChunk: the mixer's calls are ours
        std::array<std::atomic<float>, numNodes> peaks;

        std::mutex liveLock;
        std::array<std::vector<mixer::NoteEvent>, maxSynths> liveQueue, liveTaken;
        std::atomic<bool> panicPending { false };
    };
}

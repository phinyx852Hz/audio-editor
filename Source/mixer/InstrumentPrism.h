// SPDX-License-Identifier: AGPL-3.0-only
#pragma once

// An Instrument Prism (docs/prism.md, owner 2026-10-30: "instrument racks ... Instrument Rays
// that can be combined with FX in the same chain in serial mode"): a mixer track's instrument
// that holds rays side by side, like Ableton's Instrument Rack. Each ray is a chain in series,
// an instrument and then its own effects; the rays play together and are added, each at its
// gain. A ray hears only the notes in its key and velocity range, so rays layer or split the
// keyboard. The track's own inserts come after the Prism and so process all of it, as an effect
// outside a rack does in Ableton. Plain C++.
#include "Effect.h"
#include "Instruments.h"
#include "MixerEngine.h"
#include "../core/PrismSplit.h"

#include <array>
#include <atomic>
#include <memory>
#include <mutex>
#include <string>
#include <vector>

namespace mixer
{
    class InstrumentPrism final : public Instrument
    {
    public:
        static constexpr int maxRays = prism::maxRays;
        static constexpr int maxEffects = maxInserts;   // per ray, like a track's inserts

        // A ray as it is set and shown: what it plays and how it is heard.
        struct Ray
        {
            std::string name;
            InstrumentPtr instrument;     // null: the ray is silent
            Inserts effects;              // after the instrument, in order (each with bypass and Mix)
            float gainDb = 0.0f;          // -60 (off) .. +12
            bool muted = false, soloed = false;
            int lowKey = 0, highKey = 127;            // the notes it plays
            int lowVelocity = 1, highVelocity = 127;  // and how hard they are hit
            bool hears (int note, int velocity) const
            {
                return note >= lowKey && note <= highKey && velocity >= lowVelocity && velocity <= highVelocity;
            }
        };

        Kind kind() const override { return Kind::prism; }
        std::string name() const override;
        void setName (const std::string&);

        // The rays (up to maxRays; UI thread). Instruments and effects new to the Prism are
        // prepared here; the ones taken out are let go after the swap, on the calling thread.
        // A ray keeps its notes and its delay line as long as it keeps its instrument.
        void setRays (std::vector<Ray>);
        std::vector<Ray> rays() const;
        int numRays() const;
        // A ray's level since the last call (linear peak), for its meter. UI thread.
        float takeRayPeak (int ray);
        // The samples a ray's effects delay it by; the others are delayed to line up with the slowest.
        int latency() const;

        void prepare (double sampleRate, int maxBlock) override;
        void process (const NoteEvent* events, int numEvents, float* left, float* right, int frames) override;
        void releaseAll() override;
        void reset() override;
        bool setParameter (const std::string&, float) override;   // every ray's instrument that has it
        void setTuning (std::shared_ptr<const phi::Tuning>) override;
        void setRaga (int raga, int root, bool equalTempered) override;

        // The Prism's own settings, one ray a line (its instruments and effects are saved by
        // whoever made them: the mixer saves them with the project).
        std::string state() const override;
        void setState (const std::string&) override;

    private:
        // What the audio thread keeps for a ray, which follows its instrument when rays move.
        struct Lane
        {
            InstrumentPtr instrument;
            std::vector<float> left, right, dryL, dryR;
            std::vector<NoteEvent> events;
            std::array<std::array<uint8_t, 128>, 16> held {};   // notes it was given, per channel
            prism::Delay align;
            float gain = 1.0f;          // the last block's, ramped from
            std::atomic<float> peak { 0.0f };
        };

        void prepareLane (Lane&);
        void runEffects (Ray&, Lane&, int frames);
        // `base`: where the chunk starts in the block, so the events' offsets come down to it.
        void renderChunk (const NoteEvent* events, int numEvents, int base, float* left, float* right, int frames);

        mutable std::mutex lock;   // the rays; the audio thread only tries it
        std::vector<Ray> list;
        std::vector<std::shared_ptr<Lane>> lanes;   // one a ray, in the same order
        std::string title;
        double rate = 48000.0;
        int block = 512;
        std::shared_ptr<const phi::Tuning> tuning;
        int raga = -1, ragaRoot = 0;
        bool ragaEqual = false;
        static constexpr int maxDelay = 1 << 15;
    };

    // The note names a key range is shown with: 60 is C4, as in Ableton and most DAWs.
    std::string prismNoteName (int note);
}

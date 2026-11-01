// SPDX-License-Identifier: AGPL-3.0-only
#pragma once

// An Effect Prism (owner, 2026-11-01: "allow for either one of the FX sends to be a Prism, but
// with fx only, no instruments"): an effect that holds rays side by side, each a chain of
// effects in series, as the Master Section's Prism does. It divides what comes in among its
// rays (in parallel, in frequency bands, as mid and side, or left and right; core/PrismSplit.h),
// keeps them in time with each other and joins them again, each ray at its gain. The mixer's
// returns hold one. Plain C++.
#include "Effect.h"
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
    class EffectPrism final : public Effect
    {
    public:
        static constexpr const char* identifier = "phi:effect-prism";
        static constexpr int maxRays = prism::maxRays;
        static constexpr int maxEffects = maxInserts;   // per ray, like a track's inserts

        struct Ray
        {
            std::string name;
            Inserts effects;          // in order (each with bypass and Mix); none: the ray is its share as it came in
            float gainDb = 0.0f;      // -60 (off) .. +12
            bool muted = false, soloed = false;
        };

        EffectPrism();

        static bool is (const Effect& e) { return e.pluginId() == identifier; }

        std::string name() const override;
        std::string pluginId() const override { return identifier; }
        void setName (const std::string&);

        prism::Split split() const { return mode.load(); }
        void setSplit (prism::Split s) { mode = s; }
        // The three crossovers for Bands, in Hz (the bands are as many as the rays, up to four).
        std::array<double, 3> crossovers() const;
        void setCrossovers (const std::array<double, 3>&);

        // The rays (up to maxRays; UI thread). Effects new to the Prism are prepared here; the
        // ones taken out are let go after the swap, on the calling thread.
        void setRays (std::vector<Ray>);
        std::vector<Ray> rays() const;
        int numRays() const;
        // How many of the rays the split feeds (the rest are silent).
        int raysHeard() const;
        float takeRayPeak (int ray);   // linear, since the last call; UI thread

        void prepare (double sampleRate, int maxBlock) override;
        void process (float* left, float* right, int frames) override;
        void reset() override;
        int latency() const override;

        // Its own settings: the split, crossovers, name and each ray's gain, mute, solo and name.
        // Its effects are saved by whoever made them (the mixer saves them with the project).
        std::string state() const override;
        void setState (const std::string&) override;

    private:
        struct Lane
        {
            std::array<std::vector<float>, 2> buffer, dry;
            prism::Delay align;
            float gain = 1.0f;
            std::atomic<float> peak { 0.0f };
        };

        void prepareLane (Lane&);
        void runEffects (Ray&, Lane&, int frames);
        void renderChunk (float* left, float* right, int frames);

        mutable std::mutex lock;   // the rays; the audio thread only tries it
        std::vector<Ray> list;
        std::vector<std::shared_ptr<Lane>> lanes;
        std::string title;
        std::atomic<prism::Split> mode { prism::Split::parallel };
        std::array<double, 3> bandsHz { 200.0, 2000.0, 8000.0 };
        std::atomic<bool> bandsChanged { true };
        prism::Splitter splitter;
        std::array<std::vector<float>, 2> input;
        double rate = 48000.0;
        int block = 512;
        static constexpr int maxDelay = 1 << 15;
    };
}

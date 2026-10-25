// SPDX-License-Identifier: AGPL-3.0-only
#pragma once

// An effect: stereo in, stereo out, in place. The mixer's inserts and the patchbay's
// FX buses hold them; plugins come from the engine (AudioEngine::loadEffectPlugin).
// Plain C++.
#include <memory>
#include <string>
#include <vector>

namespace mixer
{
    class Effect
    {
    public:
        virtual ~Effect() = default;
        virtual std::string name() const = 0;
        virtual std::string pluginId() const { return {}; }
        virtual void prepare (double sampleRate, int maxBlock) = 0;
        virtual void process (float* left, float* right, int frames) = 0;   // audio thread
        virtual void reset() {}
        virtual int latency() const { return 0; }   // samples it delays the sound by
        virtual bool hasWindow() const { return false; }
        virtual void showWindow() {}
        virtual std::string state() const { return {}; }
        virtual void setState (const std::string&) {}

        // Its knobs, for effects without a window of their own (the built-in and Φ ones). UI thread.
        struct Parameter
        {
            std::string name, text;   // text: the value as the effect shows it, with its unit
            float value = 0.0f;       // 0..1
            int steps = 0;            // 0: continuous
            bool toggle = false;
        };
        virtual std::vector<Parameter> parameters() const { return {}; }
        virtual void setParameter (int /*index*/, float /*value*/) {}
        virtual bool setParameterText (int /*index*/, const std::string&) { return false; }
    };
    using EffectPtr = std::shared_ptr<Effect>;
}

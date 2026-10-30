// SPDX-License-Identifier: AGPL-3.0-only
#pragma once

// The modulator rack of the Device Chain, in the manner of Cubase's Modulators panel:
// sources that move any parameter of the plugins in the chain, each between a low and
// a high end, as a Prism macro does. The sources are Cubase's (LFO with morph and fold,
// Envelope Follower, Step, Random, Sample & Hold, Shaper, Attack-Decay, Macro,
// Crossfader) and PHI's own Φ Flow (core/PhiModulation.h), the sequence that never
// repeats. Random's déjà vu follows the idea of Mutable Instruments' Marbles (MIT):
// a loop of values that is replayed, or replayed in another order, or left behind.
// Nothing is random in the rolled sense: the same chain plays the same every time.
//
// Every source gives 0 to 1. The rack is worked out once a block of up to 128 samples.
// Plain C++ (no JUCE); no allocation in process().
#include <cstddef>

#include <array>
#include <cstdint>
#include <string>
#include <vector>

namespace modrack
{
    enum class Kind { lfo, follower, steps, random, sampleHold, shaper, attackDecay, macro, crossfader, phiFlow };
    static constexpr int numKinds = 10;
    const char* kindName (Kind);

    // A control of a kind of modulator. A choice has its names in `choices`; a
    // source picks another modulator (0 = none, or the input's noise for Sample & Hold).
    struct Control
    {
        const char* name = "";
        float low = 0.0f, high = 1.0f, initial = 0.0f;
        const char* unit = "";
        std::vector<const char*> choices;
        bool logarithmic = false;
        bool source = false;
    };
    const std::vector<Control>& controls (Kind);

    static constexpr int maxControls = 20;
    static constexpr int maxModulators = 16;
    static constexpr int maxRoutes = 64;
    static constexpr int maxSteps = 16;
    static constexpr int chunk = 128;   // the longest stretch between updates, in samples

    struct Modulator
    {
        Kind kind = Kind::lfo;
        std::string name;
        std::array<float, maxControls> values {};
        bool operator== (const Modulator&) const = default;
    };
    Modulator make (Kind);   // with its controls at their defaults

    // Moves parameter `parameter` of the plugin in slot `slot` (only while that slot
    // still holds `pluginId`) from `low` (the source at 0) to `high` (at 1).
    struct Route
    {
        int modulator = 0, slot = 0, parameter = 0;
        std::string pluginId;
        float low = 0.0f, high = 1.0f;
        bool operator== (const Route&) const = default;
    };

    // The step control's index for step s of a Step modulator.
    int stepControl (int s);

    class Rack
    {
    public:
        void prepare (double sampleRate);
        // Changes what is in the rack, keeping where each modulator was that stayed the same kind.
        void set (const std::vector<Modulator>&);
        void reset();   // every source back to its start
        // Moves every source on by `frames` (at most `chunk`); `input` is the chain's input.
        void process (const float* const* input, int numChannels, int frames);
        float value (int modulator) const;
        int size() const { return (int) modulators.size(); }

    private:
        struct State
        {
            Kind kind = Kind::lfo;
            double phase = 0.0;          // turns, or events for Φ Flow and Random
            float value = 0.0f, held = 0.0f, from = 0.0f, to = 0.0f;
            double envelope = 0.0;       // follower and attack-decay
            int stage = 0;               // attack-decay: 0 rest, 1 attack, 2 decay
            bool armed = true;
            std::int64_t event = -1;
            std::array<float, maxSteps> loop {};
            float smoothed = 0.0f;
        };
        float source (int control, const Modulator&) const;
        float lfoAt (const Modulator&, double phase) const;

        double rate = 48000.0;
        std::vector<Modulator> modulators;
        std::vector<State> states;
    };

    // How a route sets its parameter for a source value v: low + (high - low) × v, within 0..1.
    inline float routed (const Route& r, float v)
    {
        const auto x = r.low + (r.high - r.low) * v;
        return x < 0.0f ? 0.0f : (x > 1.0f ? 1.0f : x);
    }
}

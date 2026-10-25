// SPDX-License-Identifier: AGPL-3.0-only
#pragma once

#include "AudioEngine.h"
#include "../core/PluginMeasure.h"
#include "../mixer/Effect.h"
#include "../mixer/MixerEngine.h"

#include <functional>
#include <string>
#include <vector>

// What the Plugin Analyzer measures: copies of effects made from their plugin and
// settings, so measuring never touches what is heard. One plugin or a whole chain
// (the Master Section, a mixer track's inserts).
//
// A probe follows its source: sync() copies the source's settings across when they
// change, and makes the copies again when its plugins change. Both run on the
// message thread. measure::run on the probe may then run on another thread, as long
// as nothing calls sync() until it has finished.
class PluginProbe final : public measure::Device
{
public:
    struct Link
    {
        std::string pluginId, name, state;
        bool bypassed = false;
        float mix = 1.0f;   // dry/wet, as a slot or insert has it
        bool operator== (const Link&) const = default;
    };
    using Source = std::function<std::vector<Link>()>;

    // One device of the Master Section (inside a Prism too); its bypass and mix are ignored.
    static Source masterDevice (AudioEngine&, ChainPath);
    // The whole Master Section, slot by slot (without the master gain and dither).
    static Source masterSection (AudioEngine&);
    // A mixer track's inserts as they were handed over.
    static Source inserts (const mixer::Inserts&);

    PluginProbe (AudioEngine&, Source, double sampleRate, int block);

    // True when the copies changed. `error` names any effect that could not be copied (it is left out).
    bool sync (std::string& error);
    bool empty() const { return twins.empty(); }
    std::string title() const;   // the names, joined by arrows

    void reset() override;
    void process (float* left, float* right, int frames) override;
    int reportedLatency() const override;

private:
    struct Twin
    {
        Link link;
        mixer::EffectPtr effect;
        int latency = 0;
        std::vector<float> line;   // the dry sound kept in time with the effect: left and right interleaved
        std::size_t at = 0;
    };

    AudioEngine& engine;
    Source source;
    double rate;
    int block;
    std::vector<Twin> twins;
    std::vector<Link> applied;   // what the copies were last made or set from
    std::vector<float> dryLeft, dryRight;
};

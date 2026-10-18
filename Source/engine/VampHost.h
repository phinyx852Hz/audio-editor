// SPDX-License-Identifier: AGPL-3.0-only
#pragma once

#include "../core/AudioData.h"

#include <atomic>
#include <string>
#include <vector>

// Vamp analysis plugins (vamp-plugins.org), the plugin type Sonic Visualiser
// uses: beat trackers, onset detectors, key and tempo estimators, pitch
// trackers and more. Plugins are looked for in the stock vamp folder (the QM
// Vamp Plugins ship with PHINYX), then VAMP_PATH or the usual places (~/vamp,
// ~/.vamp, /usr/local/lib/vamp, /usr/lib/vamp). No JUCE or Qt types here.
namespace vamphost
{
    // One output of one plugin: something a plugin can find, such as "Beats"
    // of the Bar and Beat Tracker. Only outputs that give results in time are listed.
    struct Output
    {
        std::string pluginKey;      // "qm-vamp-plugins:qm-barbeattracker"
        std::string outputId;       // "beats"
        std::string pluginName, outputName, maker, description, unit;
        std::string category;       // "Time > Tempo"
        bool hasDuration = false;   // results are regions
        bool hasValue = false;      // results have a value (a curve), not only times and labels
        std::string id() const { return pluginKey + ":" + outputId; }
    };

    // Every output of every plugin found; scanned once, then kept.
    const std::vector<Output>& outputs();
    const Output* find (const std::string& id);   // "pluginKey:outputId"

    struct Feature
    {
        Frame start = 0;
        Frame duration = 0;    // 0 for an instant
        float value = 0.0f;    // the first value, when the output has one
        std::string label;
    };

    struct Result
    {
        std::vector<Feature> features;
        std::string error;     // empty when it worked
    };

    // Runs one plugin output, with its default parameters, over [start, end) of
    // the audio (all channels, mixed as the plugin wants). Feature times are
    // frames of the audio, not of the range. Call from a worker thread.
    Result run (const AudioData&, Frame start, Frame end, const std::string& outputId,
                std::atomic<float>* progress = nullptr, const std::atomic<bool>* cancel = nullptr);

    // The folders searched, for messages ("no plugins found in ...").
    std::vector<std::string> searchPath();
}

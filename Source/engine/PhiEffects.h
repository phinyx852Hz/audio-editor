// SPDX-License-Identifier: AGPL-3.0-only
#pragma once

#include <juce_audio_processors/juce_audio_processors.h>

#include <memory>
#include <vector>

// The Φ effects: PHI's own built-in effects built on the golden ratio (their
// signal side is core/Phi.h). Listed under Built-in in Add plugin, with their
// knobs on the slot's card; they exist only inside PHI.
namespace phifx
{
    std::vector<juce::PluginDescription> descriptions();
    // nullptr when `d` is not a Φ effect.
    std::unique_ptr<juce::AudioPluginInstance> create (const juce::PluginDescription& d);
}

// SPDX-License-Identifier: AGPL-3.0-only
#pragma once

#include <juce_audio_processors/juce_audio_processors.h>

#include <memory>
#include <vector>

// Effects that ship with PHINYX and load into the Master Section like plugins.
// They have no window of their own; their knobs appear on the slot's card.
namespace builtin
{
    inline constexpr const char* formatName = "Built-in";

    std::vector<juce::PluginDescription> descriptions();
    bool isBuiltin (const juce::PluginDescription&);
    // The old Dither effect, no longer offered: the Master Section's final dither replaced it.
    // Saved chains that have it still load it, so its settings can move there.
    bool isDither (const juce::PluginDescription&);
    std::unique_ptr<juce::AudioPluginInstance> create (const juce::PluginDescription&);
}

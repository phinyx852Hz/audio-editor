// SPDX-License-Identifier: AGPL-3.0-only
#pragma once

#include <juce_audio_processors/juce_audio_processors.h>

#include <memory>
#include <vector>

// The Φ effects that play from what they hear (core/AudioDriven.h): Φ Vocoder, Φ Strings,
// Φ Grains, Φ Double and Φ Trigger. Listed with the other Φ effects (phifx).
namespace drivenfx
{
    std::vector<juce::PluginDescription> descriptions();
    std::unique_ptr<juce::AudioPluginInstance> create (const juce::PluginDescription& d);   // nullptr: not one of them
}

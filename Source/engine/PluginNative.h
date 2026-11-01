// SPDX-License-Identifier: AGPL-3.0-only
#pragma once

#include <juce_audio_processors/juce_audio_processors.h>

#include <cstdint>
#include <string>
#include <vector>

// A plugin's state in its own format, as a DAWproject keeps it (Source/mixer/DawProject.h):
// a VST3's .vstpreset (its component and controller state, with its class id), a CLAP's
// state stream. JUCE's own state wraps these differently, so other hosts cannot read it.
// Message thread.
namespace pluginnative
{
    // `format` "VST3" or "CLAP"; `deviceId` the class id (32 hex digits) or the CLAP id.
    // False for another format (LV2, the built-in ones), or when the plugin gave nothing.
    bool save (juce::AudioPluginInstance&, std::string& format, std::string& deviceId, std::vector<uint8_t>& bytes);
    bool load (juce::AudioPluginInstance&, const std::vector<uint8_t>& bytes);
}

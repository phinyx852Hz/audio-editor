// SPDX-License-Identifier: AGPL-3.0-only
#include "PluginNative.h"
#include "ClapFormat.h"

#include <cctype>

namespace pluginnative
{
namespace
{
    bool isClap (const juce::AudioPluginInstance& p) { return p.getPluginDescription().pluginFormatName == ClapPluginFormat::formatName; }
}

bool save (juce::AudioPluginInstance& plugin, std::string& format, std::string& deviceId, std::vector<uint8_t>& bytes)
{
    bytes.clear();
    if (auto* vst3 = plugin.getVST3Client())
    {
        const auto preset = vst3->getPreset();
        if (preset.getSize() < 48)
            return false;
        const auto* b = static_cast<const uint8_t*> (preset.getData());
        bytes.assign (b, b + preset.getSize());
        // 'VST3', a version, then the class id as 32 hex digits.
        deviceId.assign (reinterpret_cast<const char*> (b + 8), 32);
        for (const auto ch : deviceId)
            if (! std::isxdigit ((unsigned char) ch))
                return false;
        format = "VST3";
        return true;
    }
    if (isClap (plugin))
    {
        juce::MemoryBlock block;
        plugin.getStateInformation (block);   // the CLAP state stream as it is
        const auto* b = static_cast<const uint8_t*> (block.getData());
        bytes.assign (b, b + block.getSize());
        deviceId = clapPluginId (plugin);
        format = "CLAP";
        return ! deviceId.empty();
    }
    return false;
}

bool load (juce::AudioPluginInstance& plugin, const std::vector<uint8_t>& bytes)
{
    if (bytes.empty())
        return false;
    if (auto* vst3 = plugin.getVST3Client())
        return vst3->setPreset (juce::MemoryBlock (bytes.data(), bytes.size()));
    if (isClap (plugin))
    {
        plugin.setStateInformation (bytes.data(), (int) bytes.size());
        return true;
    }
    return false;
}
}

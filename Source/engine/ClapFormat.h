// SPDX-License-Identifier: AGPL-3.0-only
#pragma once

#include <juce_audio_processors/juce_audio_processors.h>

// Hosting for CLAP plugins (https://cleveraudio.org), which JUCE 8 does not
// have. Effects only: audio in and out, parameters, saved state, latency and
// the plugin's own window (X11, so XWayland on Wayland). Note ports and MIDI
// are left unconnected.
//
// Plugin files are searched for in CLAP_PATH, ~/.clap, /usr/lib/clap and
// /usr/local/lib/clap, folders included, so the CLAP copies yabridge makes of
// Windows plugins in ~/.clap/yabridge are found too.
// The CLAP id ("org.surge-synth-team.surge-xt") of an instance made here; empty for any other.
std::string clapPluginId (const juce::AudioPluginInstance&);

class ClapPluginFormat final : public juce::AudioPluginFormat
{
public:
    static constexpr const char* formatName = "CLAP";

    juce::String getName() const override { return formatName; }

    void findAllTypesForFile (juce::OwnedArray<juce::PluginDescription>&, const juce::String& fileOrIdentifier) override;
    bool fileMightContainThisPluginType (const juce::String& fileOrIdentifier) override;
    juce::String getNameOfPluginFromIdentifier (const juce::String& fileOrIdentifier) override;
    bool pluginNeedsRescanning (const juce::PluginDescription&) override;
    bool doesPluginStillExist (const juce::PluginDescription&) override;
    bool canScanForPlugins() const override { return true; }
    bool isTrivialToScan() const override { return false; }
    juce::StringArray searchPathsForPlugins (const juce::FileSearchPath&, bool recursive, bool allowAsync) override;
    juce::FileSearchPath getDefaultLocationsToSearch() override;
    bool requiresUnblockedMessageThreadDuringCreation (const juce::PluginDescription&) const override { return false; }

private:
    void createPluginInstance (const juce::PluginDescription&, double initialSampleRate, int initialBufferSize,
                               PluginCreationCallback) override;
};

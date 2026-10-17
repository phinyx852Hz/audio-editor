// SPDX-License-Identifier: AGPL-3.0-only
#pragma once

#include "AudioEngine.h"

#include <juce_audio_processors/juce_audio_processors.h>

#include <array>
#include <atomic>
#include <memory>

// Plugin hosting for the Master Section. Internal to the engine; the UI talks
// to it through AudioEngine.
class MasterChain
{
public:
    MasterChain();
    ~MasterChain();

    void setConfigFolder (const juce::File&);

    // Plugin list
    std::vector<PluginInfo> knownPlugins() const;
    std::vector<std::string> searchFolders() const;
    int startScan();
    bool scanNext (std::string& fileBeingScanned);
    std::string finishScan();

    // Each plugin file is examined by a separate copy of PHINYX started with
    // --scan-plugin, so a plugin that crashes or hangs while loading only takes
    // that copy down; it is then skipped from then on. This is that copy's job:
    // writes what `file` holds to `output` as XML; returns the exit code.
    static int scanInThisProcess (const juce::String& formatName, const juce::String& file, const juce::File& output);
    static constexpr const char* scanOption = "--scan-plugin";

    // Slots. All of these run on the message thread.
    std::string load (int slot, const std::string& pluginId);
    void clear (int slot);
    void move (int from, int to);
    void setBypassed (int slot, bool);
    MasterSlotInfo info (int slot) const;
    void setGainDb (double);
    double gainDb() const { return gainDecibels; }
    void setAllBypassed (bool b) { allBypassed = b; }
    bool isAllBypassed() const   { return allBypassed; }
    bool isActive() const;

    // Solo: while listening, only this slot runs (-1 = none). Renders ignore it.
    void setSolo (int slot)  { soloSlot = slot; }
    int solo() const         { return soloSlot; }
    float takePeak (int slot);  // output peak since the last call, while listening

    std::vector<PluginParameterInfo> parameters (int slot) const;
    PluginParameterInfo parameter (int slot, int index) const;
    void setParameter (int slot, int index, float value);
    bool setParameterFromText (int slot, int index, const std::string& text);

    bool canShowWindow (int slot) const;
    void showWindow (int slot);

    void save() const;
    std::string restore();

    // The channel count everything is processed at (the open file's, at least 2).
    // Plugins that cannot take that many are given stereo and process the first two.
    void setChannels (int);
    static juce::AudioChannelSet channelSetFor (int numChannels);  // 6 = 5.1, 8 = 7.1... in WAV order
    int channels() const { return channelCount; }

    // Real-time use (audio thread).
    void prepare (double sampleRate, int maxBlockSize);
    void processRealtime (juce::AudioBuffer<float>&, int startSample, int numSamples);

    AudioDataPtr render (const AudioData&, Frame start, Frame end, std::string& error,
                         std::atomic<float>* progress, const std::atomic<bool>* cancel);

private:
    struct Slot
    {
        std::unique_ptr<juce::AudioPluginInstance> plugin;
        juce::PluginDescription description;
        bool bypassed = false;
    };

    class PluginWindow;

    static void addExtraFormats (juce::AudioPluginFormatManager&);   // formats JUCE does not have (CLAP)

    std::unique_ptr<juce::AudioPluginInstance> createInstance (const juce::PluginDescription&, juce::String& error) const;
    void preparePlugin (juce::AudioPluginInstance&, double rate, int blockSize) const;
    void applyLayout (juce::AudioPluginInstance&, int numChannels) const;
    void processChain (juce::AudioBuffer<float>& io, int startSample, int numSamples, bool listening);  // lock must be held
    int totalLatency() const;
    juce::File pluginListFile() const   { return configFolder.getChildFile ("plugins.xml"); }
    juce::File deadMansPedal() const    { return configFolder.getChildFile ("plugin-scan-in-progress.txt"); }
    juce::File masterFile() const       { return configFolder.getChildFile ("master-section.xml"); }

    juce::AudioPluginFormatManager formats;
    juce::KnownPluginList pluginList;
    juce::File configFolder;

    struct ScanItem { juce::AudioPluginFormat* format; juce::String file; };
    juce::Array<ScanItem> scanQueue;
    int scanIndex = 0, pluginsBefore = 0, crashedThisScan = 0;

    std::array<Slot, AudioEngine::numMasterSlots> slots;
    std::array<std::unique_ptr<PluginWindow>, AudioEngine::numMasterSlots> windows;

    juce::CriticalSection lock;  // held by the audio thread while processing, and by edits and renders
    double rate = 48000.0;
    int blockSize = 512;
    int channelCount = 2;
    juce::AudioBuffer<float> scratch;
    juce::MidiBuffer midi;
    std::atomic<float> gain { 1.0f };
    double gainDecibels = 0.0;
    std::atomic<bool> allBypassed { false };
    std::atomic<int> soloSlot { -1 };
    std::array<std::atomic<float>, AudioEngine::numMasterSlots> peaks {};
    static constexpr int maxChannels = 16;
};

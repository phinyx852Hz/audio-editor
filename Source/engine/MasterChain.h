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

    std::vector<PluginParameterInfo> parameters (int slot) const;
    void setParameter (int slot, int index, float value);

    bool canShowWindow (int slot) const;
    void showWindow (int slot);

    void save() const;
    std::string restore();

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

    std::unique_ptr<juce::AudioPluginInstance> createInstance (const juce::PluginDescription&, juce::String& error) const;
    void preparePlugin (juce::AudioPluginInstance&, double rate, int blockSize) const;
    void processChain (juce::AudioBuffer<float>& io, int startSample, int numSamples);  // lock must be held
    int totalLatency() const;
    juce::File pluginListFile() const   { return configFolder.getChildFile ("plugins.xml"); }
    juce::File deadMansPedal() const    { return configFolder.getChildFile ("plugin-scan-in-progress.txt"); }
    juce::File masterFile() const       { return configFolder.getChildFile ("master-section.xml"); }

    juce::AudioPluginFormatManager formats;
    juce::KnownPluginList pluginList;
    juce::File configFolder;

    struct ScanItem { juce::AudioPluginFormat* format; juce::String file; };
    juce::Array<ScanItem> scanQueue;
    int scanIndex = 0, pluginsBefore = 0;

    std::array<Slot, AudioEngine::numMasterSlots> slots;
    std::array<std::unique_ptr<PluginWindow>, AudioEngine::numMasterSlots> windows;

    juce::CriticalSection lock;  // held by the audio thread while processing, and by edits and renders
    double rate = 48000.0;
    int blockSize = 512;
    juce::AudioBuffer<float> scratch;
    juce::MidiBuffer midi;
    std::atomic<float> gain { 1.0f };
    double gainDecibels = 0.0;
    std::atomic<bool> allBypassed { false };
    static constexpr int maxChannels = 16;
};

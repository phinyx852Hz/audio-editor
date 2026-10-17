// SPDX-License-Identifier: AGPL-3.0-only
#pragma once

#include "../core/AudioData.h"
#include "../core/AudioDocument.h"

#include <atomic>
#include <memory>
#include <string>
#include <vector>

// Audio engine built on JUCE. This header contains no JUCE or Qt types so the
// UI layer stays independent of the engine's implementation.

struct AudioOutputChoice
{
    std::string type, name;
};

// Level data for one ~100 ms slice of what was played, for the meters.
struct MeterBlock
{
    static constexpr int maxChannels = 8;
    int numChannels = 0;
    float samplePeak[maxChannels] {};
    float truePeak[maxChannels] {};
    double loudnessPower = 0.0;  // channel-weighted K-weighted mean square (BS.1770)
};

struct SaveOptions
{
    int bitDepth = 24;      // 16, 24 or 32 (32 = float for WAV/AIFF)
    bool dither = true;     // TPDF dither when reducing below 32-bit float
    int oggQuality = 6;     // 0..10 for Ogg Vorbis
};

struct PluginInfo
{
    std::string id;           // stable identifier used to load the plugin
    std::string name, manufacturer, format, category;
};

struct MasterSlotInfo
{
    bool empty = true;
    std::string name, format, pluginId;
    bool bypassed = false;
    int latencySamples = 0;
    bool hasEditor = false;
};

struct PluginParameterInfo
{
    std::string name, valueText;
    float value = 0.0f;       // normalised 0..1
    int numSteps = 0;         // 0 = continuous
    bool isBoolean = false;
};

class AudioEngine
{
public:
    AudioEngine();
    ~AudioEngine();

    // Opens the default output device. Returns an error message, or empty on success.
    std::string openDefaultDevice();

    // Replaces what is played. Safe to call while playing; position is kept.
    void setData (AudioDataPtr);
    bool hasAudio() const;

    void play();
    void stop();
    bool isPlaying() const;

    Frame positionFrames() const;
    void setPositionFrames (Frame);
    double position() const;  // seconds
    double length() const;    // seconds

    std::string deviceDescription() const;
    std::vector<AudioOutputChoice> availableOutputs();
    AudioOutputChoice currentOutput() const;
    std::string selectOutput (const AudioOutputChoice&);

    // Meter data produced since the last call.
    std::vector<MeterBlock> takeMeterBlocks();

    // Runs pending JUCE messages; call regularly from the UI thread.
    void dispatchMessages();

    // Lower-case file extensions without the dot, e.g. "wav".
    // Where the plugin list and Master Section state are kept (~/.config/phinyx).
    void setConfigFolder (const std::string&);

    //==========================================================================
    // Plugins (VST3, LV2, LADSPA)
    std::vector<PluginInfo> knownPlugins() const;
    std::vector<std::string> pluginSearchFolders() const;  // "VST3: /usr/lib/vst3" etc.

    // Incremental scan so the UI can show progress: start, then call next until it returns false.
    int startPluginScan();                                   // returns the number of files to check
    bool scanNextPlugin (std::string& fileBeingScanned);
    std::string finishPluginScan();                          // saves the list; returns a summary

    //==========================================================================
    // Master Section: slots of plugins applied to everything played and rendered,
    // followed by the master gain.
    static constexpr int numMasterSlots = 8;
    std::string loadMasterPlugin (int slot, const std::string& pluginId);  // returns an error or empty
    void clearMasterSlot (int slot);
    void moveMasterSlot (int from, int to);
    void setMasterSlotBypassed (int slot, bool);
    MasterSlotInfo masterSlot (int slot) const;
    void setMasterGainDb (double);
    double masterGainDb() const;
    void setMasterBypassed (bool);
    bool isMasterBypassed() const;
    bool masterIsActive() const;  // any plugin running or gain not 0 dB

    std::vector<PluginParameterInfo> masterSlotParameters (int slot) const;
    void setMasterSlotParameter (int slot, int index, float normalisedValue);

    // The plugin's own window. Needs X11 or XWayland.
    bool canShowPluginWindow (int slot) const;
    void showPluginWindow (int slot);

    void saveMasterSection() const;
    std::string restoreMasterSection();  // returns problems, empty when all went well

    // Processes [start, end) through the Master Section at the file's sample rate,
    // compensating plugin latency. Playback is silent meanwhile. Call from a worker thread.
    AudioDataPtr renderThroughMaster (const AudioData&, Frame start, Frame end, std::string& error,
                                      std::atomic<float>* progress = nullptr, const std::atomic<bool>* cancel = nullptr);

    static std::vector<std::string> readableExtensions();
    static std::vector<std::string> writableExtensions();

    // File I/O, safe to call from worker threads.
    static AudioDataPtr loadFile (const std::string& path, std::vector<Marker>& markersOut, std::string& error,
                                  std::atomic<float>* progress = nullptr);
    static bool saveFile (const std::string& path, const AudioData&, const std::vector<Marker>&,
                          const SaveOptions&, std::string& error);

private:
    struct Impl;
    std::unique_ptr<Impl> impl;
};

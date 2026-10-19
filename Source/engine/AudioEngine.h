// SPDX-License-Identifier: AGPL-3.0-only
#pragma once

#include "../core/AudioData.h"
#include "../core/AudioDocument.h"
#include "../core/Metadata.h"
#include "TagIO.h"

#include <atomic>
#include <cstdint>
#include <map>
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
    static constexpr int maxChannels = 16;  // files with more channels play and meter the first 16
    int numChannels = 0;
    float samplePeak[maxChannels] {};
    float truePeak[maxChannels] {};
    double loudnessPower = 0.0;  // channel-weighted K-weighted mean square (BS.1770)
};

namespace codec { struct Format; }

struct SaveOptions
{
    int bitDepth = 24;      // 16, 24 or 32 (32 = float for WAV/AIFF)
    bool dither = true;     // TPDF dither when reducing below 32-bit float
    int oggQuality = 6;     // 0..10 for Ogg Vorbis
    tagio::Options metadata;   // which schemas are written
    std::map<std::string, std::string> codecSettings;   // compressed formats (see codec::Format); empty: defaults
};

struct PluginInfo
{
    std::string id;           // stable identifier used to load the plugin
    std::string name, manufacturer, format, category;
    std::string file;         // where it was found (empty for built-in ones)
    bool enabled = true;      // turned off in Manage Plug-ins: not offered in the plugin list
};

struct MasterSlotInfo
{
    bool empty = true;
    std::string name, format, pluginId;
    bool bypassed = false;
    int latencySamples = 0;
    bool hasEditor = false;
    int channels = 0;         // how many of the file's channels the plugin processes; the rest pass through
    std::vector<int> mainParameters;   // a stock plugin's main controls (its frame's footer), by parameter index
    bool failed = false;      // the plugin stopped working (it threw, or its bridge went away); it is skipped until reloaded
    std::string failure;      // what it said, e.g. "write: Broken pipe"
};

// Theme colours for the frame stock plugins open in, as 0xAARRGGBB.
struct PluginFrameColours
{
    std::uint32_t window = 0xff202124, panel = 0xff2b2d31, text = 0xffe8e8e8, dimText = 0xff8a8d91,
                  accent = 0xff4a90d9, mid = 0xff45484d, meterBackground = 0xff16171a,
                  meterLow = 0xff3cb371, meterMid = 0xffe0b040, meterHigh = 0xffe04040;
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
    // Plays [start, end) over and over; end <= start turns looping off.
    void setLoop (Frame start, Frame end);

    Frame positionFrames() const;        // where reading is: ahead of the sound by the delays below
    Frame heardPositionFrames() const;   // what is coming out of the speakers now, for the cursor
    int listeningLatency() const;        // the Master Section's delay while listening, in device samples

    // A/B reference: another recording, already at the file's sample rate, heard in
    // place of the file at the same position, with a short crossfade on each switch.
    void setReference (AudioDataPtr, float gain);
    void setReferenceGain (float);
    void setHearingReference (bool);
    bool isHearingReference() const;
    void setReferenceBypassesMaster (bool);   // heard without the Master Section (kept in time with it)
    void setPositionFrames (Frame);
    double position() const;  // seconds
    double length() const;    // seconds

    std::string deviceDescription() const;
    double deviceSampleRate() const;     // what the scope samples run at
    // The file's channel layout when it is not the usual one (dsp::layoutChoices): sets the stereo fold-down.
    void setLayoutName (const std::string&);
    std::vector<AudioOutputChoice> availableOutputs();
    AudioOutputChoice currentOutput() const;
    std::string selectOutput (const AudioOutputChoice&);

    // How a file with more than two channels is heard. By default it is folded
    // down to stereo (right for headphones and most desktops); separateOutputs
    // plays each channel on its own output when the device has enough (ALSA
    // devices get ALSA's speaker order). Files are never changed.
    enum class MonitorMode { stereoDownmix, separateOutputs };
    void setMonitorMode (MonitorMode);
    MonitorMode monitorMode() const;
    int outputChannels() const;   // of the open device
    bool isDownmixing() const;    // what is playing now is being folded down

    // Meter data produced since the last call.
    std::vector<MeterBlock> takeMeterBlocks();
    // Left and right samples heard since the last call, for the goniometer (appended).
    void takeScopeSamples (std::vector<float>& left, std::vector<float>& right);

    // Runs pending JUCE messages; call regularly from the UI thread.
    void dispatchMessages();

    // Lower-case file extensions without the dot, e.g. "wav".
    // Where the plugin list and Master Section state are kept (~/.config/phinyx).
    void setConfigFolder (const std::string&);

    //==========================================================================
    // Plugins (VST3, LV2, LADSPA)
    std::vector<PluginInfo> knownPlugins() const;          // the enabled ones
    std::vector<PluginInfo> allPlugins() const;            // enabled or not (Manage Plug-ins)
    void setPluginEnabled (const std::string& pluginId, bool);
    std::vector<std::string> skippedPluginFiles() const;   // crashed or hung while being checked; not checked again
    void retrySkippedPlugin (const std::string& file);     // checked again at the next scan
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
    std::string reloadMasterSlot (int slot);   // a plugin that stopped working, with its last settings
    void moveMasterSlot (int from, int to);
    void setMasterSlotBypassed (int slot, bool);
    MasterSlotInfo masterSlot (int slot) const;
    int masterChannels() const;  // channels the Master Section runs at (the file's, at least 2)
    void setMasterGainDb (double);
    double masterGainDb() const;
    void setMasterBypassed (bool);
    bool isMasterBypassed() const;
    bool masterIsActive() const;  // any plugin running or gain not 0 dB

    // Solo hears one slot alone in context while playing (-1 = none); renders ignore it.
    void setMasterSoloSlot (int slot);
    int masterSoloSlot() const;
    float takeMasterSlotPeak (int slot);  // a slot's output peak since the last call (linear)

    std::vector<PluginParameterInfo> masterSlotParameters (int slot) const;
    PluginParameterInfo masterSlotParameter (int slot, int index) const;
    // Sets a parameter from typed text such as "-6", "-6 dB" or "RMS"; false when it cannot be understood.
    bool setMasterSlotParameterText (int slot, int index, const std::string& text);
    void setMasterSlotParameter (int slot, int index, float normalisedValue);

    // The plugin's own window. Needs X11 or XWayland.
    bool canShowPluginWindow (int slot) const;
    bool pluginWindowsLost() const;   // the window manager force-closed one; the rest need a restart
    void showPluginWindow (int slot);
    void setPluginFrameColours (const PluginFrameColours&);   // call again when the theme changes

    void saveMasterSection() const;
    std::string restoreMasterSection();  // returns problems, empty when all went well

    // Processes [start, end) through the Master Section at the file's sample rate,
    // compensating plugin latency. Playback is silent meanwhile. Call from a worker thread.
    AudioDataPtr renderThroughMaster (const AudioData&, Frame start, Frame end, std::string& error,
                                      std::atomic<float>* progress = nullptr, const std::atomic<bool>* cancel = nullptr);

    static std::vector<std::string> readableExtensions();
    static std::vector<std::string> writableExtensions();

    // File I/O, safe to call from worker threads. Markers and tags are read and
    // written where the format holds them (see Tags).
    static AudioDataPtr loadFile (const std::string& path, std::vector<Marker>& markersOut, Tags& tagsOut, std::string& error,
                                  std::atomic<float>* progress = nullptr);
    static bool saveFile (const std::string& path, const AudioData&, const std::vector<Marker>&, const Tags&,
                          const SaveOptions&, std::string& error);
    // Which tags a file type keeps ("wav", "flac"...): the rest are not saved there.
    static bool keepsTag (const std::string& extension, const std::string& key);
    static std::vector<meta::Schema> schemasOf (const std::string& extension);
    // The FFmpeg format Save writes for an extension, or null for WAV, AIFF, FLAC and Ogg Vorbis.
    static const codec::Format* codecFormatFor (const std::string& extension);
    // Rewrites only the tags of a saved file (the audio stays as it is, not encoded again).
    static bool writeTags (const std::string& path, const std::vector<Marker>& markers, double sampleRate, const Tags&,
                           const tagio::Options&, std::string& error);

private:
    void useOutputsFor (int channels);

    struct Impl;
    std::unique_ptr<Impl> impl;
};

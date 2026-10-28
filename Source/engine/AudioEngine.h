// SPDX-License-Identifier: AGPL-3.0-only
#pragma once

#include "../core/AudioData.h"
#include "../core/AudioDocument.h"
#include "../core/Dsp.h"
#include "../core/Metadata.h"
#include "TagIO.h"
#include "DeviceDisplay.h"

#include <array>
#include <atomic>
#include <cstdint>
#include <functional>
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
namespace mixer { class Instrument; }
namespace mixer { class Effect; }

struct SaveOptions
{
    int bitDepth = 24;      // 16, 24 or 32 (32 = float for WAV/AIFF)
    bool dither = true;     // TPDF dither when reducing below 32-bit float
    int oggQuality = 6;     // 0..10 for Ogg Vorbis
    tagio::Options metadata;   // which schemas are written
    std::map<std::string, std::string> codecSettings;   // compressed formats (see codec::Format); empty: defaults
    // A loop for samplers and DAWs (WAV only): [loopStart, loopEnd) in the smpl chunk, and the
    // acid chunk marked as a loop of `loopBeats` beats in `beatsPerBar` time, to stretch to the song's tempo.
    Frame loopStart = -1, loopEnd = -1;
    int loopBeats = 0, beatsPerBar = 4;
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
    bool prism = false;       // a Prism: rays of plugins of its own, opened in the Prism editor
    float mix = 1.0f;         // a slot's dry/wet: 1 is all the plugin, 0 all the dry sound (slots only)
};

// A place in the Master Section: a slot, then a (ray, device) step into each Prism on the way.
// {2} is the plugin in slot 3; {2, {{1, 0}}} the first device in the second ray of the Prism there.
struct ChainPath
{
    int slot = -1;
    std::vector<std::pair<int, int>> steps;

    ChainPath inside (int ray, int device) const { auto p = *this; p.steps.push_back ({ ray, device }); return p; }
    ChainPath outer() const { auto p = *this; if (! p.steps.empty()) p.steps.pop_back(); return p; }
    bool topLevel() const { return steps.empty(); }
    bool operator== (const ChainPath&) const = default;
};

// A Prism divides the signal among its rays (each a row of plugins, a Prism among them
// if wanted), then joins them again. Eight macro knobs each turn any number of the
// parameters inside it, over a range of their own, and can follow a MIDI controller.
struct PrismMapping
{
    int ray = 0, device = 0, parameter = 0;
    std::string deviceName, parameterName;
    float low = 0.0f, high = 1.0f;   // the parameter's normalised value at the macro's start and end
};

struct PrismMacroInfo
{
    std::string name, valueText;
    float value = 0.0f;
    int controller = -1;      // MIDI CC it follows, -1 for none
    int channel = 0;          // 1..16, 0 for any
    std::vector<PrismMapping> mappings;
};

struct PrismRayInfo
{
    std::string name;         // as shown: its own name, or what the split gives it ("Low", "Side")
    bool named = false;       // named by the user
    double gainDb = 0.0;
    bool muted = false, soloed = false;
    bool heard = true;        // the split feeds it (Mid/Side feeds two rays, Bands up to four)
    int latencySamples = 0;
    std::vector<MasterSlotInfo> devices;
};

struct PrismInfo
{
    static constexpr int numMacros = 8;
    static constexpr int maxRays = 8;
    enum Split { parallel, bands, midSide, leftRight };

    bool valid = false;
    std::uint64_t uid = 0;    // the same for as long as this Prism exists, wherever it moves
    std::string name;
    int split = parallel;
    std::array<double, 3> crossoverHz {};
    std::vector<PrismRayInfo> rays;
    std::array<PrismMacroInfo, numMacros> macros;
    int latencySamples = 0;
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

    // Plays something live in place of the file (the Tracker): `render` fills left and
    // right at deviceSampleRate() on the audio thread. An empty function hands back to the file.
    using LiveRender = std::function<void (float* left, float* right, int numFrames)>;
    void setLiveSource (LiveRender render);
    bool hasLiveSource() const;
    // An instrument (the Sampler), added on top of the file, the Tracker or silence;
    // stereo at deviceSampleRate(). Empty: none.
    void setInstrument (LiveRender render);
    bool hasInstrument() const;
    // Spatial mixing's speakers (mixer::Engine::takeSpeakers): `feed` adds to the device's outputs
    // (outs: numOuts channels, frames long) on the audio thread, after everything else. The device
    // opens `outputsNeeded` outputs when it has them. Empty: none.
    using SpeakerFeed = std::function<void (float* const* outs, int numOuts, int frames)>;
    void setSpeakerFeed (SpeakerFeed feed, int outputsNeeded);
    // A sound played once on top of everything (the Φ synth's sound as the splash opens):
    // `render` adds to left and right at deviceSampleRate() and returns false once it has ended.
    using OneShot = std::function<bool (float* left, float* right, int numFrames)>;
    void playOneShot (OneShot render);
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
    // How much of each audio block's time the audio thread takes, 0 to 1; near 1 the sound drops out.
    double audioLoad() const;
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

    // The program as heard (after the Master Section, the Tracker, the Mixer and the
    // instruments), in the file's own channels, for the Performance recorder. While started,
    // the audio thread keeps about 5 s of it (at 48 kHz) for takeProgram to empty, from any one thread.
    static constexpr int programChannels = 8;   // per frame; the channels past numChannels are silent
    void startProgramCapture();
    void stopProgramCapture();
    // Appends the frames heard since the last call, programChannels values each, at
    // deviceSampleRate(). Returns the frame count; numChannels is how many carry sound (2 to 8).
    int takeProgram (std::vector<float>& interleaved, int& numChannels);
    uint64_t programFramesLost() const;   // takeProgram was not called often enough

    // Runs pending JUCE messages; call regularly from the UI thread.
    void dispatchMessages();

    // Lower-case file extensions without the dot, e.g. "wav".
    // Where the plugin list and Master Section state are kept (~/.config/phi).
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

    // Instrument plugins for the mixer's tracks (PluginInstrument.cpp): MIDI in, stereo out.
    std::vector<PluginInfo> knownInstruments() const;
    // A stock instrument's plugin id, by its key ("granular", see StockPlugins.h); empty when it was not built.
    std::string stockInstrumentId (const char* key) const;
    // The same, looking at the stock folder again first; `file` is where it should be (MasterChain).
    std::string rescanStockInstrument (const char* key, std::string& file);
    // Message thread. Null with `error` when it could not be started.
    std::shared_ptr<mixer::Instrument> loadInstrumentPlugin (const std::string& pluginId, double sampleRate, std::string& error);
    static std::string instrumentPluginId (const mixer::Instrument&);   // empty for the built-in ones
    // An effect plugin (or a built-in or stock effect from knownPlugins) for a patchbay FX bus or a
    // mixer track's inserts (PluginEffect.cpp): stereo in and out. Message thread. Null with `error`
    // when it could not be started.
    std::shared_ptr<mixer::Effect> loadEffectPlugin (const std::string& pluginId, double sampleRate, std::string& error);

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
    bool masterIsActive() const;  // any plugin running, gain not 0 dB or the final dither on
    // The final dither, last in the Master Section after the gain: 16, 20 or 24 bits (0 = off).
    void setMasterDither (int bits, dsp::Dither);
    int masterDitherBits() const;
    dsp::Dither masterDitherType() const;

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

    // Any device in the chain, inside Prisms too (see ChainPath); a path with no steps is a slot.
    MasterSlotInfo chainDevice (const ChainPath&) const;
    std::string chainDeviceState (const ChainPath&) const;   // its settings, as mixer::Effect::state (for a copy to measure)
    std::vector<PluginParameterInfo> chainParameters (const ChainPath&) const;
    PluginParameterInfo chainParameter (const ChainPath&, int index) const;
    DeviceDisplay chainDisplay (const ChainPath&) const;   // a Φ effect's picture; Kind::none for others
    void setChainParameter (const ChainPath&, int index, float normalisedValue);
    bool setChainParameterText (const ChainPath&, int index, const std::string& text);
    void setChainBypassed (const ChainPath&, bool);
    void setChainMix (const ChainPath&, float mix);   // a slot's dry/wet, 0..1; Prism devices have none
    std::string reloadChainDevice (const ChainPath&);
    void removeChainDevice (const ChainPath&);
    bool canShowChainWindow (const ChainPath&) const;
    void showChainWindow (const ChainPath&);
    float takeChainPeak (const ChainPath&);

    // Prisms (`prism` is the Prism's own path). A Prism is loaded like a plugin ("Prism", under Built-in).
    PrismInfo prismInfo (const ChainPath& prism) const;
    std::string prismAddDevice (const ChainPath& prism, int ray, int index, const std::string& pluginId);
    void prismMoveDevice (const ChainPath& prism, int fromRay, int from, int toRay, int to);
    int prismAddRay (const ChainPath& prism);   // the new ray, or -1 when there are PrismInfo::maxRays
    void prismRemoveRay (const ChainPath& prism, int ray);
    void prismSetRayGain (const ChainPath& prism, int ray, double db);
    void prismSetRayMuted (const ChainPath& prism, int ray, bool);
    void prismSetRaySoloed (const ChainPath& prism, int ray, bool);
    void prismRenameRay (const ChainPath& prism, int ray, const std::string&);   // empty: named by the split again
    void prismRename (const ChainPath& prism, const std::string&);
    void prismSetSplit (const ChainPath& prism, int split);   // PrismInfo::Split; adds the rays it needs
    float prismTakeRayPeak (const ChainPath& prism, int ray);
    // Puts devices [first, last] of a ray into a new Prism in their place, or a one-ray Prism's devices back.
    std::string prismGroup (const ChainPath& prism, int ray, int first, int last);
    std::string prismUngroup (const ChainPath& device);
    std::string groupMasterSlots (int first, int last);
    std::string ungroupMasterSlot (int slot);
    // Macros: their values are the Prism's parameters 0 to 7.
    void prismRenameMacro (const ChainPath& prism, int macro, const std::string&);
    // Maps a parameter of a device in one of the Prism's rays to a macro (normalised low and high ends).
    std::string prismMap (const ChainPath& prism, int macro, int ray, int device, int parameter, float low = 0.0f, float high = 1.0f);
    void prismUnmap (const ChainPath& prism, int macro, int mapping);
    void prismSetMappingRange (const ChainPath& prism, int macro, int mapping, float low, float high);
    // MIDI learn: the next controller moved is followed by this macro (macro -1 stops waiting).
    void prismLearnController (const ChainPath& prism, int macro);
    bool prismLearning (const ChainPath& prism, int macro) const;
    void prismForgetController (const ChainPath& prism, int macro);
    // A MIDI controller moved (call on the message thread). True when a macro waiting to learn took it.
    bool midiController (int channel, int number, int value);

    void saveMasterSection() const;
    std::string restoreMasterSection();  // returns problems, empty when all went well

    // Processes [start, end) through the Master Section at the file's sample rate,
    // compensating plugin latency. Playback is silent meanwhile. Call from a worker thread.
    // Exports pass withDither false: they round to their own bit depth as the very last step.
    AudioDataPtr renderThroughMaster (const AudioData&, Frame start, Frame end, std::string& error,
                                      std::atomic<float>* progress = nullptr, const std::atomic<bool>* cancel = nullptr,
                                      bool withDither = true);

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
    class MasterChain& masterChain();
    void useOutputsFor (int channels);
    void updateInstrumentChannels();

    struct Impl;
    std::unique_ptr<Impl> impl;
};

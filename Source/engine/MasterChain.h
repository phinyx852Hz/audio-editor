// SPDX-License-Identifier: AGPL-3.0-only
#pragma once

#include "AudioEngine.h"
#include "StockPlugins.h"

#include <juce_audio_processors/juce_audio_processors.h>

#include <array>
#include <atomic>
#include <functional>
#include <memory>
#include <mutex>

class PluginWindow;
class PrismProcessor;

// Plugin hosting for the Master Section. Internal to the engine; the UI talks
// to it through AudioEngine.
class MasterChain
{
public:
    MasterChain();
    ~MasterChain();

    void setConfigFolder (const juce::File&);

    // Plugin list
    std::vector<PluginInfo> knownPlugins() const;    // enabled only
    std::vector<PluginInfo> allPlugins() const;     // effects, enabled or not
    std::vector<PluginInfo> instruments() const;    // instrument plugins, enabled only (the mixer's tracks)
    std::string stockInstrumentId (const char* key) const;   // a stock instrument's plugin id; empty when it was not built
    const stock::Info* stockInfo (const std::string& pluginId) const;   // nullptr when not a stock plugin
    // An instrument for a mixer track: stereo out, MIDI in, prepared; nullptr with `error` if that failed.
    std::unique_ptr<juce::AudioPluginInstance> createInstrument (const std::string& pluginId, double sampleRate, int maxBlock,
                                                                 juce::String& error) const;
    // An effect for a patchbay FX bus: stereo in and out, prepared; nullptr with `error` if that failed.
    std::unique_ptr<juce::AudioPluginInstance> createEffect (const std::string& pluginId, double sampleRate, int maxBlock,
                                                             juce::String& error) const;
    void setPluginEnabled (const std::string& pluginId, bool);
    std::vector<std::string> skippedFiles() const;
    void retrySkipped (const std::string& file);
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
    std::string reload (int slot);   // a failed plugin, loaded again with its last saved settings
    void move (int from, int to);
    void setBypassed (int slot, bool);
    MasterSlotInfo info (int slot) const;
    void setGainDb (double);
    double gainDb() const { return gainDecibels; }
    void setAllBypassed (bool b) { allBypassed = b; }
    bool isAllBypassed() const   { return allBypassed; }
    bool isActive() const;

    // Final dither: after the master gain, outside the slots, so it never takes one.
    // Rounds to 16, 20 or 24 bits (0 = off) with plain rounding, TPDF or shaped noise.
    void setDither (int bits, dsp::Dither);
    int ditherBits() const { return finalBits.load(); }
    dsp::Dither ditherType() const { return (dsp::Dither) finalType.load(); }

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

    // The same for any place in the chain, inside Prisms too.
    MasterSlotInfo info (const ChainPath&) const;
    std::vector<PluginParameterInfo> parameters (const ChainPath&) const;
    PluginParameterInfo parameter (const ChainPath&, int index) const;
    void setParameter (const ChainPath&, int index, float value);
    bool setParameterFromText (const ChainPath&, int index, const std::string& text);
    void setBypassed (const ChainPath&, bool);
    std::string reload (const ChainPath&);
    void remove (const ChainPath&);
    bool canShowWindow (const ChainPath&) const;
    void showWindow (const ChainPath&);
    float takePeak (const ChainPath&);

    // Prisms. `path` is the Prism itself.
    PrismInfo prismInfo (const ChainPath&) const;
    PrismProcessor* prismAt (const ChainPath&) const;   // nullptr when there is none there
    std::string prismAdd (const ChainPath&, int ray, int index, const std::string& pluginId);
    float takeRayPeak (const ChainPath&, int ray);
    // The plugins in slots [first, last] go into a new Prism in the first one's slot, still running;
    // ungrouping puts a one-ray Prism's plugins back in the chain in its place.
    std::string group (int first, int last);
    std::string ungroup (int slot);
    // A MIDI controller moved: Prism macros that follow it turn, or one waiting to learn takes it.
    // True when it was learned.
    bool controller (int channel, int number, int value);
    void learnController (const ChainPath& prism, int macro);   // macro -1 stops waiting
    bool learning (const ChainPath& prism, int macro) const;

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
    // The delay of what is heard now, in device samples. Bypassed, soloed-out and
    // master-bypassed plugins are replaced by a delay of their latency while
    // listening, so switching them never shifts the sound in time.
    int listeningLatency() const { return heardLatency.load (std::memory_order_relaxed); }

    // `withDither` false leaves out the final dither, for exports that round to their own bit depth last.
    AudioDataPtr render (const AudioData&, Frame start, Frame end, std::string& error,
                         std::atomic<float>* progress, const std::atomic<bool>* cancel, bool withDither = true);

private:
    struct Slot
    {
        std::unique_ptr<juce::AudioPluginInstance> plugin;
        juce::PluginDescription description;
        bool bypassed = false;
        int compareSide = 0;              // A/B compare in the stock plugin frame: 0 = A, 1 = B
        juce::MemoryBlock otherSide;      // the settings of the side not being heard; empty until B is first used
        bool hasEditor = false;           // asked once at load: a VST3 builds its whole editor to answer

        // A plugin that throws (yabridge throws "write: Broken pipe" when its Wine
        // side has gone) is marked failed and never called again: not to process,
        // not for its parameters, not even to be deleted, since its destructor
        // would throw too and end PHINYX. Shared so the slot can move.
        struct Health
        {
            std::atomic<bool> failed { false };
            std::mutex mutex;
            std::string why;
            juce::MemoryBlock lastState;  // from the last save, to reload it with
        };
        std::shared_ptr<Health> health = std::make_shared<Health>();

        bool usable() const { return plugin != nullptr && ! health->failed.load (std::memory_order_acquire); }
    };

    // Runs a call into a slot's plugin; false (and the slot marked failed) if it threw.
    template <typename Fn>
    bool guard (const Slot& s, Fn&& fn) const
    {
        try
        {
            fn();
            return true;
        }
        catch (const std::exception& e) { fail (s, e.what()); }
        catch (...)                      { fail (s, "an unknown error"); }
        return false;
    }
    static void fail (const Slot&, const std::string& why);
    void discard (size_t index);
    // Creates, restores and prepares a plugin; nullptr with `error` if that failed or threw.
    std::unique_ptr<juce::AudioPluginInstance> start (const juce::PluginDescription&, const juce::MemoryBlock* state,
                                                      bool& editor, juce::String& error) const;
    static bool setParameterFromTextUnguarded (juce::AudioPluginInstance&, int index, const std::string& text);
    static PluginParameterInfo describe (juce::AudioProcessorParameter&);

    // Where a path leads: the plugin there (nullptr when it failed), and the Prism holding it.
    struct Place
    {
        juce::AudioPluginInstance* plugin = nullptr;
        PrismProcessor* owner = nullptr;   // nullptr for a slot of the chain itself
        int slot = -1, ray = -1, device = -1;
        bool exists = false;
    };
    Place locate (const ChainPath&) const;
    // Runs a call into the plugin there; false (and it marked failed) if it threw.
    bool call (const Place&, const std::function<void (juce::AudioPluginInstance&)>&) const;
    std::unique_ptr<juce::AudioPluginInstance> makePrism() const;
    ChainPath learnPath;
    int learnMacro = -1;
    void openWindow (int slot);   // empties a slot; a failed plugin and its window are let go without being deleted

    static void addExtraFormats (juce::AudioPluginFormatManager&);   // formats JUCE does not have: CLAP
    // Runs `scanInThisProcess` in a copy of PHINYX; false if that crashed or hung.
    static bool scanOutOfProcess (const juce::AudioPluginFormat&, const juce::String& file, int timeoutMs,
                                  juce::OwnedArray<juce::PluginDescription>& found);
    void loadStockPlugins();
    std::unique_ptr<juce::PluginDescription> findDescription (const std::string& pluginId) const;
    int slotOf (const juce::AudioPluginInstance*) const;
    void setCompareSide (int slot, int side);
    void copyCompareSide (int slot);

    std::vector<PluginInfo> listPlugins (bool instruments) const;
    std::unique_ptr<juce::AudioPluginInstance> createInstance (const juce::PluginDescription&, juce::String& error) const;
    void preparePlugin (juce::AudioPluginInstance&, double rate, int blockSize) const;
    void applyLayout (juce::AudioPluginInstance&, int numChannels) const;
    void processChain (juce::AudioBuffer<float>& io, int startSample, int numSamples, bool listening);  // lock must be held
    void prepareDelays();   // lock must be held
    // Writes the slot's input into its delay line; with `replace` the block becomes the input from `latency` samples ago.
    void delaySlot (size_t index, int latency, int numChannels, int numSamples, bool replace);
    int totalLatency() const;
    juce::File pluginListFile() const   { return configFolder.getChildFile ("plugins.xml"); }
    juce::File deadMansPedal() const    { return configFolder.getChildFile ("plugin-scan-in-progress.txt"); }
    juce::File masterFile() const       { return configFolder.getChildFile ("master-section.xml"); }
    juce::File disabledListFile() const { return configFolder.getChildFile ("disabled-plugins.txt"); }
    juce::StringArray disabled;        // plugin ids turned off in Manage Plug-ins
    juce::File stockListFile() const    { return configFolder.getChildFile ("stock-plugins.xml"); }

    juce::AudioPluginFormatManager formats;
    juce::KnownPluginList pluginList;
    juce::KnownPluginList stockList;   // the stock plugins found in stock::folder()
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
    std::atomic<int> finalBits { 0 };
    std::atomic<int> finalType { (int) dsp::Dither::tpdf };
    // One quantizer for listening and one for renders, each set up again when the settings change.
    struct FinalDither
    {
        dsp::Quantizer quantizer;
        int applied = -1;
        void process (int bits, dsp::Dither, float* const* channels, int numChannels, int numFrames);
    };
    FinalDither liveDither, renderDither;
    std::atomic<int> soloSlot { -1 };
    std::array<std::atomic<float>, AudioEngine::numMasterSlots> peaks {};
    std::array<std::array<std::atomic<float>, 4>, AudioEngine::numMasterSlots> frameLevels {};   // in L/R, out L/R peaks
    static constexpr int maxChannels = 16;
    static constexpr int maxDelay = 1 << 15;   // longest plugin latency kept in time while bypassed (0.68 s at 48 kHz)
    std::array<juce::AudioBuffer<float>, AudioEngine::numMasterSlots> delays;   // per slot, maxDelay long
    std::array<int, AudioEngine::numMasterSlots> delayWrite {};
    std::atomic<int> heardLatency { 0 };
};

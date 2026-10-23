// SPDX-License-Identifier: AGPL-3.0-only
#pragma once

#include "AudioEngine.h"
#include "../core/PrismSplit.h"

#include <juce_audio_processors/juce_audio_processors.h>

#include <array>
#include <atomic>
#include <functional>
#include <memory>
#include <mutex>
#include <vector>

class PluginWindow;

// A Prism: a device in the Device Chain that divides the signal among rays of
// plugins (in parallel, in frequency bands, as mid and side, or left and right),
// keeps the rays in time with each other and joins them again. A ray can hold a
// Prism too. Its first eight parameters are the macros, which turn the parameters
// mapped to them; then come the split and the three crossover frequencies.
//
// Its plugins are created by the Master Section (the factory), so a Prism holds
// whatever the chain can. Changes to the rays happen on the message thread under
// the Prism's lock; the audio thread only tries the lock, and while it cannot get
// it the sound passes through untouched for that block.
class PrismProcessor final : public juce::AudioPluginInstance
{
public:
    static constexpr const char* identifier = "phinyx:prism";   // phinyx: like the other built-ins, from before the rename to PHI
    static constexpr int numMacros = PrismInfo::numMacros;
    static constexpr int maxDevices = 16;   // per ray
    static constexpr int splitParameter = numMacros, firstCrossover = numMacros + 1;

    struct Factory
    {
        // A plugin with its settings, laid out for `numChannels` and prepared; nullptr with `error`.
        std::function<std::unique_ptr<juce::AudioPluginInstance> (const juce::PluginDescription&, const juce::MemoryBlock* state,
                                                                  int numChannels, double rate, int block, juce::String& error)> create;
        // Lays a plugin out for a new channel count (before it is prepared again).
        std::function<void (juce::AudioPluginInstance&, int numChannels)> arrange;
    };

    static juce::PluginDescription description();
    static bool isPrism (const juce::PluginDescription&);

    explicit PrismProcessor (Factory);
    ~PrismProcessor() override;

    std::uint64_t uid() const { return id; }
    PrismInfo info() const;

    bool exists (int ray, int device) const { return find (ray, device) != nullptr; }
    // A device that is a Prism, or nullptr.
    PrismProcessor* prismAt (int ray, int device) const;
    // A device's plugin when it is there and working, or nullptr.
    juce::AudioPluginInstance* plugin (int ray, int device) const;
    MasterSlotInfo deviceInfo (int ray, int device) const;
    // Runs a call into a device's plugin; false (and the device switched off for good) if it threw.
    bool guard (int ray, int device, const std::function<void (juce::AudioPluginInstance&)>&) const;

    // The rays. All of these run on the message thread.
    int numRays() const { return (int) rays.size(); }
    int addRay();   // its index, or -1 when there are maxRays
    void removeRay (int ray);
    void setRayGain (int ray, double db);
    void setRayMuted (int ray, bool);
    void setRaySoloed (int ray, bool);
    void renameRay (int ray, const juce::String&);
    void rename (const juce::String& n) { name = n; }
    void setSplit (PrismInfo::Split);   // adds rays the split needs (two for Mid/Side, three bands)

    std::string addDevice (int ray, int index, const juce::PluginDescription&);   // an error, or empty
    // Takes a plugin that is already running (grouped from the chain).
    void adopt (int ray, std::unique_ptr<juce::AudioPluginInstance>, const juce::PluginDescription&, bool bypassed,
                const juce::MemoryBlock& lastState);
    struct Released
    {
        std::unique_ptr<juce::AudioPluginInstance> plugin;   // nullptr for one that failed (it is let go, not deleted)
        juce::PluginDescription description;
        bool bypassed = false;
        juce::MemoryBlock lastState;
    };
    std::vector<Released> release (int ray);   // gives up a ray's plugins (ungrouping)
    void removeDevice (int ray, int device);
    void moveDevice (int fromRay, int from, int toRay, int to);
    void setDeviceBypassed (int ray, int device, bool);
    std::string reloadDevice (int ray, int device);
    // Puts devices [first, last] of a ray into a new Prism in their place; or the reverse.
    std::string group (int ray, int first, int last);
    std::string ungroup (int ray, int device);
    float takeDevicePeak (int ray, int device);
    float takeRayPeak (int ray);

    bool canShowWindow (int ray, int device) const;
    void showWindow (int ray, int device);   // a second call closes it

    // Macros.
    void renameMacro (int macro, const juce::String&);
    std::string map (int macro, int ray, int device, int parameter, float low, float high);
    void unmap (int macro, int mapping);
    void setMappingRange (int macro, int mapping, float low, float high);
    void setController (int macro, int controller, int channel);
    // A MIDI controller moved (message thread): turns the macros that follow it, here and in the Prisms inside.
    void controller (int channel, int number, int value);
    // Sends the macros' values to their parameters now (message thread), not waiting for the next block.
    void applyMacrosNow();

    // Renders run without the real-time limits: the lock is waited for and ray solo is ignored.
    void setNonRealtime (bool) noexcept override;

    // AudioPluginInstance
    void fillInPluginDescription (juce::PluginDescription& d) const override { d = description(); }
    const juce::String getName() const override { return name; }
    bool isBusesLayoutSupported (const BusesLayout&) const override;
    void prepareToPlay (double sampleRate, int maxBlock) override;
    void releaseResources() override;
    using AudioPluginInstance::processBlock;
    void processBlock (juce::AudioBuffer<float>&, juce::MidiBuffer&) override;
    double getTailLengthSeconds() const override;
    bool acceptsMidi() const override  { return false; }
    bool producesMidi() const override { return false; }
    juce::AudioProcessorEditor* createEditor() override { return nullptr; }
    bool hasEditor() const override { return false; }
    int getNumPrograms() override { return 1; }
    int getCurrentProgram() override { return 0; }
    void setCurrentProgram (int) override {}
    const juce::String getProgramName (int) override { return {}; }
    void changeProgramName (int, const juce::String&) override {}
    void getStateInformation (juce::MemoryBlock&) override;
    void setStateInformation (const void*, int) override;

private:
    struct Health
    {
        std::atomic<bool> failed { false };
        std::mutex mutex;
        std::string why;
        juce::MemoryBlock lastState;
    };

    struct Device
    {
        int id = 0;   // stable while it exists, so mappings follow it when it moves
        std::unique_ptr<juce::AudioPluginInstance> plugin;
        juce::PluginDescription description;
        bool bypassed = false;
        bool hasEditor = false;
        std::shared_ptr<Health> health = std::make_shared<Health>();
        prism::Delay bypassDelay;   // a switched-off plugin is heard as a delay of its latency
        std::atomic<float> peak { 0.0f };
        std::unique_ptr<PluginWindow> window;

        bool usable() const { return plugin != nullptr && ! health->failed.load (std::memory_order_acquire); }
    };

    struct Ray
    {
        juce::String name;
        double gainDb = 0.0;
        std::atomic<float> gain { 1.0f };
        bool muted = false, soloed = false;
        std::vector<std::unique_ptr<Device>> devices;
        juce::AudioBuffer<float> buffer;
        prism::Delay align;   // lines the ray up with the slowest one
        std::atomic<float> peak { 0.0f };
    };

    struct Mapping
    {
        int deviceId = 0, parameter = 0;
        float low = 0.0f, high = 1.0f;
    };

    struct Macro
    {
        juce::String name;
        std::vector<Mapping> mappings;
        int controller = -1, channel = 0;
        float applied = -1.0f;   // the value last sent to its parameters
    };

    static void fail (const Device&, const std::string& why);
    static void letGo (std::unique_ptr<Device>);   // deletes it, unless its plugin failed (that would call into it again)
    Device* find (int ray, int device) const;
    Device* findId (int deviceId) const;
    std::unique_ptr<Device> makeDevice (std::unique_ptr<juce::AudioPluginInstance>, const juce::PluginDescription&) ;
    void prepareDevice (Device&);
    void openWindow (Device&);
    int rayLatency (const Ray&) const;
    juce::String rayName (int ray) const;
    void applyMacros();   // lock held
    void forgetMappings (int deviceId);
    PrismInfo::Split currentSplit() const;
    std::array<double, 3> crossovers() const;
    std::unique_ptr<juce::XmlElement> toXml() const;

    Factory factory;
    const std::uint64_t id;
    juce::String name { "Prism" };
    std::vector<std::unique_ptr<Ray>> rays;
    std::array<Macro, numMacros> macros;
    std::array<juce::AudioParameterFloat*, numMacros> macroParameters {};
    juce::AudioParameterChoice* split = nullptr;
    std::array<juce::AudioParameterFloat*, 3> crossoverParameters {};
    int nextDeviceId = 1;

    juce::CriticalSection lock;
    std::atomic<bool> offline { false };
    double rate = 48000.0;
    int blockSize = 512, channels = 2;
    prism::Splitter splitter;
    juce::MidiBuffer midi;
    std::shared_ptr<bool> alive = std::make_shared<bool> (true);   // for window callbacks that come after it is gone
    static constexpr int maxDelay = 1 << 15;
};

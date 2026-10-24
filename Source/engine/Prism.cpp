// SPDX-License-Identifier: AGPL-3.0-only
#include "Prism.h"
#include "BuiltinEffects.h"
#include "PluginWindow.h"
#include "StockFrame.h"
#include "StockPlugins.h"
#include "X11Windows.h"

#include <algorithm>
#include <cmath>
#include <cstdlib>

namespace
{
    std::atomic<std::uint64_t> nextPrismId { 1 };

    // Crossovers from 20 Hz to 20 kHz, evenly spread on a log scale.
    juce::NormalisableRange<float> frequencyRange()
    {
        return { 20.0f, 20000.0f,
                 [] (float, float, float x) { return 20.0f * std::pow (1000.0f, std::clamp (x, 0.0f, 1.0f)); },
                 [] (float, float, float hz) { return std::log (std::clamp (hz, 20.0f, 20000.0f) / 20.0f) / std::log (1000.0f); },
                 [] (float, float, float hz) { return std::clamp (hz, 20.0f, 20000.0f); } };
    }

    juce::String hertz (float hz)
    {
        return hz >= 1000.0f ? juce::String (hz / 1000.0f, hz >= 10000.0f ? 1 : 2) + " kHz" : juce::String (juce::roundToInt (hz)) + " Hz";
    }

    float parseHertz (const juce::String& text)
    {
        const auto v = text.retainCharacters ("+-.0123456789eE").getFloatValue();
        return text.containsIgnoreCase ("k") ? v * 1000.0f : v;
    }

    constexpr const char* bandNames[4][4] {
        { "Full" }, { "Low", "High" }, { "Low", "Mid", "High" }, { "Low", "Low Mid", "High Mid", "High" } };
}

//==============================================================================
juce::PluginDescription PrismProcessor::description()
{
    juce::PluginDescription p;
    p.name = p.descriptiveName = "Prism";
    p.pluginFormatName = builtin::formatName;
    p.category = "Container";
    p.manufacturerName = APP_DISPLAY_NAME;
    p.version = "1";
    p.fileOrIdentifier = identifier;
    p.uniqueId = p.deprecatedUid = 0x5048505a;  // "PHPZ"
    p.isInstrument = false;
    p.numInputChannels = p.numOutputChannels = 2;
    return p;
}

bool PrismProcessor::isPrism (const juce::PluginDescription& d)
{
    return builtin::isBuiltin (d) && d.fileOrIdentifier == identifier;
}

PrismProcessor::PrismProcessor (Factory f)
    : AudioPluginInstance (BusesProperties().withInput ("Input", juce::AudioChannelSet::stereo())
                                            .withOutput ("Output", juce::AudioChannelSet::stereo())),
      factory (std::move (f)), id (nextPrismId++)
{
    for (int m = 0; m < numMacros; ++m)
    {
        auto p = std::make_unique<juce::AudioParameterFloat> (juce::ParameterID { "macro" + juce::String (m + 1), 1 },
            "Macro " + juce::String (m + 1), juce::NormalisableRange<float> (0.0f, 1.0f), 0.0f,
            juce::AudioParameterFloatAttributes()
                .withStringFromValueFunction ([] (float v, int) { return juce::String (juce::roundToInt (v * 100.0f)) + "%"; })
                .withValueFromStringFunction ([] (const juce::String& t) { return t.retainCharacters ("+-.0123456789eE").getFloatValue() / 100.0f; }));
        macroParameters[(size_t) m] = p.get();
        macros[(size_t) m].name = "Macro " + juce::String (m + 1);
        addHostedParameter (std::move (p));
    }

    auto s = std::make_unique<juce::AudioParameterChoice> (juce::ParameterID { "split", 1 }, "Split",
        juce::StringArray { "Parallel", "Bands", "Mid/Side", "Left/Right" }, 0);
    split = s.get();
    addHostedParameter (std::move (s));

    static constexpr float defaults[] { 150.0f, 1500.0f, 6000.0f };
    for (int i = 0; i < 3; ++i)
    {
        auto c = std::make_unique<juce::AudioParameterFloat> (juce::ParameterID { "crossover" + juce::String (i + 1), 1 },
            "Crossover " + juce::String (i + 1), frequencyRange(), defaults[i],
            juce::AudioParameterFloatAttributes()
                .withStringFromValueFunction ([] (float v, int) { return hertz (v); })
                .withValueFromStringFunction ([] (const juce::String& t) { return parseHertz (t); }));
        crossoverParameters[(size_t) i] = c.get();
        addHostedParameter (std::move (c));
    }

    rays.push_back (std::make_unique<Ray>());
}

PrismProcessor::~PrismProcessor()
{
    *alive = false;
    for (auto& r : rays)
        for (auto& d : r->devices)
            letGo (std::move (d));
}

void PrismProcessor::fail (const Device& d, const std::string& why)
{
    {
        const std::lock_guard<std::mutex> l (d.health->mutex);
        if (d.health->why.empty())
            d.health->why = why.empty() ? "an unknown error" : why;
    }
    d.health->failed.store (true, std::memory_order_release);
}

void PrismProcessor::letGo (std::unique_ptr<Device> d)
{
    if (d == nullptr)
        return;
    if (d->plugin != nullptr && d->health->failed.load())
    {
        // Its destructor would call into a plugin that already threw.
        if (auto* w = d->window.release())
            try { w->setVisible (false); } catch (...) {}
        (void) d->plugin.release();
    }
    d->window.reset();
    d.reset();
}

PrismProcessor::Device* PrismProcessor::find (int ray, int device) const
{
    if (ray < 0 || ray >= (int) rays.size())
        return nullptr;
    const auto& devices = rays[(size_t) ray]->devices;
    return device >= 0 && device < (int) devices.size() ? devices[(size_t) device].get() : nullptr;
}

PrismProcessor::Device* PrismProcessor::findId (int deviceId) const
{
    for (const auto& r : rays)
        for (const auto& d : r->devices)
            if (d->id == deviceId)
                return d.get();
    return nullptr;
}

juce::AudioPluginInstance* PrismProcessor::plugin (int ray, int device) const
{
    auto* d = find (ray, device);
    return d != nullptr && d->usable() ? d->plugin.get() : nullptr;
}

PrismProcessor* PrismProcessor::prismAt (int ray, int device) const
{
    return dynamic_cast<PrismProcessor*> (plugin (ray, device));
}

bool PrismProcessor::guard (int ray, int device, const std::function<void (juce::AudioPluginInstance&)>& fn) const
{
    auto* d = find (ray, device);
    if (d == nullptr || ! d->usable())
        return false;
    try
    {
        fn (*d->plugin);
        return true;
    }
    catch (const std::exception& e) { fail (*d, e.what()); }
    catch (...)                      { fail (*d, "an unknown error"); }
    return false;
}

//==============================================================================
PrismInfo::Split PrismProcessor::currentSplit() const
{
    return (PrismInfo::Split) std::clamp (split->getIndex(), 0, prism::numSplits - 1);
}

std::array<double, 3> PrismProcessor::crossovers() const
{
    return { (double) crossoverParameters[0]->get(), (double) crossoverParameters[1]->get(), (double) crossoverParameters[2]->get() };
}

juce::String PrismProcessor::rayName (int ray) const
{
    if (ray < 0 || ray >= (int) rays.size())
        return {};
    if (rays[(size_t) ray]->name.isNotEmpty())
        return rays[(size_t) ray]->name;

    const auto mode = currentSplit();
    const auto heard = prism::raysHeard ((prism::Split) mode, (int) rays.size());
    if (ray < heard)
    {
        if (mode == PrismInfo::bands)
            return bandNames[(size_t) heard - 1][(size_t) ray];
        if (mode == PrismInfo::midSide)
            return ray == 0 ? "Mid" : "Side";
        if (mode == PrismInfo::leftRight)
            return ray == 0 ? "Left" : "Right";
    }
    return "Ray " + juce::String (ray + 1);
}

int PrismProcessor::rayLatency (const Ray& r) const
{
    int total = 0;
    for (const auto& d : r.devices)
        if (d->usable())
            total += std::max (0, d->plugin->getLatencySamples());
    return std::min (total, maxDelay - 1);
}

MasterSlotInfo PrismProcessor::deviceInfo (int ray, int device) const
{
    MasterSlotInfo i;
    auto* d = find (ray, device);
    if (d == nullptr)
        return i;

    i.empty = false;
    i.name = d->description.name.toStdString();
    const auto* stockInfo = stock::find (d->description);
    i.format = stockInfo != nullptr ? builtin::formatName : d->description.pluginFormatName.toStdString();
    i.pluginId = d->description.createIdentifierString().toStdString();
    i.bypassed = d->bypassed;
    i.prism = isPrism (d->description);
    if (d->usable())
        guard (ray, device, [&] (juce::AudioPluginInstance& p)
        {
            if (stockInfo != nullptr)
                i.mainParameters = stock::footerParameters (*stockInfo, p);
            if (auto* inner = dynamic_cast<PrismProcessor*> (&p))
                i.name = inner->name.toStdString();
            i.latencySamples = p.getLatencySamples();
            i.hasEditor = d->hasEditor;
            i.channels = std::min (channels, std::max (p.getTotalNumInputChannels(), p.getTotalNumOutputChannels()));
        });
    if (! d->usable())
    {
        i.failed = true;
        i.hasEditor = false;
        i.mainParameters.clear();
        const std::lock_guard<std::mutex> l (d->health->mutex);
        i.failure = d->health->why;
    }
    return i;
}

PrismInfo PrismProcessor::info() const
{
    PrismInfo p;
    p.valid = true;
    p.uid = id;
    p.name = name.toStdString();
    p.split = currentSplit();
    p.crossoverHz = crossovers();
    const auto heard = prism::raysHeard ((prism::Split) p.split, (int) rays.size());
    int slowest = 0;
    for (int r = 0; r < (int) rays.size(); ++r)
    {
        const auto& ray = *rays[(size_t) r];
        PrismRayInfo ri;
        ri.name = rayName (r).toStdString();
        ri.named = ray.name.isNotEmpty();
        ri.gainDb = ray.gainDb;
        ri.muted = ray.muted;
        ri.soloed = ray.soloed;
        ri.heard = r < heard;
        ri.latencySamples = rayLatency (ray);
        for (int d = 0; d < (int) ray.devices.size(); ++d)
            ri.devices.push_back (deviceInfo (r, d));
        if (ri.heard)
            slowest = std::max (slowest, ri.latencySamples);
        p.rays.push_back (std::move (ri));
    }
    p.latencySamples = slowest;

    for (int m = 0; m < numMacros; ++m)
    {
        const auto& macro = macros[(size_t) m];
        auto& mi = p.macros[(size_t) m];
        mi.name = macro.name.toStdString();
        mi.value = macroParameters[(size_t) m]->get();
        mi.valueText = macroParameters[(size_t) m]->getCurrentValueAsText().toStdString();
        mi.controller = macro.controller;
        mi.channel = macro.channel;
        for (const auto& map : macro.mappings)
        {
            PrismMapping pm;
            pm.low = map.low;
            pm.high = map.high;
            pm.parameter = map.parameter;
            for (int r = 0; r < (int) rays.size(); ++r)
                for (int d = 0; d < (int) rays[(size_t) r]->devices.size(); ++d)
                    if (const auto& dev = rays[(size_t) r]->devices[(size_t) d]; dev->id == map.deviceId)
                    {
                        pm.ray = r;
                        pm.device = d;
                        pm.deviceName = deviceInfo (r, d).name;
                        if (dev->usable())
                            guard (r, d, [&] (juce::AudioPluginInstance& plug)
                            {
                                const auto& params = plug.getParameters();
                                if (map.parameter >= 0 && map.parameter < params.size())
                                    pm.parameterName = params[map.parameter]->getName (64).toStdString();
                            });
                    }
            mi.mappings.push_back (pm);
        }
    }
    return p;
}

//==============================================================================
int PrismProcessor::addRay()
{
    if ((int) rays.size() >= PrismInfo::maxRays)
        return -1;
    auto ray = std::make_unique<Ray>();
    ray->buffer.setSize (channels, blockSize);
    ray->align.prepare (channels, maxDelay);
    const juce::ScopedLock sl (lock);
    rays.push_back (std::move (ray));
    return (int) rays.size() - 1;
}

void PrismProcessor::removeRay (int ray)
{
    if (ray < 0 || ray >= (int) rays.size() || rays.size() <= 1)
        return;

    std::unique_ptr<Ray> old;
    {
        const juce::ScopedLock sl (lock);
        old = std::move (rays[(size_t) ray]);
        rays.erase (rays.begin() + ray);
    }
    for (auto& d : old->devices)
    {
        forgetMappings (d->id);
        letGo (std::move (d));
    }
}

void PrismProcessor::setRayGain (int ray, double db)
{
    if (ray >= 0 && ray < (int) rays.size())
    {
        auto& r = *rays[(size_t) ray];
        r.gainDb = std::clamp (db, -60.0, 12.0);
        r.gain = r.gainDb <= -60.0 ? 0.0f : (float) std::pow (10.0, r.gainDb / 20.0);
    }
}

void PrismProcessor::setRayMuted (int ray, bool b)
{
    if (ray >= 0 && ray < (int) rays.size())
    {
        const juce::ScopedLock sl (lock);
        rays[(size_t) ray]->muted = b;
    }
}

void PrismProcessor::setRaySoloed (int ray, bool b)
{
    if (ray >= 0 && ray < (int) rays.size())
    {
        const juce::ScopedLock sl (lock);
        rays[(size_t) ray]->soloed = b;
    }
}

void PrismProcessor::renameRay (int ray, const juce::String& n)
{
    if (ray >= 0 && ray < (int) rays.size())
        rays[(size_t) ray]->name = n.trim();
}

void PrismProcessor::setSplit (PrismInfo::Split s)
{
    const auto wanted = s == PrismInfo::bands ? 3 : s == PrismInfo::midSide || s == PrismInfo::leftRight ? 2 : 1;
    while ((int) rays.size() < wanted && addRay() >= 0) {}
    split->setValueNotifyingHost (split->convertTo0to1 ((float) s));
}

//==============================================================================
std::unique_ptr<PrismProcessor::Device> PrismProcessor::makeDevice (std::unique_ptr<juce::AudioPluginInstance> p,
                                                                    const juce::PluginDescription& d)
{
    auto device = std::make_unique<Device>();
    device->id = nextDeviceId++;
    device->description = d;
    device->plugin = std::move (p);
    if (device->plugin != nullptr)
    {
        try { device->hasEditor = device->plugin->hasEditor(); }
        catch (...) { fail (*device, "an unknown error"); }
    }
    device->bypassDelay.prepare (channels, maxDelay);
    return device;
}

void PrismProcessor::prepareDevice (Device& d)
{
    d.bypassDelay.prepare (channels, maxDelay);
    if (! d.usable())
        return;
    try
    {
        d.plugin->releaseResources();
        if (factory.arrange)
            factory.arrange (*d.plugin, channels);
        d.plugin->setRateAndBufferSizeDetails (rate, blockSize);
        d.plugin->prepareToPlay (rate, blockSize);
        if (auto* inner = dynamic_cast<PrismProcessor*> (d.plugin.get()))
            inner->setNonRealtime (offline.load());
    }
    catch (const std::exception& e) { fail (d, e.what()); }
    catch (...)                      { fail (d, "an unknown error"); }
}

std::string PrismProcessor::addDevice (int ray, int index, const juce::PluginDescription& d)
{
    if (ray < 0 || ray >= (int) rays.size())
        return "No such ray";
    if ((int) rays[(size_t) ray]->devices.size() >= maxDevices)
        return "This ray is full";
    if (! factory.create)
        return "Plugins cannot be loaded here";

    juce::String error;
    auto plugin = factory.create (d, nullptr, channels, rate, blockSize, error);
    if (plugin == nullptr)
        return ("Could not load " + d.name + ": " + error).toStdString();

    auto device = makeDevice (std::move (plugin), d);
    auto& devices = rays[(size_t) ray]->devices;
    const juce::ScopedLock sl (lock);
    devices.insert (devices.begin() + std::clamp (index, 0, (int) devices.size()), std::move (device));
    return {};
}

void PrismProcessor::adopt (int ray, std::unique_ptr<juce::AudioPluginInstance> p, const juce::PluginDescription& d, bool bypassed,
                            const juce::MemoryBlock& lastState)
{
    if (ray < 0 || ray >= (int) rays.size())
        return;
    auto device = makeDevice (std::move (p), d);
    device->bypassed = bypassed;
    device->health->lastState = lastState;
    if (device->plugin == nullptr)
        fail (*device, "it could not be loaded");
    prepareDevice (*device);
    const juce::ScopedLock sl (lock);
    rays[(size_t) ray]->devices.push_back (std::move (device));
}

std::vector<PrismProcessor::Released> PrismProcessor::release (int ray)
{
    std::vector<Released> result;
    if (ray < 0 || ray >= (int) rays.size())
        return result;

    std::vector<std::unique_ptr<Device>> taken;
    {
        const juce::ScopedLock sl (lock);
        taken = std::move (rays[(size_t) ray]->devices);
        rays[(size_t) ray]->devices.clear();
    }
    for (auto& d : taken)
    {
        forgetMappings (d->id);
        d->window.reset();
        Released r;
        r.description = d->description;
        r.bypassed = d->bypassed;
        if (d->usable())
            try { d->plugin->getStateInformation (r.lastState); } catch (...) {}
        else
        {
            const std::lock_guard<std::mutex> l (d->health->mutex);
            r.lastState = d->health->lastState;
        }
        if (d->usable())
            r.plugin = std::move (d->plugin);
        letGo (std::move (d));
        result.push_back (std::move (r));
    }
    return result;
}

void PrismProcessor::removeDevice (int ray, int device)
{
    if (find (ray, device) == nullptr)
        return;

    std::unique_ptr<Device> old;
    {
        const juce::ScopedLock sl (lock);
        auto& devices = rays[(size_t) ray]->devices;
        old = std::move (devices[(size_t) device]);
        devices.erase (devices.begin() + device);
    }
    forgetMappings (old->id);
    letGo (std::move (old));
}

void PrismProcessor::moveDevice (int fromRay, int from, int toRay, int to)
{
    if (find (fromRay, from) == nullptr || toRay < 0 || toRay >= (int) rays.size())
        return;
    if (fromRay != toRay && (int) rays[(size_t) toRay]->devices.size() >= maxDevices)
        return;

    const juce::ScopedLock sl (lock);
    auto& source = rays[(size_t) fromRay]->devices;
    auto device = std::move (source[(size_t) from]);
    source.erase (source.begin() + from);
    auto& target = rays[(size_t) toRay]->devices;
    target.insert (target.begin() + std::clamp (to, 0, (int) target.size()), std::move (device));
}

void PrismProcessor::setDeviceBypassed (int ray, int device, bool b)
{
    if (auto* d = find (ray, device))
    {
        const juce::ScopedLock sl (lock);
        d->bypassed = b;
    }
}

std::string PrismProcessor::reloadDevice (int ray, int device)
{
    auto* d = find (ray, device);
    if (d == nullptr)
        return "There is no plugin here.";

    juce::MemoryBlock state;
    {
        const std::lock_guard<std::mutex> l (d->health->mutex);
        state = d->health->lastState;
    }
    if (d->usable())
        guard (ray, device, [&] (juce::AudioPluginInstance& p) { p.getStateInformation (state); });

    juce::String error;
    auto plugin = factory.create ? factory.create (d->description, &state, channels, rate, blockSize, error) : nullptr;
    if (plugin == nullptr)
        return ("Could not load " + d->description.name + " again: " + error).toStdString();

    auto fresh = makeDevice (std::move (plugin), d->description);
    fresh->id = d->id;   // keeps its mappings
    fresh->bypassed = d->bypassed;
    fresh->health->lastState = state;
    std::unique_ptr<Device> old;
    {
        const juce::ScopedLock sl (lock);
        old = std::move (rays[(size_t) ray]->devices[(size_t) device]);
        rays[(size_t) ray]->devices[(size_t) device] = std::move (fresh);
    }
    letGo (std::move (old));
    return {};
}

std::string PrismProcessor::group (int ray, int first, int last)
{
    if (ray < 0 || ray >= (int) rays.size())
        return "No such ray";
    auto& devices = rays[(size_t) ray]->devices;
    first = std::max (0, first);
    last = std::min (last, (int) devices.size() - 1);
    if (first > last)
        return "Pick the plugins to group first";
    if (! factory.create)
        return "Prisms cannot be made here";

    juce::String error;
    auto made = factory.create (description(), nullptr, channels, rate, blockSize, error);
    auto* inner = dynamic_cast<PrismProcessor*> (made.get());
    if (inner == nullptr)
        return ("Could not make a Prism: " + error).toStdString();

    // Out of this ray and into the new Prism, still running.
    std::vector<std::unique_ptr<Device>> taken;
    {
        const juce::ScopedLock sl (lock);
        for (int i = first; i <= last; ++i)
            taken.push_back (std::move (devices[(size_t) i]));
        devices.erase (devices.begin() + first, devices.begin() + last + 1);
    }
    for (auto& d : taken)
    {
        forgetMappings (d->id);
        d->window.reset();
        juce::MemoryBlock state;
        {
            const std::lock_guard<std::mutex> l (d->health->mutex);
            state = d->health->lastState;
        }
        if (d->usable())
            inner->adopt (0, std::move (d->plugin), d->description, d->bypassed, state);
        else
        {
            // A failed plugin stays failed in the new Prism, with its settings kept.
            inner->adopt (0, nullptr, d->description, d->bypassed, state);
            letGo (std::move (d));
        }
    }

    auto device = makeDevice (std::move (made), description());
    const juce::ScopedLock sl (lock);
    devices.insert (devices.begin() + first, std::move (device));
    return {};
}

std::string PrismProcessor::ungroup (int ray, int device)
{
    auto* inner = prismAt (ray, device);
    if (inner == nullptr)
        return "That is not a Prism";

    int filled = -1;
    for (int r = 0; r < inner->numRays(); ++r)
        if (! inner->rays[(size_t) r]->devices.empty())
        {
            if (filled >= 0)
                return "Only a Prism with one ray of plugins can be ungrouped";
            filled = r;
        }
    auto& devices = rays[(size_t) ray]->devices;
    const auto count = filled < 0 ? 0 : (int) inner->rays[(size_t) filled]->devices.size();
    if ((int) devices.size() - 1 + count > maxDevices)
        return "There is no room in this ray for its plugins";

    auto released = filled >= 0 ? inner->release (filled) : std::vector<Released> {};
    std::vector<std::unique_ptr<Device>> fresh;
    for (auto& r : released)
    {
        auto d = makeDevice (std::move (r.plugin), r.description);
        d->bypassed = r.bypassed;
        d->health->lastState = r.lastState;
        if (d->plugin == nullptr)
            fail (*d, "it stopped working before");
        prepareDevice (*d);
        fresh.push_back (std::move (d));
    }

    std::unique_ptr<Device> old;
    {
        const juce::ScopedLock sl (lock);
        old = std::move (devices[(size_t) device]);
        devices.erase (devices.begin() + device);
        for (int i = 0; i < (int) fresh.size(); ++i)
            devices.insert (devices.begin() + device + i, std::move (fresh[(size_t) i]));
    }
    forgetMappings (old->id);
    letGo (std::move (old));
    return {};
}

float PrismProcessor::takeDevicePeak (int ray, int device)
{
    auto* d = find (ray, device);
    return d != nullptr ? d->peak.exchange (0.0f, std::memory_order_relaxed) : 0.0f;
}

float PrismProcessor::takeRayPeak (int ray)
{
    return ray >= 0 && ray < (int) rays.size() ? rays[(size_t) ray]->peak.exchange (0.0f, std::memory_order_relaxed) : 0.0f;
}

//==============================================================================
bool PrismProcessor::canShowWindow (int ray, int device) const
{
    auto* d = find (ray, device);
    const auto* display = std::getenv ("DISPLAY");
    return d != nullptr && d->usable() && d->hasEditor && display != nullptr && *display != 0 && ! x11::connectionLost();
}

void PrismProcessor::showWindow (int ray, int device)
{
    if (! canShowWindow (ray, device))
        return;
    auto* d = find (ray, device);
    if (d->window != nullptr)
    {
        d->window.reset();
        return;
    }
    try { openWindow (*d); }
    catch (const std::exception& e) { fail (*d, e.what()); }
    catch (...)                      { fail (*d, "an unknown error"); }
}

void PrismProcessor::openWindow (Device& d)
{
    auto* editor = d.plugin->createEditorAndMakeActive();
    if (editor == nullptr)
        return;

    const auto deviceId = d.id;
    std::weak_ptr<bool> living = alive;
    juce::Component* content = editor;
    if (const auto* info = stock::find (d.description))
    {
        StockFrame::Hooks hooks;
        hooks.isBypassed = [this, deviceId] { auto* dev = findId (deviceId); return dev != nullptr && dev->bypassed; };
        hooks.setBypassed = [this, deviceId] (bool b)
        {
            if (auto* dev = findId (deviceId))
            {
                const juce::ScopedLock sl (lock);
                dev->bypassed = b;
            }
        };
        hooks.compareSide = [] { return 0; };
        hooks.setCompareSide = [] (int) {};
        hooks.copyCompareSide = [] {};
        hooks.takeLevels = [this, deviceId]
        {
            const auto* dev = findId (deviceId);
            const auto peak = dev != nullptr ? const_cast<Device*> (dev)->peak.load (std::memory_order_relaxed) : 0.0f;
            return std::array<float, 4> { peak, peak, peak, peak };
        };
        content = new StockFrame (editor, *d.plugin, *info, d.description.name, std::move (hooks));
    }

    const auto contentResizes = content != editor || editor->isResizable();
    d.window = std::make_unique<PluginWindow> (content, contentResizes, d.description.name + " - " + name + " - " APP_DISPLAY_NAME,
        [this, deviceId, living]
        {
            juce::MessageManager::callAsync ([this, deviceId, living]
            {
                if (auto still = living.lock(); still != nullptr && *still)
                    if (auto* dev = findId (deviceId))
                        dev->window.reset();
            });
        },
        [] (int) {});
    if (auto* peer = d.window->getPeer())
        x11::setWindowClass (peer->getNativeHandle());
}

//==============================================================================
void PrismProcessor::renameMacro (int macro, const juce::String& n)
{
    if (macro >= 0 && macro < numMacros)
        macros[(size_t) macro].name = n.trim().isEmpty() ? "Macro " + juce::String (macro + 1) : n.trim();
}

std::string PrismProcessor::map (int macro, int ray, int device, int parameter, float low, float high)
{
    auto* d = find (ray, device);
    if (macro < 0 || macro >= numMacros || d == nullptr || ! d->usable())
        return "There is nothing there to map";
    int count = 0;
    guard (ray, device, [&] (juce::AudioPluginInstance& p) { count = p.getParameters().size(); });
    if (parameter < 0 || parameter >= count)
        return "That parameter is not there";

    const juce::ScopedLock sl (lock);
    auto& m = macros[(size_t) macro];
    for (auto& existing : m.mappings)
        if (existing.deviceId == d->id && existing.parameter == parameter)
        {
            existing.low = low;
            existing.high = high;
            m.applied = -1.0f;
            return {};
        }
    // A parameter follows one macro at a time.
    for (auto& other : macros)
        other.mappings.erase (std::remove_if (other.mappings.begin(), other.mappings.end(),
                                              [&] (const Mapping& x) { return x.deviceId == d->id && x.parameter == parameter; }),
                              other.mappings.end());
    m.mappings.push_back ({ d->id, parameter, std::clamp (low, 0.0f, 1.0f), std::clamp (high, 0.0f, 1.0f) });
    m.applied = -1.0f;
    return {};
}

void PrismProcessor::unmap (int macro, int mapping)
{
    if (macro < 0 || macro >= numMacros)
        return;
    auto& list = macros[(size_t) macro].mappings;
    if (mapping >= 0 && mapping < (int) list.size())
    {
        const juce::ScopedLock sl (lock);
        list.erase (list.begin() + mapping);
    }
}

void PrismProcessor::setMappingRange (int macro, int mapping, float low, float high)
{
    if (macro < 0 || macro >= numMacros)
        return;
    auto& m = macros[(size_t) macro];
    if (mapping >= 0 && mapping < (int) m.mappings.size())
    {
        const juce::ScopedLock sl (lock);
        m.mappings[(size_t) mapping].low = std::clamp (low, 0.0f, 1.0f);
        m.mappings[(size_t) mapping].high = std::clamp (high, 0.0f, 1.0f);
        m.applied = -1.0f;
    }
}

void PrismProcessor::forgetMappings (int deviceId)
{
    const juce::ScopedLock sl (lock);
    for (auto& m : macros)
        m.mappings.erase (std::remove_if (m.mappings.begin(), m.mappings.end(), [deviceId] (const Mapping& x) { return x.deviceId == deviceId; }),
                          m.mappings.end());
}

void PrismProcessor::setController (int macro, int number, int channel)
{
    if (macro >= 0 && macro < numMacros)
    {
        macros[(size_t) macro].controller = number >= 0 && number < 128 ? number : -1;
        macros[(size_t) macro].channel = std::clamp (channel, 0, 16);
    }
}

void PrismProcessor::controller (int channel, int number, int value)
{
    for (int m = 0; m < numMacros; ++m)
    {
        const auto& macro = macros[(size_t) m];
        if (macro.controller == number && (macro.channel == 0 || macro.channel == channel))
            macroParameters[(size_t) m]->setValueNotifyingHost (std::clamp (value, 0, 127) / 127.0f);
    }
    for (int r = 0; r < (int) rays.size(); ++r)
        for (int d = 0; d < (int) rays[(size_t) r]->devices.size(); ++d)
            if (auto* inner = prismAt (r, d))
                inner->controller (channel, number, value);
    applyMacrosNow();
}

void PrismProcessor::applyMacros()
{
    for (int m = 0; m < numMacros; ++m)
    {
        auto& macro = macros[(size_t) m];
        const auto v = macroParameters[(size_t) m]->get();
        if (std::abs (v - macro.applied) < 1.0e-7f)
            continue;
        macro.applied = v;
        for (const auto& map : macro.mappings)
        {
            auto* d = findId (map.deviceId);
            if (d == nullptr || ! d->usable())
                continue;
            try
            {
                const auto& params = d->plugin->getParameters();
                if (map.parameter >= 0 && map.parameter < params.size())
                    params[map.parameter]->setValueNotifyingHost (std::clamp (map.low + (map.high - map.low) * v, 0.0f, 1.0f));
            }
            catch (const std::exception& e) { fail (*d, e.what()); }
            catch (...)                      { fail (*d, "an unknown error"); }
        }
    }
}

void PrismProcessor::applyMacrosNow()
{
    {
        const juce::ScopedLock sl (lock);
        applyMacros();
    }
    // A macro mapped to an inner Prism's macro turns what that one is mapped to.
    for (int r = 0; r < (int) rays.size(); ++r)
        for (int d = 0; d < (int) rays[(size_t) r]->devices.size(); ++d)
            if (auto* inner = prismAt (r, d))
                inner->applyMacrosNow();
}

void PrismProcessor::setNonRealtime (bool b) noexcept
{
    AudioPluginInstance::setNonRealtime (b);
    offline = b;
    for (auto& r : rays)
        for (auto& d : r->devices)
            if (auto* inner = d->usable() ? dynamic_cast<PrismProcessor*> (d->plugin.get()) : nullptr)
                inner->setNonRealtime (b);
}

//==============================================================================
bool PrismProcessor::isBusesLayoutSupported (const BusesLayout& l) const
{
    const auto in = l.getMainInputChannelSet(), out = l.getMainOutputChannelSet();
    return in == out && ! in.isDisabled() && in.size() <= prism::maxChannels;
}

void PrismProcessor::prepareToPlay (double sampleRate, int maxBlock)
{
    const juce::ScopedLock sl (lock);
    rate = sampleRate;
    blockSize = std::max (32, maxBlock);
    channels = std::clamp (std::max (getTotalNumInputChannels(), getTotalNumOutputChannels()), 1, prism::maxChannels);
    splitter.prepare (rate, channels);
    for (auto& r : rays)
    {
        r->buffer.setSize (channels, blockSize, false, true, true);
        r->align.prepare (channels, maxDelay);
        for (auto& d : r->devices)
            prepareDevice (*d);
    }
    for (auto& m : macros)
        m.applied = -1.0f;
}

void PrismProcessor::releaseResources()
{
    const juce::ScopedLock sl (lock);
    for (auto& r : rays)
        for (auto& d : r->devices)
            if (d->usable())
                try { d->plugin->releaseResources(); }
                catch (const std::exception& e) { fail (*d, e.what()); }
                catch (...)                      { fail (*d, "an unknown error"); }
}

double PrismProcessor::getTailLengthSeconds() const
{
    double longest = 0.0;
    for (const auto& r : rays)
        for (const auto& d : r->devices)
            if (d->usable())
                try { longest = std::max (longest, d->plugin->getTailLengthSeconds()); } catch (...) {}
    return longest;
}

void PrismProcessor::processBlock (juce::AudioBuffer<float>& buffer, juce::MidiBuffer&)
{
    // While the rays are being changed, the sound passes through untouched (renders wait instead).
    const auto isOffline = offline.load();
    if (isOffline)
        lock.enter();
    else if (! lock.tryEnter())
        return;
    struct Exit { juce::CriticalSection& l; ~Exit() { l.exit(); } } exit { lock };

    applyMacros();
    if (rays.empty())
        return;

    const auto numChannels = std::min ({ buffer.getNumChannels(), channels, prism::maxChannels });
    const auto mode = currentSplit();
    const auto numRays = (int) rays.size();
    const auto heard = prism::raysHeard ((prism::Split) mode, numRays);
    if (mode == PrismInfo::bands)
    {
        const auto hz = crossovers();
        splitter.setBands (heard, hz.data());
    }

    std::array<int, PrismInfo::maxRays> latency {};
    std::array<float, PrismInfo::maxRays> gains {};
    int slowest = 0;
    bool anySolo = false;
    for (int r = 0; r < heard; ++r)
    {
        latency[(size_t) r] = rayLatency (*rays[(size_t) r]);
        slowest = std::max (slowest, latency[(size_t) r]);
        anySolo = anySolo || (rays[(size_t) r]->soloed && ! isOffline);
    }
    for (int r = 0; r < heard; ++r)
    {
        const auto& ray = *rays[(size_t) r];
        gains[(size_t) r] = ray.muted || (anySolo && ! ray.soloed) ? 0.0f : ray.gain.load();
    }

    std::array<std::array<float*, prism::maxChannels>, PrismInfo::maxRays> rayChannels {};
    std::array<float* const*, PrismInfo::maxRays> rayPointers {};
    std::array<const float* const*, PrismInfo::maxRays> rayReadPointers {};
    std::array<float*, prism::maxChannels> io {};

    const auto total = buffer.getNumSamples();
    for (int done = 0; done < total;)
    {
        const auto n = std::min (blockSize, total - done);
        for (int ch = 0; ch < numChannels; ++ch)
            io[(size_t) ch] = buffer.getWritePointer (ch, done);
        for (int r = 0; r < numRays; ++r)
        {
            for (int ch = 0; ch < numChannels; ++ch)
                rayChannels[(size_t) r][(size_t) ch] = rays[(size_t) r]->buffer.getWritePointer (ch);
            rayPointers[(size_t) r] = rayChannels[(size_t) r].data();
            rayReadPointers[(size_t) r] = rayChannels[(size_t) r].data();
        }

        splitter.split ((prism::Split) mode, io.data(), numChannels, n, rayPointers.data(), numRays);

        for (int r = 0; r < heard; ++r)
        {
            auto& ray = *rays[(size_t) r];
            auto* const* ptrs = rayChannels[(size_t) r].data();
            for (auto& dp : ray.devices)
            {
                auto& d = *dp;
                if (! d.usable() || d.plugin->isSuspended())
                    continue;
                const auto lat = std::max (0, d.plugin->getLatencySamples());
                if (d.bypassed)
                {
                    if (lat > 0)
                        d.bypassDelay.process (ptrs, numChannels, n, lat);
                    continue;
                }
                const auto deviceChannels = std::clamp (std::max (d.plugin->getTotalNumInputChannels(), d.plugin->getTotalNumOutputChannels()),
                                                        1, numChannels);
                juce::AudioBuffer<float> proxy (rayChannels[(size_t) r].data(), deviceChannels, n);
                try
                {
                    d.plugin->processBlock (proxy, midi);
                }
                catch (const std::exception& e) { fail (d, e.what()); }
                catch (...)                      { fail (d, "an unknown error"); }
                midi.clear();
                if (d.usable() && d.plugin->getTotalNumOutputChannels() == 1 && numChannels > 1)
                    std::copy (ptrs[0], ptrs[0] + n, ptrs[1]);

                float peak = 0.0f;
                for (int ch = 0; ch < numChannels; ++ch)
                {
                    const auto range = juce::FloatVectorOperations::findMinAndMax (ptrs[ch], n);
                    peak = std::max ({ peak, std::abs (range.getStart()), std::abs (range.getEnd()) });
                }
                if (peak > d.peak.load (std::memory_order_relaxed))
                    d.peak.store (peak, std::memory_order_relaxed);
            }

            if (slowest > latency[(size_t) r])
                ray.align.process (ptrs, numChannels, n, slowest - latency[(size_t) r]);

            float peak = 0.0f;
            for (int ch = 0; ch < numChannels; ++ch)
            {
                const auto range = juce::FloatVectorOperations::findMinAndMax (ptrs[ch], n);
                peak = std::max ({ peak, std::abs (range.getStart()), std::abs (range.getEnd()) });
            }
            peak *= gains[(size_t) r];
            if (peak > ray.peak.load (std::memory_order_relaxed))
                ray.peak.store (peak, std::memory_order_relaxed);
        }

        prism::Splitter::join ((prism::Split) mode, rayReadPointers.data(), gains.data(), numRays, io.data(), numChannels, n);
        done += n;
    }

    if (getLatencySamples() != slowest)
        setLatencySamples (slowest);
}

//==============================================================================
std::unique_ptr<juce::XmlElement> PrismProcessor::toXml() const
{
    auto xml = std::make_unique<juce::XmlElement> ("PHINYX_PRISM");   // PHINYX_ like the other built-ins' state tags
    xml->setAttribute ("version", 1);
    xml->setAttribute ("name", name);
    xml->setAttribute ("split", split->getIndex());
    for (int i = 0; i < 3; ++i)
        xml->setAttribute ("crossover" + juce::String (i + 1), (double) crossoverParameters[(size_t) i]->get());

    for (int m = 0; m < numMacros; ++m)
    {
        const auto& macro = macros[(size_t) m];
        auto* e = xml->createNewChildElement ("MACRO");
        e->setAttribute ("index", m);
        e->setAttribute ("name", macro.name);
        e->setAttribute ("value", (double) macroParameters[(size_t) m]->get());
        e->setAttribute ("controller", macro.controller);
        e->setAttribute ("channel", macro.channel);
        for (const auto& map : macro.mappings)
        {
            auto* me = e->createNewChildElement ("MAP");
            me->setAttribute ("device", map.deviceId);
            me->setAttribute ("parameter", map.parameter);
            me->setAttribute ("low", (double) map.low);
            me->setAttribute ("high", (double) map.high);
        }
    }

    for (const auto& r : rays)
    {
        auto* re = xml->createNewChildElement ("RAY");
        re->setAttribute ("name", r->name);
        re->setAttribute ("gainDb", r->gainDb);
        re->setAttribute ("muted", r->muted);
        re->setAttribute ("soloed", r->soloed);
        for (const auto& d : r->devices)
        {
            auto* de = re->createNewChildElement ("DEVICE");
            de->setAttribute ("id", d->id);
            de->setAttribute ("bypassed", d->bypassed);
            // A failed plugin is saved with its settings from before it failed.
            juce::MemoryBlock state;
            bool fresh = false;
            if (d->usable())
            {
                try { d->plugin->getStateInformation (state); fresh = true; }
                catch (const std::exception& e) { fail (*d, e.what()); }
                catch (...)                      { fail (*d, "an unknown error"); }
            }
            {
                const std::lock_guard<std::mutex> l (d->health->mutex);
                if (fresh)
                    d->health->lastState = state;
                else
                    state = d->health->lastState;
            }
            de->setAttribute ("state", state.toBase64Encoding());
            de->addChildElement (d->description.createXml().release());
        }
    }
    return xml;
}

void PrismProcessor::getStateInformation (juce::MemoryBlock& data)
{
    copyXmlToBinary (*toXml(), data);
}

void PrismProcessor::setStateInformation (const void* data, int size)
{
    auto xml = getXmlFromBinary (data, size);
    if (xml == nullptr || ! xml->hasTagName ("PHINYX_PRISM"))
        return;

    // Build the new rays first, then swap them in.
    std::vector<std::unique_ptr<Ray>> fresh;
    int highestId = 0;
    for (auto* re : xml->getChildWithTagNameIterator ("RAY"))
    {
        if ((int) fresh.size() >= PrismInfo::maxRays)
            break;
        auto ray = std::make_unique<Ray>();
        ray->name = re->getStringAttribute ("name");
        ray->gainDb = std::clamp (re->getDoubleAttribute ("gainDb", 0.0), -60.0, 12.0);
        ray->gain = ray->gainDb <= -60.0 ? 0.0f : (float) std::pow (10.0, ray->gainDb / 20.0);
        ray->muted = re->getBoolAttribute ("muted", false);
        ray->soloed = re->getBoolAttribute ("soloed", false);
        ray->buffer.setSize (channels, blockSize);
        ray->align.prepare (channels, maxDelay);
        for (auto* de : re->getChildWithTagNameIterator ("DEVICE"))
        {
            juce::PluginDescription d;
            if (de->getFirstChildElement() == nullptr || ! d.loadFromXml (*de->getFirstChildElement())
                || (int) ray->devices.size() >= maxDevices)
                continue;
            juce::MemoryBlock state;
            if (! state.fromBase64Encoding (de->getStringAttribute ("state")))
                state.reset();
            juce::String error;
            auto plugin = factory.create ? factory.create (d, &state, channels, rate, blockSize, error) : nullptr;
            auto device = makeDevice (std::move (plugin), d);
            device->id = std::max (1, de->getIntAttribute ("id", 0));
            highestId = std::max (highestId, device->id);
            device->bypassed = de->getBoolAttribute ("bypassed", false);
            device->health->lastState = state;
            // One that will not load stays in its place with its settings, to be reloaded later.
            if (device->plugin == nullptr)
                fail (*device, error.isEmpty() ? "it could not be loaded" : error.toStdString());
            ray->devices.push_back (std::move (device));
        }
        fresh.push_back (std::move (ray));
    }
    if (fresh.empty())
        fresh.push_back (std::make_unique<Ray>());

    // Ids saved twice (an older file) are made unique again.
    std::vector<int> seen;
    for (auto& r : fresh)
        for (auto& d : r->devices)
        {
            if (std::find (seen.begin(), seen.end(), d->id) != seen.end())
                d->id = ++highestId;
            seen.push_back (d->id);
        }

    std::vector<std::unique_ptr<Ray>> old;
    {
        const juce::ScopedLock sl (lock);
        old = std::move (rays);
        rays = std::move (fresh);
        nextDeviceId = highestId + 1;
        name = xml->getStringAttribute ("name", "Prism");
        for (int m = 0; m < numMacros; ++m)
        {
            macros[(size_t) m] = {};
            macros[(size_t) m].name = "Macro " + juce::String (m + 1);
        }
        for (auto* e : xml->getChildWithTagNameIterator ("MACRO"))
        {
            const auto m = e->getIntAttribute ("index", -1);
            if (m < 0 || m >= numMacros)
                continue;
            auto& macro = macros[(size_t) m];
            macro.name = e->getStringAttribute ("name", macro.name);
            macro.controller = e->getIntAttribute ("controller", -1);
            macro.channel = std::clamp (e->getIntAttribute ("channel", 0), 0, 16);
            for (auto* me : e->getChildWithTagNameIterator ("MAP"))
                macro.mappings.push_back ({ me->getIntAttribute ("device"), me->getIntAttribute ("parameter"),
                                            (float) me->getDoubleAttribute ("low", 0.0), (float) me->getDoubleAttribute ("high", 1.0) });
        }
    }
    for (auto& r : old)
        for (auto& d : r->devices)
            letGo (std::move (d));

    split->setValueNotifyingHost (split->convertTo0to1 ((float) std::clamp (xml->getIntAttribute ("split", 0), 0, prism::numSplits - 1)));
    for (int i = 0; i < 3; ++i)
    {
        auto* p = crossoverParameters[(size_t) i];
        p->setValueNotifyingHost (p->convertTo0to1 ((float) xml->getDoubleAttribute ("crossover" + juce::String (i + 1), p->get())));
    }
    for (auto* e : xml->getChildWithTagNameIterator ("MACRO"))
    {
        const auto m = e->getIntAttribute ("index", -1);
        if (m >= 0 && m < numMacros)
            macroParameters[(size_t) m]->setValueNotifyingHost ((float) std::clamp (e->getDoubleAttribute ("value", 0.0), 0.0, 1.0));
    }
}

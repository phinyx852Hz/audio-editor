// SPDX-License-Identifier: AGPL-3.0-only
#include "MasterChain.h"
#include "PluginWindow.h"
#include "BuiltinEffects.h"
#include "ClapFormat.h"
#include "StockFrame.h"
#include "StockPlugins.h"
#include "X11Windows.h"
#include "../core/Dsp.h"

#include <algorithm>
#include <cstdlib>

//==============================================================================
void MasterChain::addExtraFormats (juce::AudioPluginFormatManager& manager)
{
    manager.addFormat (std::make_unique<ClapPluginFormat>());
}

MasterChain::MasterChain()
{
    juce::addDefaultFormatsToManager (formats);
    addExtraFormats (formats);
    scratch.setSize (maxChannels, blockSize);
}

MasterChain::~MasterChain()
{
    for (size_t i = 0; i < slots.size(); ++i)
        discard (i);
}

void MasterChain::fail (const Slot& s, const std::string& why)
{
    {
        const std::lock_guard<std::mutex> l (s.health->mutex);
        if (s.health->why.empty())
            s.health->why = why.empty() ? "an unknown error" : why;
    }
    s.health->failed.store (true, std::memory_order_release);
}

void MasterChain::discard (size_t index)
{
    auto& s = slots[index];
    const auto failed = s.plugin != nullptr && ! s.usable();
    std::unique_ptr<juce::AudioPluginInstance> old;
    if (failed)
    {
        if (auto* w = windows[index].release())
            guard (s, [w] { w->setVisible (false); });
    }
    else
        windows[index].reset();

    {
        const juce::ScopedLock sl (lock);
        old = std::move (s.plugin);
        s = {};
        if (soloSlot == (int) index)
            soloSlot = -1;
    }

    if (failed)
        (void) old.release();   // deleting it would call into it again
}

std::unique_ptr<juce::AudioPluginInstance> MasterChain::start (const juce::PluginDescription& d, const juce::MemoryBlock* state,
                                                               bool& editor, juce::String& error) const
{
    std::unique_ptr<juce::AudioPluginInstance> instance;
    try
    {
        instance = createInstance (d, error);
        if (instance == nullptr)
            return nullptr;
        if (state != nullptr && state->getSize() > 0)
            instance->setStateInformation (state->getData(), (int) state->getSize());
        preparePlugin (*instance, rate, blockSize);
        editor = instance->hasEditor();
        return instance;
    }
    catch (const std::exception& e) { error = e.what(); }
    catch (...)                      { error = "an unknown error"; }
    (void) instance.release();
    return nullptr;
}

void MasterChain::setConfigFolder (const juce::File& folder)
{
    configFolder = folder;
    configFolder.createDirectory();

    if (auto xml = juce::XmlDocument::parse (pluginListFile()))
        pluginList.recreateFromXml (*xml);
    disabled = juce::StringArray::fromLines (disabledListFile().loadFileAsString());
    disabled.removeEmptyStrings();

    loadStockPlugins();
}

void MasterChain::loadStockPlugins()
{
    // Remembered from the last start; a stock file that is new or changed
    // (PHINYX was updated) is looked at again.
    if (auto xml = juce::XmlDocument::parse (stockListFile()))
        stockList.recreateFromXml (*xml);

    const auto folder = stock::folder();
    auto formatNamed = [this] (const juce::String& name) -> juce::AudioPluginFormat*
    {
        for (auto* f : formats.getFormats())
            if (f->getName() == name)
                return f;
        return nullptr;
    };

    for (const auto& d : stockList.getTypes())
        if (auto* format = formatNamed (d.pluginFormatName); stock::find (d) == nullptr || format == nullptr || ! format->doesPluginStillExist (d))
            stockList.removeType (d);

    bool changed = false;
    for (const auto& info : stock::all())
    {
        auto* format = formatNamed (info.format);
        const auto file = folder.getChildFile (info.file);   // a VST3 is a folder
        if (format == nullptr || folder == juce::File() || ! file.exists() || stockList.isListingUpToDate (file.getFullPathName(), *format))
            continue;

        for (const auto& d : stockList.getTypes())
            if (d.fileOrIdentifier == file.getFullPathName())
                stockList.removeType (d);

        juce::OwnedArray<juce::PluginDescription> found;
        if (scanOutOfProcess (*format, file.getFullPathName(), 60000, found))
            for (auto* d : found)
            {
                d->name = info.namePrefix + d->name;
                if (info.maker != nullptr)
                    d->manufacturerName = info.maker;
                stockList.addType (*d);
            }
        changed = true;
    }

    if (changed)
        if (auto xml = stockList.createXml())
            xml->writeTo (stockListFile());
}

std::unique_ptr<juce::PluginDescription> MasterChain::findDescription (const std::string& pluginId) const
{
    for (const auto& d : builtin::descriptions())
        if (d.createIdentifierString().toStdString() == pluginId)
            return std::make_unique<juce::PluginDescription> (d);

    if (auto d = stockList.getTypeForIdentifierString (pluginId))
        return d;
    return pluginList.getTypeForIdentifierString (pluginId);
}

//==============================================================================
std::vector<PluginInfo> MasterChain::knownPlugins() const
{
    auto all = allPlugins();
    all.erase (std::remove_if (all.begin(), all.end(), [] (const PluginInfo& p) { return ! p.enabled; }), all.end());
    return all;
}

void MasterChain::setPluginEnabled (const std::string& pluginId, bool on)
{
    const juce::String id (pluginId);
    if (on)
        disabled.removeString (id);
    else
        disabled.addIfNotAlreadyThere (id);
    disabledListFile().replaceWithText (disabled.joinIntoString ("\n"));
}

std::vector<std::string> MasterChain::skippedFiles() const
{
    std::vector<std::string> result;
    for (const auto& f : pluginList.getBlacklistedFiles())
        result.push_back (f.toStdString());
    return result;
}

void MasterChain::retrySkipped (const std::string& file)
{
    pluginList.removeFromBlacklist (file);
    if (auto xml = pluginList.createXml())
        xml->writeTo (pluginListFile());
}

std::vector<PluginInfo> MasterChain::allPlugins() const { return listPlugins (false); }

std::vector<PluginInfo> MasterChain::instruments() const
{
    auto all = listPlugins (true);
    all.erase (std::remove_if (all.begin(), all.end(), [] (const PluginInfo& p) { return ! p.enabled; }), all.end());
    return all;
}

std::vector<PluginInfo> MasterChain::listPlugins (bool wantInstruments) const
{
    std::vector<PluginInfo> result;
    auto all = builtin::descriptions();
    for (const auto* list : { &stockList, &pluginList })
        for (const auto& d : list->getTypes())
            all.push_back (d);

    // VST3 categories come as "Fx|EQ": every effect is Fx, so show just "EQ".
    auto category = [] (const juce::PluginDescription& d)
    {
        auto parts = juce::StringArray::fromTokens (d.category, "|", {});
        if (parts.size() > 1)
            parts.removeString ("Fx");
        return parts.joinIntoString (", ").toStdString();
    };

    for (const auto& d : all)
        if (d.isInstrument == wantInstruments)
        {
            const auto id = d.createIdentifierString();
            result.push_back ({ id.toStdString(), d.name.toStdString(), d.manufacturerName.toStdString(),
                                stock::find (d) != nullptr ? builtin::formatName : d.pluginFormatName.toStdString(),
                                category (d), builtin::isBuiltin (d) ? std::string() : d.fileOrIdentifier.toStdString(),
                                ! disabled.contains (id) });
        }

    std::sort (result.begin(), result.end(), [] (const PluginInfo& a, const PluginInfo& b)
    {
        return juce::String (a.name).compareNatural (juce::String (b.name)) < 0;
    });
    return result;
}

std::vector<std::string> MasterChain::searchFolders() const
{
    std::vector<std::string> result;
    for (auto* f : formats.getFormats())
        result.push_back ((f->getName() + ": " + f->getDefaultLocationsToSearch().toString().replace (";", ", ")).toStdString());

    return result;
}

int MasterChain::startScan()
{
    // A file named in the dead man's pedal crashed the last scan: never try it again.
    if (deadMansPedal().existsAsFile())
    {
        const auto crashed = deadMansPedal().loadFileAsString().trim();
        if (crashed.isNotEmpty())
            pluginList.addToBlacklist (crashed);
        deadMansPedal().deleteFile();
    }

    scanQueue.clear();
    for (auto* format : formats.getFormats())
    {
        // LV2 names a bundle once for each plugin in it; look inside each bundle once.
        auto files = format->searchPathsForPlugins (format->getDefaultLocationsToSearch(), true, false);
        files.removeDuplicates (false);
        for (const auto& file : files)
            scanQueue.add ({ format, file });
    }

    scanIndex = 0;
    crashedThisScan = 0;
    pluginsBefore = pluginList.getNumTypes();
    return scanQueue.size();
}

bool MasterChain::scanNext (std::string& fileBeingScanned)
{
    if (scanIndex >= scanQueue.size())
        return false;

    const auto item = scanQueue[scanIndex++];
    fileBeingScanned = item.file.toStdString();

    if (pluginList.getBlacklistedFiles().contains (item.file) || pluginList.isListingUpToDate (item.file, *item.format))
        return true;

    // Windows plugins through yabridge start Wine first, which can take a while.
    const auto timeoutMs = item.file.containsIgnoreCase ("yabridge") ? 180000 : 60000;
    juce::OwnedArray<juce::PluginDescription> found;
    if (! scanOutOfProcess (*item.format, item.file, timeoutMs, found))
    {
        pluginList.addToBlacklist (item.file);
        ++crashedThisScan;
        return true;
    }

    for (auto* d : found)
        pluginList.addType (*d);
    return true;
}

bool MasterChain::scanOutOfProcess (const juce::AudioPluginFormat& format, const juce::String& file, int timeoutMs,
                                    juce::OwnedArray<juce::PluginDescription>& found)
{
    // Look inside the file from a separate process (see scanInThisProcess).
    const auto output = juce::File::createTempFile (".xml");
    juce::ChildProcess child;
    const juce::StringArray command { juce::File::getSpecialLocation (juce::File::currentExecutableFile).getFullPathName(),
                                      scanOption, format.getName(), file, output.getFullPathName() };
    const auto started = child.start (command, 0);
    const auto finished = started && child.waitForProcessToFinish (timeoutMs);
    if (started && ! finished)
        child.kill();

    const auto ok = finished && child.getExitCode() == 0;
    if (ok)
        if (auto xml = juce::XmlDocument::parse (output))
            for (auto* e : xml->getChildIterator())
                if (auto d = std::make_unique<juce::PluginDescription>(); d->loadFromXml (*e))
                    found.add (std::move (d));

    output.deleteFile();
    return ok;
}

int MasterChain::scanInThisProcess (const juce::String& formatName, const juce::String& file, const juce::File& output)
{
    juce::ScopedJuceInitialiser_GUI juce;
    juce::AudioPluginFormatManager formats;
    juce::addDefaultFormatsToManager (formats);
    addExtraFormats (formats);

    for (auto* format : formats.getFormats())
    {
        if (format->getName() != formatName)
            continue;

        juce::OwnedArray<juce::PluginDescription> found;
        format->findAllTypesForFile (found, file);
        juce::XmlElement list ("PLUGINS");
        for (auto* d : found)
            list.addChildElement (d->createXml().release());
        return list.writeTo (output) ? 0 : 2;
    }
    return 3;
}

std::string MasterChain::finishScan()
{
    pluginList.scanFinished();
    if (auto xml = pluginList.createXml())
        xml->writeTo (pluginListFile());

    scanQueue.clear();
    int total = 0;
    for (const auto& d : pluginList.getTypes())
        total += d.isInstrument ? 0 : 1;
    const auto added = std::max (0, pluginList.getNumTypes() - pluginsBefore);
    auto summary = juce::String (total) + (total == 1 ? " effect plugin" : " effect plugins") + " available";
    if (added > 0)
        summary << " (" << added << " new)";
    if (crashedThisScan > 0)
        summary << ". " << crashedThisScan << (crashedThisScan == 1 ? " file" : " files") << " crashed or hung while being checked and will be skipped";
    else if (! pluginList.getBlacklistedFiles().isEmpty())
        summary << ". Skipping " << pluginList.getBlacklistedFiles().size() << " that crashed before";

    return summary.toStdString();
}

//==============================================================================
std::unique_ptr<juce::AudioPluginInstance> MasterChain::createInstance (const juce::PluginDescription& d, juce::String& error) const
{
    auto instance = builtin::isBuiltin (d) ? builtin::create (d) : formats.createPluginInstance (d, rate, blockSize, error);
    if (instance == nullptr)
    {
        if (error.isEmpty())
            error = "this effect is not part of this version";
        return nullptr;
    }

    applyLayout (*instance, channelCount);
    return instance;
}

juce::AudioChannelSet MasterChain::channelSetFor (int n)
{
    switch (n)
    {
        case 1:  return juce::AudioChannelSet::mono();
        case 2:  return juce::AudioChannelSet::stereo();
        case 3:  return juce::AudioChannelSet::createLCR();
        case 4:  return juce::AudioChannelSet::quadraphonic();
        case 5:  return juce::AudioChannelSet::create5point0();
        case 6:  return juce::AudioChannelSet::create5point1();
        case 7:  return juce::AudioChannelSet::create6point1();
        case 8:  return juce::AudioChannelSet::create7point1();
        case 10: return juce::AudioChannelSet::create7point1point2();
        case 12: return juce::AudioChannelSet::create7point1point4();
        default: return juce::AudioChannelSet::discreteChannels (n);
    }
}

namespace
{
    bool trySetLayout (juce::AudioPluginInstance& p, const juce::AudioChannelSet& set)
    {
        auto layout = p.getBusesLayout();
        if (! layout.inputBuses.isEmpty())
            layout.inputBuses.getReference (0) = set;
        if (! layout.outputBuses.isEmpty())
            layout.outputBuses.getReference (0) = set;
        return p.setBusesLayout (layout);
    }
}

void MasterChain::applyLayout (juce::AudioPluginInstance& p, int n) const
{
    // Main bus only; side chains and extra buses stay off. A plugin that cannot
    // take every channel gets stereo (or keeps its own layout) and the rest pass through.
    p.disableNonMainBuses();
    if (n > 2 && (trySetLayout (p, channelSetFor (n)) || trySetLayout (p, juce::AudioChannelSet::discreteChannels (n))))
        return;
    trySetLayout (p, juce::AudioChannelSet::stereo());
}

std::unique_ptr<juce::AudioPluginInstance> MasterChain::createEffect (const std::string& pluginId, double sampleRate, int maxBlock,
                                                                      juce::String& error) const
{
    const auto d = findDescription (pluginId);
    if (d == nullptr)
    {
        error = "this effect is not in the plugin list; scan for plugins again";
        return nullptr;
    }
    std::unique_ptr<juce::AudioPluginInstance> instance;
    try
    {
        instance = builtin::isBuiltin (*d) ? builtin::create (*d) : formats.createPluginInstance (*d, sampleRate, maxBlock, error);
        if (instance == nullptr)
        {
            if (error.isEmpty())
                error = "this effect is not part of this version";
            return nullptr;
        }
        instance->disableNonMainBuses();
        trySetLayout (*instance, juce::AudioChannelSet::stereo());
        preparePlugin (*instance, sampleRate, maxBlock);
        return instance;
    }
    catch (const std::exception& e) { error = e.what(); }
    catch (...)                      { error = "an unknown error"; }
    (void) instance.release();   // it threw: deleting it would call into it again
    return nullptr;
}

std::unique_ptr<juce::AudioPluginInstance> MasterChain::createInstrument (const std::string& pluginId, double sampleRate, int maxBlock,
                                                                          juce::String& error) const
{
    const auto d = findDescription (pluginId);
    if (d == nullptr)
    {
        error = "this instrument is not in the plugin list; scan for plugins again";
        return nullptr;
    }
    std::unique_ptr<juce::AudioPluginInstance> instance;
    try
    {
        instance = formats.createPluginInstance (*d, sampleRate, maxBlock, error);
        if (instance == nullptr)
            return nullptr;
        // Stereo out, no audio in where the plugin allows it; MIDI in.
        instance->disableNonMainBuses();
        auto layout = instance->getBusesLayout();
        if (! layout.inputBuses.isEmpty())
            layout.inputBuses.getReference (0) = juce::AudioChannelSet::disabled();
        if (! layout.outputBuses.isEmpty())
            layout.outputBuses.getReference (0) = juce::AudioChannelSet::stereo();
        if (! instance->setBusesLayout (layout))
            trySetLayout (*instance, juce::AudioChannelSet::stereo());
        preparePlugin (*instance, sampleRate, maxBlock);
        return instance;
    }
    catch (const std::exception& e) { error = e.what(); }
    catch (...)                      { error = "an unknown error"; }
    (void) instance.release();   // it threw: deleting it would call into it again
    return nullptr;
}

void MasterChain::setChannels (int n)
{
    n = std::clamp (n, 2, maxChannels);
    const juce::ScopedLock sl (lock);
    if (n == channelCount)
        return;

    channelCount = n;
    prepareDelays();
    for (auto& s : slots)
    {
        if (s.usable())
            guard (s, [&]
            {
                s.plugin->releaseResources();
                applyLayout (*s.plugin, n);
                preparePlugin (*s.plugin, rate, blockSize);
            });
    }
}

void MasterChain::preparePlugin (juce::AudioPluginInstance& p, double sampleRate, int maxBlock) const
{
    p.releaseResources();
    p.setRateAndBufferSizeDetails (sampleRate, maxBlock);
    p.prepareToPlay (sampleRate, maxBlock);
}

std::string MasterChain::load (int index, const std::string& pluginId)
{
    if (index < 0 || index >= (int) slots.size())
        return "No such slot";

    auto description = findDescription (pluginId);
    if (description == nullptr)
        return "This plugin is not in the plugin list. Scan for plugins again.";

    juce::String error;
    bool editor = false;
    auto instance = start (*description, nullptr, editor, error);
    if (instance == nullptr)
        return ("Could not load " + description->name + ": " + error).toStdString();

    discard ((size_t) index);
    const juce::ScopedLock sl (lock);
    slots[(size_t) index] = { std::move (instance), *description, false, 0, {}, editor };
    return {};
}

void MasterChain::clear (int index)
{
    if (index >= 0 && index < (int) slots.size())
        discard ((size_t) index);
}

std::string MasterChain::reload (int index)
{
    if (index < 0 || index >= (int) slots.size() || slots[(size_t) index].plugin == nullptr)
        return "There is no plugin in this slot.";

    auto& s = slots[(size_t) index];
    const auto description = s.description;
    const auto bypassed = s.bypassed;
    juce::MemoryBlock state;
    {
        const std::lock_guard<std::mutex> l (s.health->mutex);
        state = s.health->lastState;
    }
    if (s.usable())
        guard (s, [&] { s.plugin->getStateInformation (state); });

    juce::String error;
    bool editor = false;
    auto instance = start (description, &state, editor, error);
    if (instance == nullptr)
        return ("Could not load " + description.name + " again: " + error).toStdString();

    discard ((size_t) index);
    const juce::ScopedLock sl (lock);
    slots[(size_t) index] = { std::move (instance), description, bypassed, 0, {}, editor };
    slots[(size_t) index].health->lastState = state;
    return {};
}

void MasterChain::move (int from, int to)
{
    const auto n = (int) slots.size();
    if (from < 0 || from >= n || to < 0 || to >= n || from == to)
        return;

    const juce::ScopedLock sl (lock);
    soloSlot = -1;
    auto rotateOne = [&] (auto& array)
    {
        if (from < to)
            std::rotate (array.begin() + from, array.begin() + from + 1, array.begin() + to + 1);
        else
            std::rotate (array.begin() + to, array.begin() + from, array.begin() + from + 1);
    };
    rotateOne (slots);
    rotateOne (windows);
    rotateOne (delays);
    rotateOne (delayWrite);
}

void MasterChain::setBypassed (int index, bool b)
{
    if (index >= 0 && index < (int) slots.size())
    {
        const juce::ScopedLock sl (lock);
        slots[(size_t) index].bypassed = b;
    }
}

MasterSlotInfo MasterChain::info (int index) const
{
    MasterSlotInfo i;
    if (index < 0 || index >= (int) slots.size() || slots[(size_t) index].plugin == nullptr)
        return i;

    const auto& s = slots[(size_t) index];
    i.empty = false;
    i.name = s.description.name.toStdString();
    const auto* stockInfo = stock::find (s.description);
    i.format = stockInfo != nullptr ? builtin::formatName : s.description.pluginFormatName.toStdString();
    if (stockInfo != nullptr && s.usable())
        guard (s, [&] { i.mainParameters = stock::footerParameters (*stockInfo, *s.plugin); });
    i.pluginId = s.description.createIdentifierString().toStdString();
    i.bypassed = s.bypassed;
    if (s.usable())
        guard (s, [&]
        {
            i.latencySamples = s.plugin->getLatencySamples();
            i.hasEditor = s.hasEditor;
            i.channels = std::min (channelCount, std::max (s.plugin->getTotalNumInputChannels(), s.plugin->getTotalNumOutputChannels()));
        });
    if (! s.usable())
    {
        i.failed = true;
        i.hasEditor = false;
        i.mainParameters.clear();
        const std::lock_guard<std::mutex> l (s.health->mutex);
        i.failure = s.health->why;
    }
    return i;
}

void MasterChain::setGainDb (double db)
{
    gainDecibels = std::clamp (db, -60.0, 24.0);
    gain = (float) dsp::dbToGain (gainDecibels);
}

void MasterChain::setDither (int bits, dsp::Dither type)
{
    finalBits = bits == 16 || bits == 20 || bits == 24 ? bits : 0;
    finalType = (int) type;
}

void MasterChain::FinalDither::process (int bits, dsp::Dither type, float* const* channels, int numChannels, int numFrames)
{
    if (bits <= 0)
        return;
    const auto settings = (bits * 4 + (int) type) * 64 + numChannels;
    if (settings != applied)
    {
        quantizer.set (bits, type, numChannels);
        applied = settings;
    }
    quantizer.process (channels, numChannels, numFrames);
}

bool MasterChain::isActive() const
{
    if (allBypassed)
        return false;

    if (std::abs (gainDecibels) > 1.0e-6 || finalBits > 0)
        return true;

    return std::any_of (slots.begin(), slots.end(), [] (const Slot& s) { return s.usable() && ! s.bypassed; });
}

int MasterChain::totalLatency() const
{
    int total = 0;
    for (const auto& s : slots)
        if (s.usable() && ! s.bypassed)
            total += s.plugin->getLatencySamples();

    return total;
}

//==============================================================================
std::vector<PluginParameterInfo> MasterChain::parameters (int index) const
{
    std::vector<PluginParameterInfo> result;
    if (index < 0 || index >= (int) slots.size() || ! slots[(size_t) index].usable())
        return result;

    const auto& s = slots[(size_t) index];
    if (! guard (s, [&]
        {
            for (auto* p : s.plugin->getParameters())
            {
                PluginParameterInfo info;
                info.name = p->getName (64).toStdString();
                info.value = p->getValue();
                info.valueText = (p->getCurrentValueAsText() + " " + stock::unit (*p)).trim().toStdString();
                info.isBoolean = p->isBoolean();
                const auto steps = p->getNumSteps();
                info.numSteps = steps > 1 && steps <= 1000 ? steps : 0;
                result.push_back (info);
            }
        }))
        result.clear();

    return result;
}

PluginParameterInfo MasterChain::parameter (int index, int parameter) const
{
    PluginParameterInfo info;
    if (index < 0 || index >= (int) slots.size() || ! slots[(size_t) index].usable())
        return info;

    const auto& s = slots[(size_t) index];
    const auto& params = s.plugin->getParameters();
    if (parameter < 0 || parameter >= params.size())
        return info;

    auto* p = params[parameter];
    if (! guard (s, [&]
        {
            info.name = p->getName (64).toStdString();
            info.value = p->getValue();
            info.valueText = (p->getCurrentValueAsText() + " " + stock::unit (*p)).trim().toStdString();
            info.isBoolean = p->isBoolean();
            const auto steps = p->getNumSteps();
            info.numSteps = steps > 1 && steps <= 1000 ? steps : 0;
        }))
        info = {};
    return info;
}

bool MasterChain::setParameterFromText (int index, int parameter, const std::string& typed)
{
    if (index < 0 || index >= (int) slots.size() || ! slots[(size_t) index].usable())
        return false;

    bool done = false;
    guard (slots[(size_t) index], [&] { done = setParameterFromTextUnguarded (index, parameter, typed); });
    return done;
}

bool MasterChain::setParameterFromTextUnguarded (int index, int parameter, const std::string& typed)
{
    const auto& params = slots[(size_t) index].plugin->getParameters();
    if (parameter < 0 || parameter >= params.size())
        return false;

    auto* p = params[parameter];
    const auto text = juce::String (typed).trim();
    if (text.isEmpty())
        return false;

    // The number a parameter shows for a normalised value, e.g. "-6.0 dB" -> -6.
    auto shown = [p] (float v)
    {
        const auto t = p->getText (v, 64);
        if (t.containsIgnoreCase ("inf"))
            return t.trimStart().startsWithChar ('-') ? -1.0e12 : 1.0e12;  // "-inf dB" is silence, not zero
        return t.retainCharacters ("+-.0123456789eE").getDoubleValue();
    };
    const auto infinite = text.containsIgnoreCase ("inf");
    const auto isNumber = infinite || text.containsAnyOf ("0123456789");
    const auto target = infinite ? (text.startsWithChar ('-') ? -1.0e12 : 1.0e12)
                                 : text.retainCharacters ("+-.0123456789eE").getDoubleValue();
    auto close = [&] (float v) { return std::abs (shown (v) - target) <= std::max (0.005, std::abs (target) * 0.005); };

    // Ask the plugin first; many understand their own units.
    const auto asked = std::clamp (p->getValueForText (text), 0.0f, 1.0f);
    if (! isNumber || close (asked))
    {
        if (! isNumber)
        {
            // A choice such as "RMS": find the step whose label matches.
            const auto steps = std::max (2, std::min (p->getNumSteps(), 1000));
            for (int i = 0; i < steps; ++i)
            {
                const auto v = (float) i / (float) (steps - 1);
                if (p->getText (v, 64).trim().equalsIgnoreCase (text))
                {
                    p->setValueNotifyingHost (v);
                    return true;
                }
            }
            if (! p->getText (asked, 64).trim().equalsIgnoreCase (text))
                return false;
        }

        p->setValueNotifyingHost (asked);
        return true;
    }

    // Otherwise search for the value that displays the typed number (displays rise or fall steadily).
    float lo = 0.0f, hi = 1.0f;
    const bool rising = shown (1.0f) >= shown (0.0f);
    if ((rising && (target < shown (0.0f) || target > shown (1.0f))) || (! rising && (target > shown (0.0f) || target < shown (1.0f))))
    {
        p->setValueNotifyingHost ((target < shown (0.0f)) == rising ? 0.0f : 1.0f);  // out of range: go to the nearest end
        return true;
    }

    for (int i = 0; i < 40; ++i)
    {
        const auto mid = (lo + hi) * 0.5f;
        if ((shown (mid) < target) == rising)
            lo = mid;
        else
            hi = mid;
    }

    p->setValueNotifyingHost ((lo + hi) * 0.5f);
    return true;
}

float MasterChain::takePeak (int index)
{
    return index >= 0 && index < (int) peaks.size() ? peaks[(size_t) index].exchange (0.0f, std::memory_order_relaxed) : 0.0f;
}

void MasterChain::setParameter (int index, int parameter, float value)
{
    if (index < 0 || index >= (int) slots.size() || ! slots[(size_t) index].usable())
        return;

    const auto& s = slots[(size_t) index];
    const auto& params = s.plugin->getParameters();
    if (parameter >= 0 && parameter < params.size())
        guard (s, [&] { params[parameter]->setValueNotifyingHost (std::clamp (value, 0.0f, 1.0f)); });
}

bool MasterChain::canShowWindow (int index) const
{
    const auto* display = std::getenv ("DISPLAY");
    return index >= 0 && index < (int) slots.size() && slots[(size_t) index].usable()
        && slots[(size_t) index].hasEditor && display != nullptr && *display != 0 && ! x11::connectionLost();
}

void MasterChain::showWindow (int index)
{
    if (! canShowWindow (index))
        return;

    // A second press closes the window again (the Window button and E toggle it).
    if (windows[(size_t) index] != nullptr)
    {
        windows[(size_t) index].reset();
        return;
    }

    guard (slots[(size_t) index], [&] { openWindow (index); });
}

void MasterChain::openWindow (int index)
{
    auto* plugin = slots[(size_t) index].plugin.get();
    auto* editor = plugin->createEditorAndMakeActive();
    if (editor == nullptr)
        return;

    // Stock plugins open in the PHINYX frame.
    juce::Component* content = editor;
    const auto& description = slots[(size_t) index].description;
    if (const auto* info = stock::find (description))
    {
        StockFrame::Hooks hooks;
        hooks.isBypassed = [this, plugin] { const auto i = slotOf (plugin); return i >= 0 && slots[(size_t) i].bypassed; };
        hooks.setBypassed = [this, plugin] (bool b) { setBypassed (slotOf (plugin), b); };
        hooks.compareSide = [this, plugin] { const auto i = slotOf (plugin); return i >= 0 ? slots[(size_t) i].compareSide : 0; };
        hooks.setCompareSide = [this, plugin] (int side) { setCompareSide (slotOf (plugin), side); };
        hooks.copyCompareSide = [this, plugin] { copyCompareSide (slotOf (plugin)); };
        hooks.takeLevels = [this, plugin]
        {
            std::array<float, 4> result {};
            if (const auto i = slotOf (plugin); i >= 0)
                for (size_t k = 0; k < 4; ++k)
                    result[k] = frameLevels[(size_t) i][k].exchange (0.0f, std::memory_order_relaxed);
            return result;
        };
        content = new StockFrame (editor, *plugin, *info, description.name, std::move (hooks));
    }

    // A StockFrame lays itself out at any size; a bare editor that cannot resize gets centred.
    const auto contentResizes = content != editor || editor->isResizable();
    windows[(size_t) index] = std::make_unique<PluginWindow> (content, contentResizes, description.name + " - " APP_DISPLAY_NAME,
        [this, plugin]
        {
            // Close later: the window cannot delete itself from its own callback.
            juce::MessageManager::callAsync ([this, plugin]
            {
                for (size_t i = 0; i < slots.size(); ++i)
                    if (slots[i].plugin.get() == plugin)
                        windows[i].reset();
            });
        },
        [this, plugin] (int direction)
        {
            // The next open plugin window in slot order, wrapping round.
            const auto from = slotOf (plugin);
            const auto count = (int) windows.size();
            for (int step = 1; from >= 0 && step < count; ++step)
                if (auto& w = windows[(size_t) (((from + direction * step) % count + count) % count)])
                {
                    w->toFront (true);
                    break;
                }
        });

    if (auto* peer = windows[(size_t) index]->getPeer())
        x11::setWindowClass (peer->getNativeHandle());
    x11::surviveLostConnection ([this]
    {
        for (auto& w : windows)
            w.reset();
    });
}

int MasterChain::slotOf (const juce::AudioPluginInstance* plugin) const
{
    for (size_t i = 0; i < slots.size(); ++i)
        if (plugin != nullptr && slots[i].plugin.get() == plugin)
            return (int) i;
    return -1;
}

void MasterChain::setCompareSide (int index, int side)
{
    if (index < 0 || index >= (int) slots.size() || ! slots[(size_t) index].usable())
        return;

    auto& s = slots[(size_t) index];
    if (side == s.compareSide)
        return;

    // Swap the settings being heard with the other side's; B starts as a copy of A.
    guard (s, [&]
    {
        juce::MemoryBlock current;
        s.plugin->getStateInformation (current);
        if (s.otherSide.getSize() > 0)
            s.plugin->setStateInformation (s.otherSide.getData(), (int) s.otherSide.getSize());
        s.otherSide = std::move (current);
        s.compareSide = side;
    });
}

void MasterChain::copyCompareSide (int index)
{
    if (index >= 0 && index < (int) slots.size() && slots[(size_t) index].usable())
    {
        auto& s = slots[(size_t) index];
        guard (s, [&] { s.plugin->getStateInformation (s.otherSide); });
    }
}

//==============================================================================
void MasterChain::save() const
{
    if (configFolder == juce::File())
        return;

    juce::XmlElement root ("PHINYX_MASTER_SECTION");
    root.setAttribute ("gainDb", gainDecibels);
    root.setAttribute ("bypassed", (bool) allBypassed);
    root.setAttribute ("ditherBits", finalBits.load());
    root.setAttribute ("ditherType", finalType.load());

    for (size_t i = 0; i < slots.size(); ++i)
    {
        const auto& s = slots[i];
        if (s.plugin == nullptr)
            continue;

        auto* slot = root.createNewChildElement ("SLOT");
        slot->setAttribute ("index", (int) i);
        slot->setAttribute ("bypassed", s.bypassed);
        // A failed plugin is saved with its settings from before it failed.
        juce::MemoryBlock state;
        const auto fresh = s.usable() && guard (s, [&] { s.plugin->getStateInformation (state); });
        {
            const std::lock_guard<std::mutex> l (s.health->mutex);
            if (fresh)
                s.health->lastState = state;
            else
                state = s.health->lastState;
        }
        slot->setAttribute ("state", state.toBase64Encoding());
        slot->addChildElement (s.description.createXml().release());
    }

    root.writeTo (masterFile());
}

std::string MasterChain::restore()
{
    auto xml = juce::XmlDocument::parse (masterFile());
    if (xml == nullptr || ! xml->hasTagName ("PHINYX_MASTER_SECTION"))
        return {};

    setGainDb (xml->getDoubleAttribute ("gainDb", 0.0));
    allBypassed = xml->getBoolAttribute ("bypassed", false);
    setDither (xml->getIntAttribute ("ditherBits", 0), (dsp::Dither) std::clamp (xml->getIntAttribute ("ditherType", 1), 0, 2));

    juce::StringArray problems;
    for (auto* slot : xml->getChildWithTagNameIterator ("SLOT"))
    {
        const auto index = slot->getIntAttribute ("index", -1);
        juce::PluginDescription d;
        if (index < 0 || index >= (int) slots.size() || slot->getFirstChildElement() == nullptr
            || ! d.loadFromXml (*slot->getFirstChildElement()))
            continue;

        // A stock plugin saved from another place (the build folder, an older
        // install) loads from where the stock plugins are now.
        const auto saved = juce::File::createFileWithoutCheckingPath (d.fileOrIdentifier);
        const auto savedFile = saved.getFileName();
        if (stock::find (d) == nullptr && saved.getParentDirectory().getFileName() == "stock")
            for (const auto& candidate : stockList.getTypes())
                if (candidate.uniqueId == d.uniqueId
                    && juce::File::createFileWithoutCheckingPath (candidate.fileOrIdentifier).getFileName() == savedFile)
                    d = candidate;

        juce::MemoryBlock state;
        if (! state.fromBase64Encoding (slot->getStringAttribute ("state")))
            state.reset();

        juce::String error;
        bool editor = false;
        auto instance = start (d, &state, editor, error);
        if (instance == nullptr)
        {
            problems.add (d.name + ": " + error);
            continue;
        }

        // The Dither effect used to take a slot; its settings move to the final dither.
        if (builtin::isDither (d))
        {
            static constexpr int depths[] { 16, 20, 24 };
            const auto params = instance->getParameters();
            auto index = [&] (int i)
            {
                auto* choice = i < params.size() ? dynamic_cast<juce::AudioParameterChoice*> (params[i]) : nullptr;
                return choice != nullptr ? choice->getIndex() : -1;
            };
            if (! slot->getBoolAttribute ("bypassed", false))
                setDither (depths[std::clamp (index (0), 0, 2)], (dsp::Dither) std::clamp (index (1), 0, 2));
            continue;
        }

        const juce::ScopedLock sl (lock);
        slots[(size_t) index] = { std::move (instance), d, slot->getBoolAttribute ("bypassed", false), 0, {}, editor };
        slots[(size_t) index].health->lastState = state;
    }

    return problems.joinIntoString ("\n").toStdString();
}

//==============================================================================
void MasterChain::prepare (double sampleRate, int maxBlockSize)
{
    const juce::ScopedLock sl (lock);
    rate = sampleRate;
    blockSize = std::max (32, maxBlockSize);
    scratch.setSize (maxChannels, blockSize, false, false, true);
    prepareDelays();

    for (auto& s : slots)
        if (s.usable())
            guard (s, [&] { preparePlugin (*s.plugin, rate, blockSize); });
}

void MasterChain::prepareDelays()
{
    for (size_t i = 0; i < delays.size(); ++i)
    {
        delays[i].setSize (channelCount, maxDelay, false, true, true);
        delays[i].clear();
        delayWrite[i] = 0;
    }
}

void MasterChain::delaySlot (size_t index, int latency, int numChannels, int numSamples, bool replace)
{
    auto& ring = delays[index];
    numChannels = std::min (numChannels, ring.getNumChannels());
    auto& write = delayWrite[index];
    auto copyRing = [&] (int ringStart, int count, int offset, bool toRing)
    {
        for (int ch = 0; ch < numChannels; ++ch)
            if (toRing)
                ring.copyFrom (ch, ringStart, scratch, ch, offset, count);
            else
                scratch.copyFrom (ch, offset, ring, ch, ringStart, count);
    };
    // Both copies go round the ring in at most two parts.
    auto transfer = [&] (int ringPos, bool toRing)
    {
        const auto first = std::min (numSamples, maxDelay - ringPos);
        copyRing (ringPos, first, 0, toRing);
        if (first < numSamples)
            copyRing (0, numSamples - first, first, toRing);
    };

    transfer (write, true);
    if (replace)
        transfer ((write - latency + maxDelay) % maxDelay, false);
    write = (write + numSamples) % maxDelay;
}

void MasterChain::processRealtime (juce::AudioBuffer<float>& buffer, int startSample, int numSamples)
{
    const juce::ScopedTryLock sl (lock);
    if (! sl.isLocked())
    {
        // An edit or a render owns the chain right now; stay silent rather than wait.
        buffer.clear (startSample, numSamples);
        return;
    }

    processChain (buffer, startSample, numSamples, true);

    if (! allBypassed)
    {
        std::array<float*, maxChannels> ptrs {};
        const auto channels = std::min (buffer.getNumChannels(), maxChannels);
        for (int ch = 0; ch < channels; ++ch)
            ptrs[(size_t) ch] = buffer.getWritePointer (ch, startSample);
        liveDither.process (finalBits, (dsp::Dither) finalType.load(), ptrs.data(), channels, numSamples);
    }
}

void MasterChain::processChain (juce::AudioBuffer<float>& io, int startSample, int numSamples, bool listening)
{
    const auto solo = listening ? soloSlot.load() : -1;
    const auto bypassAll = listening && allBypassed.load();
    const auto ioChannels = std::min (io.getNumChannels(), maxChannels);
    const auto g = bypassAll ? 1.0f : gain.load();
    int heard = 0;

    for (int done = 0; done < numSamples;)
    {
        const auto n = std::min (blockSize, numSamples - done);

        for (int ch = 0; ch < maxChannels; ++ch)
        {
            if (ch < ioChannels)
                scratch.copyFrom (ch, 0, io, ch, startSample + done, n);
            else
                scratch.clear (ch, 0, n);
        }

        for (size_t index = 0; index < slots.size(); ++index)
        {
            auto& s = slots[index];
            if (! s.usable() || s.plugin->isSuspended())
                continue;

            const auto channels = std::clamp (std::max (s.plugin->getTotalNumInputChannels(), s.plugin->getTotalNumOutputChannels()),
                                              1, maxChannels);
            const auto runs = ! s.bypassed && ! bypassAll && (solo < 0 || solo == (int) index);
            if (listening)
            {
                // Keeps time: a plugin that is switched off is heard as a delay of its latency.
                const auto latency = s.plugin->getLatencySamples();
                if (latency > 0 && latency + n <= maxDelay)
                {
                    const auto monoOut = s.plugin->getTotalNumOutputChannels() == 1;   // feeds every side, see below
                    delaySlot (index, latency, monoOut ? ioChannels : std::min (channels, ioChannels), n, ! runs);
                    heard += latency;
                }
            }
            if (! runs)
                continue;

            // For the meters at the sides of a stock plugin's frame.
            auto& levels = frameLevels[index];
            auto raise = [&] (size_t k, int ch)
            {
                const auto peak = scratch.getMagnitude (std::min (ch, ioChannels - 1), 0, n);
                if (peak > levels[k].load (std::memory_order_relaxed))
                    levels[k].store (peak, std::memory_order_relaxed);
            };
            if (listening)
            {
                raise (0, 0);
                raise (1, 1);
            }

            juce::AudioBuffer<float> proxy (scratch.getArrayOfWritePointers(), channels, n);
            const auto ok = guard (s, [&] { s.plugin->processBlock (proxy, midi); });
            midi.clear();
            if (! ok)
                continue;

            // A mono-out plugin feeds both sides.
            if (s.plugin->getTotalNumOutputChannels() == 1 && ioChannels > 1)
                scratch.copyFrom (1, 0, scratch, 0, 0, n);

            if (listening)
            {
                raise (2, 0);
                raise (3, 1);
            }

            if (listening)
            {
                float peak = 0.0f;
                for (int ch = 0; ch < ioChannels; ++ch)
                    peak = std::max (peak, scratch.getMagnitude (ch, 0, n));
                if (peak > peaks[index].load (std::memory_order_relaxed))
                    peaks[index].store (peak, std::memory_order_relaxed);
            }
        }

        for (int ch = 0; ch < ioChannels; ++ch)
        {
            io.copyFrom (ch, startSample + done, scratch, ch, 0, n);
            if (std::abs (g - 1.0f) > 1.0e-7f)
                io.applyGain (ch, startSample + done, n, g);
        }

        done += n;
    }

    if (listening)
        heardLatency.store (heard, std::memory_order_relaxed);
}

AudioDataPtr MasterChain::render (const AudioData& data, Frame start, Frame end, std::string& error,
                                  std::atomic<float>* progress, const std::atomic<bool>* cancel, bool withDither)
{
    const auto fileChannels = data.numChannels();
    if (fileChannels < 1 || fileChannels > maxChannels)
    {
        error = "The Master Section renders files with up to " + std::to_string (maxChannels) + " channels.";
        return nullptr;
    }
    const auto ioChannels = std::max (fileChannels, 2);   // a mono file runs as two equal sides

    start = std::clamp<Frame> (start, 0, data.length());
    end = std::clamp<Frame> (end, start, data.length());
    const auto length = end - start;

    const juce::ScopedLock sl (lock);
    std::array<bool, AudioEngine::numMasterSlots> wasUsable {};
    for (size_t i = 0; i < slots.size(); ++i)
        wasUsable[i] = slots[i].usable();
    const auto deviceRate = rate;
    const auto deviceBlock = blockSize;
    const auto deviceChannels = channelCount;
    constexpr int renderBlock = 4096;
    setChannels (ioChannels);
    prepare (data.sampleRate(), renderBlock);

    // Some plugins (look-ahead limiters, for example) only report their latency
    // once they have processed audio, and VST3 hosts learn about the change on
    // the message thread. Run a little silence through, give the message thread
    // a moment, then read the latency. (Called from a worker thread.)
    juce::AudioBuffer<float> io (ioChannels, renderBlock);
    if (! allBypassed)
    {
        for (int pass = 0; pass < 2; ++pass)
        {
            for (int i = 0; i < 4; ++i)
            {
                io.clear();
                processChain (io, 0, renderBlock, false);
            }

            if (! juce::MessageManager::getInstance()->isThisTheMessageThread())
                juce::Thread::sleep (150);
        }
    }

    const auto latency = (Frame) totalLatency();
    // A mono file comes out as stereo when the chain makes its sides differ
    // (Stereoize, a stereo reverb...), as it is heard while listening.
    const auto outChannels = fileChannels == 1 && ! allBypassed ? 2 : fileChannels;
    float sideDifference = 0.0f;
    AudioData::Builder out (outChannels, data.sampleRate());
    const auto ditherBits = withDither && ! allBypassed ? finalBits.load() : 0;
    const auto ditherType = (dsp::Dither) finalType.load();
    renderDither.applied = -1;   // fresh noise shaping state for every render
    Frame consumed = 0, produced = 0, toSkip = latency;
    bool cancelled = false;

    while (produced < length)
    {
        if (cancel != nullptr && cancel->load())
        {
            cancelled = true;
            break;
        }

        io.clear();
        const auto valid = (int) std::clamp<Frame> (end - (start + consumed), 0, renderBlock);
        if (valid > 0)
        {
            data.read (start + consumed, valid, io.getArrayOfWritePointers());
            if (fileChannels == 1)
                io.copyFrom (1, 0, io, 0, 0, valid);
        }
        consumed += renderBlock;

        if (! allBypassed)
            processChain (io, 0, renderBlock, false);

        const auto skip = (int) std::min<Frame> (toSkip, renderBlock);
        toSkip -= skip;
        const auto count = (int) std::min<Frame> (renderBlock - skip, length - produced);
        if (count > 0)
        {
            std::array<const float*, maxChannels> ptrs {};
            for (int ch = 0; ch < outChannels; ++ch)
                ptrs[(size_t) ch] = io.getReadPointer (ch) + skip;
            if (outChannels != fileChannels)
                for (int i = 0; i < count; ++i)
                    sideDifference = std::max (sideDifference, std::abs (ptrs[0][i] - ptrs[1][i]));
            // Last, and after the mono check so its noise does not make two equal sides differ.
            if (ditherBits > 0)
            {
                std::array<float*, maxChannels> write {};
                for (int ch = 0; ch < outChannels; ++ch)
                    write[(size_t) ch] = io.getWritePointer (ch) + skip;
                renderDither.process (ditherBits, ditherType, write.data(), outChannels, count);
            }
            out.append (ptrs.data(), count);
            produced += count;
        }

        if (progress != nullptr)
            progress->store (length > 0 ? (float) produced / (float) length : 1.0f);
    }

    setChannels (deviceChannels);
    prepare (deviceRate, deviceBlock);

    if (cancelled)
    {
        error = "Cancelled";
        return nullptr;
    }

    for (size_t i = 0; i < slots.size(); ++i)
        if (wasUsable[i] && ! slots[i].usable())
        {
            const std::lock_guard<std::mutex> l (slots[i].health->mutex);
            error = slots[i].description.name.toStdString() + " stopped working during the render (" + slots[i].health->why
                  + "). It is switched off; press Reload on its card to load it again.";
            return nullptr;
        }

    auto result = out.finish();
    if (outChannels != fileChannels && sideDifference <= 1.0e-4f)   // both sides the same (below dither level): stay mono
    {
        AudioData::Builder mono (1, result->sampleRate());
        std::vector<float> block (renderBlock), unused (renderBlock);
        std::array<float*, 2> dest { block.data(), unused.data() };
        for (Frame pos = 0; pos < result->length(); pos += renderBlock)
        {
            const auto n = (int) std::min<Frame> (renderBlock, result->length() - pos);
            result->read (pos, n, dest.data());
            const float* src = block.data();
            mono.append (&src, n);
        }
        result = mono.finish();
    }
    return result;
}

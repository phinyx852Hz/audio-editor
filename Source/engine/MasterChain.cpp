// SPDX-License-Identifier: AGPL-3.0-only
#include "MasterChain.h"
#include "../core/Dsp.h"

#include <algorithm>
#include <cstdlib>

//==============================================================================
// The plugin's own editor in a JUCE window (X11, so XWayland on Wayland).
class MasterChain::PluginWindow final : public juce::DocumentWindow
{
public:
    PluginWindow (juce::AudioProcessorEditor* editor, const juce::String& title, std::function<void()> onClose)
        : DocumentWindow (title, juce::Colours::black, DocumentWindow::closeButton), closed (std::move (onClose))
    {
        setUsingNativeTitleBar (true);
        setContentOwned (editor, true);
        setResizable (editor->isResizable(), false);
        centreWithSize (getWidth(), getHeight());
        setVisible (true);
        toFront (true);
    }

    void closeButtonPressed() override
    {
        if (closed)
            closed();
    }

private:
    std::function<void()> closed;
};

//==============================================================================
MasterChain::MasterChain()
{
    juce::addDefaultFormatsToManager (formats);
    scratch.setSize (maxChannels, blockSize);
}

MasterChain::~MasterChain()
{
    for (auto& w : windows)
        w.reset();

    const juce::ScopedLock sl (lock);
    for (auto& s : slots)
        s.plugin.reset();
}

void MasterChain::setConfigFolder (const juce::File& folder)
{
    configFolder = folder;
    configFolder.createDirectory();

    if (auto xml = juce::XmlDocument::parse (pluginListFile()))
        pluginList.recreateFromXml (*xml);
}

//==============================================================================
std::vector<PluginInfo> MasterChain::knownPlugins() const
{
    std::vector<PluginInfo> result;
    for (const auto& d : pluginList.getTypes())
        if (! d.isInstrument)
            result.push_back ({ d.createIdentifierString().toStdString(), d.name.toStdString(),
                                d.manufacturerName.toStdString(), d.pluginFormatName.toStdString(), d.category.toStdString() });

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
        for (const auto& file : format->searchPathsForPlugins (format->getDefaultLocationsToSearch(), true, false))
            scanQueue.add ({ format, file });

    scanIndex = 0;
    pluginsBefore = (int) knownPlugins().size();
    return scanQueue.size();
}

bool MasterChain::scanNext (std::string& fileBeingScanned)
{
    if (scanIndex >= scanQueue.size())
        return false;

    const auto item = scanQueue[scanIndex++];
    fileBeingScanned = item.file.toStdString();

    if (pluginList.getBlacklistedFiles().contains (item.file))
        return true;

    deadMansPedal().replaceWithText (item.file);
    juce::OwnedArray<juce::PluginDescription> found;
    pluginList.scanAndAddFile (item.file, true, found, *item.format);
    deadMansPedal().deleteFile();
    return true;
}

std::string MasterChain::finishScan()
{
    pluginList.scanFinished();
    if (auto xml = pluginList.createXml())
        xml->writeTo (pluginListFile());

    scanQueue.clear();
    const auto total = (int) knownPlugins().size();
    const auto added = std::max (0, total - pluginsBefore);
    auto summary = juce::String (total) + (total == 1 ? " effect plugin" : " effect plugins") + " available";
    if (added > 0)
        summary << " (" << added << " new)";
    if (! pluginList.getBlacklistedFiles().isEmpty())
        summary << ". Skipped " << pluginList.getBlacklistedFiles().size() << " that crashed while scanning";

    return summary.toStdString();
}

//==============================================================================
std::unique_ptr<juce::AudioPluginInstance> MasterChain::createInstance (const juce::PluginDescription& d, juce::String& error) const
{
    auto instance = formats.createPluginInstance (d, rate, blockSize, error);
    if (instance == nullptr)
        return nullptr;

    // Main stereo in and out; side chains and extra buses stay off.
    instance->disableNonMainBuses();
    auto layout = instance->getBusesLayout();
    if (! layout.inputBuses.isEmpty())
        layout.inputBuses.getReference (0) = juce::AudioChannelSet::stereo();
    if (! layout.outputBuses.isEmpty())
        layout.outputBuses.getReference (0) = juce::AudioChannelSet::stereo();
    instance->setBusesLayout (layout);  // plugins that refuse keep their own layout

    return instance;
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

    auto description = pluginList.getTypeForIdentifierString (pluginId);
    if (description == nullptr)
        return "This plugin is not in the plugin list. Scan for plugins again.";

    juce::String error;
    auto instance = createInstance (*description, error);
    if (instance == nullptr)
        return ("Could not load " + description->name + ": " + error).toStdString();

    preparePlugin (*instance, rate, blockSize);
    windows[(size_t) index].reset();

    std::unique_ptr<juce::AudioPluginInstance> old;
    {
        const juce::ScopedLock sl (lock);
        old = std::move (slots[(size_t) index].plugin);
        slots[(size_t) index] = { std::move (instance), *description, false };
    }

    return {};  // `old` is released here, outside the lock
}

void MasterChain::clear (int index)
{
    if (index < 0 || index >= (int) slots.size())
        return;

    windows[(size_t) index].reset();
    std::unique_ptr<juce::AudioPluginInstance> old;
    {
        const juce::ScopedLock sl (lock);
        old = std::move (slots[(size_t) index].plugin);
        slots[(size_t) index] = {};
        if (soloSlot == index)
            soloSlot = -1;
    }
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
    i.format = s.description.pluginFormatName.toStdString();
    i.pluginId = s.description.createIdentifierString().toStdString();
    i.bypassed = s.bypassed;
    i.latencySamples = s.plugin->getLatencySamples();
    i.hasEditor = s.plugin->hasEditor();
    return i;
}

void MasterChain::setGainDb (double db)
{
    gainDecibels = std::clamp (db, -60.0, 24.0);
    gain = (float) dsp::dbToGain (gainDecibels);
}

bool MasterChain::isActive() const
{
    if (allBypassed)
        return false;

    if (std::abs (gainDecibels) > 1.0e-6)
        return true;

    return std::any_of (slots.begin(), slots.end(), [] (const Slot& s) { return s.plugin != nullptr && ! s.bypassed; });
}

int MasterChain::totalLatency() const
{
    int total = 0;
    for (const auto& s : slots)
        if (s.plugin != nullptr && ! s.bypassed)
            total += s.plugin->getLatencySamples();

    return total;
}

//==============================================================================
std::vector<PluginParameterInfo> MasterChain::parameters (int index) const
{
    std::vector<PluginParameterInfo> result;
    if (index < 0 || index >= (int) slots.size() || slots[(size_t) index].plugin == nullptr)
        return result;

    for (auto* p : slots[(size_t) index].plugin->getParameters())
    {
        PluginParameterInfo info;
        info.name = p->getName (64).toStdString();
        info.value = p->getValue();
        info.valueText = (p->getCurrentValueAsText() + " " + p->getLabel()).trim().toStdString();
        info.isBoolean = p->isBoolean();
        const auto steps = p->getNumSteps();
        info.numSteps = steps > 1 && steps <= 1000 ? steps : 0;
        result.push_back (info);
    }

    return result;
}

PluginParameterInfo MasterChain::parameter (int index, int parameter) const
{
    PluginParameterInfo info;
    if (index < 0 || index >= (int) slots.size() || slots[(size_t) index].plugin == nullptr)
        return info;

    const auto& params = slots[(size_t) index].plugin->getParameters();
    if (parameter < 0 || parameter >= params.size())
        return info;

    auto* p = params[parameter];
    info.name = p->getName (64).toStdString();
    info.value = p->getValue();
    info.valueText = (p->getCurrentValueAsText() + " " + p->getLabel()).trim().toStdString();
    info.isBoolean = p->isBoolean();
    const auto steps = p->getNumSteps();
    info.numSteps = steps > 1 && steps <= 1000 ? steps : 0;
    return info;
}

float MasterChain::takePeak (int index)
{
    return index >= 0 && index < (int) peaks.size() ? peaks[(size_t) index].exchange (0.0f, std::memory_order_relaxed) : 0.0f;
}

void MasterChain::setParameter (int index, int parameter, float value)
{
    if (index < 0 || index >= (int) slots.size() || slots[(size_t) index].plugin == nullptr)
        return;

    const auto& params = slots[(size_t) index].plugin->getParameters();
    if (parameter >= 0 && parameter < params.size())
        params[parameter]->setValueNotifyingHost (std::clamp (value, 0.0f, 1.0f));
}

bool MasterChain::canShowWindow (int index) const
{
    const auto* display = std::getenv ("DISPLAY");
    return index >= 0 && index < (int) slots.size() && slots[(size_t) index].plugin != nullptr
        && slots[(size_t) index].plugin->hasEditor() && display != nullptr && *display != 0;
}

void MasterChain::showWindow (int index)
{
    if (! canShowWindow (index))
        return;

    if (auto& w = windows[(size_t) index])
    {
        w->toFront (true);
        return;
    }

    auto* plugin = slots[(size_t) index].plugin.get();
    auto* editor = plugin->createEditorAndMakeActive();
    if (editor == nullptr)
        return;

    windows[(size_t) index] = std::make_unique<PluginWindow> (editor, slots[(size_t) index].description.name + " - " APP_DISPLAY_NAME,
        [this, plugin]
        {
            // Close later: the window cannot delete itself from its own callback.
            juce::MessageManager::callAsync ([this, plugin]
            {
                for (size_t i = 0; i < slots.size(); ++i)
                    if (slots[i].plugin.get() == plugin)
                        windows[i].reset();
            });
        });
}

//==============================================================================
void MasterChain::save() const
{
    if (configFolder == juce::File())
        return;

    juce::XmlElement root ("PHINYX_MASTER_SECTION");
    root.setAttribute ("gainDb", gainDecibels);
    root.setAttribute ("bypassed", (bool) allBypassed);

    for (size_t i = 0; i < slots.size(); ++i)
    {
        const auto& s = slots[i];
        if (s.plugin == nullptr)
            continue;

        auto* slot = root.createNewChildElement ("SLOT");
        slot->setAttribute ("index", (int) i);
        slot->setAttribute ("bypassed", s.bypassed);
        juce::MemoryBlock state;
        s.plugin->getStateInformation (state);
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

    juce::StringArray problems;
    for (auto* slot : xml->getChildWithTagNameIterator ("SLOT"))
    {
        const auto index = slot->getIntAttribute ("index", -1);
        juce::PluginDescription d;
        if (index < 0 || index >= (int) slots.size() || slot->getFirstChildElement() == nullptr
            || ! d.loadFromXml (*slot->getFirstChildElement()))
            continue;

        juce::String error;
        auto instance = createInstance (d, error);
        if (instance == nullptr)
        {
            problems.add (d.name + ": " + error);
            continue;
        }

        juce::MemoryBlock state;
        if (state.fromBase64Encoding (slot->getStringAttribute ("state")) && state.getSize() > 0)
            instance->setStateInformation (state.getData(), (int) state.getSize());

        preparePlugin (*instance, rate, blockSize);
        const juce::ScopedLock sl (lock);
        slots[(size_t) index] = { std::move (instance), d, slot->getBoolAttribute ("bypassed", false) };
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

    for (auto& s : slots)
        if (s.plugin != nullptr)
            preparePlugin (*s.plugin, rate, blockSize);
}

void MasterChain::processRealtime (juce::AudioBuffer<float>& buffer, int startSample, int numSamples)
{
    if (allBypassed)
        return;

    const juce::ScopedTryLock sl (lock);
    if (! sl.isLocked())
    {
        // An edit or a render owns the chain right now; stay silent rather than wait.
        buffer.clear (startSample, numSamples);
        return;
    }

    processChain (buffer, startSample, numSamples, true);
}

void MasterChain::processChain (juce::AudioBuffer<float>& io, int startSample, int numSamples, bool listening)
{
    const auto solo = listening ? soloSlot.load() : -1;
    const auto ioChannels = std::min (io.getNumChannels(), maxChannels);
    const auto g = gain.load();

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
            if (s.plugin == nullptr || s.bypassed || s.plugin->isSuspended() || (solo >= 0 && solo != (int) index))
                continue;

            const auto channels = std::clamp (std::max (s.plugin->getTotalNumInputChannels(), s.plugin->getTotalNumOutputChannels()),
                                              1, maxChannels);
            juce::AudioBuffer<float> proxy (scratch.getArrayOfWritePointers(), channels, n);
            s.plugin->processBlock (proxy, midi);
            midi.clear();

            // A mono-out plugin feeds both sides.
            if (s.plugin->getTotalNumOutputChannels() == 1 && ioChannels > 1)
                scratch.copyFrom (1, 0, scratch, 0, 0, n);

            if (listening)
            {
                float peak = 0.0f;
                for (int ch = 0; ch < std::min (ioChannels, 2); ++ch)
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
}

AudioDataPtr MasterChain::render (const AudioData& data, Frame start, Frame end, std::string& error,
                                  std::atomic<float>* progress, const std::atomic<bool>* cancel)
{
    const auto fileChannels = data.numChannels();
    if (fileChannels < 1 || fileChannels > 2)
    {
        error = "The Master Section renders mono and stereo files for now.";
        return nullptr;
    }

    start = std::clamp<Frame> (start, 0, data.length());
    end = std::clamp<Frame> (end, start, data.length());
    const auto length = end - start;

    const juce::ScopedLock sl (lock);
    const auto deviceRate = rate;
    const auto deviceBlock = blockSize;
    constexpr int renderBlock = 4096;
    prepare (data.sampleRate(), renderBlock);

    // Some plugins (look-ahead limiters, for example) only report their latency
    // once they have processed audio, and VST3 hosts learn about the change on
    // the message thread. Run a little silence through, give the message thread
    // a moment, then read the latency. (Called from a worker thread.)
    juce::AudioBuffer<float> io (2, renderBlock);
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
    AudioData::Builder out (fileChannels, data.sampleRate());
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
            float* ptrs[2] = { io.getWritePointer (0), io.getWritePointer (1) };
            data.read (start + consumed, valid, ptrs);
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
            const float* ptrs[2] = { io.getReadPointer (0) + skip, io.getReadPointer (1) + skip };
            out.append (ptrs, count);
            produced += count;
        }

        if (progress != nullptr)
            progress->store (length > 0 ? (float) produced / (float) length : 1.0f);
    }

    prepare (deviceRate, deviceBlock);

    if (cancelled)
    {
        error = "Cancelled";
        return nullptr;
    }

    return out.finish();
}

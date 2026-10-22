// SPDX-License-Identifier: AGPL-3.0-only
// Instrument plugins (VST3, LV2, CLAP) on the mixer's tracks: the track's MIDI goes
// in, stereo comes out. Made through AudioEngine::loadInstrumentPlugin.
#include "AudioEngine.h"
#include "InstrumentFrame.h"
#include "MasterChain.h"
#include "PluginWindow.h"
#include "../mixer/Instruments.h"

#include <cstdlib>

namespace
{
    class PluginInstrument final : public mixer::Instrument
    {
    public:
        PluginInstrument (std::unique_ptr<juce::AudioPluginInstance> p, std::string id, const stock::Info* s)
            : plugin (std::move (p)), pluginId (std::move (id)), title (plugin->getName().toStdString()), stockInfo (s)
        {
            editor = plugin->hasEditor();
        }

        ~PluginInstrument() override
        {
            window.reset();
            // A plugin that threw is not called again, not even to be deleted.
            if (failed.load())
                (void) plugin.release();
        }

        Kind kind() const override { return Kind::plugin; }
        std::string name() const override { return title; }
        const std::string& id() const { return pluginId; }

        void prepare (double sampleRate, int maxBlock) override
        {
            if (failed.load())
                return;
            guard ([&]
            {
                plugin->releaseResources();
                plugin->setRateAndBufferSizeDetails (sampleRate, maxBlock);
                plugin->prepareToPlay (sampleRate, maxBlock);
            });
            const auto channels = std::max ({ 2, plugin->getTotalNumInputChannels(), plugin->getTotalNumOutputChannels() });
            buffer.setSize (channels, maxBlock);
            midi.ensureSize (2048);
        }

        void process (const mixer::NoteEvent* events, int numEvents, float* left, float* right, int frames) override
        {
            if (failed.load (std::memory_order_relaxed) || frames > buffer.getNumSamples())
                return;
            midi.clear();
            // Letting go and silencing: all notes off (and the pedal up) on every channel.
            const auto off = pendingOff.exchange (0);
            for (int ch = 1; off != 0 && ch <= 16; ++ch)
            {
                midi.addEvent (juce::MidiMessage::controllerEvent (ch, 64, 0), 0);
                midi.addEvent (juce::MidiMessage::allNotesOff (ch), 0);
                if (off > 1)
                    midi.addEvent (juce::MidiMessage::allSoundOff (ch), 0);
            }
            for (int i = 0; i < numEvents; ++i)
            {
                const auto& e = events[i];
                const auto size = (e.status & 0xF0) == 0xC0 || (e.status & 0xF0) == 0xD0 ? 2 : 3;
                const juce::uint8 bytes[3] = { e.status, e.data1, e.data2 };
                midi.addEvent (bytes, size, std::clamp (e.offset, 0, frames - 1));
            }
            for (int c = 0; c < buffer.getNumChannels(); ++c)
                buffer.clear (c, 0, frames);
            juce::AudioBuffer<float> view (buffer.getArrayOfWritePointers(), buffer.getNumChannels(), frames);
            if (! guard ([&] { plugin->processBlock (view, midi); }))
                return;
            const auto outs = plugin->getTotalNumOutputChannels();
            const auto* l = view.getReadPointer (0);
            const auto* r = view.getReadPointer (outs > 1 ? 1 : 0);
            for (int i = 0; i < frames; ++i)
            {
                left[i] += l[i];
                right[i] += r[i];
            }
        }

        void releaseAll() override { pendingOff = std::max (pendingOff.load(), 1); }
        void reset() override { pendingOff = 2; }

        bool hasWindow() const override
        {
            const auto* display = std::getenv ("DISPLAY");
            return editor && ! failed.load() && display != nullptr && *display != 0 && ! x11::connectionLost();
        }

        void openWindow() override
        {
            if (window != nullptr)
                window->toFront (true);
            else
                showWindow();
        }

        void showWindow() override
        {
            if (! hasWindow())
                return;
            // A second press closes it again.
            if (window != nullptr)
            {
                window.reset();
                return;
            }
            guard ([&]
            {
                auto* view = plugin->createEditorAndMakeActive();
                if (view == nullptr)
                    return;
                // A stock instrument keeps its own view, with PHINYX's opacity bar below it.
                juce::Component* content = view;
                if (stockInfo != nullptr && stockInfo->role != stock::Role::effect)
                    content = new InstrumentFrame (view, *stockInfo);
                window = std::make_unique<PluginWindow> (content, content != view || view->isResizable(), juce::String (title) + " - " APP_DISPLAY_NAME,
                    [this] { juce::MessageManager::callAsync ([w = juce::Component::SafePointer<PluginWindow> (window.get()), this]
                    {
                        if (w != nullptr)
                            window.reset();
                    }); },
                    [] (int) {});
                x11::useHostErrorHandler();
            });
        }

        std::string state() const override
        {
            if (failed.load())
                return lastState;
            juce::MemoryBlock block;
            if (! const_cast<PluginInstrument*> (this)->guard ([&] { plugin->getStateInformation (block); }))
                return lastState;
            lastState = block.toBase64Encoding().toStdString();
            return lastState;
        }

        void setState (const std::string& text) override
        {
            juce::MemoryBlock block;
            if (text.empty() || ! block.fromBase64Encoding (text) || block.getSize() == 0)
                return;
            lastState = text;
            guard ([&] { plugin->setStateInformation (block.getData(), (int) block.getSize()); });
        }

        bool setParameter (const std::string& name, float value) override
        {
            const auto wanted = juce::String (name);
            bool found = false;
            guard ([&]
            {
                for (auto* p : plugin->getParameters())
                    if (p->getName (256).equalsIgnoreCase (wanted))
                    {
                        p->setValueNotifyingHost (std::clamp (value, 0.0f, 1.0f));
                        found = true;
                        return;
                    }
            });
            return found;
        }

    private:
        template <typename Fn>
        bool guard (Fn&& fn)
        {
            if (failed.load())
                return false;
            try
            {
                fn();
                return true;
            }
            catch (...) { failed = true; }
            return false;
        }

        std::unique_ptr<juce::AudioPluginInstance> plugin;
        std::string pluginId, title;
        const stock::Info* stockInfo = nullptr;   // a stock instrument's, for its window
        bool editor = false;
        std::atomic<bool> failed { false };
        std::atomic<int> pendingOff { 0 };   // 1 let go, 2 silence
        juce::AudioBuffer<float> buffer;
        juce::MidiBuffer midi;
        std::unique_ptr<PluginWindow> window;
        mutable std::string lastState;
    };
}

std::shared_ptr<mixer::Instrument> AudioEngine::loadInstrumentPlugin (const std::string& pluginId, double sampleRate, std::string& error)
{
    juce::String why;
    auto instance = masterChain().createInstrument (pluginId, sampleRate, 256, why);
    if (instance == nullptr)
    {
        error = why.isEmpty() ? std::string ("the instrument could not be started") : why.toStdString();
        return nullptr;
    }
    return std::make_shared<PluginInstrument> (std::move (instance), pluginId, masterChain().stockInfo (pluginId));
}

std::string AudioEngine::instrumentPluginId (const mixer::Instrument& instrument)
{
    if (instrument.kind() != mixer::Instrument::Kind::plugin)
        return {};
    return static_cast<const PluginInstrument&> (instrument).id();
}

std::string AudioEngine::stockInstrumentId (const char* key) const { return const_cast<AudioEngine*> (this)->masterChain().stockInstrumentId (key); }

std::vector<PluginInfo> AudioEngine::knownInstruments() const { return const_cast<AudioEngine*> (this)->masterChain().instruments(); }

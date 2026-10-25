// SPDX-License-Identifier: AGPL-3.0-only
// Effect plugins (VST3, LV2, LADSPA, CLAP, and the built-in and stock effects) on the
// patchbay's FX buses and the mixer's inserts: stereo in, stereo out. Made through AudioEngine::loadEffectPlugin.
#include "AudioEngine.h"
#include "MasterChain.h"
#include "PluginWindow.h"
#include "../patchbay/PatchBay.h"

#include <algorithm>
#include <cstdlib>

namespace
{
    class PluginEffect final : public mixer::Effect
    {
    public:
        PluginEffect (std::unique_ptr<juce::AudioPluginInstance> p, std::string pluginIdentifier)
            : plugin (std::move (p)), id (std::move (pluginIdentifier)), title (plugin->getName().toStdString())
        {
            editor = plugin->hasEditor();
        }

        ~PluginEffect() override
        {
            window.reset();
            // A plugin that threw is not called again, not even to be deleted.
            if (failed.load())
                (void) plugin.release();
        }

        std::string name() const override { return title; }
        std::string pluginId() const override { return id; }

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
        }

        void process (float* left, float* right, int frames) override
        {
            if (failed.load (std::memory_order_relaxed) || frames > buffer.getNumSamples())
                return;
            if (pendingReset.exchange (false))
                guard ([&] { plugin->reset(); });
            const auto ins = plugin->getTotalNumInputChannels();
            for (int c = 0; c < buffer.getNumChannels(); ++c)
                buffer.clear (c, 0, frames);
            if (ins == 1)
                for (int i = 0; i < frames; ++i)
                    buffer.setSample (0, i, 0.5f * (left[i] + right[i]));
            else
            {
                buffer.copyFrom (0, 0, left, frames);
                buffer.copyFrom (1, 0, right, frames);
            }
            juce::AudioBuffer<float> view (buffer.getArrayOfWritePointers(), buffer.getNumChannels(), frames);
            midi.clear();
            if (! guard ([&] { plugin->processBlock (view, midi); }))
                return;
            const auto outs = plugin->getTotalNumOutputChannels();
            const auto* l = view.getReadPointer (0);
            const auto* r = view.getReadPointer (outs > 1 ? 1 : 0);
            std::copy (l, l + frames, left);
            std::copy (r, r + frames, right);
        }

        void reset() override { pendingReset = true; }

        bool hasWindow() const override
        {
            const auto* display = std::getenv ("DISPLAY");
            return editor && ! failed.load() && display != nullptr && *display != 0 && ! x11::connectionLost();
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
                window = std::make_unique<PluginWindow> (view, view->isResizable(), juce::String (title) + " - " APP_DISPLAY_NAME,
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
            if (! const_cast<PluginEffect*> (this)->guard ([&] { plugin->getStateInformation (block); }))
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

        std::vector<Parameter> parameters() const override
        {
            std::vector<Parameter> list;
            const_cast<PluginEffect*> (this)->guard ([&]
            {
                for (auto* p : plugin->getParameters())
                {
                    Parameter info;
                    info.name = p->getName (64).toStdString();
                    info.value = p->getValue();
                    info.text = (p->getCurrentValueAsText() + " " + p->getLabel()).trim().toStdString();
                    info.toggle = p->isBoolean();
                    const auto steps = p->getNumSteps();
                    info.steps = steps > 1 && steps <= 1000 ? steps : 0;
                    list.push_back (std::move (info));
                }
            });
            return list;
        }

        void setParameter (int index, float value) override
        {
            guard ([&]
            {
                const auto& all = plugin->getParameters();
                if (index >= 0 && index < all.size())
                    all[index]->setValueNotifyingHost (std::clamp (value, 0.0f, 1.0f));
            });
        }

        bool setParameterText (int index, const std::string& text) override
        {
            bool done = false;
            guard ([&]
            {
                const auto& all = plugin->getParameters();
                if (index < 0 || index >= all.size())
                    return;
                auto* p = all[index];
                p->setValueNotifyingHost (std::clamp (p->getValueForText (juce::String (text)), 0.0f, 1.0f));
                done = true;
            });
            return done;
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
        std::string id, title;
        bool editor = false;
        std::atomic<bool> failed { false }, pendingReset { false };
        juce::AudioBuffer<float> buffer;
        juce::MidiBuffer midi;
        std::unique_ptr<PluginWindow> window;
        mutable std::string lastState;
    };
}

std::shared_ptr<mixer::Effect> AudioEngine::loadEffectPlugin (const std::string& pluginId, double sampleRate, std::string& error)
{
    juce::String why;
    auto instance = masterChain().createEffect (pluginId, sampleRate, patchbay::PatchBay::block, why);
    if (instance == nullptr)
    {
        error = why.isEmpty() ? std::string ("the effect could not be started") : why.toStdString();
        return nullptr;
    }
    return std::make_shared<PluginEffect> (std::move (instance), pluginId);
}

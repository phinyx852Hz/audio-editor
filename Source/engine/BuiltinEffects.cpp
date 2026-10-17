// SPDX-License-Identifier: AGPL-3.0-only
#include "BuiltinEffects.h"
#include "../core/Dsp.h"

#include <algorithm>
#include <cmath>

namespace
{
    //==========================================================================
    // Stereoize: the live version of Process > Stereoize. Widens the front left
    // and right from their mid signal, so mono sources get a stereo image while
    // L + R stays the same (it sums back to mono cleanly).
    class StereoizeProcessor final : public juce::AudioPluginInstance
    {
    public:
        static constexpr const char* identifier = "phinyx:stereoize";

        StereoizeProcessor()
            : AudioPluginInstance (BusesProperties().withInput ("Input", juce::AudioChannelSet::stereo())
                                                    .withOutput ("Output", juce::AudioChannelSet::stereo()))
        {
            auto number = [] (int decimals, const char* unit)
            {
                return juce::AudioParameterFloatAttributes()
                    .withLabel (unit)
                    .withStringFromValueFunction ([decimals] (float v, int) { return juce::String (v, decimals); })
                    .withValueFromStringFunction ([] (const juce::String& t) { return t.retainCharacters ("+-.0123456789eE").getFloatValue(); });
            };

            auto w = std::make_unique<juce::AudioParameterFloat> (juce::ParameterID { "width", 1 }, "Width",
                juce::NormalisableRange<float> (0.0f, 100.0f, 0.1f), 60.0f, number (0, "%"));
            auto d = std::make_unique<juce::AudioParameterFloat> (juce::ParameterID { "delay", 1 }, "Delay",
                juce::NormalisableRange<float> (1.0f, (float) dsp::Stereoizer::maxDelayMs, 0.1f), 12.0f, number (1, "ms"));
            auto b = std::make_unique<juce::AudioParameterFloat> (juce::ParameterID { "bass", 1 }, "Bass",
                juce::NormalisableRange<float> (0.0f, 1000.0f, 1.0f, 0.5f), 200.0f,
                juce::AudioParameterFloatAttributes()   // the unit is in the text, so "Off" stands alone
                    .withStringFromValueFunction ([] (float v, int) { return v < 0.5f ? juce::String ("Off") : juce::String (juce::roundToInt (v)) + " Hz"; })
                    .withValueFromStringFunction ([] (const juce::String& t)
                    {
                        return t.trim().equalsIgnoreCase ("off") ? 0.0f : t.retainCharacters ("+-.0123456789eE").getFloatValue();
                    }));

            width = w.get(); delay = d.get(); bass = b.get();
            addHostedParameter (std::move (w));
            addHostedParameter (std::move (d));
            addHostedParameter (std::move (b));
        }

        static juce::PluginDescription description()
        {
            juce::PluginDescription p;
            p.name = p.descriptiveName = "Stereoize";
            p.pluginFormatName = builtin::formatName;
            p.category = "Spatial";
            p.manufacturerName = APP_DISPLAY_NAME;
            p.version = "1";
            p.fileOrIdentifier = identifier;
            p.uniqueId = p.deprecatedUid = 0x50485354;  // "PHST"
            p.isInstrument = false;
            p.numInputChannels = p.numOutputChannels = 2;
            return p;
        }

        void fillInPluginDescription (juce::PluginDescription& d) const override { d = description(); }

        const juce::String getName() const override { return "Stereoize"; }
        bool isBusesLayoutSupported (const BusesLayout& l) const override
        {
            return l.getMainInputChannelSet() == juce::AudioChannelSet::stereo()
                && l.getMainOutputChannelSet() == juce::AudioChannelSet::stereo();
        }

        void prepareToPlay (double sampleRate, int) override
        {
            core.prepare (sampleRate);
            applied = current();
            core.set (applied);
            smoothedWidth.reset (sampleRate, 0.03);
            smoothedWidth.setCurrentAndTargetValue ((float) applied.width);
            fade = 1.0f;
            fadeStep = (float) (1.0 / (0.01 * sampleRate));   // 10 ms
        }

        void releaseResources() override {}

        using AudioPluginInstance::processBlock;
        void processBlock (juce::AudioBuffer<float>& buffer, juce::MidiBuffer&) override
        {
            if (buffer.getNumChannels() < 2)
                return;

            const auto wanted = current();
            smoothedWidth.setTargetValue ((float) wanted.width);
            // The filter can change on the fly; a new delay is switched in while
            // the widening is briefly faded out, so it does not click.
            if (std::abs (wanted.bassHz - applied.bassHz) > 1.0e-6)
            {
                applied.bassHz = wanted.bassHz;
                core.set (applied);
            }

            auto* left = buffer.getWritePointer (0);
            auto* right = buffer.getWritePointer (1);
            for (int i = 0; i < buffer.getNumSamples(); ++i)
            {
                if (fade <= 0.0f && std::abs (wanted.delayMs - applied.delayMs) > 1.0e-6)
                {
                    applied.delayMs = wanted.delayMs;
                    core.set (applied);
                }
                const bool fadingOut = std::abs (wanted.delayMs - applied.delayMs) > 1.0e-6;
                fade = std::clamp (fade + (fadingOut ? -fadeStep : fadeStep), 0.0f, 1.0f);

                const auto mid = 0.5f * (left[i] + right[i]);
                const auto side = smoothedWidth.getNextValue() * fade * core.process (mid);
                left[i] = mid + side;
                right[i] = mid - side;
            }
        }

        double getTailLengthSeconds() const override { return dsp::Stereoizer::maxDelayMs / 1000.0; }
        bool acceptsMidi() const override  { return false; }
        bool producesMidi() const override { return false; }
        juce::AudioProcessorEditor* createEditor() override { return nullptr; }
        bool hasEditor() const override { return false; }

        int getNumPrograms() override { return 1; }
        int getCurrentProgram() override { return 0; }
        void setCurrentProgram (int) override {}
        const juce::String getProgramName (int) override { return {}; }
        void changeProgramName (int, const juce::String&) override {}

        void getStateInformation (juce::MemoryBlock& data) override
        {
            juce::XmlElement xml ("PHINYX_STEREOIZE");
            xml.setAttribute ("width", (double) width->get());
            xml.setAttribute ("delayMs", (double) delay->get());
            xml.setAttribute ("bassHz", (double) bass->get());
            copyXmlToBinary (xml, data);
        }

        void setStateInformation (const void* data, int size) override
        {
            if (auto xml = getXmlFromBinary (data, size); xml != nullptr && xml->hasTagName ("PHINYX_STEREOIZE"))
            {
                *width = (float) xml->getDoubleAttribute ("width", 60.0);
                *delay = (float) xml->getDoubleAttribute ("delayMs", 12.0);
                *bass = (float) xml->getDoubleAttribute ("bassHz", 200.0);
            }
        }

    private:
        dsp::StereoizeSettings current() const
        {
            return { width->get() / 100.0, delay->get(), bass->get() < 0.5f ? 0.0 : (double) bass->get() };
        }

        juce::AudioParameterFloat* width = nullptr;
        juce::AudioParameterFloat* delay = nullptr;
        juce::AudioParameterFloat* bass = nullptr;
        dsp::Stereoizer core;
        dsp::StereoizeSettings applied;
        juce::SmoothedValue<float> smoothedWidth;
        float fade = 1.0f, fadeStep = 0.002f;
    };
}

namespace builtin
{
    std::vector<juce::PluginDescription> descriptions()
    {
        return { StereoizeProcessor::description() };
    }

    bool isBuiltin (const juce::PluginDescription& d)
    {
        return d.pluginFormatName == formatName;
    }

    std::unique_ptr<juce::AudioPluginInstance> create (const juce::PluginDescription& d)
    {
        if (isBuiltin (d) && d.fileOrIdentifier == StereoizeProcessor::identifier)
            return std::make_unique<StereoizeProcessor>();
        return nullptr;
    }
}

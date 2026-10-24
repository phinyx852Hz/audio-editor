// SPDX-License-Identifier: AGPL-3.0-only
#pragma once

#include "BuiltinEffects.h"
#include "DeviceDisplay.h"

#include <juce_audio_processors/juce_audio_processors.h>

// The base of the built-in effects that have knobs on their card and no window
// (BuiltinEffects.cpp and PhiEffects.cpp). Internal to the engine.
namespace builtin
{
    // What the built-in mastering and Φ effects share: one bus of 1 or 2
    // channels in and out, no window, and their parameters saved by ID.
    class MasteringEffect : public juce::AudioPluginInstance
    {
    public:
        MasteringEffect (const char* stateTag)
            : AudioPluginInstance (BusesProperties().withInput ("Input", juce::AudioChannelSet::stereo())
                                                    .withOutput ("Output", juce::AudioChannelSet::stereo())),
              tag (stateTag)
        {
        }

        static juce::PluginDescription describe (const juce::String& name, const char* id, const char* category, int uid)
        {
            juce::PluginDescription p;
            p.name = p.descriptiveName = name;
            p.pluginFormatName = builtin::formatName;
            p.category = category;
            p.manufacturerName = APP_DISPLAY_NAME;
            p.version = "1";
            p.fileOrIdentifier = id;
            p.uniqueId = p.deprecatedUid = uid;
            p.isInstrument = false;
            p.numInputChannels = p.numOutputChannels = 2;
            return p;
        }

        bool isBusesLayoutSupported (const BusesLayout& l) const override
        {
            const auto in = l.getMainInputChannelSet();
            return in == l.getMainOutputChannelSet()
                && (in == juce::AudioChannelSet::mono() || in == juce::AudioChannelSet::stereo());
        }

        // What a Φ effect's window draws; nothing for the others. Called from the message thread.
        virtual DeviceDisplay display() { return {}; }

        void releaseResources() override {}
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
            juce::XmlElement xml (tag);
            for (auto* p : getParameters())
                if (auto* r = dynamic_cast<juce::RangedAudioParameter*> (p))
                    xml.setAttribute (r->getParameterID(), (double) r->convertFrom0to1 (r->getValue()));
            copyXmlToBinary (xml, data);
        }

        void setStateInformation (const void* data, int size) override
        {
            if (auto xml = getXmlFromBinary (data, size); xml != nullptr && xml->hasTagName (tag))
                for (auto* p : getParameters())
                    if (auto* r = dynamic_cast<juce::RangedAudioParameter*> (p); r != nullptr && xml->hasAttribute (r->getParameterID()))
                        r->setValueNotifyingHost (r->convertTo0to1 ((float) xml->getDoubleAttribute (r->getParameterID())));
        }

    protected:
        static juce::AudioParameterFloatAttributes number (int decimals, const char* unit)
        {
            return juce::AudioParameterFloatAttributes()
                .withLabel (unit)
                .withStringFromValueFunction ([decimals] (float v, int) { return juce::String (v, decimals); })
                .withValueFromStringFunction ([] (const juce::String& t) { return t.retainCharacters ("+-.0123456789eE").getFloatValue(); });
        }

    private:
        juce::String tag;
    };
}

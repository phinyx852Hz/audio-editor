// SPDX-License-Identifier: AGPL-3.0-only
#include "BuiltinEffects.h"
#include "../core/Dsp.h"
#include "../core/MidSide.h"

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

    //==========================================================================
    // What the mastering effects below share: one bus of 1 or 2 channels in and
    // out, no window, and their parameters saved by ID.
    class MasteringEffect : public juce::AudioPluginInstance
    {
    public:
        MasteringEffect (const char* stateTag)
            : AudioPluginInstance (BusesProperties().withInput ("Input", juce::AudioChannelSet::stereo())
                                                    .withOutput ("Output", juce::AudioChannelSet::stereo())),
              tag (stateTag)
        {
        }

        static juce::PluginDescription describe (const char* name, const char* id, const char* category, int uid)
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

    //==========================================================================
    // Mid/Side Width: the live version of Process > Mid/Side Width (core/MidSide.h).
    class MidSideProcessor final : public MasteringEffect
    {
    public:
        static constexpr const char* identifier = "phinyx:mid-side-width";

        MidSideProcessor() : MasteringEffect ("PHINYX_MIDSIDE")
        {
            auto w = std::make_unique<juce::AudioParameterFloat> (juce::ParameterID { "width", 1 }, "Width",
                juce::NormalisableRange<float> (0.0f, 300.0f, 1.0f), 100.0f, number (0, "%"));
            auto h = std::make_unique<juce::AudioParameterBool> (juce::ParameterID { "huge", 1 }, "Huge", false);
            auto m = std::make_unique<juce::AudioParameterFloat> (juce::ParameterID { "monoBelow", 1 }, "Mono Below",
                juce::NormalisableRange<float> (0.0f, 500.0f, 1.0f, 0.5f), 0.0f,
                juce::AudioParameterFloatAttributes()
                    .withStringFromValueFunction ([] (float v, int) { return v < 20.0f ? juce::String ("Off") : juce::String (juce::roundToInt (v)) + " Hz"; })
                    .withValueFromStringFunction ([] (const juce::String& t)
                    {
                        return t.trim().equalsIgnoreCase ("off") ? 0.0f : t.retainCharacters ("+-.0123456789eE").getFloatValue();
                    }));
            auto g = std::make_unique<juce::AudioParameterFloat> (juce::ParameterID { "generate", 1 }, "Generate",
                juce::NormalisableRange<float> (0.0f, 100.0f, 1.0f), 0.0f, number (0, "%"));
            auto a = std::make_unique<juce::AudioParameterFloat> (juce::ParameterID { "air", 1 }, "Air",
                juce::NormalisableRange<float> (-6.0f, 6.0f, 0.1f), 0.0f, number (1, "dB"));
            auto r = std::make_unique<juce::AudioParameterFloat> (juce::ParameterID { "rotation", 1 }, "Rotation",
                juce::NormalisableRange<float> (-45.0f, 45.0f, 0.5f), 0.0f, number (1, "°"));
            auto l = std::make_unique<juce::AudioParameterFloat> (juce::ParameterID { "lift", 1 }, "Lift",
                juce::NormalisableRange<float> (0.0f, 100.0f, 1.0f), 0.0f, number (0, "%"));
            auto o = std::make_unique<juce::AudioParameterBool> (juce::ParameterID { "matchLevel", 1 }, "Match Level", true);
            auto li = std::make_unique<juce::AudioParameterChoice> (juce::ParameterID { "listen", 1 }, "Listen",
                juce::StringArray { "Stereo", "Mono", "Side", "Bass" }, 0);
            width = w.get(); huge = h.get(); monoBelow = m.get(); generate = g.get(); air = a.get();
            rotation = r.get(); lift = l.get(); matchLevel = o.get(); listen = li.get();
            addHostedParameter (std::move (w));
            addHostedParameter (std::move (h));
            addHostedParameter (std::move (m));
            addHostedParameter (std::move (g));
            addHostedParameter (std::move (a));
            addHostedParameter (std::move (r));
            addHostedParameter (std::move (l));
            addHostedParameter (std::move (o));
            addHostedParameter (std::move (li));
        }

        static juce::PluginDescription description()
        {
            return describe ("Mid/Side Width", identifier, "Spatial", 0x50484d53);   // "PHMS"
        }
        void fillInPluginDescription (juce::PluginDescription& d) const override { d = description(); }
        const juce::String getName() const override { return "Mid/Side Width"; }
        double getTailLengthSeconds() const override { return 0.01; }

        void prepareToPlay (double sampleRate, int) override
        {
            core.prepare (sampleRate);
            core.set (current());
        }

        using AudioPluginInstance::processBlock;
        void processBlock (juce::AudioBuffer<float>& buffer, juce::MidiBuffer&) override
        {
            if (buffer.getNumChannels() < 2)
                return;
            core.set (current());
            auto* left = buffer.getWritePointer (0);
            auto* right = buffer.getWritePointer (1);
            for (int i = 0; i < buffer.getNumSamples(); ++i)
                core.process (left[i], right[i]);
        }

    private:
        midside::Settings current() const
        {
            midside::Settings s;
            s.width = width->get() / 100.0;
            s.huge = huge->get();
            s.monoBelowHz = monoBelow->get() < 20.0f ? 0.0 : (double) monoBelow->get();
            s.generate = generate->get() / 100.0;
            s.airDb = air->get();
            s.rotation = rotation->get();
            s.lift = lift->get() / 100.0;
            s.matchLevel = matchLevel->get();
            s.listen = (midside::Listen) listen->getIndex();
            return s;
        }

        juce::AudioParameterFloat *width = nullptr, *monoBelow = nullptr, *generate = nullptr, *air = nullptr, *rotation = nullptr, *lift = nullptr;
        juce::AudioParameterBool *huge = nullptr, *matchLevel = nullptr;
        juce::AudioParameterChoice* listen = nullptr;
        midside::Processor core;
    };

    //==========================================================================
    // True Peak Limiter: a look-ahead brickwall limiter that keeps the true peak
    // (as loudness meters and streaming services measure it) under the ceiling.
    class LimiterProcessor final : public MasteringEffect
    {
    public:
        static constexpr const char* identifier = "phinyx:true-peak-limiter";

        LimiterProcessor() : MasteringEffect ("PHINYX_LIMITER")
        {
            auto in = std::make_unique<juce::AudioParameterFloat> (juce::ParameterID { "input", 1 }, "Input",
                juce::NormalisableRange<float> (0.0f, 18.0f, 0.1f), 0.0f, number (1, "dB"));
            auto c = std::make_unique<juce::AudioParameterFloat> (juce::ParameterID { "ceiling", 1 }, "Ceiling",
                juce::NormalisableRange<float> (-12.0f, 0.0f, 0.1f), -1.0f, number (1, "dBTP"));
            auto r = std::make_unique<juce::AudioParameterFloat> (juce::ParameterID { "release", 1 }, "Release",
                juce::NormalisableRange<float> (1.0f, 1000.0f, 1.0f, 0.4f), 100.0f, number (0, "ms"));
            input = in.get(); ceiling = c.get(); release = r.get();
            addHostedParameter (std::move (in));
            addHostedParameter (std::move (c));
            addHostedParameter (std::move (r));

            core.prepare (48000.0, 2);
            setLatencySamples (core.latency());
        }

        static juce::PluginDescription description()
        {
            return describe ("True Peak Limiter", identifier, "Dynamics", 0x5048544c);   // "PHTL"
        }
        void fillInPluginDescription (juce::PluginDescription& d) const override { d = description(); }
        const juce::String getName() const override { return "True Peak Limiter"; }

        void prepareToPlay (double sampleRate, int) override
        {
            core.prepare (sampleRate, std::max (1, getTotalNumInputChannels()));
            core.set (ceiling->get(), release->get());
            setLatencySamples (core.latency());
            inputGain.reset (sampleRate, 0.03);
            inputGain.setCurrentAndTargetValue (juce::Decibels::decibelsToGain (input->get()));
        }

        using AudioPluginInstance::processBlock;
        void processBlock (juce::AudioBuffer<float>& buffer, juce::MidiBuffer&) override
        {
            core.set (ceiling->get(), release->get());
            inputGain.setTargetValue (juce::Decibels::decibelsToGain (input->get()));
            const auto channels = buffer.getNumChannels();
            for (int i = 0; i < buffer.getNumSamples(); ++i)
            {
                const auto g = inputGain.getNextValue();
                for (int ch = 0; ch < channels; ++ch)
                    buffer.getWritePointer (ch)[i] *= g;
            }
            core.process (buffer.getArrayOfWritePointers(), channels, buffer.getNumSamples());
        }

        double getTailLengthSeconds() const override { return 0.01; }

    private:
        juce::AudioParameterFloat* input = nullptr;
        juce::AudioParameterFloat* ceiling = nullptr;
        juce::AudioParameterFloat* release = nullptr;
        dsp::TruePeakLimiter core;
        juce::SmoothedValue<float, juce::ValueSmoothingTypes::Multiplicative> inputGain { 1.0f };
    };

    //==========================================================================
    // Dither: rounds to the bit depth the file will be saved at, with dither noise
    // that hides the rounding distortion. Kept only to read chains saved when it took
    // a slot; the Master Section's final dither does this now.
    class DitherProcessor final : public MasteringEffect
    {
    public:
        static constexpr const char* identifier = "phinyx:dither";

        DitherProcessor() : MasteringEffect ("PHINYX_DITHER")
        {
            auto b = std::make_unique<juce::AudioParameterChoice> (juce::ParameterID { "bits", 1 }, "Bits",
                juce::StringArray { "16", "20", "24" }, 0);
            auto t = std::make_unique<juce::AudioParameterChoice> (juce::ParameterID { "type", 1 }, "Type",
                juce::StringArray { "None", "TPDF", "Shaped" }, 1);
            bits = b.get(); type = t.get();
            addHostedParameter (std::move (b));
            addHostedParameter (std::move (t));
        }

        static juce::PluginDescription description()
        {
            return describe ("Dither", identifier, "Utility", 0x50484454);   // "PHDT"
        }
        void fillInPluginDescription (juce::PluginDescription& d) const override { d = description(); }
        const juce::String getName() const override { return "Dither"; }

        void prepareToPlay (double, int) override { apply(); }

        using AudioPluginInstance::processBlock;
        void processBlock (juce::AudioBuffer<float>& buffer, juce::MidiBuffer&) override
        {
            if (bits->getIndex() != appliedBits || type->getIndex() != appliedType)
                apply();
            core.process (buffer.getArrayOfWritePointers(), buffer.getNumChannels(), buffer.getNumSamples());
        }

        double getTailLengthSeconds() const override { return 0.0; }

    private:
        void apply()
        {
            static constexpr int depths[] { 16, 20, 24 };
            static constexpr dsp::Dither kinds[] { dsp::Dither::none, dsp::Dither::tpdf, dsp::Dither::shaped };
            appliedBits = bits->getIndex();
            appliedType = type->getIndex();
            core.set (depths[std::clamp (appliedBits, 0, 2)], kinds[std::clamp (appliedType, 0, 2)], std::max (1, getTotalNumInputChannels()));
        }

        juce::AudioParameterChoice* bits = nullptr;
        juce::AudioParameterChoice* type = nullptr;
        int appliedBits = -1, appliedType = -1;
        dsp::Quantizer core;
    };
}

namespace builtin
{
    std::vector<juce::PluginDescription> descriptions()
    {
        return { StereoizeProcessor::description(), MidSideProcessor::description(), LimiterProcessor::description() };
    }

    bool isDither (const juce::PluginDescription& d)
    {
        return isBuiltin (d) && d.fileOrIdentifier == DitherProcessor::identifier;
    }

    bool isBuiltin (const juce::PluginDescription& d)
    {
        return d.pluginFormatName == formatName;
    }

    std::unique_ptr<juce::AudioPluginInstance> create (const juce::PluginDescription& d)
    {
        if (isBuiltin (d) && d.fileOrIdentifier == StereoizeProcessor::identifier)
            return std::make_unique<StereoizeProcessor>();
        if (isBuiltin (d) && d.fileOrIdentifier == MidSideProcessor::identifier)
            return std::make_unique<MidSideProcessor>();
        if (isBuiltin (d) && d.fileOrIdentifier == LimiterProcessor::identifier)
            return std::make_unique<LimiterProcessor>();
        if (isBuiltin (d) && d.fileOrIdentifier == DitherProcessor::identifier)
            return std::make_unique<DitherProcessor>();
        return nullptr;
    }
}

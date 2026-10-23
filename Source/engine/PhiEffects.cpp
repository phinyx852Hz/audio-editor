// SPDX-License-Identifier: AGPL-3.0-only
#include "PhiEffects.h"
#include "MasteringEffect.h"
#include "../core/Phi.h"

#include <algorithm>
#include <cmath>

namespace
{
    using builtin::MasteringEffect;

    // "Φ Echo": the names hold a Greek letter, so they are read as UTF-8.
    juce::String named (const char* utf8) { return juce::String::fromUTF8 (utf8); }

    juce::AudioParameterFloatAttributes percent()
    {
        return juce::AudioParameterFloatAttributes()
            .withLabel ("%")
            .withStringFromValueFunction ([] (float v, int) { return juce::String (v, 1); })
            .withValueFromStringFunction ([] (const juce::String& t) { return t.retainCharacters ("+-.0123456789eE").getFloatValue(); });
    }

    juce::NormalisableRange<float> skewed (float low, float high, float interval, float centre)
    {
        juce::NormalisableRange<float> r (low, high, interval);
        r.setSkewForCentre (centre);
        return r;
    }

    //==========================================================================
    // Φ EQ: twelve bands a golden ratio apart, from a Super Low shelf at 80 Hz
    // through ten bells to a Super High shelf at 15.9 kHz.
    class PhiEqProcessor final : public MasteringEffect
    {
    public:
        static constexpr const char* identifier = "phinyx:phi-eq";

        PhiEqProcessor() : MasteringEffect ("PHI_EQ")
        {
            for (int b = 0; b < phi::Eq::numBands; ++b)
            {
                const auto hz = phi::Eq::bandHz (b);
                const auto name = b == 0                         ? juce::String ("Super Low")
                                : b == phi::Eq::numBands - 1     ? juce::String ("Super High")
                                : hz < 1000.0                    ? juce::String (juce::roundToInt (hz)) + " Hz"
                                                                 : juce::String (hz / 1000.0, 1) + " kHz";
                auto p = std::make_unique<juce::AudioParameterFloat> (juce::ParameterID { "band" + juce::String (b), 1 }, name,
                    juce::NormalisableRange<float> (-12.0f, 12.0f, 0.1f), 0.0f, number (1, "dB"));
                bands[(size_t) b] = p.get();
                addHostedParameter (std::move (p));
            }
            auto o = std::make_unique<juce::AudioParameterFloat> (juce::ParameterID { "output", 1 }, "Output",
                juce::NormalisableRange<float> (-12.0f, 12.0f, 0.1f), 0.0f, number (1, "dB"));
            output = o.get();
            addHostedParameter (std::move (o));
        }

        static juce::PluginDescription description()
        {
            return describe (named ("Φ EQ"), identifier, "EQ", 0x50484951);   // "PHIQ"
        }
        void fillInPluginDescription (juce::PluginDescription& d) const override { d = description(); }
        const juce::String getName() const override { return named ("Φ EQ"); }
        double getTailLengthSeconds() const override { return 0.05; }

        void prepareToPlay (double sampleRate, int) override
        {
            core.prepare (sampleRate, std::max (1, getTotalNumInputChannels()));
            core.set (gains());
            outputGain.reset (sampleRate, 0.03);
            outputGain.setCurrentAndTargetValue (juce::Decibels::decibelsToGain (output->get()));
        }

        using AudioPluginInstance::processBlock;
        void processBlock (juce::AudioBuffer<float>& buffer, juce::MidiBuffer&) override
        {
            core.set (gains());
            core.process (buffer.getArrayOfWritePointers(), buffer.getNumChannels(), buffer.getNumSamples());
            outputGain.setTargetValue (juce::Decibels::decibelsToGain (output->get()));
            outputGain.applyGain (buffer, buffer.getNumSamples());
        }

    private:
        std::array<double, phi::Eq::numBands> gains() const
        {
            std::array<double, phi::Eq::numBands> g {};
            for (int b = 0; b < phi::Eq::numBands; ++b)
                g[(size_t) b] = bands[(size_t) b]->get();
            return g;
        }

        std::array<juce::AudioParameterFloat*, phi::Eq::numBands> bands {};
        juce::AudioParameterFloat* output = nullptr;
        phi::Eq core;
        juce::SmoothedValue<float, juce::ValueSmoothingTypes::Multiplicative> outputGain { 1.0f };
    };

    //==========================================================================
    // Φ Echo: taps at time × φⁿ, each 1/φ quieter, panned by the golden angle.
    class PhiEchoProcessor final : public MasteringEffect
    {
    public:
        static constexpr const char* identifier = "phinyx:phi-echo";

        PhiEchoProcessor() : MasteringEffect ("PHI_ECHO")
        {
            auto t = std::make_unique<juce::AudioParameterFloat> (juce::ParameterID { "time", 1 }, "Time",
                skewed (1.0f, (float) (phi::Echo::maxTimeSeconds * 1000.0), 1.0f, 150.0f), 233.0f, number (0, "ms"));
            auto n = std::make_unique<juce::AudioParameterInt> (juce::ParameterID { "taps", 1 }, "Taps", 1, phi::Echo::maxTaps, 5);
            auto d = std::make_unique<juce::AudioParameterFloat> (juce::ParameterID { "decay", 1 }, "Decay",
                juce::NormalisableRange<float> (0.0f, 100.0f, 0.1f), 61.8f, percent());
            auto s = std::make_unique<juce::AudioParameterFloat> (juce::ParameterID { "spread", 1 }, "Spread",
                juce::NormalisableRange<float> (0.0f, 100.0f, 0.1f), 100.0f, percent());
            auto f = std::make_unique<juce::AudioParameterFloat> (juce::ParameterID { "feedback", 1 }, "Feedback",
                juce::NormalisableRange<float> (0.0f, 90.0f, 0.1f), 0.0f, percent());
            auto m = std::make_unique<juce::AudioParameterFloat> (juce::ParameterID { "mix", 1 }, "Mix",
                juce::NormalisableRange<float> (0.0f, 100.0f, 0.1f), 38.2f, percent());
            time = t.get(); taps = n.get(); decay = d.get(); spread = s.get(); feedback = f.get(); mix = m.get();
            // The card shows the first four.
            addHostedParameter (std::move (t));
            addHostedParameter (std::move (d));
            addHostedParameter (std::move (f));
            addHostedParameter (std::move (m));
            addHostedParameter (std::move (n));
            addHostedParameter (std::move (s));
        }

        static juce::PluginDescription description()
        {
            return describe (named ("Φ Echo"), identifier, "Delay", 0x50484945);   // "PHIE"
        }
        void fillInPluginDescription (juce::PluginDescription& d) const override { d = description(); }
        const juce::String getName() const override { return named ("Φ Echo"); }
        double getTailLengthSeconds() const override { return phi::Echo::maxSpanSeconds; }

        void prepareToPlay (double sampleRate, int) override
        {
            core.set (current());
            core.prepare (sampleRate, std::max (1, getTotalNumInputChannels()));
        }

        using AudioPluginInstance::processBlock;
        void processBlock (juce::AudioBuffer<float>& buffer, juce::MidiBuffer&) override
        {
            core.set (current());
            core.process (buffer.getArrayOfWritePointers(), buffer.getNumChannels(), buffer.getNumSamples());
        }

    private:
        phi::Echo::Settings current() const
        {
            phi::Echo::Settings s;
            s.timeMs = time->get();
            s.taps = taps->get();
            s.decay = decay->get() / 100.0;
            s.spread = spread->get() / 100.0;
            s.feedback = feedback->get() / 100.0;
            s.mix = mix->get() / 100.0;
            return s;
        }

        juce::AudioParameterFloat *time = nullptr, *decay = nullptr, *spread = nullptr, *feedback = nullptr, *mix = nullptr;
        juce::AudioParameterInt* taps = nullptr;
        phi::Echo core;
    };

    //==========================================================================
    // Φ Space: a reverb of eight delay lines √φ apart in length.
    class PhiSpaceProcessor final : public MasteringEffect
    {
    public:
        static constexpr const char* identifier = "phinyx:phi-space";

        PhiSpaceProcessor() : MasteringEffect ("PHI_SPACE")
        {
            auto s = std::make_unique<juce::AudioParameterFloat> (juce::ParameterID { "size", 1 }, "Size",
                juce::NormalisableRange<float> (0.0f, 100.0f, 0.1f), 61.8f, percent());
            auto d = std::make_unique<juce::AudioParameterFloat> (juce::ParameterID { "decay", 1 }, "Decay",
                skewed (0.1f, 20.0f, 0.01f, 2.0f), 1.618f, number (2, "s"));
            auto a = std::make_unique<juce::AudioParameterFloat> (juce::ParameterID { "damping", 1 }, "Damping",
                juce::NormalisableRange<float> (0.0f, 100.0f, 0.1f), 38.2f, percent());
            auto p = std::make_unique<juce::AudioParameterFloat> (juce::ParameterID { "preDelay", 1 }, "Pre-delay",
                juce::NormalisableRange<float> (0.0f, 200.0f, 1.0f), 13.0f, number (0, "ms"));
            auto m = std::make_unique<juce::AudioParameterFloat> (juce::ParameterID { "mix", 1 }, "Mix",
                juce::NormalisableRange<float> (0.0f, 100.0f, 0.1f), 23.6f, percent());
            size = s.get(); decay = d.get(); damping = a.get(); preDelay = p.get(); mix = m.get();
            addHostedParameter (std::move (s));
            addHostedParameter (std::move (d));
            addHostedParameter (std::move (a));
            addHostedParameter (std::move (m));
            addHostedParameter (std::move (p));
        }

        static juce::PluginDescription description()
        {
            return describe (named ("Φ Space"), identifier, "Reverb", 0x50484953);   // "PHIS"
        }
        void fillInPluginDescription (juce::PluginDescription& d) const override { d = description(); }
        const juce::String getName() const override { return named ("Φ Space"); }
        double getTailLengthSeconds() const override { return decay->get() + 0.2; }

        void prepareToPlay (double sampleRate, int) override
        {
            core.prepare (sampleRate);
            core.set (current());
        }

        using AudioPluginInstance::processBlock;
        void processBlock (juce::AudioBuffer<float>& buffer, juce::MidiBuffer&) override
        {
            core.set (current());
            core.process (buffer.getArrayOfWritePointers(), buffer.getNumChannels(), buffer.getNumSamples());
        }

    private:
        phi::Space::Settings current() const
        {
            phi::Space::Settings s;
            s.size = size->get() / 100.0;
            s.decaySeconds = decay->get();
            s.damping = damping->get() / 100.0;
            s.preDelayMs = preDelay->get();
            s.mix = mix->get() / 100.0;
            return s;
        }

        juce::AudioParameterFloat *size = nullptr, *decay = nullptr, *damping = nullptr, *preDelay = nullptr, *mix = nullptr;
        phi::Space core;
    };

    //==========================================================================
    // Φ Drift: chorus, tremolo or auto-pan from LFOs at rate × φⁿ, never repeating.
    class PhiDriftProcessor final : public MasteringEffect
    {
    public:
        static constexpr const char* identifier = "phinyx:phi-drift";

        PhiDriftProcessor() : MasteringEffect ("PHI_DRIFT")
        {
            auto o = std::make_unique<juce::AudioParameterChoice> (juce::ParameterID { "mode", 1 }, "Mode",
                juce::StringArray { "Chorus", "Tremolo", "Pan" }, 0);
            auto r = std::make_unique<juce::AudioParameterFloat> (juce::ParameterID { "rate", 1 }, "Rate",
                skewed (0.01f, 20.0f, 0.001f, 1.0f), 0.618f, number (3, "Hz"));
            auto d = std::make_unique<juce::AudioParameterFloat> (juce::ParameterID { "depth", 1 }, "Depth",
                juce::NormalisableRange<float> (0.0f, 100.0f, 0.1f), 61.8f, percent());
            auto m = std::make_unique<juce::AudioParameterFloat> (juce::ParameterID { "mix", 1 }, "Mix",
                juce::NormalisableRange<float> (0.0f, 100.0f, 0.1f), 100.0f, percent());
            mode = o.get(); rate = r.get(); depth = d.get(); mix = m.get();
            addHostedParameter (std::move (o));
            addHostedParameter (std::move (r));
            addHostedParameter (std::move (d));
            addHostedParameter (std::move (m));
        }

        static juce::PluginDescription description()
        {
            return describe (named ("Φ Drift"), identifier, "Modulation", 0x50484944);   // "PHID"
        }
        void fillInPluginDescription (juce::PluginDescription& d) const override { d = description(); }
        const juce::String getName() const override { return named ("Φ Drift"); }
        double getTailLengthSeconds() const override { return phi::Drift::maxDelayMs / 1000.0; }

        void prepareToPlay (double sampleRate, int) override
        {
            core.prepare (sampleRate, std::max (1, getTotalNumInputChannels()));
            core.set (current());
        }

        using AudioPluginInstance::processBlock;
        void processBlock (juce::AudioBuffer<float>& buffer, juce::MidiBuffer&) override
        {
            core.set (current());
            core.process (buffer.getArrayOfWritePointers(), buffer.getNumChannels(), buffer.getNumSamples());
        }

    private:
        phi::Drift::Settings current() const
        {
            return { (phi::Drift::Mode) std::clamp (mode->getIndex(), 0, 2), (double) rate->get(), depth->get() / 100.0, mix->get() / 100.0 };
        }

        juce::AudioParameterChoice* mode = nullptr;
        juce::AudioParameterFloat *rate = nullptr, *depth = nullptr, *mix = nullptr;
        phi::Drift core;
    };

    //==========================================================================
    // Φ Rhythm: a gate that plays the Fibonacci word (or a sparser golden rhythm).
    // It follows the host's tempo and position when there are any, else its own Tempo.
    class PhiRhythmProcessor final : public MasteringEffect
    {
    public:
        static constexpr const char* identifier = "phinyx:phi-rhythm";

        PhiRhythmProcessor() : MasteringEffect ("PHI_RHYTHM")
        {
            auto t = std::make_unique<juce::AudioParameterFloat> (juce::ParameterID { "tempo", 1 }, "Tempo",
                juce::NormalisableRange<float> (20.0f, 400.0f, 0.1f), 120.0f, number (1, "BPM"));
            auto s = std::make_unique<juce::AudioParameterChoice> (juce::ParameterID { "step", 1 }, "Step",
                juce::StringArray { "1/8", "1/16", "1/32" }, 1);
            auto d = std::make_unique<juce::AudioParameterChoice> (juce::ParameterID { "density", 1 }, "Density",
                juce::StringArray { "61.8 %", "38.2 %", "23.6 %", "14.6 %" }, 0);
            auto h = std::make_unique<juce::AudioParameterFloat> (juce::ParameterID { "shift", 1 }, "Shift",
                juce::NormalisableRange<float> (0.0f, 100.0f, 0.1f), 0.0f, percent());
            auto p = std::make_unique<juce::AudioParameterFloat> (juce::ParameterID { "depth", 1 }, "Depth",
                juce::NormalisableRange<float> (0.0f, 100.0f, 0.1f), 100.0f, percent());
            auto m = std::make_unique<juce::AudioParameterFloat> (juce::ParameterID { "smooth", 1 }, "Smooth",
                skewed (0.1f, 50.0f, 0.1f, 5.0f), 3.0f, number (1, "ms"));
            tempo = t.get(); step = s.get(); density = d.get(); shift = h.get(); depth = p.get(); smooth = m.get();
            addHostedParameter (std::move (t));
            addHostedParameter (std::move (d));
            addHostedParameter (std::move (h));
            addHostedParameter (std::move (p));
            addHostedParameter (std::move (s));
            addHostedParameter (std::move (m));
        }

        static juce::PluginDescription description()
        {
            return describe (named ("Φ Rhythm"), identifier, "Rhythm", 0x50484952);   // "PHIR"
        }
        void fillInPluginDescription (juce::PluginDescription& d) const override { d = description(); }
        const juce::String getName() const override { return named ("Φ Rhythm"); }
        double getTailLengthSeconds() const override { return 0.0; }

        void prepareToPlay (double sampleRate, int) override
        {
            core.prepare (sampleRate);
            core.set (current (tempo->get()));
        }

        using AudioPluginInstance::processBlock;
        void processBlock (juce::AudioBuffer<float>& buffer, juce::MidiBuffer&) override
        {
            double bpm = tempo->get();
            if (auto* head = getPlayHead())
                if (const auto position = head->getPosition())
                {
                    if (const auto hostBpm = position->getBpm(); hostBpm.hasValue() && *hostBpm > 0.0)
                        bpm = *hostBpm;
                    if (const auto ppq = position->getPpqPosition(); ppq.hasValue() && position->getIsPlaying())
                        core.setPosition (*ppq);
                }
            core.set (current (bpm));
            core.process (buffer.getArrayOfWritePointers(), buffer.getNumChannels(), buffer.getNumSamples());
        }

    private:
        phi::Rhythm::Settings current (double bpm) const
        {
            static constexpr int steps[] { 2, 4, 8 };
            phi::Rhythm::Settings s;
            s.bpm = bpm;
            s.stepsPerBeat = steps[std::clamp (step->getIndex(), 0, 2)];
            s.densityIndex = density->getIndex();
            s.shift = shift->get() / 100.0;
            s.depth = depth->get() / 100.0;
            s.smoothMs = smooth->get();
            return s;
        }

        juce::AudioParameterFloat *tempo = nullptr, *shift = nullptr, *depth = nullptr, *smooth = nullptr;
        juce::AudioParameterChoice *step = nullptr, *density = nullptr;
        phi::Rhythm core;
    };
}

namespace phifx
{
    std::vector<juce::PluginDescription> descriptions()
    {
        return { PhiEqProcessor::description(), PhiEchoProcessor::description(), PhiSpaceProcessor::description(),
                 PhiDriftProcessor::description(), PhiRhythmProcessor::description() };
    }

    std::unique_ptr<juce::AudioPluginInstance> create (const juce::PluginDescription& d)
    {
        if (! builtin::isBuiltin (d))
            return nullptr;
        const auto& id = d.fileOrIdentifier;
        if (id == PhiEqProcessor::identifier)     return std::make_unique<PhiEqProcessor>();
        if (id == PhiEchoProcessor::identifier)   return std::make_unique<PhiEchoProcessor>();
        if (id == PhiSpaceProcessor::identifier)  return std::make_unique<PhiSpaceProcessor>();
        if (id == PhiDriftProcessor::identifier)  return std::make_unique<PhiDriftProcessor>();
        if (id == PhiRhythmProcessor::identifier) return std::make_unique<PhiRhythmProcessor>();
        return nullptr;
    }
}

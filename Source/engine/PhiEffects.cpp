// SPDX-License-Identifier: AGPL-3.0-only
#include "PhiEffects.h"
#include "MasteringEffect.h"
#include "../core/Phi.h"

#include <algorithm>
#include <array>
#include <atomic>
#include <cmath>
#include <complex>

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

    // The Φ windows' pictures (DeviceDisplay): 128 points from 20 Hz to 20 kHz.
    constexpr int displayPoints = 128;
    double displayHz (int i) { return 20.0 * std::pow (1000.0, (double) i / (displayPoints - 1)); }

    double magnitudeDb (const phi::Biquad& c, double hz, double rate)
    {
        const auto z1 = std::polar (1.0, -2.0 * juce::MathConstants<double>::pi * hz / rate), z2 = z1 * z1;
        const auto h = (c.b0 + c.b1 * z1 + c.b2 * z2) / (1.0 + c.a1 * z1 + c.a2 * z2);
        return 20.0 * std::log10 (std::abs (h) + 1.0e-12);
    }

    std::string decimal (double v, int decimals) { return juce::String (v, decimals).toStdString(); }
    std::string hertz (double hz) { return hz < 1000.0 ? decimal (hz, hz < 100.0 ? 1 : 0) + " Hz" : decimal (hz / 1000.0, 2) + " kHz"; }

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

        DeviceDisplay display() override
        {
            // The response of the twelve bands as they are set, plus the output gain.
            DeviceDisplay d;
            d.kind = DeviceDisplay::Kind::frequency;
            d.lowDb = -15.0f;
            d.highDb = 15.0f;
            phi::Eq view;
            view.prepare (48000.0, 1);
            view.set (gains());
            for (int i = 0; i < displayPoints; ++i)
                d.front.push_back ((float) (view.responseDb (displayHz (i)) + output->get()));
            d.caption = "12 bands at 80 Hz × φⁿ";
            return d;
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

        DeviceDisplay display() override
        {
            // The taps across time, each 1/φ further than the one before and Decay quieter.
            DeviceDisplay d;
            d.kind = DeviceDisplay::Kind::taps;
            const auto s = current();
            const auto span = phi::Echo::tapSeconds (s, s.taps - 1) * 1.1;
            double level = 1.0;
            for (int t = 0; t < s.taps; ++t)
            {
                d.front.push_back ((float) (phi::Echo::tapSeconds (s, t) / span));
                d.front.push_back ((float) level);
                level *= s.decay;
            }
            d.axis = "0 to " + decimal (span, 2) + " s";
            d.caption = "taps at " + decimal (s.timeMs, 0) + " ms × φⁿ";
            return d;
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

        DeviceDisplay display() override
        {
            // How the tail dies away: silent for the pre-delay, then falling 60 dB in Decay.
            DeviceDisplay d;
            d.kind = DeviceDisplay::Kind::curve;
            const auto s = current();
            const auto span = s.preDelayMs / 1000.0 + s.decaySeconds * 1.2;
            for (int i = 0; i < displayPoints; ++i)
            {
                const auto t = span * i / (displayPoints - 1) - s.preDelayMs / 1000.0;
                d.front.push_back (t < 0.0 ? 0.0f : (float) std::max (0.0, 1.0 - t / s.decaySeconds));   // dB down, as a share of 60
            }
            d.axis = "0 to " + decimal (span, 1) + " s";
            d.caption = "8 lines √φ apart · RT60 " + decimal (s.decaySeconds, 2) + " s";
            return d;
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

        DeviceDisplay display() override
        {
            // The LFO over ten of its slowest cycles: three sines at rate × 1, φ and φ², never repeating.
            DeviceDisplay d;
            d.kind = DeviceDisplay::Kind::curve;
            const auto hz = std::max (0.01, (double) rate->get());
            const auto span = 10.0 / hz;
            for (int i = 0; i < displayPoints * 2; ++i)
                d.front.push_back ((float) (0.5 + 0.5 * depth->get() / 100.0 * phi::Drift::lfo (span * i / (displayPoints * 2 - 1), hz, 0.0)));
            d.axis = "0 to " + decimal (span, span < 10.0 ? 1 : 0) + " s";
            d.caption = "LFOs at " + decimal (hz, 3) + " Hz × 1, φ, φ²";
            return d;
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
            playing = core.currentStep();
        }

        DeviceDisplay display() override
        {
            // 34 steps of the pattern from the bar the playing step is in, the playing one marked.
            DeviceDisplay d;
            d.kind = DeviceDisplay::Kind::steps;
            const auto s = current (tempo->get());
            const auto now = playing.load();
            const auto first = now - (now % 34 + 34) % 34;
            for (std::int64_t n = first; n < first + 34; ++n)
            {
                d.front.push_back (phi::hit (n, phi::density (s.densityIndex), s.shift) ? 1.0f : 0.0f);
                d.back.push_back (n == now ? 1.0f : 0.0f);
            }
            d.caption = "density 1/φ" + std::string (s.densityIndex == 0 ? "" : s.densityIndex == 1 ? "²" : s.densityIndex == 2 ? "³" : "⁴")
                      + " · " + decimal (100.0 * phi::density (s.densityIndex), 1) + " % of steps";
            return d;
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
        std::atomic<std::int64_t> playing { 0 };   // the step heard last, for the window
    };

    //==========================================================================
    // Φ Resonator: narrow band-passes at root × φⁿ, fanned out left and right.
    class PhiResonatorProcessor final : public MasteringEffect
    {
    public:
        static constexpr const char* identifier = "phinyx:phi-resonator";

        PhiResonatorProcessor() : MasteringEffect ("PHI_RESONATOR")
        {
            auto r = std::make_unique<juce::AudioParameterFloat> (juce::ParameterID { "root", 1 }, "Root",
                skewed (20.0f, 2000.0f, 0.1f, 220.0f), 220.0f, number (1, "Hz"));
            auto b = std::make_unique<juce::AudioParameterInt> (juce::ParameterID { "bands", 1 }, "Bands", 1, phi::Resonator::maxBands, 5);
            auto q = std::make_unique<juce::AudioParameterFloat> (juce::ParameterID { "sharpness", 1 }, "Sharpness",
                skewed (2.0f, 100.0f, 0.1f, 20.0f), 30.0f, number (1, "Q"));
            auto m = std::make_unique<juce::AudioParameterFloat> (juce::ParameterID { "mix", 1 }, "Mix",
                juce::NormalisableRange<float> (0.0f, 100.0f, 0.1f), 100.0f, percent());
            auto w = std::make_unique<juce::AudioParameterFloat> (juce::ParameterID { "width", 1 }, "Width",
                juce::NormalisableRange<float> (0.0f, 100.0f, 0.1f), 100.0f, percent());
            auto n = std::make_unique<juce::AudioParameterFloat> (juce::ParameterID { "noise", 1 }, "Noise",
                juce::NormalisableRange<float> (0.0f, 100.0f, 0.1f), 0.0f, percent());
            auto o = std::make_unique<juce::AudioParameterFloat> (juce::ParameterID { "output", 1 }, "Output",
                juce::NormalisableRange<float> (-24.0f, 24.0f, 0.1f), 0.0f, number (1, "dB"));
            root = r.get(); bands = b.get(); sharpness = q.get(); mix = m.get(); width = w.get(); noise = n.get(); output = o.get();
            addHostedParameter (std::move (r));
            addHostedParameter (std::move (b));
            addHostedParameter (std::move (q));
            addHostedParameter (std::move (m));
            addHostedParameter (std::move (w));
            addHostedParameter (std::move (n));
            addHostedParameter (std::move (o));
        }

        static juce::PluginDescription description()
        {
            return describe (named ("Φ Resonator"), identifier, "Filter", 0x5048494e);   // "PHIN"
        }
        void fillInPluginDescription (juce::PluginDescription& d) const override { d = description(); }
        const juce::String getName() const override { return named ("Φ Resonator"); }
        double getTailLengthSeconds() const override { return 1.0; }

        void prepareToPlay (double sampleRate, int) override
        {
            core.prepare (sampleRate, std::max (1, getTotalNumInputChannels()));
            core.set (current());
            outputGain.reset (sampleRate, 0.03);
            outputGain.setCurrentAndTargetValue (juce::Decibels::decibelsToGain (output->get()));
        }

        using AudioPluginInstance::processBlock;
        void processBlock (juce::AudioBuffer<float>& buffer, juce::MidiBuffer&) override
        {
            core.set (current());
            core.process (buffer.getArrayOfWritePointers(), buffer.getNumChannels(), buffer.getNumSamples());
            outputGain.setTargetValue (juce::Decibels::decibelsToGain (output->get()));
            outputGain.applyGain (buffer, buffer.getNumSamples());
        }

        DeviceDisplay display() override
        {
            // The band-passes at root × φⁿ, summed.
            DeviceDisplay d;
            d.kind = DeviceDisplay::Kind::frequency;
            d.lowDb = -48.0f;
            d.highDb = 6.0f;
            const auto s = current();
            for (int i = 0; i < displayPoints; ++i)
            {
                double sum = 0.0;
                for (int b = 0; b < s.bands; ++b)
                    if (const auto hz = phi::Resonator::bandHz (s, b); hz < 20000.0)
                        sum += std::pow (10.0, magnitudeDb (phi::Biquad::bandPass (hz, s.q, 48000.0), displayHz (i), 48000.0) / 10.0);
                d.front.push_back ((float) (10.0 * std::log10 (sum + 1.0e-12)));
            }
            d.caption = "bands at " + hertz (s.rootHz) + " × φⁿ";
            return d;
        }

    private:
        phi::Resonator::Settings current() const
        {
            phi::Resonator::Settings s;
            s.rootHz = root->get();
            s.bands = bands->get();
            s.q = sharpness->get();
            s.width = width->get() / 100.0;
            s.noise = noise->get() / 100.0;
            s.mix = mix->get() / 100.0;
            return s;
        }

        juce::AudioParameterFloat *root = nullptr, *sharpness = nullptr, *mix = nullptr, *width = nullptr, *noise = nullptr, *output = nullptr;
        juce::AudioParameterInt* bands = nullptr;
        phi::Resonator core;
        juce::SmoothedValue<float, juce::ValueSmoothingTypes::Multiplicative> outputGain { 1.0f };
    };

    //==========================================================================
    // Φ Stack: copies pitched up by φ, φ², φ³..., fanned out around the original.
    class PhiStackProcessor final : public MasteringEffect
    {
    public:
        static constexpr const char* identifier = "phinyx:phi-stack";

        PhiStackProcessor() : MasteringEffect ("PHI_STACK")
        {
            auto c = std::make_unique<juce::AudioParameterInt> (juce::ParameterID { "copies", 1 }, "Copies", 1, phi::Stack::maxCopies, 4);
            auto d = std::make_unique<juce::AudioParameterFloat> (juce::ParameterID { "decay", 1 }, "Decay",
                juce::NormalisableRange<float> (0.0f, 100.0f, 0.1f), 61.8f, percent());
            auto t = std::make_unique<juce::AudioParameterFloat> (juce::ParameterID { "tone", 1 }, "Tone",
                skewed (200.0f, 20000.0f, 1.0f, 2241.0f), 8000.0f, number (0, "Hz"));
            auto m = std::make_unique<juce::AudioParameterFloat> (juce::ParameterID { "mix", 1 }, "Mix",
                juce::NormalisableRange<float> (0.0f, 100.0f, 0.1f), 100.0f, percent());
            auto w = std::make_unique<juce::AudioParameterFloat> (juce::ParameterID { "width", 1 }, "Width",
                juce::NormalisableRange<float> (0.0f, 100.0f, 0.1f), 100.0f, percent());
            copies = c.get(); decay = d.get(); tone = t.get(); mix = m.get(); width = w.get();
            addHostedParameter (std::move (c));
            addHostedParameter (std::move (d));
            addHostedParameter (std::move (t));
            addHostedParameter (std::move (m));
            addHostedParameter (std::move (w));
        }

        static juce::PluginDescription description()
        {
            return describe (named ("Φ Stack"), identifier, "Pitch Shift", 0x5048494b);   // "PHIK"
        }
        void fillInPluginDescription (juce::PluginDescription& d) const override { d = description(); }
        const juce::String getName() const override { return named ("Φ Stack"); }
        double getTailLengthSeconds() const override { return phi::Stack::grainMs / 1000.0; }

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

        DeviceDisplay display() override
        {
            // The copies by pitch: the original, then × φ, φ², φ³ (8.33 semitones apart), each Decay quieter.
            DeviceDisplay d;
            d.kind = DeviceDisplay::Kind::taps;
            const auto s = current();
            const auto top = 12.0 * std::log2 (phi::power (phi::Stack::maxCopies)) * 1.08;
            double level = 1.0;
            for (int c = 0; c <= s.copies; ++c)
            {
                d.front.push_back ((float) (12.0 * std::log2 (phi::power (c)) / top));
                d.front.push_back ((float) level);
                level *= s.decay;
            }
            d.axis = "0 to " + decimal (top, 0) + " semitones up";
            d.caption = "copies at × φⁿ, +8.33 semitones each";
            return d;
        }

    private:
        phi::Stack::Settings current() const
        {
            phi::Stack::Settings s;
            s.copies = copies->get();
            s.decay = decay->get() / 100.0;
            s.toneHz = tone->get();
            s.mix = mix->get() / 100.0;
            s.width = width->get() / 100.0;
            return s;
        }

        juce::AudioParameterInt* copies = nullptr;
        juce::AudioParameterFloat *decay = nullptr, *tone = nullptr, *mix = nullptr, *width = nullptr;
        phi::Stack core;
    };

    //==========================================================================
    // Φ Tame: the golden EQ, a cut at the fundamental × φ ÷ 2 × 10.
    class PhiTameProcessor final : public MasteringEffect
    {
    public:
        static constexpr const char* identifier = "phinyx:phi-tame";

        PhiTameProcessor() : MasteringEffect ("PHI_TAME")
        {
            auto d = std::make_unique<juce::AudioParameterFloat> (juce::ParameterID { "depth", 1 }, "Depth",
                juce::NormalisableRange<float> (-18.0f, 0.0f, 0.1f), -6.0f, number (1, "dB"));
            auto f = std::make_unique<juce::AudioParameterFloat> (juce::ParameterID { "fundamental", 1 }, "Fundamental",
                skewed ((float) phi::Tame::lowestHz, (float) phi::Tame::highestHz, 0.1f, 60.0f), 55.0f, number (1, "Hz"));
            auto w = std::make_unique<juce::AudioParameterFloat> (juce::ParameterID { "width", 1 }, "Width",
                skewed (0.3f, 10.0f, 0.01f, 1.618f), 1.618f, number (2, "Q"));
            auto a = std::make_unique<juce::AudioParameterBool> (juce::ParameterID { "auto", 1 }, "Find Fundamental", true);
            depth = d.get(); fundamental = f.get(); width = w.get(); automatic = a.get();
            addHostedParameter (std::move (d));
            addHostedParameter (std::move (f));
            addHostedParameter (std::move (w));
            addHostedParameter (std::move (a));
        }

        static juce::PluginDescription description()
        {
            return describe (named ("Φ Tame"), identifier, "EQ", 0x50484954);   // "PHIT"
        }
        void fillInPluginDescription (juce::PluginDescription& d) const override { d = description(); }
        const juce::String getName() const override { return named ("Φ Tame"); }
        double getTailLengthSeconds() const override { return 0.05; }

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
            found = core.fundamental();
        }

        DeviceDisplay display() override
        {
            // The cut at fundamental × φ ÷ 2 × 10, following what it found.
            DeviceDisplay d;
            d.kind = DeviceDisplay::Kind::frequency;
            d.lowDb = -18.0f;
            d.highDb = 6.0f;
            const auto s = current();
            const auto fundamental = s.automatic ? found.load() : s.fundamentalHz;
            const auto cut = phi::Biquad::bell (phi::Tame::cutHz (fundamental), s.q, s.depthDb, 48000.0);
            for (int i = 0; i < displayPoints; ++i)
                d.front.push_back ((float) magnitudeDb (cut, displayHz (i), 48000.0));
            d.caption = "fundamental " + hertz (fundamental) + " → cut at " + hertz (phi::Tame::cutHz (fundamental));
            return d;
        }

    private:
        phi::Tame::Settings current() const
        {
            phi::Tame::Settings s;
            s.automatic = automatic->get();
            s.fundamentalHz = fundamental->get();
            s.depthDb = depth->get();
            s.q = width->get();
            return s;
        }

        juce::AudioParameterFloat *depth = nullptr, *fundamental = nullptr, *width = nullptr;
        juce::AudioParameterBool* automatic = nullptr;
        phi::Tame core;
        std::atomic<double> found { 55.0 };   // the fundamental the cut follows, for the window
    };

    //==========================================================================
    // Φ De-resonator: turns resonances down while they ring.
    class PhiDeresonatorProcessor final : public MasteringEffect
    {
    public:
        static constexpr const char* identifier = "phinyx:phi-deresonator";

        PhiDeresonatorProcessor() : MasteringEffect ("PHI_DERESONATOR")
        {
            auto d = std::make_unique<juce::AudioParameterFloat> (juce::ParameterID { "depth", 1 }, "Depth",
                juce::NormalisableRange<float> (0.0f, 100.0f, 0.1f), 50.0f, percent());
            auto s = std::make_unique<juce::AudioParameterFloat> (juce::ParameterID { "sharpness", 1 }, "Sharpness",
                juce::NormalisableRange<float> (0.0f, 100.0f, 0.1f), 50.0f, percent());
            auto p = std::make_unique<juce::AudioParameterFloat> (juce::ParameterID { "speed", 1 }, "Speed",
                juce::NormalisableRange<float> (0.0f, 100.0f, 0.1f), 50.0f, percent());
            auto m = std::make_unique<juce::AudioParameterFloat> (juce::ParameterID { "mix", 1 }, "Mix",
                juce::NormalisableRange<float> (0.0f, 100.0f, 0.1f), 100.0f, percent());
            auto l = std::make_unique<juce::AudioParameterFloat> (juce::ParameterID { "low", 1 }, "Low",
                skewed (20.0f, 20000.0f, 1.0f, 1000.0f), 80.0f, number (0, "Hz"));
            auto h = std::make_unique<juce::AudioParameterFloat> (juce::ParameterID { "high", 1 }, "High",
                skewed (20.0f, 20000.0f, 1.0f, 1000.0f), 18000.0f, number (0, "Hz"));
            auto e = std::make_unique<juce::AudioParameterBool> (juce::ParameterID { "delta", 1 }, "Delta", false);
            depth = d.get(); sharpness = s.get(); speed = p.get(); mix = m.get(); low = l.get(); high = h.get(); delta = e.get();
            addHostedParameter (std::move (d));
            addHostedParameter (std::move (s));
            addHostedParameter (std::move (p));
            addHostedParameter (std::move (m));
            addHostedParameter (std::move (l));
            addHostedParameter (std::move (h));
            addHostedParameter (std::move (e));

            core.prepare (48000.0, 2);
            setLatencySamples (core.latency());
        }

        static juce::PluginDescription description()
        {
            return describe (named ("Φ De-resonator"), identifier, "Dynamics", 0x50484942);   // "PHIB"
        }
        void fillInPluginDescription (juce::PluginDescription& d) const override { d = description(); }
        const juce::String getName() const override { return named ("Φ De-resonator"); }
        double getTailLengthSeconds() const override { return 0.1; }

        void prepareToPlay (double sampleRate, int) override
        {
            core.prepare (sampleRate, std::max (1, getTotalNumInputChannels()));
            core.set (current());
            setLatencySamples (core.latency());
        }

        using AudioPluginInstance::processBlock;
        void processBlock (juce::AudioBuffer<float>& buffer, juce::MidiBuffer&) override
        {
            core.set (current());
            core.process (buffer.getArrayOfWritePointers(), buffer.getNumChannels(), buffer.getNumSamples());
        }

        DeviceDisplay display() override
        {
            // What it hears (grey) and what it takes away now (gold), live.
            DeviceDisplay d;
            d.kind = DeviceDisplay::Kind::frequency;
            d.lowDb = -24.0f;
            d.highDb = 0.0f;
            std::array<float, phi::Deresonator::pictureSize> level {}, cut {};
            core.picture (level.data(), cut.data());
            d.back.assign (level.begin(), level.end());
            d.front.assign (cut.begin(), cut.end());
            d.caption = "deepest cut " + decimal (*std::min_element (cut.begin(), cut.end()), 1) + " dB · latency "
                      + decimal (1000.0 * core.latency() / std::max (1.0, getSampleRate()), 1) + " ms";
            return d;
        }

    private:
        phi::Deresonator::Settings current() const
        {
            phi::Deresonator::Settings s;
            s.depth = depth->get() / 100.0;
            s.sharpness = sharpness->get() / 100.0;
            s.speed = speed->get() / 100.0;
            s.mix = mix->get() / 100.0;
            s.lowHz = std::min (low->get(), high->get());
            s.highHz = std::max (low->get(), high->get());
            s.delta = delta->get();
            return s;
        }

        juce::AudioParameterFloat *depth = nullptr, *sharpness = nullptr, *speed = nullptr, *mix = nullptr, *low = nullptr, *high = nullptr;
        juce::AudioParameterBool* delta = nullptr;
        phi::Deresonator core;
    };
}

namespace phifx
{
    std::vector<juce::PluginDescription> descriptions()
    {
        return { PhiEqProcessor::description(), PhiEchoProcessor::description(), PhiSpaceProcessor::description(),
                 PhiDriftProcessor::description(), PhiRhythmProcessor::description(), PhiResonatorProcessor::description(),
                 PhiStackProcessor::description(), PhiTameProcessor::description(), PhiDeresonatorProcessor::description() };
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
        if (id == PhiResonatorProcessor::identifier)   return std::make_unique<PhiResonatorProcessor>();
        if (id == PhiStackProcessor::identifier)       return std::make_unique<PhiStackProcessor>();
        if (id == PhiTameProcessor::identifier)        return std::make_unique<PhiTameProcessor>();
        if (id == PhiDeresonatorProcessor::identifier) return std::make_unique<PhiDeresonatorProcessor>();
        return nullptr;
    }
}

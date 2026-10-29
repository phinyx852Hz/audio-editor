// SPDX-License-Identifier: AGPL-3.0-only
#include "PhiEffects.h"
#include "MasteringEffect.h"
#include "../core/Phi.h"
#include "../core/PhiChorus.h"
#include "../core/PhiDynamics.h"
#include "../core/PhiModulation.h"
#include "../core/PhiPhaser.h"

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
    // Φ EQ: twelve bands on a golden grid (80 Hz × φ^(n/4)), starting a golden ratio
    // apart from a Super Low shelf at 80 Hz through ten bells to a Super High shelf at
    // 15.9 kHz. Each band can move along the grid, be widened or narrowed in golden steps
    // and be a bell, shelf, notch or band-pass; low and high cuts and a tilt besides.
    // The first thirteen parameters (the gains and Output) are those of the first Φ EQ.
    class PhiEqProcessor final : public MasteringEffect
    {
    public:
        static constexpr const char* identifier = "phinyx:phi-eq";
        static constexpr int numSteps = phi::Eq::maxStep - phi::Eq::minStep + 1;

        PhiEqProcessor() : MasteringEffect ("PHI_EQ")
        {
            const juce::StringArray typeNames { "Bell", "Low Shelf", "High Shelf", "Notch", "Band-pass", "Off" };
            const juce::StringArray slopeNames { "12 dB/oct", "24 dB/oct", "48 dB/oct" };
            std::vector<std::unique_ptr<juce::RangedAudioParameter>> later;
            for (int b = 0; b < phi::Eq::numBands; ++b)
            {
                const auto name = "Band " + juce::String (b + 1);
                const auto id = "band" + juce::String (b);
                const auto first = phi::Eq::defaultBand (b);
                auto g = std::make_unique<juce::AudioParameterFloat> (juce::ParameterID { id, 1 }, name + " Gain",
                    juce::NormalisableRange<float> (-12.0f, 12.0f, 0.1f), 0.0f, number (1, "dB"));
                auto t = std::make_unique<juce::AudioParameterChoice> (juce::ParameterID { id + "Type", 1 }, name + " Type",
                    typeNames, (int) first.type);
                auto f = std::make_unique<juce::AudioParameterInt> (juce::ParameterID { id + "Hz", 1 }, name + " Freq",
                    phi::Eq::minStep, phi::Eq::maxStep, 4 * b, gridAttributes (phi::Eq::minStep, 0));
                auto* type = t.get();
                auto q = std::make_unique<juce::AudioParameterInt> (juce::ParameterID { id + "Q", 1 }, name + " Q",
                    phi::Eq::minQStep, phi::Eq::maxQStep, 0, juce::AudioParameterIntAttributes()
                        .withStringFromValueFunction ([type] (int v, int) { return juce::String (phi::Eq::stepQ (v, isShelf (type->getIndex())), 2); })
                        .withValueFromStringFunction ([type] (const juce::String& text)
                        {
                            const auto wanted = std::max (0.01, text.retainCharacters ("+-.0123456789eE").getDoubleValue());
                            const auto base = phi::Eq::stepQ (0, isShelf (type->getIndex()));
                            return (int) std::lround (2.0 * std::log (wanted / base) / std::log (phi::ratio));
                        }));
                gains[(size_t) b] = g.get();
                bandHz[(size_t) b] = f.get();
                bandQ[(size_t) b] = q.get();
                types[(size_t) b] = type;
                addHostedParameter (std::move (g));
                later.push_back (std::move (f));
                later.push_back (std::move (q));
                later.push_back (std::move (t));
            }
            auto o = std::make_unique<juce::AudioParameterFloat> (juce::ParameterID { "output", 1 }, "Output",
                juce::NormalisableRange<float> (-12.0f, 12.0f, 0.1f), 0.0f, number (1, "dB"));
            output = o.get();
            addHostedParameter (std::move (o));
            for (auto& p : later)
                addHostedParameter (std::move (p));

            auto tl = std::make_unique<juce::AudioParameterFloat> (juce::ParameterID { "tilt", 1 }, "Tilt",
                juce::NormalisableRange<float> (-6.0f, 6.0f, 0.1f), 0.0f, number (1, "dB"));
            // The cuts: off at the end their name points to, then the grid.
            auto lc = std::make_unique<juce::AudioParameterInt> (juce::ParameterID { "lowCut", 1 }, "Low Cut",
                0, numSteps, 0, gridAttributes (phi::Eq::minStep - 1, -1));
            auto ls = std::make_unique<juce::AudioParameterChoice> (juce::ParameterID { "lowCutSlope", 1 }, "Low Cut Slope", slopeNames, 1);
            auto hc = std::make_unique<juce::AudioParameterInt> (juce::ParameterID { "highCut", 1 }, "High Cut",
                0, numSteps, numSteps, gridAttributes (phi::Eq::minStep, 1));
            auto hs = std::make_unique<juce::AudioParameterChoice> (juce::ParameterID { "highCutSlope", 1 }, "High Cut Slope", slopeNames, 1);
            tilt = tl.get(); lowCut = lc.get(); lowSlope = ls.get(); highCut = hc.get(); highSlope = hs.get();
            addHostedParameter (std::move (tl));
            addHostedParameter (std::move (lc));
            addHostedParameter (std::move (ls));
            addHostedParameter (std::move (hc));
            addHostedParameter (std::move (hs));
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
            // The response as set, plus the output gain, with a node for each band and cut.
            DeviceDisplay d;
            d.kind = DeviceDisplay::Kind::frequency;
            d.lowDb = -15.0f;
            d.highDb = 15.0f;
            const auto s = current();
            phi::Eq view;
            view.prepare (48000.0, 1);
            view.set (s);
            const auto out = (double) output->get();
            for (int i = 0; i < displayPoints; ++i)
                d.front.push_back ((float) (view.responseDb (displayHz (i)) + out));
            for (int step = phi::Eq::minStep; step <= phi::Eq::maxStep; ++step)
                d.grid.push_back ((float) phi::Eq::stepHz (step));
            for (const auto& t : types[0]->choices)
                d.types.push_back (t.toStdString());

            const auto params = getParameters();
            auto index = [&] (juce::AudioProcessorParameter* p) { return params.indexOf (p); };
            auto resetOf = [&] (std::initializer_list<juce::RangedAudioParameter*> ps)
            {
                std::vector<std::pair<int, float>> r;
                for (auto* p : ps)
                    r.emplace_back (index (p), p->getDefaultValue());
                return r;
            };
            for (int b = 0; b < phi::Eq::numBands; ++b)
            {
                const auto& band = s.bands[(size_t) b];
                const bool hasGain = band.type == phi::Eq::Type::bell || isShelf ((int) band.type);
                DeviceDisplay::Handle h;
                h.hz = (float) band.hz;
                h.db = (float) ((hasGain ? band.gainDb : band.type == phi::Eq::Type::off ? 0.0 : view.responseDb (band.hz)) + out);
                h.hzParameter = index (bandHz[(size_t) b]);
                h.dbParameter = hasGain ? index (gains[(size_t) b]) : -1;
                h.qParameter = band.type == phi::Eq::Type::off ? -1 : index (bandQ[(size_t) b]);
                h.typeParameter = index (types[(size_t) b]);
                h.on = band.type != phi::Eq::Type::off;
                h.label = std::to_string (b + 1);
                h.reset = resetOf ({ gains[(size_t) b], bandHz[(size_t) b], bandQ[(size_t) b], types[(size_t) b] });
                h.knobs = { index (gains[(size_t) b]), h.hzParameter, index (bandQ[(size_t) b]), h.typeParameter };
                d.handles.push_back (h);
            }
            for (const bool low : { true, false })
            {
                DeviceDisplay::Handle h;
                const auto hz = low ? s.lowCutHz : s.highCutHz;
                h.on = hz > 0.0;
                h.hz = (float) (h.on ? hz : low ? d.grid.front() : d.grid.back());
                h.db = (float) (h.on ? view.responseDb (hz) + out : 0.0);
                h.hzParameter = index (low ? lowCut : highCut);
                h.offSide = low ? -1 : 1;
                h.label = low ? "L" : "H";
                h.reset = resetOf ({ low ? lowCut : highCut, low ? lowSlope : highSlope });
                h.knobs = { h.hzParameter, index (low ? lowSlope : highSlope) };
                d.handles.push_back (h);
            }
            for (auto* p : { (juce::RangedAudioParameter*) lowCut, (juce::RangedAudioParameter*) highCut,
                             (juce::RangedAudioParameter*) tilt, (juce::RangedAudioParameter*) output })
                d.knobs.push_back (index (p));
            d.caption = "on the golden grid, 80 Hz × φ^(n/4)";
            return d;
        }

    private:
        static bool isShelf (int type) { return type == (int) phi::Eq::Type::lowShelf || type == (int) phi::Eq::Type::highShelf; }

        // A frequency on the grid, from the parameter's value: value + `first` is the step.
        // `offSide` -1: 0 is off (a low cut); 1: the top is off (a high cut).
        static juce::AudioParameterIntAttributes gridAttributes (int first, int offSide)
        {
            return juce::AudioParameterIntAttributes()
                .withStringFromValueFunction ([first, offSide] (int v, int)
                {
                    if ((offSide < 0 && v == 0) || (offSide > 0 && v == numSteps))
                        return juce::String ("Off");
                    return juce::String (hertz (phi::Eq::stepHz (v + first)));
                })
                .withValueFromStringFunction ([first, offSide] (const juce::String& text)
                {
                    if (text.containsIgnoreCase ("off"))
                        return offSide < 0 ? 0 : numSteps;
                    auto hz = text.retainCharacters ("+-.0123456789eE").getDoubleValue();
                    if (text.containsIgnoreCase ("k"))
                        hz *= 1000.0;
                    return phi::Eq::nearestStep (hz) - first;
                });
        }

        static double cutHz (const juce::AudioParameterInt* p, bool low)
        {
            const auto v = p->get();
            if (low ? v == 0 : v == numSteps)
                return 0.0;
            return phi::Eq::stepHz (v + (low ? phi::Eq::minStep - 1 : phi::Eq::minStep));
        }

        static int slopeOf (const juce::AudioParameterChoice* p)
        {
            static constexpr int slopes[] { 12, 24, 48 };
            return slopes[std::clamp (p->getIndex(), 0, 2)];
        }

        phi::Eq::Settings current() const
        {
            phi::Eq::Settings s;
            for (int b = 0; b < phi::Eq::numBands; ++b)
            {
                auto& band = s.bands[(size_t) b];
                band.type = (phi::Eq::Type) std::clamp (types[(size_t) b]->getIndex(), 0, phi::Eq::numTypes - 1);
                band.hz = phi::Eq::stepHz (bandHz[(size_t) b]->get());
                band.q = phi::Eq::stepQ (bandQ[(size_t) b]->get(), isShelf ((int) band.type));
                band.gainDb = gains[(size_t) b]->get();
            }
            s.lowCutHz = cutHz (lowCut, true);
            s.lowCutSlope = slopeOf (lowSlope);
            s.highCutHz = cutHz (highCut, false);
            s.highCutSlope = slopeOf (highSlope);
            s.tiltDb = tilt->get();
            return s;
        }

        std::array<juce::AudioParameterFloat*, phi::Eq::numBands> gains {};
        std::array<juce::AudioParameterInt*, phi::Eq::numBands> bandHz {}, bandQ {};
        std::array<juce::AudioParameterChoice*, phi::Eq::numBands> types {};
        juce::AudioParameterFloat* output = nullptr;
        juce::AudioParameterFloat* tilt = nullptr;
        juce::AudioParameterInt* lowCut = nullptr;
        juce::AudioParameterInt* highCut = nullptr;
        juce::AudioParameterChoice* lowSlope = nullptr;
        juce::AudioParameterChoice* highSlope = nullptr;
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
    // Φ Phaser: 2, 3, 5, 8 or 13 allpass stages tuned φ apart, swept by LFOs at rate × 1 and φ.
    class PhiPhaserProcessor final : public MasteringEffect
    {
    public:
        static constexpr const char* identifier = "phinyx:phi-phaser";

        PhiPhaserProcessor() : MasteringEffect ("PHI_PHASER")
        {
            juce::StringArray stageNames;
            for (const auto n : phi::Phaser::stageChoices)
                stageNames.add (juce::String (n));
            auto st = std::make_unique<juce::AudioParameterChoice> (juce::ParameterID { "stages", 1 }, "Stages", stageNames, 2);
            auto r = std::make_unique<juce::AudioParameterFloat> (juce::ParameterID { "rate", 1 }, "Rate",
                skewed (0.01f, 10.0f, 0.001f, 0.618f), 0.382f, number (3, "Hz"));
            auto d = std::make_unique<juce::AudioParameterFloat> (juce::ParameterID { "depth", 1 }, "Depth",
                juce::NormalisableRange<float> (0.0f, 100.0f, 0.1f), 61.8f, percent());
            auto c = std::make_unique<juce::AudioParameterFloat> (juce::ParameterID { "centre", 1 }, "Centre",
                skewed (50.0f, 5000.0f, 1.0f, 618.0f), 618.0f, number (0, "Hz"));
            auto sp = std::make_unique<juce::AudioParameterFloat> (juce::ParameterID { "spread", 1 }, "Spread",
                juce::NormalisableRange<float> (0.0f, 100.0f, 0.1f), 100.0f, percent());
            auto f = std::make_unique<juce::AudioParameterFloat> (juce::ParameterID { "feedback", 1 }, "Feedback",
                juce::NormalisableRange<float> (-90.0f, 90.0f, 0.1f), 38.2f, percent());
            auto w = std::make_unique<juce::AudioParameterFloat> (juce::ParameterID { "stereo", 1 }, "Stereo",
                juce::NormalisableRange<float> (0.0f, 100.0f, 0.1f), 100.0f, percent());
            auto m = std::make_unique<juce::AudioParameterFloat> (juce::ParameterID { "mix", 1 }, "Mix",
                juce::NormalisableRange<float> (0.0f, 100.0f, 0.1f), 50.0f, percent());
            stages = st.get(); rate = r.get(); depth = d.get(); centre = c.get();
            spread = sp.get(); feedback = f.get(); stereo = w.get(); mix = m.get();
            addHostedParameter (std::move (st));
            addHostedParameter (std::move (r));
            addHostedParameter (std::move (d));
            addHostedParameter (std::move (c));
            addHostedParameter (std::move (sp));
            addHostedParameter (std::move (f));
            addHostedParameter (std::move (w));
            addHostedParameter (std::move (m));
        }

        static juce::PluginDescription description()
        {
            return describe (named ("Φ Phaser"), identifier, "Modulation", 0x50484950);   // "PHIP"
        }
        void fillInPluginDescription (juce::PluginDescription& d) const override { d = description(); }
        const juce::String getName() const override { return named ("Φ Phaser"); }
        double getTailLengthSeconds() const override { return 0.1; }

        void prepareToPlay (double sampleRate, int) override
        {
            core.prepare (sampleRate, std::max (1, getTotalNumInputChannels()));
            core.set (current());
            core.reset();
        }

        using AudioPluginInstance::processBlock;
        void processBlock (juce::AudioBuffer<float>& buffer, juce::MidiBuffer&) override
        {
            core.set (current());
            core.process (buffer.getArrayOfWritePointers(), buffer.getNumChannels(), buffer.getNumSamples());
        }

        DeviceDisplay display() override
        {
            // The notches with the LFOs at rest; the sweep moves them all together.
            DeviceDisplay d;
            d.kind = DeviceDisplay::Kind::frequency;
            d.lowDb = -30.0f;
            d.highDb = 12.0f;
            const auto s = current();
            const auto rateHz = getSampleRate() > 0.0 ? getSampleRate() : 48000.0;
            for (int i = 0; i < displayPoints; ++i)
                d.front.push_back ((float) phi::Phaser::responseDb (s, displayHz (i), rateHz));
            d.caption = juce::String (s.stages).toStdString() + " stages, " + hertz (phi::Phaser::stageHz (s, 0, 0.0))
                      + " to " + hertz (phi::Phaser::stageHz (s, s.stages - 1, 0.0)) + ", LFOs at "
                      + decimal (s.rateHz, 3) + " Hz × 1, φ";
            return d;
        }

    private:
        phi::Phaser::Settings current() const
        {
            phi::Phaser::Settings s;
            s.stages = phi::Phaser::stageChoices[(size_t) std::clamp (stages->getIndex(), 0, phi::Phaser::numStageChoices - 1)];
            s.rateHz = rate->get();
            s.depth = depth->get() / 100.0;
            s.centreHz = centre->get();
            s.spread = spread->get() / 100.0;
            s.feedback = feedback->get() / 100.0;
            s.stereo = stereo->get() / 100.0;
            s.mix = mix->get() / 100.0;
            return s;
        }

        juce::AudioParameterChoice* stages = nullptr;
        juce::AudioParameterFloat *rate = nullptr, *depth = nullptr, *centre = nullptr, *spread = nullptr,
                                  *feedback = nullptr, *stereo = nullptr, *mix = nullptr;
        phi::Phaser core;
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

    //==========================================================================
    // The Φ dynamics (core/PhiDynamics.h): golden ratios, the golden two-stage release.
    juce::AudioParameterIntAttributes ratioAttributes()
    {
        return juce::AudioParameterIntAttributes()
            .withStringFromValueFunction ([] (int v, int) { return juce::String (phi::ratioOf (v), 2) + ":1"; })
            .withValueFromStringFunction ([] (const juce::String& t)
            {
                const auto r = std::max (1.0001, t.upToFirstOccurrenceOf (":", false, false).retainCharacters (".0123456789").getDoubleValue());
                return std::clamp ((int) std::lround (2.0 * std::log (r) / std::log (phi::ratio)), 1, phi::maxRatioStep);
            });
    }

    juce::AudioParameterFloatAttributes decibelAttributes()
    {
        return juce::AudioParameterFloatAttributes()
            .withLabel ("dB")
            .withStringFromValueFunction ([] (float v, int) { return juce::String (v, 1); })
            .withValueFromStringFunction ([] (const juce::String& t) { return t.retainCharacters ("+-.0123456789eE").getFloatValue(); });
    }

    // dB of reduction as the captions show it: "-2.4", and "0.0" rather than "-0.0".
    std::string reductionText (double db) { return db < 0.05 ? std::string ("0.0") : decimal (-db, 1); }

    float defaultOf (const juce::AudioProcessorParameter* p) { return p->getDefaultValue(); }

    std::unique_ptr<juce::AudioParameterFloat> decibels (const char* id, const char* name, float low, float high, float initial)
    {
        return std::make_unique<juce::AudioParameterFloat> (juce::ParameterID { id, 1 }, name,
            juce::NormalisableRange<float> (low, high, 0.1f), initial, decibelAttributes());
    }

    // The reduction history as a curve: 0 dB of reduction at the top, `rangeDb` at the bottom.
    void reductionCurve (DeviceDisplay& d, const phi::ReductionHistory& h, float rangeDb)
    {
        std::array<float, phi::ReductionHistory::historySize> past {};
        h.read (past.data());
        for (const auto r : past)
            d.front.push_back (std::clamp (1.0f - r / rangeDb, 0.0f, 1.0f));
        d.axis = "the last " + decimal (phi::ReductionHistory::seconds, 0) + " s, 0 to -" + decimal (rangeDb, 0) + " dB";
    }

    //==========================================================================
    // Φ Compressor: ratios φ^(n/2), 13 ms attack and 233 ms release (φ⁶ apart), the
    // golden two-stage release, a φ⁴ dB knee.
    class PhiCompressorProcessor final : public MasteringEffect
    {
    public:
        static constexpr const char* identifier = "phinyx:phi-compressor";

        PhiCompressorProcessor() : MasteringEffect ("PHI_COMPRESSOR")
        {
            auto t = decibels ("threshold", "Threshold", -60.0f, 0.0f, -18.0f);
            auto r = std::make_unique<juce::AudioParameterInt> (juce::ParameterID { "ratio", 1 }, "Ratio", 1, phi::maxRatioStep, 2, ratioAttributes());
            auto a = std::make_unique<juce::AudioParameterFloat> (juce::ParameterID { "attack", 1 }, "Attack",
                skewed (0.1f, 233.0f, 0.1f, 13.0f), 13.0f, number (1, "ms"));
            auto l = std::make_unique<juce::AudioParameterFloat> (juce::ParameterID { "release", 1 }, "Release",
                skewed (5.0f, 2584.0f, 1.0f, 233.0f), 233.0f, number (0, "ms"));
            auto k = decibels ("knee", "Knee", 0.0f, 24.0f, 6.9f);
            auto m = decibels ("makeup", "Makeup", 0.0f, 24.0f, 0.0f);
            auto x = std::make_unique<juce::AudioParameterFloat> (juce::ParameterID { "mix", 1 }, "Mix",
                juce::NormalisableRange<float> (0.0f, 100.0f, 0.1f), 100.0f, percent());
            threshold = t.get(); ratio = r.get(); attack = a.get(); release = l.get(); knee = k.get(); makeup = m.get(); mix = x.get();
            addHostedParameter (std::move (t));
            addHostedParameter (std::move (r));
            addHostedParameter (std::move (a));
            addHostedParameter (std::move (l));
            addHostedParameter (std::move (k));
            addHostedParameter (std::move (m));
            addHostedParameter (std::move (x));
        }

        static juce::PluginDescription description()
        {
            return describe (named ("Φ Compressor"), identifier, "Dynamics", 0x50484943);   // "PHIC"
        }
        void fillInPluginDescription (juce::PluginDescription& d) const override { d = description(); }
        const juce::String getName() const override { return named ("Φ Compressor"); }
        double getTailLengthSeconds() const override { return 0.0; }

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
            // The curve, level in across and out up, from -60 to 0 dB; in grey, in = out.
            DeviceDisplay d;
            d.kind = DeviceDisplay::Kind::curve;
            const auto s = current();
            for (int i = 0; i < displayPoints; ++i)
            {
                const auto in = -60.0 + 60.0 * i / (displayPoints - 1);
                d.front.push_back ((float) std::clamp ((phi::Compressor::curveDb (s, in) + 60.0) / 60.0, 0.0, 1.0));
                d.back.push_back ((float) ((in + 60.0) / 60.0));
            }
            d.axis = "in and out, -60 to 0 dB";
            d.caption = "reduction " + reductionText (core.reductionDb()) + " dB at " + juce::String (phi::ratioOf (s.ratioStep), 2).toStdString() + ":1";
            return d;
        }

    private:
        phi::Compressor::Settings current() const
        {
            phi::Compressor::Settings s;
            s.thresholdDb = threshold->get();
            s.ratioStep = ratio->get();
            s.attackMs = attack->get();
            s.releaseMs = release->get();
            s.kneeDb = knee->get();
            s.makeupDb = makeup->get();
            s.mix = mix->get() / 100.0;
            return s;
        }

        juce::AudioParameterFloat *threshold = nullptr, *attack = nullptr, *release = nullptr, *knee = nullptr, *makeup = nullptr, *mix = nullptr;
        juce::AudioParameterInt* ratio = nullptr;
        phi::Compressor core;
    };

    //==========================================================================
    // Φ Multiband: four bands split at 80 Hz × φⁿ, each compressed on its own.
    class PhiMultibandProcessor final : public MasteringEffect
    {
    public:
        static constexpr const char* identifier = "phinyx:phi-multiband";
        static constexpr int numBands = phi::Multiband::numBands;

        PhiMultibandProcessor() : MasteringEffect ("PHI_MULTIBAND")
        {
            static constexpr const char* names[] { "Low", "Low Mid", "High Mid", "High" };
            std::vector<std::unique_ptr<juce::RangedAudioParameter>> gainsLater;
            for (int b = 0; b < numBands; ++b)
            {
                const auto id = juce::String ("band") + juce::String (b);
                auto t = std::make_unique<juce::AudioParameterFloat> (juce::ParameterID { id + "Threshold", 1 }, juce::String (names[b]) + " Threshold",
                    juce::NormalisableRange<float> (-60.0f, 0.0f, 0.1f), -18.0f, decibelAttributes());
                auto g = std::make_unique<juce::AudioParameterFloat> (juce::ParameterID { id + "Gain", 1 }, juce::String (names[b]) + " Gain",
                    juce::NormalisableRange<float> (-12.0f, 12.0f, 0.1f), 0.0f, decibelAttributes());
                thresholds[(size_t) b] = t.get();
                gains[(size_t) b] = g.get();
                addHostedParameter (std::move (t));
                gainsLater.push_back (std::move (g));
            }
            for (auto& g : gainsLater)
                addHostedParameter (std::move (g));

            auto r = std::make_unique<juce::AudioParameterInt> (juce::ParameterID { "ratio", 1 }, "Ratio", 1, phi::maxRatioStep, 2, ratioAttributes());
            auto a = std::make_unique<juce::AudioParameterFloat> (juce::ParameterID { "attack", 1 }, "Attack",
                skewed (0.1f, 233.0f, 0.1f, 13.0f), 13.0f, number (1, "ms"));
            auto l = std::make_unique<juce::AudioParameterFloat> (juce::ParameterID { "release", 1 }, "Release",
                skewed (5.0f, 2584.0f, 1.0f, 233.0f), 233.0f, number (0, "ms"));
            auto x = std::make_unique<juce::AudioParameterFloat> (juce::ParameterID { "mix", 1 }, "Mix",
                juce::NormalisableRange<float> (0.0f, 100.0f, 0.1f), 100.0f, percent());
            ratio = r.get(); attack = a.get(); release = l.get(); mix = x.get();
            addHostedParameter (std::move (r));
            addHostedParameter (std::move (a));
            addHostedParameter (std::move (l));
            addHostedParameter (std::move (x));

            const phi::Multiband::Settings defaults;
            for (int c = 0; c < numBands - 1; ++c)
            {
                auto p = std::make_unique<juce::AudioParameterInt> (juce::ParameterID { "crossover" + juce::String (c + 1), 1 },
                    "Crossover " + juce::String (c + 1), phi::Multiband::minCrossoverStep, phi::Multiband::maxCrossoverStep,
                    defaults.crossoverSteps[(size_t) c], juce::AudioParameterIntAttributes()
                        .withStringFromValueFunction ([] (int v, int) { return juce::String (hertz (phi::Multiband::crossoverHz (v))); })
                        .withValueFromStringFunction ([] (const juce::String& t)
                        {
                            auto hz = std::max (1.0, t.retainCharacters (".0123456789").getDoubleValue());
                            if (t.containsIgnoreCase ("k"))
                                hz *= 1000.0;
                            return std::clamp ((int) std::lround (std::log (hz / 80.0) / std::log (phi::ratio)),
                                               phi::Multiband::minCrossoverStep, phi::Multiband::maxCrossoverStep);
                        }));
                crossovers[(size_t) c] = p.get();
                addHostedParameter (std::move (p));
            }
        }

        static juce::PluginDescription description()
        {
            return describe (named ("Φ Multiband"), identifier, "Dynamics", 0x5048494d);   // "PHIM"
        }
        void fillInPluginDescription (juce::PluginDescription& d) const override { d = description(); }
        const juce::String getName() const override { return named ("Φ Multiband"); }
        double getTailLengthSeconds() const override { return 0.01; }

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
            // Each band's gain less what it takes away now, across frequency, with a node
            // for each band (up and down for its gain) and each crossover (across, on the grid).
            DeviceDisplay d;
            d.kind = DeviceDisplay::Kind::frequency;
            d.lowDb = -15.0f;
            d.highDb = 15.0f;
            const auto s = current();
            const auto hz = phi::Multiband::crossovers (s);
            std::array<double, numBands> level {};
            for (int b = 0; b < numBands; ++b)
                level[(size_t) b] = s.bands[(size_t) b].gainDb - core.reductionDb (b) * s.mix;
            for (int i = 0; i < displayPoints; ++i)
            {
                // Blended over half a golden step either side of each crossover.
                const auto f = displayHz (i);
                double v = level[0];
                for (int c = 0; c < numBands - 1; ++c)
                {
                    const auto t = std::clamp (0.5 + std::log (f / hz[(size_t) c]) / std::log (phi::ratio), 0.0, 1.0);
                    v += (level[(size_t) c + 1] - level[(size_t) c]) * t * t * (3.0 - 2.0 * t);
                }
                d.front.push_back ((float) v);
            }
            for (int step = phi::Multiband::minCrossoverStep; step <= phi::Multiband::maxCrossoverStep; ++step)
                d.grid.push_back ((float) phi::Multiband::crossoverHz (step));

            const auto params = getParameters();
            auto index = [&] (juce::AudioProcessorParameter* p) { return params.indexOf (p); };
            for (int b = 0; b < numBands; ++b)
            {
                const auto from = b == 0 ? 20.0 : hz[(size_t) b - 1], to = b == numBands - 1 ? 20000.0 : hz[(size_t) b];
                DeviceDisplay::Handle h;
                h.hz = (float) std::sqrt (from * to);
                h.db = (float) s.bands[(size_t) b].gainDb;
                h.dbParameter = index (gains[(size_t) b]);
                h.label = std::to_string (b + 1);
                h.reset = { { index (thresholds[(size_t) b]), defaultOf (thresholds[(size_t) b]) },
                            { index (gains[(size_t) b]), defaultOf (gains[(size_t) b]) } };
                h.knobs = { index (thresholds[(size_t) b]), index (gains[(size_t) b]) };
                d.handles.push_back (h);
            }
            for (int c = 0; c < numBands - 1; ++c)
            {
                DeviceDisplay::Handle h;
                h.hz = (float) phi::Multiband::crossoverHz (crossovers[(size_t) c]->get());
                h.db = d.front[(size_t) std::clamp ((int) std::lround ((displayPoints - 1) * std::log (h.hz / 20.0) / std::log (1000.0)), 0, displayPoints - 1)];
                h.hzParameter = index (crossovers[(size_t) c]);
                h.label = "×";
                h.reset = { { h.hzParameter, defaultOf (crossovers[(size_t) c]) } };
                h.knobs = { h.hzParameter };
                d.handles.push_back (h);
            }
            for (auto* p : { (juce::RangedAudioParameter*) ratio, (juce::RangedAudioParameter*) attack, (juce::RangedAudioParameter*) release, (juce::RangedAudioParameter*) mix })
                d.knobs.push_back (index (p));

            std::string taken;
            for (int b = 0; b < numBands; ++b)
                taken += (b == 0 ? "" : " · ") + reductionText (core.reductionDb (b));
            d.caption = "reduction " + taken + " dB";
            return d;
        }

    private:
        phi::Multiband::Settings current() const
        {
            phi::Multiband::Settings s;
            for (int b = 0; b < numBands; ++b)
                s.bands[(size_t) b] = { thresholds[(size_t) b]->get(), gains[(size_t) b]->get() };
            for (int c = 0; c < numBands - 1; ++c)
                s.crossoverSteps[(size_t) c] = crossovers[(size_t) c]->get();
            s.ratioStep = ratio->get();
            s.attackMs = attack->get();
            s.releaseMs = release->get();
            s.mix = mix->get() / 100.0;
            return s;
        }

        std::array<juce::AudioParameterFloat*, numBands> thresholds {}, gains {};
        std::array<juce::AudioParameterInt*, numBands - 1> crossovers {};
        juce::AudioParameterInt* ratio = nullptr;
        juce::AudioParameterFloat *attack = nullptr, *release = nullptr, *mix = nullptr;
        phi::Multiband core;
    };

    //==========================================================================
    // Φ Limiter: true peak, φ² ms of look-ahead, the golden two-stage release.
    class PhiLimiterProcessor final : public MasteringEffect
    {
    public:
        static constexpr const char* identifier = "phinyx:phi-limiter";

        PhiLimiterProcessor() : MasteringEffect ("PHI_LIMITER")
        {
            auto g = decibels ("drive", "Drive", 0.0f, 18.0f, 0.0f);
            auto c = std::make_unique<juce::AudioParameterFloat> (juce::ParameterID { "ceiling", 1 }, "Ceiling",
                juce::NormalisableRange<float> (-12.0f, 0.0f, 0.1f), -1.0f, number (1, "dBTP"));
            auto r = std::make_unique<juce::AudioParameterFloat> (juce::ParameterID { "release", 1 }, "Release",
                skewed (1.0f, 987.0f, 1.0f, 89.0f), 89.0f, number (0, "ms"));
            drive = g.get(); ceiling = c.get(); release = r.get();
            addHostedParameter (std::move (g));
            addHostedParameter (std::move (c));
            addHostedParameter (std::move (r));

            core.prepare (48000.0, 2);
            setLatencySamples (core.latency());
        }

        static juce::PluginDescription description()
        {
            return describe (named ("Φ Limiter"), identifier, "Dynamics", 0x5048494c);   // "PHIL"
        }
        void fillInPluginDescription (juce::PluginDescription& d) const override { d = description(); }
        const juce::String getName() const override { return named ("Φ Limiter"); }
        double getTailLengthSeconds() const override { return 0.01; }

        void prepareToPlay (double sampleRate, int) override
        {
            core.set (current());
            core.prepare (sampleRate, std::max (1, getTotalNumInputChannels()));
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
            // The gain reduction of the last seconds, from the top down.
            DeviceDisplay d;
            d.kind = DeviceDisplay::Kind::curve;
            reductionCurve (d, core.history(), 12.0f);
            d.caption = "reduction " + reductionText (core.reductionDb()) + " dB · ceiling " + decimal (ceiling->get(), 1) + " dBTP";
            return d;
        }

    private:
        phi::Limiter::Settings current() const
        {
            phi::Limiter::Settings s;
            s.driveDb = drive->get();
            s.ceilingDb = ceiling->get();
            s.releaseMs = release->get();
            return s;
        }

        juce::AudioParameterFloat *drive = nullptr, *ceiling = nullptr, *release = nullptr;
        phi::Limiter core;
    };

    //==========================================================================
    // Φ Chorus: 2, 3, 5 or 8 voices, rates √φ apart, spread by the golden angle.
    class PhiChorusProcessor final : public MasteringEffect
    {
    public:
        static constexpr const char* identifier = "phinyx:phi-chorus";

        PhiChorusProcessor() : MasteringEffect ("PHI_CHORUS")
        {
            auto r = std::make_unique<juce::AudioParameterFloat> (juce::ParameterID { "rate", 1 }, "Rate",
                skewed (0.05f, 5.0f, 0.001f, 0.618f), 0.618f, number (3, "Hz"));
            auto d = std::make_unique<juce::AudioParameterFloat> (juce::ParameterID { "depth", 1 }, "Depth",
                juce::NormalisableRange<float> (0.0f, 10.0f, 0.001f), 2.618f, number (2, "ms"));
            auto m = std::make_unique<juce::AudioParameterFloat> (juce::ParameterID { "mix", 1 }, "Mix",
                juce::NormalisableRange<float> (0.0f, 100.0f, 0.1f), 38.2f, percent());
            juce::StringArray counts;
            for (const auto n : phi::Chorus::voiceCounts)
                counts.add (juce::String (n));
            auto v = std::make_unique<juce::AudioParameterChoice> (juce::ParameterID { "voices", 1 }, "Voices", counts, 2);
            auto w = std::make_unique<juce::AudioParameterFloat> (juce::ParameterID { "width", 1 }, "Width",
                juce::NormalisableRange<float> (0.0f, 100.0f, 0.1f), 100.0f, percent());
            auto t = std::make_unique<juce::AudioParameterFloat> (juce::ParameterID { "delay", 1 }, "Delay",
                juce::NormalisableRange<float> (1.0f, 34.0f, 0.1f), 13.0f, number (1, "ms"));
            rate = r.get(); depth = d.get(); mix = m.get(); voices = v.get(); width = w.get(); delay = t.get();
            addHostedParameter (std::move (r));
            addHostedParameter (std::move (d));
            addHostedParameter (std::move (m));
            addHostedParameter (std::move (v));
            addHostedParameter (std::move (w));
            addHostedParameter (std::move (t));
        }

        static juce::PluginDescription description()
        {
            return describe (named ("Φ Chorus"), identifier, "Modulation", 0x50484948);   // "PHIH"
        }
        void fillInPluginDescription (juce::PluginDescription& d) const override { d = description(); }
        const juce::String getName() const override { return named ("Φ Chorus"); }
        double getTailLengthSeconds() const override { return phi::Chorus::maxDelayMs / 1000.0; }

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
            // The voices where their delays are now, the middle one tallest, each further
            // out a golden step shorter.
            DeviceDisplay d;
            d.kind = DeviceDisplay::Kind::taps;
            const auto s = current();
            const auto first = phi::Chorus::voiceDelayMs (s, 0), last = phi::Chorus::voiceDelayMs (s, s.voices - 1) + s.depthMs;
            const auto margin = (last - first) * 0.08 + 0.1, from = std::max (0.0, first - margin), to = last + margin;
            for (int v = 0; v < s.voices; ++v)
            {
                d.front.push_back ((float) std::clamp ((core.delayNowMs (v) - from) / (to - from), 0.0, 1.0));
                d.front.push_back ((float) std::pow (phi::ratio, -std::abs (v - (s.voices - 1) / 2.0) / 2.0));
            }
            d.axis = decimal (from, 1) + " to " + decimal (to, 1) + " ms";
            d.caption = std::to_string (s.voices) + " voices at " + decimal (s.rateHz, 3) + " Hz × √φⁿ";
            return d;
        }

    private:
        phi::Chorus::Settings current() const
        {
            phi::Chorus::Settings s;
            s.rateHz = rate->get();
            s.depthMs = depth->get();
            s.mix = mix->get() / 100.0;
            s.voices = phi::Chorus::voiceCounts[(size_t) std::clamp (voices->getIndex(), 0, 3)];
            s.width = width->get() / 100.0;
            s.delayMs = delay->get();
            return s;
        }

        juce::AudioParameterFloat *rate = nullptr, *depth = nullptr, *mix = nullptr, *width = nullptr, *delay = nullptr;
        juce::AudioParameterChoice* voices = nullptr;
        phi::Chorus core;
    };
    //==========================================================================
    // Φ Modulation (core/PhiModulation.h): Flow, the sequence engine, and three effects that
    // follow it, Space (a spectral remap), Grain (granular texture) and Matrix (an effect
    // network whose routing changes), in that order, each with its own mix. Its window
    // has a page for each; the card shows Flow's rate, ratio and mode and the three mixes.
    class PhiModulationProcessor final : public MasteringEffect
    {
    public:
        static constexpr const char* identifier = "phinyx:phi-modulation";

        // The parameters, in the order the card and the pages use them.
        enum Index
        {
            flowRate, ratio, mode, spaceMix, grainMix, matrixMix, shape, jitter, seed, smooth,
            spaceRatio, anchor, blend, partials, tilt, spaceFlow,
            density, size, spread, pitch, grainWidth, grainFlow, freeze,
            events, time, feedback, tone, drive, glide, matrixWidth, matrixFlow,
            page, numParameters
        };

        PhiModulationProcessor() : MasteringEffect ("PHI_MODULATION")
        {
            auto ranged = [this] (Index i, const char* id, const char* name, juce::NormalisableRange<float> range, float initial,
                                  juce::AudioParameterFloatAttributes attributes)
            {
                auto p = std::make_unique<juce::AudioParameterFloat> (juce::ParameterID { id, 1 }, name, range, initial, attributes);
                floats[(size_t) i] = p.get();
                addHostedParameter (std::move (p));
            };
            auto share = [&] (Index i, const char* id, const char* name, float initial, float most = 100.0f)
            {
                ranged (i, id, name, juce::NormalisableRange<float> (0.0f, most, 0.1f), initial, percent());
            };
            auto choice = [this] (Index i, const char* id, const char* name, juce::StringArray items, int initial)
            {
                auto p = std::make_unique<juce::AudioParameterChoice> (juce::ParameterID { id, 1 }, name, items, initial);
                choices[(size_t) i] = p.get();
                addHostedParameter (std::move (p));
            };

            ranged (flowRate, "rate", "Flow Rate", skewed (0.01f, 20.0f, 0.001f, 0.5f), 0.1f, number (3, "Hz"));
            ranged (ratio, "ratio", "Ratio", skewed (1.05f, 4.0f, 0.001f, 1.618f), (float) phi::ratio, number (3, ""));
            choice (mode, "mode", "Mode", { "Periodic", "Irrational", "Fibonacci", "Hybrid" }, 1);
            share (spaceMix, "spaceMix", "Space Mix", 0.0f);
            share (grainMix, "grainMix", "Grain Mix", 38.2f);
            share (matrixMix, "matrixMix", "Matrix Mix", 23.6f);
            choice (shape, "shape", "Shape", { "Smooth", "Stepped", "Wave" }, 0);
            share (jitter, "jitter", "Jitter", 25.0f);
            share (seed, "seed", "Seed", 0.0f);
            ranged (smooth, "smooth", "Smooth", skewed (0.0f, 1000.0f, 0.1f, 50.0f), 50.0f, number (1, "ms"));

            ranged (spaceRatio, "spaceRatio", "Space Ratio", skewed (1.05f, 4.0f, 0.001f, 1.618f), (float) phi::ratio, number (3, ""));
            ranged (anchor, "anchor", "Anchor", skewed (20.0f, 2000.0f, 0.1f, 220.0f), 220.0f, number (1, "Hz"));
            share (blend, "blend", "Blend", 100.0f);
            share (partials, "partials", "Partials", 100.0f);
            ranged (tilt, "tilt", "Tilt", juce::NormalisableRange<float> (-6.0f, 6.0f, 0.1f), 0.0f, number (1, "dB/oct"));
            share (spaceFlow, "spaceFlow", "Space Flow", 0.0f);

            ranged (density, "density", "Density", skewed (1.0f, 89.0f, 0.1f, 13.0f), 13.0f, number (1, "/s"));
            ranged (size, "size", "Size", skewed (10.0f, 500.0f, 0.1f, 89.0f), 89.0f, number (1, "ms"));
            ranged (spread, "spread", "Spread", juce::NormalisableRange<float> (0.0f, 5.0f, 0.01f), 1.0f, number (2, "s"));
            ranged (pitch, "pitch", "Pitch", juce::NormalisableRange<float> (0.0f, 12.0f, 0.01f), 0.0f, number (2, "st"));
            share (grainWidth, "grainWidth", "Grain Width", 100.0f);
            share (grainFlow, "grainFlow", "Grain Flow", 38.2f);
            {
                auto p = std::make_unique<juce::AudioParameterBool> (juce::ParameterID { "freeze", 1 }, "Freeze", false);
                frozen = p.get();
                addHostedParameter (std::move (p));
            }

            ranged (events, "events", "Events", skewed (0.05f, 8.0f, 0.001f, 0.5f), 0.382f, number (3, "Hz"));
            ranged (time, "time", "Time", skewed (10.0f, 1000.0f, 0.1f, 233.0f), 233.0f, number (1, "ms"));
            share (feedback, "feedback", "Feedback", 61.8f, 95.0f);
            ranged (tone, "tone", "Tone", skewed (100.0f, 8000.0f, 1.0f, 1000.0f), 1000.0f, number (0, "Hz"));
            ranged (drive, "drive", "Drive", juce::NormalisableRange<float> (0.0f, 36.0f, 0.1f), 12.0f, number (1, "dB"));
            ranged (glide, "glide", "Glide", skewed (5.0f, 2000.0f, 0.1f, 89.0f), 89.0f, number (1, "ms"));
            share (matrixWidth, "matrixWidth", "Matrix Width", 100.0f);
            share (matrixFlow, "matrixFlow", "Matrix Flow", 50.0f);

            choice (page, "page", "Page", { "Flow", "Space", "Grain", "Matrix" }, 0);

            space.prepare (48000.0, 2);
            setLatencySamples (space.latency());
        }

        static juce::PluginDescription description()
        {
            return describe (named ("Φ Modulation"), identifier, "Modulation", 0x5048494f);   // "PHIO"
        }
        void fillInPluginDescription (juce::PluginDescription& d) const override { d = description(); }
        const juce::String getName() const override { return named ("Φ Modulation"); }
        double getTailLengthSeconds() const override { return 2.0 * phi::Matrix::maxDelaySeconds; }

        void prepareToPlay (double sampleRate, int) override
        {
            const auto channels = std::max (1, getTotalNumInputChannels());
            flow.prepare (sampleRate);
            space.prepare (sampleRate, channels);
            grain.prepare (sampleRate, channels);
            matrix.prepare (sampleRate, channels);
            const auto f = flowSettings();
            flow.set (f);
            grain.set (grainSettings(), f);
            matrix.set (matrixSettings(), f);
            matrix.reset();
            setLatencySamples (space.latency());
        }

        using AudioPluginInstance::processBlock;
        void processBlock (juce::AudioBuffer<float>& buffer, juce::MidiBuffer&) override
        {
            const auto f = flowSettings();
            flow.set (f);
            auto* const* channels = buffer.getArrayOfWritePointers();
            const auto n = buffer.getNumChannels(), frames = buffer.getNumSamples();

            // Playing from the same place plays the same: when the host starts or jumps, Flow
            // and Matrix's routing move to where they would be that far from the start.
            bool playing = false;
            if (auto* head = getPlayHead())
                if (const auto position = head->getPosition(); position && position->getIsPlaying())
                    if (const auto seconds = position->getTimeInSeconds(); seconds.hasValue())
                    {
                        playing = true;
                        if (! wasPlaying || std::abs (*seconds - expectedSeconds) > 0.01)
                        {
                            flow.locate (*seconds);
                            matrix.set (matrixSettings(), f);
                            matrix.locate (*seconds);
                        }
                        expectedSeconds = *seconds + frames / std::max (1.0, getSampleRate());
                    }
            wasPlaying = playing;

            // Flow output 1 moves Space's blend; 0 and 3 Grain's start and size; 2 Matrix's tone.
            auto s = spaceSettings();
            const auto amount = value (spaceFlow) / 100.0;
            s.blend *= 1.0 - amount + amount * flow.output (1);
            space.set (s);
            space.process (channels, n, frames);
            grain.set (grainSettings(), f);
            grain.process (channels, n, frames, flow.output (0), flow.output (3));
            matrix.set (matrixSettings(), f);
            matrix.process (channels, n, frames, flow.output (2));

            flow.advance (frames);
            for (int k = 0; k < phi::Flow::numOutputs; ++k)
                shownPosition[(size_t) k].store (flow.position (k));
            shownBlend.store (s.blend);
        }

        DeviceDisplay display() override
        {
            DeviceDisplay d;
            d.pages = { "Flow", "Space", "Grain", "Matrix" };
            d.pageKnobs = { { flowRate, ratio, mode, shape, jitter, seed, smooth },
                            { spaceMix, spaceRatio, anchor, blend, partials, tilt, spaceFlow },
                            { grainMix, density, size, spread, pitch, grainWidth, grainFlow, freeze },
                            { matrixMix, events, time, feedback, tone, drive, glide, matrixWidth, matrixFlow } };
            d.pageParameter = page;
            d.page = std::clamp (choices[page]->getIndex(), 0, 3);
            const auto f = flowSettings();
            static const char* modeNames[] { "Periodic", "Irrational", "Fibonacci", "Hybrid" };

            if (d.page == 0)
            {
                // The next 13 events of output 1 (gold) and, over the same time, output 2 (grey).
                d.kind = DeviceDisplay::Kind::curve;
                const auto slow = std::max (1.0e-6, phi::Flow::outputRate (f, 0));
                const auto seconds = 13.0 / slow;
                const auto from0 = shownPosition[0].load(), from1 = shownPosition[1].load();
                constexpr int points = displayPoints * 2;
                for (int i = 0; i < points; ++i)
                {
                    const auto t = seconds * i / (points - 1);
                    d.front.push_back ((float) phi::Flow::at (f, 0, from0 + t * slow));
                    d.back.push_back ((float) phi::Flow::at (f, 1, from1 + t * phi::Flow::outputRate (f, 1)));
                }
                d.axis = "the next " + decimal (seconds, seconds < 10.0 ? 1 : 0) + " s";
                d.caption = std::string (modeNames[(int) f.mode]) + " · ratio " + decimal (phi::Flow::ratioOf (f), 3) + " · outputs at";
                for (int k = 0; k < phi::Flow::numOutputs; ++k)
                    d.caption += (k == 0 ? " " : ", ") + decimal (phi::Flow::outputRate (f, k), 3);
                d.caption += " Hz";
            }
            else if (d.page == 1)
            {
                // What it hears (grey) and what it makes (gold).
                d.kind = DeviceDisplay::Kind::frequency;
                d.lowDb = -90.0f;
                d.highDb = 0.0f;
                std::array<float, phi::SpectralSpace::pictureSize> heard {}, made {};
                space.picture (heard.data(), made.data());
                d.back.assign (heard.begin(), heard.end());
                d.front.assign (made.begin(), made.end());
                auto s = spaceSettings();
                s.blend = shownBlend.load();
                const auto a = s.anchorHz;
                d.caption = hertz (a) + ", " + hertz (2.0 * a) + ", " + hertz (4.0 * a) + " → " + hertz (a) + ", "
                          + hertz (phi::SpectralSpace::mappedHz (s, 2.0 * a)) + ", " + hertz (phi::SpectralSpace::mappedHz (s, 4.0 * a))
                          + " · " + std::to_string (space.partialsFound()) + " partials · latency "
                          + decimal (1000.0 * space.latency() / std::max (1.0, getSampleRate()), 1) + " ms";
            }
            else if (d.page == 2)
            {
                // The grains playing: how far back each started, and how loud it is now.
                d.kind = DeviceDisplay::Kind::taps;
                grain.picture (d.front);
                const auto g = grainSettings();
                d.axis = "now to " + decimal (g.spreadSeconds, 2) + " s back";
                d.caption = decimal (g.density, 1) + " grains/s · " + decimal (g.sizeMs, 0) + " ms · ±" + decimal (g.pitch, 1) + " st"
                          + (g.freeze ? " · frozen" : "");
            }
            else
            {
                // The four nodes and how loud each is; the route of the event now.
                d.kind = DeviceDisplay::Kind::taps;
                for (int node = 0; node < phi::Matrix::numNodes; ++node)
                {
                    d.front.push_back ((node + 0.5f) / phi::Matrix::numNodes);
                    d.front.push_back (std::clamp (std::sqrt (matrix.level (node)), 0.0f, 1.0f));
                }
                d.axis = "Echo · Filter · Drive · Pitch";
                const auto r = matrix.currentRoute();
                d.caption = "event " + std::to_string (matrix.currentEvent()) + ": in → " + phi::Matrix::nodeName (r[0]) + " → "
                          + phi::Matrix::nodeName (r[1]) + " → " + phi::Matrix::nodeName (r[2]) + " ↺";
            }
            return d;
        }

    private:
        double value (Index i) const { return floats[(size_t) i]->get(); }

        phi::Flow::Settings flowSettings() const
        {
            phi::Flow::Settings s;
            s.mode = (phi::Flow::Mode) std::clamp (choices[mode]->getIndex(), 0, phi::Flow::numModes - 1);
            s.shape = (phi::Flow::Shape) std::clamp (choices[shape]->getIndex(), 0, phi::Flow::numShapes - 1);
            s.ratio = value (ratio);
            s.rateHz = value (flowRate);
            s.jitter = value (jitter) / 100.0;
            s.seed = value (seed) / 100.0;
            s.smoothMs = value (smooth);
            return s;
        }

        phi::SpectralSpace::Settings spaceSettings() const
        {
            phi::SpectralSpace::Settings s;
            s.ratio = value (spaceRatio);
            s.anchorHz = value (anchor);
            s.blend = value (blend) / 100.0;
            s.density = value (partials) / 100.0;
            s.tiltDb = value (tilt);
            s.mix = value (spaceMix) / 100.0;
            return s;
        }

        phi::Grain::Settings grainSettings() const
        {
            phi::Grain::Settings s;
            s.density = value (density);
            s.sizeMs = value (size);
            s.spreadSeconds = value (spread);
            s.pitch = value (pitch);
            s.width = value (grainWidth) / 100.0;
            s.flow = value (grainFlow) / 100.0;
            s.mix = value (grainMix) / 100.0;
            s.freeze = frozen->get();
            return s;
        }

        phi::Matrix::Settings matrixSettings() const
        {
            phi::Matrix::Settings s;
            s.eventsHz = value (events);
            s.timeMs = value (time);
            s.feedback = value (feedback) / 100.0;
            s.toneHz = value (tone);
            s.driveDb = value (drive);
            s.glideMs = value (glide);
            s.width = value (matrixWidth) / 100.0;
            s.flow = value (matrixFlow) / 100.0;
            s.mix = value (matrixMix) / 100.0;
            return s;
        }

        std::array<juce::AudioParameterFloat*, numParameters> floats {};
        std::array<juce::AudioParameterChoice*, numParameters> choices {};
        juce::AudioParameterBool* frozen = nullptr;
        phi::Flow flow;
        phi::SpectralSpace space;
        phi::Grain grain;
        phi::Matrix matrix;
        std::array<std::atomic<double>, phi::Flow::numOutputs> shownPosition {};
        std::atomic<double> shownBlend { 1.0 };
        bool wasPlaying = false;
        double expectedSeconds = 0.0;
    };
}

namespace phifx
{
    std::vector<juce::PluginDescription> descriptions()
    {
        return { PhiEqProcessor::description(), PhiEchoProcessor::description(), PhiSpaceProcessor::description(),
                 PhiDriftProcessor::description(), PhiPhaserProcessor::description(), PhiRhythmProcessor::description(), PhiResonatorProcessor::description(),
                 PhiStackProcessor::description(), PhiTameProcessor::description(), PhiDeresonatorProcessor::description(),
                 PhiCompressorProcessor::description(), PhiMultibandProcessor::description(), PhiLimiterProcessor::description(),
                 PhiChorusProcessor::description(), PhiModulationProcessor::description() };
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
        if (id == PhiPhaserProcessor::identifier) return std::make_unique<PhiPhaserProcessor>();
        if (id == PhiRhythmProcessor::identifier) return std::make_unique<PhiRhythmProcessor>();
        if (id == PhiResonatorProcessor::identifier)   return std::make_unique<PhiResonatorProcessor>();
        if (id == PhiStackProcessor::identifier)       return std::make_unique<PhiStackProcessor>();
        if (id == PhiTameProcessor::identifier)        return std::make_unique<PhiTameProcessor>();
        if (id == PhiDeresonatorProcessor::identifier) return std::make_unique<PhiDeresonatorProcessor>();
        if (id == PhiCompressorProcessor::identifier)  return std::make_unique<PhiCompressorProcessor>();
        if (id == PhiMultibandProcessor::identifier)   return std::make_unique<PhiMultibandProcessor>();
        if (id == PhiLimiterProcessor::identifier)     return std::make_unique<PhiLimiterProcessor>();
        if (id == PhiChorusProcessor::identifier)      return std::make_unique<PhiChorusProcessor>();
        if (id == PhiModulationProcessor::identifier)  return std::make_unique<PhiModulationProcessor>();
        return nullptr;
    }
}

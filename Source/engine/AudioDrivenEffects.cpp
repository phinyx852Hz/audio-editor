// SPDX-License-Identifier: AGPL-3.0-only
#include "AudioDrivenEffects.h"
#include "MasteringEffect.h"
#include "../core/AudioDriven.h"

#include <algorithm>
#include <cmath>

namespace
{
    using builtin::MasteringEffect;

    juce::String named (const char* utf8) { return juce::String::fromUTF8 (utf8); }

    juce::String noteName (int note)
    {
        static const char* const names[] { "C", "C#", "D", "D#", "E", "F", "F#", "G", "G#", "A", "A#", "B" };
        note = std::clamp (note, 0, 127);
        return juce::String (names[note % 12]) + juce::String (note / 12 - 1);
    }

    // A Φ effect of this family: its parameters made in a line each, in the order the card shows them.
    class DrivenEffect : public MasteringEffect
    {
    public:
        using MasteringEffect::MasteringEffect;

        void fillInPluginDescription (juce::PluginDescription& d) const override { d = describeMe(); }
        const juce::String getName() const override { return describeMe().name; }
        virtual juce::PluginDescription describeMe() const = 0;

    protected:
        juce::AudioParameterFloat* add (const char* id, const char* name, float low, float high, float step, float value,
                                        int decimals, const char* unit, float centre = 0.0f)
        {
            juce::NormalisableRange<float> range (low, high, step);
            if (centre > 0.0f)
                range.setSkewForCentre (centre);
            auto p = std::make_unique<juce::AudioParameterFloat> (juce::ParameterID { id, 1 }, name, range, value, number (decimals, unit));
            auto* raw = p.get();
            addHostedParameter (std::move (p));
            return raw;
        }

        juce::AudioParameterFloat* percent (const char* id, const char* name, float value)
        {
            return add (id, name, 0.0f, 100.0f, 0.1f, value, 1, "%");
        }

        juce::AudioParameterInt* integer (const char* id, const char* name, int low, int high, int value,
                                          std::function<juce::String (int)> text = {})
        {
            auto attributes = juce::AudioParameterIntAttributes();
            if (text)
                attributes = attributes.withStringFromValueFunction ([text] (int v, int) { return text (v); });
            auto p = std::make_unique<juce::AudioParameterInt> (juce::ParameterID { id, 1 }, name, low, high, value, attributes);
            auto* raw = p.get();
            addHostedParameter (std::move (p));
            return raw;
        }

        juce::AudioParameterChoice* choice (const char* id, const char* name, const juce::StringArray& options, int value)
        {
            auto p = std::make_unique<juce::AudioParameterChoice> (juce::ParameterID { id, 1 }, name, options, value);
            auto* raw = p.get();
            addHostedParameter (std::move (p));
            return raw;
        }

        juce::AudioParameterBool* toggle (const char* id, const char* name, bool value)
        {
            auto p = std::make_unique<juce::AudioParameterBool> (juce::ParameterID { id, 1 }, name, value);
            auto* raw = p.get();
            addHostedParameter (std::move (p));
            return raw;
        }

        int channels() const { return std::max (1, getTotalNumInputChannels()); }
    };

    //==========================================================================
    // Φ Vocoder: the sound shapes a carrier band by band.
    class PhiVocoderProcessor final : public DrivenEffect
    {
    public:
        static constexpr const char* identifier = "phi:phi-vocoder";

        PhiVocoderProcessor() : DrivenEffect ("PHI_VOCODER")
        {
            bands = integer ("bands", "Bands", 4, phi::Vocoder::maxBands, 16);
            carrier = choice ("carrier", "Carrier", { "Saw", "Square", "Noise", "Saw + Noise" }, 0);
            note = integer ("note", "Note", 24, 84, 48, [] (int v) { return noteName (v); });
            chord = choice ("chord", "Chord", { "Single", "Octaves", "Fifth", "Major", "Minor", "Sus4" }, 0);
            follow = toggle ("follow", "Follow Pitch", false);
            formant = add ("formant", "Formant", -12.0f, 12.0f, 0.1f, 0.0f, 1, "st");
            release = add ("release", "Release", 5.0f, 500.0f, 1.0f, 40.0f, 0, "ms", 60.0f);
            sibilance = percent ("sibilance", "Sibilance", 30.0f);
            mix = percent ("mix", "Mix", 100.0f);
        }

        static juce::PluginDescription description() { return describe (named ("Φ Vocoder"), identifier, "Modulation", 0x50485631); }   // "PHV1"
        juce::PluginDescription describeMe() const override { return description(); }
        double getTailLengthSeconds() const override { return 0.5; }

        void prepareToPlay (double rate, int) override
        {
            core.prepare (rate);
            core.set (current());
        }

        using AudioPluginInstance::processBlock;
        void processBlock (juce::AudioBuffer<float>& buffer, juce::MidiBuffer&) override
        {
            core.set (current());
            core.process (buffer.getArrayOfWritePointers(), std::min (channels(), buffer.getNumChannels()), buffer.getNumSamples());
        }

    private:
        phi::Vocoder::Settings current() const
        {
            phi::Vocoder::Settings s;
            s.bands = bands->get();
            s.carrier = (phi::Vocoder::Carrier) carrier->getIndex();
            s.note = note->get();
            s.chord = (phi::Vocoder::Chord) chord->getIndex();
            s.follow = follow->get();
            s.formant = formant->get();
            s.releaseMs = release->get();
            s.sibilance = sibilance->get() / 100.0;
            s.mix = mix->get() / 100.0;
            return s;
        }

        juce::AudioParameterInt *bands, *note;
        juce::AudioParameterChoice *carrier, *chord;
        juce::AudioParameterBool* follow;
        juce::AudioParameterFloat *formant, *release, *sibilance, *mix;
        phi::Vocoder core;
    };

    //==========================================================================
    // Φ Strings: sympathetic strings tuned to a chord.
    class PhiStringsProcessor final : public DrivenEffect
    {
    public:
        static constexpr const char* identifier = "phi:phi-strings";

        PhiStringsProcessor() : DrivenEffect ("PHI_STRINGS")
        {
            note = integer ("note", "Note", 24, 72, 45, [] (int v) { return noteName (v); });
            chord = choice ("chord", "Chord", { "Unison", "Octaves", "Fifths", "Major", "Minor", "Sus4", "Major 7", "Minor 7", juce::String::fromUTF8 ("Φ") }, 2);
            decay = add ("decay", "Decay", 0.1f, 20.0f, 0.01f, 3.0f, 2, "s", 3.0f);
            mix = percent ("mix", "Mix", 50.0f);
            strings = integer ("strings", "Strings", 1, phi::Strings::maxStrings, 6);
            brightness = percent ("brightness", "Brightness", 60.0f);
            width = percent ("width", "Width", 80.0f);
        }

        static juce::PluginDescription description() { return describe (named ("Φ Strings"), identifier, "Filter", 0x50485632); }   // "PHV2"
        juce::PluginDescription describeMe() const override { return description(); }
        double getTailLengthSeconds() const override { return decay->get(); }

        void prepareToPlay (double rate, int) override
        {
            core.prepare (rate);
            core.set (current());
        }

        using AudioPluginInstance::processBlock;
        void processBlock (juce::AudioBuffer<float>& buffer, juce::MidiBuffer&) override
        {
            core.set (current());
            core.process (buffer.getArrayOfWritePointers(), std::min (channels(), buffer.getNumChannels()), buffer.getNumSamples());
        }

    private:
        phi::Strings::Settings current() const
        {
            phi::Strings::Settings s;
            s.note = note->get();
            s.chord = (phi::Strings::Chord) chord->getIndex();
            s.strings = strings->get();
            s.decaySeconds = decay->get();
            s.brightness = brightness->get() / 100.0;
            s.width = width->get() / 100.0;
            s.mix = mix->get() / 100.0;
            return s;
        }

        juce::AudioParameterInt *note, *strings;
        juce::AudioParameterChoice* chord;
        juce::AudioParameterFloat *decay, *brightness, *width, *mix;
        phi::Strings core;
    };

    //==========================================================================
    // Φ Grains: a live granular sampler.
    class PhiGrainsProcessor final : public DrivenEffect
    {
    public:
        static constexpr const char* identifier = "phi:phi-grains";

        PhiGrainsProcessor() : DrivenEffect ("PHI_GRAINS")
        {
            density = add ("density", "Density", 1.0f, 100.0f, 0.1f, 20.0f, 1, "/s", 15.0f);
            size = add ("size", "Size", 10.0f, 1000.0f, 1.0f, 120.0f, 0, "ms", 120.0f);
            position = add ("position", "Position", 0.0f, 5000.0f, 1.0f, 300.0f, 0, "ms", 500.0f);
            mix = percent ("mix", "Mix", 50.0f);
            pitch = add ("pitch", "Pitch", -24.0f, 24.0f, 0.1f, 0.0f, 1, "st");
            jitter = percent ("jitter", "Jitter", 50.0f);
            detune = add ("detune", "Detune", 0.0f, 12.0f, 0.01f, 0.0f, 2, "st");
            spread = percent ("spread", "Spread", 70.0f);
            reverse = toggle ("reverse", "Reverse", false);
            freeze = toggle ("freeze", "Freeze", false);
        }

        static juce::PluginDescription description() { return describe (named ("Φ Grains"), identifier, "Delay", 0x50485633); }   // "PHV3"
        juce::PluginDescription describeMe() const override { return description(); }
        double getTailLengthSeconds() const override { return (position->get() + size->get()) / 1000.0; }

        void prepareToPlay (double rate, int) override
        {
            core.prepare (rate);
            core.set (current());
        }

        using AudioPluginInstance::processBlock;
        void processBlock (juce::AudioBuffer<float>& buffer, juce::MidiBuffer&) override
        {
            core.set (current());
            core.process (buffer.getArrayOfWritePointers(), std::min (channels(), buffer.getNumChannels()), buffer.getNumSamples());
        }

    private:
        phi::Grains::Settings current() const
        {
            phi::Grains::Settings s;
            s.density = density->get();
            s.sizeMs = size->get();
            s.positionMs = position->get();
            s.jitter = jitter->get() / 100.0;
            s.pitch = pitch->get();
            s.pitchJitter = detune->get();
            s.spread = spread->get() / 100.0;
            s.reverse = reverse->get();
            s.freeze = freeze->get();
            s.mix = mix->get() / 100.0;
            return s;
        }

        juce::AudioParameterFloat *density, *size, *position, *mix, *pitch, *jitter, *detune, *spread;
        juce::AudioParameterBool *reverse, *freeze;
        phi::Grains core;
    };

    //==========================================================================
    // Φ Double: an oscillator follows a monophonic line.
    class PhiDoubleProcessor final : public DrivenEffect
    {
    public:
        static constexpr const char* identifier = "phi:phi-double";

        PhiDoubleProcessor() : DrivenEffect ("PHI_DOUBLE")
        {
            interval = add ("interval", "Interval", -24.0f, 24.0f, 1.0f, -12.0f, 0, "st");
            wave = choice ("wave", "Wave", { "Sine", "Triangle", "Saw", "Square", juce::String::fromUTF8 ("Φ") }, 2);
            tone = percent ("tone", "Tone", 60.0f);
            mix = percent ("mix", "Mix", 50.0f);
            glide = add ("glide", "Glide", 1.0f, 500.0f, 1.0f, 30.0f, 0, "ms", 50.0f);
            threshold = percent ("threshold", "Certainty", 50.0f);
            quantize = toggle ("quantize", "Snap to Semitones", true);
        }

        static juce::PluginDescription description() { return describe (named ("Φ Double"), identifier, "Pitch Shift", 0x50485634); }   // "PHV4"
        juce::PluginDescription describeMe() const override { return description(); }
        double getTailLengthSeconds() const override { return 0.1; }

        void prepareToPlay (double rate, int) override
        {
            core.prepare (rate);
            core.set (current());
        }

        using AudioPluginInstance::processBlock;
        void processBlock (juce::AudioBuffer<float>& buffer, juce::MidiBuffer&) override
        {
            core.set (current());
            core.process (buffer.getArrayOfWritePointers(), std::min (channels(), buffer.getNumChannels()), buffer.getNumSamples());
        }

    private:
        phi::Double::Settings current() const
        {
            phi::Double::Settings s;
            s.wave = (phi::Double::Wave) wave->getIndex();
            s.interval = interval->get();
            s.quantize = quantize->get();
            s.glideMs = glide->get();
            s.threshold = threshold->get() / 100.0;
            s.tone = tone->get() / 100.0;
            s.mix = mix->get() / 100.0;
            return s;
        }

        juce::AudioParameterFloat *interval, *tone, *mix, *glide, *threshold;
        juce::AudioParameterChoice* wave;
        juce::AudioParameterBool* quantize;
        phi::Double core;
    };

    //==========================================================================
    // Φ Trigger: each hit fires a drum voice.
    class PhiTriggerProcessor final : public DrivenEffect
    {
    public:
        static constexpr const char* identifier = "phi:phi-trigger";

        PhiTriggerProcessor() : DrivenEffect ("PHI_TRIGGER")
        {
            sound = choice ("sound", "Sound", { "Kick", "Snare", "Clap", "Hat", "Tom", "Rim", juce::String::fromUTF8 ("Φ Bell") }, 0);
            threshold = add ("threshold", "Threshold", -60.0f, 0.0f, 0.1f, -30.0f, 1, "dB");
            sensitivity = percent ("sensitivity", "Sensitivity", 50.0f);
            mix = percent ("mix", "Mix", 100.0f);
            hold = add ("hold", "Hold", 5.0f, 500.0f, 1.0f, 60.0f, 0, "ms", 60.0f);
            tune = add ("tune", "Tune", -24.0f, 24.0f, 0.1f, 0.0f, 1, "st");
            decay = percent ("decay", "Decay", 50.0f);
            dynamics = percent ("dynamics", "Dynamics", 80.0f);
        }

        static juce::PluginDescription description() { return describe (named ("Φ Trigger"), identifier, "Drum", 0x50485635); }   // "PHV5"
        juce::PluginDescription describeMe() const override { return description(); }
        double getTailLengthSeconds() const override { return 2.0; }

        void prepareToPlay (double rate, int) override
        {
            core.prepare (rate);
            core.set (current());
        }

        using AudioPluginInstance::processBlock;
        void processBlock (juce::AudioBuffer<float>& buffer, juce::MidiBuffer&) override
        {
            core.set (current());
            core.process (buffer.getArrayOfWritePointers(), std::min (channels(), buffer.getNumChannels()), buffer.getNumSamples());
        }

    private:
        phi::Trigger::Settings current() const
        {
            phi::Trigger::Settings s;
            s.sound = (phi::Trigger::Sound) sound->getIndex();
            s.thresholdDb = threshold->get();
            s.sensitivity = sensitivity->get() / 100.0;
            s.holdMs = hold->get();
            s.tune = tune->get();
            s.decay = decay->get() / 100.0;
            s.dynamics = dynamics->get() / 100.0;
            s.mix = mix->get() / 100.0;
            return s;
        }

        juce::AudioParameterChoice* sound;
        juce::AudioParameterFloat *threshold, *sensitivity, *mix, *hold, *tune, *decay, *dynamics;
        phi::Trigger core;
    };
}

namespace drivenfx
{
    std::vector<juce::PluginDescription> descriptions()
    {
        return { PhiVocoderProcessor::description(), PhiStringsProcessor::description(), PhiGrainsProcessor::description(),
                 PhiDoubleProcessor::description(), PhiTriggerProcessor::description() };
    }

    std::unique_ptr<juce::AudioPluginInstance> create (const juce::PluginDescription& d)
    {
        if (! builtin::isBuiltin (d))
            return nullptr;
        const auto& id = d.fileOrIdentifier;
        if (id == PhiVocoderProcessor::identifier) return std::make_unique<PhiVocoderProcessor>();
        if (id == PhiStringsProcessor::identifier) return std::make_unique<PhiStringsProcessor>();
        if (id == PhiGrainsProcessor::identifier)  return std::make_unique<PhiGrainsProcessor>();
        if (id == PhiDoubleProcessor::identifier)  return std::make_unique<PhiDoubleProcessor>();
        if (id == PhiTriggerProcessor::identifier) return std::make_unique<PhiTriggerProcessor>();
        return nullptr;
    }
}

// SPDX-License-Identifier: AGPL-3.0-only
// phi --analyze-plugin: the Plugin Analyzer without the window, for scripts and checks.
#include "AnalyzeCommand.h"
#include "MasterChain.h"
#include "../core/PhiForensics.h"
#include "../core/PluginMeasure.h"

#include <juce_audio_processors/juce_audio_processors.h>
#include <juce_events/juce_events.h>

#include <cstdio>
#include <cstdlib>

namespace
{
    // A plugin instance as a measure::Device: stereo in, stereo out (a mono input gets the mix of both).
    class InstanceDevice final : public measure::Device
    {
    public:
        explicit InstanceDevice (juce::AudioPluginInstance& p) : plugin (p) {}

        void reset() override { plugin.reset(); }

        void process (float* left, float* right, int frames) override
        {
            const auto channels = std::max ({ 2, plugin.getTotalNumInputChannels(), plugin.getTotalNumOutputChannels() });
            buffer.setSize (channels, frames, false, false, true);
            buffer.clear();
            if (plugin.getTotalNumInputChannels() == 1)
                for (int i = 0; i < frames; ++i)
                    buffer.setSample (0, i, 0.5f * (left[i] + right[i]));
            else
            {
                buffer.copyFrom (0, 0, left, frames);
                buffer.copyFrom (1, 0, right, frames);
            }
            midi.clear();
            plugin.processBlock (buffer, midi);
            const auto outs = plugin.getTotalNumOutputChannels();
            std::copy (buffer.getReadPointer (0), buffer.getReadPointer (0) + frames, left);
            std::copy (buffer.getReadPointer (outs > 1 ? 1 : 0), buffer.getReadPointer (outs > 1 ? 1 : 0) + frames, right);
        }

        int reportedLatency() const override { return plugin.getLatencySamples(); }

    private:
        juce::AudioPluginInstance& plugin;
        juce::AudioBuffer<float> buffer;
        juce::MidiBuffer midi;
    };

    int usage()
    {
        std::fprintf (stderr,
            "Usage: phi --analyze-plugin <name or id> [options]\n"
            "  --set \"Parameter=value\"   set a parameter by name, as its text (repeatable)\n"
            "  --level dB                test level, dBFS peak (default -12)\n"
            "  --tail seconds            impulse response length (default 1.5)\n"
            "  --sweep seconds           sweep length (default 2)\n"
            "  --sine Hz                 the harmonics test's tone (default 1000)\n"
            "  --modulation seconds      the modulation test's length (default 12)\n"
            "  --rate Hz                 sample rate (default 48000)\n"
            "  --skip linear,harmonics,aliasing,dynamics,modulation\n"
            "  --list                    list the plugins PHI knows\n");
        return 2;
    }
}

int analyzePluginCommand (int argc, char* argv[])
{
    juce::ScopedJuceInitialiser_GUI juce;
    MasterChain chain;
    const auto home = juce::File::getSpecialLocation (juce::File::userHomeDirectory);
    auto config = juce::SystemStats::getEnvironmentVariable ("XDG_CONFIG_HOME", home.getChildFile (".config").getFullPathName());
    chain.setConfigFolder (juce::File (config).getChildFile (APP_ID));

    if (argc >= 3 && juce::String (argv[2]) == "--list")
    {
        for (const auto& p : chain.allPlugins())
            std::printf ("%s\t%s\t%s\n", p.name.c_str(), p.format.c_str(), p.id.c_str());
        return 0;
    }
    if (argc < 3)
        return usage();

    const juce::String wanted = juce::String::fromUTF8 (argv[2]);
    std::string id;
    for (const auto& p : chain.allPlugins())
        if (juce::String::fromUTF8 (p.name.c_str()).equalsIgnoreCase (wanted) || p.id == wanted.toStdString())
            id = p.id;
    if (id.empty())
    {
        std::fprintf (stderr, "No plugin named \"%s\" (try --list)\n", argv[2]);
        return 1;
    }

    measure::Settings settings;
    std::vector<std::pair<juce::String, juce::String>> sets;
    for (int i = 3; i < argc; ++i)
    {
        const juce::String option (argv[i]);
        const auto value = i + 1 < argc ? juce::String::fromUTF8 (argv[i + 1]) : juce::String();
        if (option == "--set" && value.contains ("="))
            sets.push_back ({ value.upToFirstOccurrenceOf ("=", false, false).trim(), value.fromFirstOccurrenceOf ("=", false, false).trim() });
        else if (option == "--level")      settings.levelDb = value.getDoubleValue();
        else if (option == "--tail")       settings.tailSeconds = value.getDoubleValue();
        else if (option == "--sweep")      settings.sweepSeconds = value.getDoubleValue();
        else if (option == "--sine")       settings.sineHz = value.getDoubleValue();
        else if (option == "--modulation") settings.modulationSeconds = value.getDoubleValue();
        else if (option == "--rate")       settings.sampleRate = value.getDoubleValue();
        else if (option == "--skip")
        {
            settings.linear = ! value.contains ("linear");
            settings.harmonics = ! value.contains ("harmonics");
            settings.aliasing = ! value.contains ("aliasing");
            settings.dynamics = ! value.contains ("dynamics");
            settings.modulation = ! value.contains ("modulation");
        }
        else
            return usage();
        ++i;
    }

    juce::String error;
    auto plugin = chain.createEffect (id, settings.sampleRate, settings.block, error);
    if (plugin == nullptr)
    {
        std::fprintf (stderr, "Could not start %s: %s\n", argv[2], error.toRawUTF8());
        return 1;
    }

    for (const auto& [name, text] : sets)
    {
        juce::AudioProcessorParameter* found = nullptr;
        for (auto* p : plugin->getParameters())
            if (p->getName (128).equalsIgnoreCase (name))
                found = p;
        if (found == nullptr)
        {
            std::fprintf (stderr, "%s has no parameter \"%s\". It has:", argv[2], name.toRawUTF8());
            for (auto* p : plugin->getParameters())
                std::fprintf (stderr, " \"%s\"", p->getName (128).toRawUTF8());
            std::fprintf (stderr, "\n");
            return 1;
        }
        found->setValueNotifyingHost (found->getValueForText (text));
    }

    std::string title = plugin->getName().toStdString();
    for (const auto& [name, text] : sets)
        title += ", " + name.toStdString() + " " + text.toStdString();
    // Settings as the plugin shows them.
    std::string shown;
    for (auto* p : plugin->getParameters())
        shown += (shown.empty() ? "" : ", ") + p->getName (64).toStdString() + " " + (p->getCurrentValueAsText() + " " + p->getLabel()).trim().toStdString();

    InstanceDevice device (*plugin);
    const auto results = measure::run (device, settings);
    std::printf ("%s\nSettings: %s\n\n%s\n", measure::report (results, title).c_str(), shown.c_str(),
                 forensic::text (forensic::analyse (results)).c_str());
    std::fflush (stdout);
    return 0;
}

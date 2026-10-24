// SPDX-License-Identifier: AGPL-3.0-only
#include "StockPlugins.h"

#include <algorithm>

namespace stock
{
    const std::vector<Info>& all()
    {
        static const std::vector<Info> list {
            { "toneshifteq.clap", "ToneShiftEQ by brummer10", "BSD-3-Clause", "https://github.com/brummer10/ToneShiftEQ",
              { "Lowcut freq", "Highcut freq", "HF Fade", "Volume Out" } },
            { "wolf-shaper.clap", "Wolf Shaper by Patrick Desaulniers", "GPL-3.0", "https://github.com/wolf-plugins/wolf-shaper",
              { "Pre Gain", "Wet", "Post Gain" } },
            { "lsp-plugins-clipper.clap", "LSP Clipper by Vladimir Sadovnikov", "LGPL-3.0-or-later", "https://lsp-plug.in",
              { "Input gain", "Clipping threshold", "Output gain" }, "LSP ", "LSP" },
            { "lsp-plugins-mb-clipper.clap", "LSP Multiband Clipper by Vladimir Sadovnikov", "LGPL-3.0-or-later", "https://lsp-plug.in",
              { "Input gain", "Clipping threshold", "Output gain" }, "LSP ", "LSP" },
            // x1 names its controls "... 1"; x2 and x4 have "... Global" ones for every channel at once.
            { "lsp-plugins-oscilloscope.clap", "LSP Oscilloscope by Vladimir Sadovnikov", "LGPL-3.0-or-later", "https://lsp-plug.in",
              { "Time Division Global|Time Division 1", "Vertical Division Global|Vertical Division 1",
                "Trigger Level Global|Trigger Level 1", "Global Freeze Switch" }, "LSP ", "LSP" },
            // ZL Audio's plugins are JUCE VST3s, built with their own JUCE.
            { "ZL Equalizer 2.vst3", "ZL Equalizer 2 by ZL Audio (zsliu98)", "AGPL-3.0", "https://github.com/ZL-Audio/ZLEqualizer",
              { "Gain Scale", "Output Gain" }, "", "ZL Audio", "VST3" },
            { "ZL Spectrum Equalizer.vst3", "ZL Spectrum Equalizer by ZL Audio (zsliu98)", "AGPL-3.0", "https://github.com/ZL-Audio/ZLSpectrumEqualizer",
              { "Gain Scale", "Output Gain" }, "", "ZL Audio", "VST3" },
            { "ZL Compressor.vst3", "ZL Compressor by ZL Audio (zsliu98)", "AGPL-3.0", "https://github.com/ZL-Audio/ZLCompressor",
              { "Threshold (dB)", "Ratio", "Attack", "Release", "Makeup Gain", "Wet" }, "", "ZL Audio", "VST3" },
            { "ZL Limiter.vst3", "ZL Limiter by ZL Audio (zsliu98)", "AGPL-3.0", "https://github.com/ZL-Audio/ZLLimiter",
              { "Input Gain", "Output Ceiling", "Release", "Lookahead" }, "", "ZL Audio", "VST3" },
            { "ZL Splitter.vst3", "ZL Splitter by ZL Audio (zsliu98)", "AGPL-3.0", "https://github.com/ZL-Audio/ZLSplitter",
              { "Split Type", "Mix" }, "", "ZL Audio", "VST3" },

            // Instruments (cmake/Instruments.cmake). Samplers first, then synths, in menu order.
            { .file = "GRNLR.vst3", .credit = "GRNLR by Raffael Seyfried", .licence = "GPL-3.0-or-later",
              .url = "https://github.com/passivist/GRNLR", .format = "VST3",
              .role = Role::sampler, .key = "granular", .function = "Granular", .original = "GRNLR" },
            { .file = "Ami Sampler.vst3", .credit = "Ami Sampler by Finley Baker (_astriid_)", .licence = "GPL-3.0",
              .url = "https://github.com/astriiddev/Ami-Sampler-VST", .format = "VST3",
              .role = Role::sampler, .key = "retroSampler", .function = "Retro Sampler", .original = "Ami" },
            { .file = "VASTvaporizer2.vst3", .credit = "Vaporizer2 by VAST Dynamics", .licence = "GPL-3.0",
              .url = "https://github.com/VASTDynamics/Vaporizer2", .format = "VST3",
              .role = Role::synth, .key = "wavetable", .function = "Wavetable", .original = "Vaporizer2" },
            { .file = "Cardinal.clap/CardinalSynth.clap", .credit = "Cardinal by DISTRHO, with VCV Fundamental", .licence = "GPL-3.0-or-later",
              .url = "https://github.com/DISTRHO/Cardinal", .format = "CLAP",
              .role = Role::synth, .key = "modular", .function = "Modular", .original = "Cardinal" },
            { .file = "Overtonium.vst3", .credit = "Overtonium by Benjamin Dehli", .licence = "AGPL-3.0",
              .url = "https://github.com/benjamindehli/overtonium", .format = "VST3",
              .role = Role::synth, .key = "additive", .function = "Additive", .original = "Overtonium" },
            { .file = "CA-72.clap", .credit = "CA-72 by Idle Foundry", .licence = "GPL-3.0-or-later",
              .url = "https://github.com/idlefoundry/ca-72", .format = "CLAP",
              .role = Role::synth, .key = "subtractive1", .function = "Subtractive1 mono", .original = "CA-72" },
            { .file = "Helm.vst3", .credit = "Helm by Matt Tytel", .licence = "GPL-3.0",
              .url = "https://github.com/amilajack/helm", .format = "VST3",
              .role = Role::synth, .key = "subtractive2", .function = "Subtractive2 poly", .original = "Helm" },
            { .file = "octasine.clap", .credit = "OctaSine by Joakim Frostegård", .licence = "AGPL-3.0",
              .url = "https://github.com/greatest-ape/OctaSine", .format = "CLAP",
              .role = Role::synth, .key = "fm", .function = "FM", .original = "OctaSine" },
        };
        return list;
    }

    const Info* instrument (const char* key)
    {
        for (const auto& i : all())
            if (i.key != nullptr && juce::String (i.key) == key)
                return &i;
        return nullptr;
    }

    juce::File folder()
    {
        if (const auto env = juce::SystemStats::getEnvironmentVariable ("PHI_STOCK_PATH", {}); env.isNotEmpty())
            return juce::File (env);

        const auto exe = juce::File::getSpecialLocation (juce::File::currentExecutableFile);
        if (const auto installed = exe.getParentDirectory().getChildFile (PHI_STOCK_RELATIVE); installed.isDirectory())
            return installed;

        if (const auto built = juce::File (PHI_STOCK_BUILD_DIR); built.isDirectory())
            return built;

        return {};
    }

    std::vector<int> footerParameters (const Info& info, const juce::AudioProcessor& plugin)
    {
        std::vector<int> result;
        const auto& params = plugin.getParameters();
        for (const auto* wanted : info.footer)
        {
            for (const auto& name : juce::StringArray::fromTokens (wanted, "|", {}))
            {
                const auto found = std::find_if (params.begin(), params.end(), [&] (auto* p) { return p->getName (64) == name; });
                if (found != params.end())
                {
                    result.push_back ((int) std::distance (params.begin(), found));
                    break;
                }
            }
        }
        return result;
    }

    juce::String unit (const juce::AudioProcessorParameter& p)
    {
        const auto label = p.getLabel();
        return label == p.getName (128) ? juce::String() : label;
    }

    const Info* find (const juce::PluginDescription& d)
    {
        const auto file = juce::File::createFileWithoutCheckingPath (d.fileOrIdentifier);
        const auto dir = folder();
        for (const auto& i : all())
            if (d.pluginFormatName == i.format && dir != juce::File() && file == dir.getChildFile (i.file))
                return &i;

        return nullptr;
    }
}

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
        };
        return list;
    }

    juce::File folder()
    {
        if (const auto env = juce::SystemStats::getEnvironmentVariable ("PHINYX_STOCK_PATH", {}); env.isNotEmpty())
            return juce::File (env);

        const auto exe = juce::File::getSpecialLocation (juce::File::currentExecutableFile);
        if (const auto installed = exe.getParentDirectory().getChildFile (PHINYX_STOCK_RELATIVE); installed.isDirectory())
            return installed;

        if (const auto built = juce::File (PHINYX_STOCK_BUILD_DIR); built.isDirectory())
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

    const Info* find (const juce::PluginDescription& d)
    {
        if (d.pluginFormatName != "CLAP")
            return nullptr;

        const auto file = juce::File::createFileWithoutCheckingPath (d.fileOrIdentifier);
        for (const auto& i : all())
            if (file.getFileName() == i.file && file.getParentDirectory() == folder())
                return &i;

        return nullptr;
    }
}

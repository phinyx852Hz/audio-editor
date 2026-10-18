// SPDX-License-Identifier: AGPL-3.0-only
#pragma once

#include <juce_audio_processors/juce_audio_processors.h>

#include <vector>

// Stock plugins: free plugins by other authors that ship with PHINYX as CLAPs
// or VST3s (built by cmake/StockPlugins.cmake) and are listed under Built-in. They open
// in the PHINYX frame (StockFrame) with their own view in the middle.
namespace stock
{
    struct Info
    {
        const char* file;      // the .clap file's or .vst3 bundle's name
        const char* credit;    // "ToneShiftEQ by brummer10"
        const char* licence;   // SPDX id
        const char* url;
        // Parameters given knobs in the frame's footer, by name; "A|B" takes A, or B when there is no A.
        std::vector<const char*> footer;
        const char* namePrefix = "";       // put before the plugin's own name in the plugin list
        const char* maker = nullptr;       // shown instead of the plugin's own vendor name
        const char* format = "CLAP";       // the plugin format's name, "CLAP" or "VST3"
    };

    const std::vector<Info>& all();

    // The folder holding the stock plugins: PHINYX_STOCK_PATH when set, else
    // lib/phinyx/stock next to the installed binary, else the build folder's.
    // An invalid File when there is none.
    juce::File folder();

    const Info* find (const juce::PluginDescription&);   // nullptr when not a stock plugin

    // A parameter's unit ("dB", "Hz"), or nothing when the plugin puts its
    // name there instead (the ZL plugins do), so values read "-3.0 dB", not
    // "-3.0 Output Gain".
    juce::String unit (const juce::AudioProcessorParameter&);

    // The indices of `info.footer`'s parameters in `plugin`, in footer order.
    std::vector<int> footerParameters (const Info&, const juce::AudioProcessor& plugin);
}

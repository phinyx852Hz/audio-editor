// SPDX-License-Identifier: AGPL-3.0-only
#pragma once

#include <juce_audio_processors/juce_audio_processors.h>

#include <vector>

// Stock plugins: free plugins by other authors that ship with PHI as CLAPs
// or VST3s (built by cmake/StockPlugins.cmake) and are listed under Built-in. They open
// in the PHI frame (StockFrame) with their own view in the middle.
// The stock instruments (cmake/Instruments.cmake) are the samplers and synths of
// the Sample and Synth menus and Find's Instruments, named by what they do
// ("Granular"); their own view opens untouched in a floating window with an
// opacity bar below (InstrumentFrame).
namespace stock
{
    enum class Role { effect, sampler, synth };

    struct Info
    {
        const char* file;      // the .clap file's or .vst3 bundle's name
        const char* credit;    // "ToneShiftEQ by brummer10"
        const char* licence;   // SPDX id
        const char* url;
        // Parameters given knobs in the frame's footer, by name; "A|B" takes A, or B when there is no A.
        std::vector<const char*> footer {};
        const char* namePrefix = "";       // put before the plugin's own name in the plugin list
        const char* maker = nullptr;       // shown instead of the plugin's own vendor name
        const char* format = "CLAP";       // the plugin format's name, "CLAP" or "VST3"
        // Instruments only:
        Role role = Role::effect;
        const char* key = nullptr;         // "granular": the command is instrument.granular
        const char* function = nullptr;    // "Granular": its name in PHI, by what it does
        const char* original = nullptr;    // "GRNLR": the name its authors gave it
    };

    const std::vector<Info>& all();
    const Info* instrument (const char* key);   // nullptr when there is none by that key

    // The folder holding the stock plugins: PHI_STOCK_PATH when set, else
    // lib/phi/stock next to the installed binary, else the build folder's.
    // An invalid File when there is none.
    juce::File folder();

    const Info* find (const juce::PluginDescription&);   // nullptr when not a stock plugin

    // The file is the .clap file's or .vst3 bundle's path inside folder(), as
    // "Cardinal.clap/CardinalSynth.clap" for one of a bundle's plugins.

    // A parameter's unit ("dB", "Hz"), or nothing when the plugin puts its
    // name there instead (the ZL plugins do), so values read "-3.0 dB", not
    // "-3.0 Output Gain".
    juce::String unit (const juce::AudioProcessorParameter&);

    // The indices of `info.footer`'s parameters in `plugin`, in footer order.
    std::vector<int> footerParameters (const Info&, const juce::AudioProcessor& plugin);
}

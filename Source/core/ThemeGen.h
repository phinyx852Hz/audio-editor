// SPDX-License-Identifier: AGPL-3.0-only
#pragma once

// The Theme Generator (Tools > Theme Generator): builds a whole colour theme from a
// few choices. Plain C++ with no Qt, so the tests and scripts can call it too.
// Colours are worked out in OKLCH, where equal steps in lightness look equal, and
// written with the keys of a PHI theme file (docs/themes.md).

#include <map>
#include <string>
#include <vector>

namespace themegen
{
    struct Rgb
    {
        double r = 0, g = 0, b = 0;   // sRGB, 0 to 1
        double a = 1;
    };

    enum class Harmony { mono, analogous, complementary, splitComplementary, triadic };
    enum class Meters { classic, palette };   // green/yellow/red, or the theme's own colours

    struct Settings
    {
        Rgb seed { 0.784, 0.635, 0.290 };   // the main colour: highlight, waveform
        bool dark = true;
        Harmony harmony = Harmony::complementary;
        double saturation = 1.0;   // 0 (grey) to 2
        double contrast = 0.5;     // 0 (soft) to 1 (strong)
        double tint = 0.5;         // how much the main colour tints the backgrounds, 0 to 1
        Meters meters = Meters::classic;
        int variation = 0;         // Shuffle: moves the second and third colours around
    };

    using Colors = std::map<std::string, Rgb>;

    // The keys of a PHI theme, in the order of Theme::colorKeys().
    const std::vector<std::string>& colorKeys();

    struct Result
    {
        Colors colors;                // every key of Theme::colorKeys()
        std::vector<Rgb> spectrogram; // from silence to full scale
        bool dark = true;
    };

    // Colours in `locked` are kept as they are; the rest are generated.
    Result generate (const Settings&, const Colors& locked = {});

    // The main colours of a picture, most common first, from pixels already scaled down.
    struct Swatch { Rgb color; double share = 0; };
    std::vector<Swatch> paletteFromPixels (const std::vector<Rgb>& pixels, int count = 6);

    // Settings for a picture: its most striking common colour, dark when the picture is.
    Settings settingsFromPalette (const std::vector<Swatch>&, Settings start = {});

    // Contrast, WCAG 2 style: 1 (none) to 21 (black on white).
    double luminance (Rgb);
    double contrastRatio (Rgb a, Rgb b);

    struct ContrastCheck
    {
        std::string foreground, background;   // theme keys
        std::string what;                     // what the pair is for, in words
        double ratio = 0;
        double needed = 0;                    // 4.5 for text, 3 for lines and graphics
        bool ok() const { return ratio >= needed; }
    };
    std::vector<ContrastCheck> checkContrast (const Colors&);

    // Moves the lightness of failing colours (the text, or its background if the text is
    // locked) until each pair passes. Returns how many pairs were fixed.
    int fixContrast (Colors&, const Colors& locked = {});

    // #rrggbb, or #rrggbbaa when not opaque.
    std::string toHex (Rgb);
    bool fromHex (const std::string&, Rgb&);

    // A theme file for ~/.local/share/phi/themes. The settings are kept in it under
    // "generator" so the theme can be opened in the generator again.
    std::string toJson (const std::string& name, const Result&, const Settings&);

    // The colours of a REAPER colour theme (the text of a .ReaperTheme file, also found
    // inside a .ReaperThemeZip), mapped to PHI's keys as far as they match. Images and
    // WALTER layouts have no counterpart in PHI and are left out. `settings` gets a seed
    // and dark or light to generate the colours REAPER has no match for. False when
    // the text has no colours PHI can use.
    bool fromReaperTheme (const std::string& text, Colors& out, Settings& settings);

    const char* harmonyName (Harmony);   // "complementary" ...
    bool harmonyFromName (const std::string&, Harmony&);
}

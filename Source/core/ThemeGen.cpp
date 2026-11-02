// SPDX-License-Identifier: AGPL-3.0-only
#include "ThemeGen.h"

#include <algorithm>
#include <cctype>
#include <cmath>
#include <cstdio>
#include <sstream>

namespace themegen
{
namespace
{
    constexpr double pi = 3.14159265358979323846;

    struct Lab { double L = 0, a = 0, b = 0; };
    struct Lch { double L = 0, C = 0, h = 0; };   // h in degrees

    double toLinear (double c) { return c <= 0.04045 ? c / 12.92 : std::pow ((c + 0.055) / 1.055, 2.4); }
    double fromLinear (double c) { return c <= 0.0031308 ? c * 12.92 : 1.055 * std::pow (c, 1.0 / 2.4) - 0.055; }

    Lab toLab (Rgb c)
    {
        const double r = toLinear (c.r), g = toLinear (c.g), b = toLinear (c.b);
        const double l = std::cbrt (0.4122214708 * r + 0.5363325363 * g + 0.0514459929 * b);
        const double m = std::cbrt (0.2119034982 * r + 0.6806995451 * g + 0.1073969566 * b);
        const double s = std::cbrt (0.0883024619 * r + 0.2817188376 * g + 0.6299787005 * b);
        return { 0.2104542553 * l + 0.7936177850 * m - 0.0040720468 * s,
                 1.9779984951 * l - 2.4285922050 * m + 0.4505937099 * s,
                 0.0259040371 * l + 0.7827717662 * m - 0.8086757660 * s };
    }

    // Unclamped: components outside 0..1 mean the colour is outside sRGB.
    Rgb fromLabRaw (Lab c)
    {
        const double l = std::pow (c.L + 0.3963377774 * c.a + 0.2158037573 * c.b, 3.0);
        const double m = std::pow (c.L - 0.1055613458 * c.a - 0.0638541728 * c.b, 3.0);
        const double s = std::pow (c.L - 0.0894841775 * c.a - 1.2914855480 * c.b, 3.0);
        return { fromLinear (4.0767416621 * l - 3.3077115913 * m + 0.2309699292 * s),
                 fromLinear (-1.2684380046 * l + 2.6097574011 * m - 0.3413193965 * s),
                 fromLinear (-0.0041960863 * l - 0.7034186147 * m + 1.7076147010 * s) };
    }

    Lch toLch (Rgb c)
    {
        const auto lab = toLab (c);
        auto h = std::atan2 (lab.b, lab.a) * 180.0 / pi;
        if (h < 0)
            h += 360.0;
        return { lab.L, std::hypot (lab.a, lab.b), h };
    }

    Lab toLab (Lch c) { return { c.L, c.C * std::cos (c.h * pi / 180.0), c.C * std::sin (c.h * pi / 180.0) }; }

    bool inGamut (Rgb c)
    {
        constexpr double e = 1e-4;
        return c.r >= -e && c.r <= 1 + e && c.g >= -e && c.g <= 1 + e && c.b >= -e && c.b <= 1 + e;
    }

    Rgb clamped (Rgb c, double alpha = 1.0)
    {
        return { std::clamp (c.r, 0.0, 1.0), std::clamp (c.g, 0.0, 1.0), std::clamp (c.b, 0.0, 1.0), alpha };
    }

    // An OKLCH colour as sRGB, giving up chroma (not lightness or hue) when it does not fit.
    Rgb fromLch (Lch c, double alpha = 1.0)
    {
        c.L = std::clamp (c.L, 0.0, 1.0);
        c.C = std::max (0.0, c.C);
        if (inGamut (fromLabRaw (toLab (c))))
            return clamped (fromLabRaw (toLab (c)), alpha);

        double lo = 0, hi = c.C;
        for (int i = 0; i < 24; ++i)
        {
            const double mid = (lo + hi) / 2;
            (inGamut (fromLabRaw (toLab (Lch { c.L, mid, c.h }))) ? lo : hi) = mid;
        }
        return clamped (fromLabRaw (toLab (Lch { c.L, lo, c.h })), alpha);
    }

    Rgb mix (Rgb a, Rgb b, double t)
    {
        const auto x = toLab (a), y = toLab (b);
        return clamped (fromLabRaw ({ x.L + (y.L - x.L) * t, x.a + (y.a - x.a) * t, x.b + (y.b - x.b) * t }));
    }

    double wrap (double h) { return std::fmod (std::fmod (h, 360.0) + 360.0, 360.0); }

    // The kind of colour a key is, to pick which way to move it when fixing contrast.
    double distance2 (Lab a, Lab b) { return (a.L - b.L) * (a.L - b.L) + (a.a - b.a) * (a.a - b.a) + (a.b - b.b) * (a.b - b.b); }
}

const std::vector<std::string>& colorKeys()
{
    static const std::vector<std::string> keys {
        "window", "windowText", "base", "alternateBase", "text", "placeholderText", "button", "buttonText",
        "highlight", "highlightedText", "accent", "mid", "disabledText",
        "waveform", "waveformBackground", "centerLine", "selection", "cursor", "marker", "region",
        "meterBackground", "meterLow", "meterMid", "meterHigh", "meterPeakHold", "scope",
        "levelQuiet", "levelSoft", "levelMedium", "levelLoud", "levelHot", "levelPeak"
    };
    return keys;
}

Result generate (const Settings& settings, const Colors& locked)
{
    const bool dark = settings.dark;
    const double k = std::clamp (settings.contrast, 0.0, 1.0);
    const double sat = std::clamp (settings.saturation, 0.0, 2.0);
    const double tint = std::clamp (settings.tint, 0.0, 1.0);

    const auto seed = toLch (settings.seed);
    const double hue = seed.h;
    const bool grey = seed.C < 0.02;   // a grey seed has no hue worth building on
    const double C = std::min (seed.C * sat, 0.32);

    // The second and third hues, from the harmony; Shuffle turns them a little.
    const double turn = settings.variation == 0 ? 0.0 : (double) ((settings.variation * 53) % 100 - 50);
    double hueA = hue, hueB = hue;
    switch (settings.harmony)
    {
        case Harmony::mono:               hueA = hue + turn * 0.3;   hueB = hue - turn * 0.3; break;
        case Harmony::analogous:          hueA = hue + 35 + turn;    hueB = hue - 35 - turn; break;
        case Harmony::complementary:      hueA = hue + 180 + turn;   hueB = hue + 150 + turn; break;
        case Harmony::splitComplementary: hueA = hue + 150 + turn;   hueB = hue + 210 + turn; break;
        case Harmony::triadic:            hueA = hue + 120 + turn;   hueB = hue + 240 + turn; break;
    }
    hueA = wrap (hueA);
    hueB = wrap (hueB);
    const double accentC = grey ? C : std::max (C, 0.08 * std::min (sat, 1.0));

    Result out;
    out.dark = dark;
    auto& c = out.colors;
    const auto set = [&c] (const char* key, Rgb value) { c[key] = value; };

    // Backgrounds: near-neutral, tinted towards the main colour.
    const double surfaceC = std::min (0.045, C * 0.5) * tint;
    const auto surface = [&] (double L) { return fromLch ({ L, surfaceC, hue }); };
    double textL = 0;
    if (dark)
    {
        const double bg = 0.205 - 0.07 * k;
        textL = 0.86 + 0.1 * k;
        set ("window", surface (bg));
        set ("base", surface (bg - 0.035));
        set ("alternateBase", surface (bg + 0.02));
        set ("button", surface (bg + 0.05));
        set ("mid", surface (bg + 0.11));
        set ("meterBackground", surface (bg - 0.055));
        set ("placeholderText", fromLch ({ bg + (textL - bg) * 0.58, surfaceC * 1.5, hue }));
        set ("disabledText", fromLch ({ bg + (textL - bg) * 0.4, surfaceC, hue }));
    }
    else
    {
        const double bg = 0.94 + 0.025 * k;
        textL = 0.30 - 0.12 * k;
        set ("window", surface (bg));
        set ("base", surface (std::min (0.995, bg + 0.045)));
        set ("alternateBase", surface (std::min (0.98, bg + 0.02)));
        set ("button", surface (bg - 0.03));
        set ("mid", surface (bg - 0.13));
        set ("meterBackground", surface (bg - 0.045));
        set ("placeholderText", fromLch ({ textL + (bg - textL) * 0.42, surfaceC * 1.5, hue }));
        set ("disabledText", fromLch ({ textL + (bg - textL) * 0.55, surfaceC, hue }));
    }
    const auto text = fromLch ({ textL, surfaceC * 0.6, hue });
    set ("text", text);
    set ("windowText", text);
    set ("buttonText", text);
    set ("waveformBackground", c["base"]);
    set ("centerLine", c["mid"]);

    // The main colour, at a lightness that reads on the background.
    const double mainL = dark ? std::clamp (seed.L, 0.62, 0.86) : std::clamp (seed.L, 0.40, 0.60);
    const auto main = fromLch ({ mainL, C, hue });
    set ("highlight", main);
    set ("waveform", main);
    set ("scope", main);

    if (settings.harmony == Harmony::mono)
        set ("accent", fromLch ({ dark ? 0.88 : 0.40, C * 0.7, hueA }));
    else
        set ("accent", fromLch ({ dark ? 0.80 : 0.50, accentC, hueA }));
    set ("marker", c["accent"]);
    set ("region", fromLch ({ dark ? 0.68 : 0.48, settings.harmony == Harmony::mono ? C * 0.6 : accentC, hueB }));
    const auto cursor = dark ? fromLch ({ 0.97, std::min (0.02, C), hue }) : fromLch ({ 0.12, std::min (0.02, C), hue });
    set ("cursor", cursor);
    set ("meterPeakHold", cursor);

    // Meters and the level colours (View > Colour Waveform by Level).
    const double meterC = 0.17 * std::clamp (sat, 0.3, 1.4);
    const auto red = fromLch ({ dark ? 0.64 : 0.54, 0.2 * std::clamp (sat, 0.4, 1.4), 27 });
    if (settings.meters == Meters::classic)
    {
        set ("meterLow", fromLch ({ dark ? 0.72 : 0.52, meterC, 145 }));
        set ("meterMid", fromLch ({ dark ? 0.85 : 0.6, meterC, 85 }));
        set ("meterHigh", red);
        const double levelL = dark ? 0.68 : 0.50;
        set ("levelQuiet", fromLch ({ levelL - 0.05, 0.16, 262 }));
        set ("levelSoft", fromLch ({ levelL, 0.12, 215 }));
        set ("levelMedium", fromLch ({ levelL, 0.17, 145 }));
        set ("levelLoud", fromLch ({ levelL + 0.12, 0.16, 95 }));
        set ("levelHot", fromLch ({ levelL + 0.02, 0.18, 50 }));
        set ("levelPeak", red);
    }
    else
    {
        set ("meterLow", fromLch ({ mainL + (dark ? -0.1 : 0.05), C, hue }));
        set ("meterMid", fromLch ({ dark ? 0.82 : 0.62, accentC, hueA }));
        set ("meterHigh", red);
        const auto bg = c["window"];
        set ("levelQuiet", mix (bg, main, 0.45));
        set ("levelSoft", mix (bg, main, 0.72));
        set ("levelMedium", main);
        set ("levelLoud", mix (main, c["accent"], 0.5));
        set ("levelHot", c["accent"]);
        set ("levelPeak", red);
    }

    // Spectrogram: silence to full scale, through the theme's colours.
    const double sc = std::max (C, grey ? 0.0 : 0.08);
    const std::vector<Lch> stops { { 0.0, 0.0, hueB }, { 0.2, sc * 0.5, hueB }, { 0.38, sc * 0.9, hueB },
                                   { 0.58, sc, hue }, { 0.76, sc, hueA }, { 0.9, sc * 0.5, hueA }, { 0.99, 0.01, hueA } };
    for (const auto& s : stops)
        out.spectrogram.push_back (fromLch (s));
    if (! dark)   // light themes go from white to dark, like PHI Light
        std::reverse (out.spectrogram.begin(), out.spectrogram.end());

    for (const auto& [key, value] : locked)
        c[key] = value;

    // Selected text follows the (possibly locked) highlight: black or white, whichever reads.
    if (! locked.count ("highlightedText"))
    {
        const auto h = c["highlight"];
        const auto light = fromLch ({ 0.99, 0.0, hue }), darkText = fromLch ({ 0.16, std::min (0.02, C), hue });
        set ("highlightedText", contrastRatio (darkText, h) >= contrastRatio (light, h) ? darkText : light);
    }
    if (! locked.count ("selection"))
    {
        auto s = c["highlight"];
        s.a = dark ? 0.25 : 0.2;
        set ("selection", s);
    }
    return out;
}

std::vector<Swatch> paletteFromPixels (const std::vector<Rgb>& pixels, int count)
{
    std::vector<Swatch> result;
    if (pixels.empty() || count < 1)
        return result;

    std::vector<Lab> points;
    points.reserve (pixels.size());
    for (const auto& p : pixels)
        if (p.a > 0.5)
            points.push_back (toLab (p));
    if (points.empty())
        return result;

    // Farthest-point start, so the result is the same every time.
    Lab mean;
    for (const auto& p : points)
    {
        mean.L += p.L;
        mean.a += p.a;
        mean.b += p.b;
    }
    mean = { mean.L / points.size(), mean.a / points.size(), mean.b / points.size() };
    std::vector<Lab> centres;
    centres.push_back (*std::min_element (points.begin(), points.end(),
                                          [&] (const Lab& x, const Lab& y) { return distance2 (x, mean) < distance2 (y, mean); }));
    while ((int) centres.size() < count)
    {
        double best = -1;
        Lab far;
        for (const auto& p : points)
        {
            double d = 1e9;
            for (const auto& ctr : centres)
                d = std::min (d, distance2 (p, ctr));
            if (d > best)
            {
                best = d;
                far = p;
            }
        }
        if (best < 1e-6)
            break;   // fewer distinct colours than asked for
        centres.push_back (far);
    }

    std::vector<int> owner (points.size(), 0);
    std::vector<int> sizes;
    for (int iteration = 0; iteration < 12; ++iteration)
    {
        for (size_t i = 0; i < points.size(); ++i)
        {
            double best = 1e9;
            for (size_t j = 0; j < centres.size(); ++j)
                if (const auto d = distance2 (points[i], centres[j]); d < best)
                {
                    best = d;
                    owner[i] = (int) j;
                }
        }
        std::vector<Lab> sums (centres.size());
        sizes.assign (centres.size(), 0);
        for (size_t i = 0; i < points.size(); ++i)
        {
            auto& s = sums[(size_t) owner[i]];
            s.L += points[i].L;
            s.a += points[i].a;
            s.b += points[i].b;
            ++sizes[(size_t) owner[i]];
        }
        for (size_t j = 0; j < centres.size(); ++j)
            if (sizes[j] > 0)
                centres[j] = { sums[j].L / sizes[j], sums[j].a / sizes[j], sums[j].b / sizes[j] };
    }

    for (size_t j = 0; j < centres.size(); ++j)
        if (sizes[j] > 0)
            result.push_back ({ clamped (fromLabRaw (centres[j])), (double) sizes[j] / (double) points.size() });
    std::sort (result.begin(), result.end(), [] (const Swatch& x, const Swatch& y) { return x.share > y.share; });
    return result;
}

Settings settingsFromPalette (const std::vector<Swatch>& palette, Settings start)
{
    if (palette.empty())
        return start;

    double meanL = 0, best = -1;
    for (const auto& s : palette)
    {
        const auto lch = toLch (s.color);
        meanL += lch.L * s.share;
        const double score = (lch.L > 0.3 && lch.L < 0.92 ? lch.C : lch.C * 0.3) * std::sqrt (s.share);
        if (score > best)
        {
            best = score;
            start.seed = s.color;
        }
    }
    if (toLch (start.seed).C < 0.03)
        start.seed = palette.front().color;
    start.dark = meanL < 0.6;
    start.seed.a = 1;
    return start;
}

double luminance (Rgb c)
{
    return 0.2126 * toLinear (c.r) + 0.7152 * toLinear (c.g) + 0.0722 * toLinear (c.b);
}

double contrastRatio (Rgb a, Rgb b)
{
    const double x = luminance (a), y = luminance (b);
    return (std::max (x, y) + 0.05) / (std::min (x, y) + 0.05);
}

namespace
{
    struct Pair { const char* fg; const char* bg; const char* what; double needed; };
    const Pair pairs[] = {
        { "text", "base", "Text in lists and fields", 4.5 },
        { "windowText", "window", "Text on panels", 4.5 },
        { "buttonText", "button", "Button and menu text", 4.5 },
        { "highlightedText", "highlight", "Selected item text", 4.5 },
        { "placeholderText", "base", "Hint text in fields", 4.5 },
        { "accent", "window", "Focus outline and links", 3.0 },
        { "waveform", "waveformBackground", "Waveform", 3.0 },
        { "cursor", "waveformBackground", "Cursor", 3.0 },
        { "marker", "waveformBackground", "Markers", 3.0 },
        { "region", "waveformBackground", "Regions", 3.0 },
        { "meterLow", "meterBackground", "Meter, low levels", 3.0 },
        { "meterMid", "meterBackground", "Meter, middle levels", 3.0 },
        { "meterHigh", "meterBackground", "Meter, high levels", 3.0 },
    };

    Rgb at (const Colors& colors, const char* key)
    {
        const auto it = colors.find (key);
        return it == colors.end() ? Rgb {} : it->second;
    }

    // Moves a colour's lightness one way until it has `needed` contrast with `other`.
    bool moveLightness (Rgb& c, const Rgb& other, double needed, bool lighter)
    {
        auto lch = toLch (c);
        for (int i = 0; i < 100 && contrastRatio (c, other) < needed; ++i)
        {
            lch.L = std::clamp (lch.L + (lighter ? 0.01 : -0.01), 0.0, 1.0);
            c = fromLch (lch, c.a);
            if (lch.L <= 0.0 || lch.L >= 1.0)
                break;
        }
        return contrastRatio (c, other) >= needed;
    }
}

std::vector<ContrastCheck> checkContrast (const Colors& colors)
{
    std::vector<ContrastCheck> result;
    for (const auto& p : pairs)
        if (colors.count (p.fg) && colors.count (p.bg))
            result.push_back ({ p.fg, p.bg, p.what, contrastRatio (at (colors, p.fg), at (colors, p.bg)), p.needed });
    return result;
}

int fixContrast (Colors& colors, const Colors& locked)
{
    int failing = 0;
    for (const auto& check : checkContrast (colors))
        failing += check.ok() ? 0 : 1;

    // A background can be shared by several pairs, so go round a few times.
    for (int pass = 0; pass < 3; ++pass)
        for (const auto& p : pairs)
        {
            if (! colors.count (p.fg) || ! colors.count (p.bg))
                continue;
            auto fg = colors[p.fg], bg = colors[p.bg];
            if (contrastRatio (fg, bg) >= p.needed)
                continue;

            // Away from the other colour first; if that runs out of room, the other way.
            const auto move = [&] (Rgb& c, const Rgb& other)
            {
                const bool lighter = luminance (c) >= luminance (other);
                auto tried = c;
                if (moveLightness (tried, other, p.needed, lighter))
                    c = tried;
                else if (auto back = c; moveLightness (back, other, p.needed, ! lighter))
                    c = back;
                else
                    c = tried;
            };
            if (! locked.count (p.fg))
            {
                move (fg, bg);
                colors[p.fg] = fg;
            }
            else if (! locked.count (p.bg))
            {
                move (bg, fg);
                colors[p.bg] = bg;
            }
        }

    int still = 0;
    for (const auto& check : checkContrast (colors))
        still += check.ok() ? 0 : 1;
    return std::max (0, failing - still);
}

std::string toHex (Rgb c)
{
    const auto byte = [] (double v) { return (int) std::lround (std::clamp (v, 0.0, 1.0) * 255.0); };
    char text[16];
    if (byte (c.a) >= 255)
        std::snprintf (text, sizeof text, "#%02x%02x%02x", byte (c.r), byte (c.g), byte (c.b));
    else
        std::snprintf (text, sizeof text, "#%02x%02x%02x%02x", byte (c.r), byte (c.g), byte (c.b), byte (c.a));
    return text;
}

bool fromHex (const std::string& textIn, Rgb& out)
{
    auto text = textIn;
    while (! text.empty() && std::isspace ((unsigned char) text.back()))
        text.pop_back();
    while (! text.empty() && std::isspace ((unsigned char) text.front()))
        text.erase (text.begin());
    if (text.empty() || text[0] != '#')
        return false;
    text.erase (text.begin());
    for (const char ch : text)
        if (! std::isxdigit ((unsigned char) ch))
            return false;

    if (text.size() == 3)
        text = std::string { text[0], text[0], text[1], text[1], text[2], text[2] };
    if (text.size() != 6 && text.size() != 8)
        return false;

    const auto part = [&text] (size_t i) { return std::stoi (text.substr (i, 2), nullptr, 16) / 255.0; };
    out = { part (0), part (2), part (4), text.size() == 8 ? part (6) : 1.0 };
    return true;
}

namespace
{
    std::string quoted (const std::string& s)
    {
        std::string r = "\"";
        for (const unsigned char ch : s)
        {
            if (ch == '"' || ch == '\\')
                (r += '\\') += (char) ch;
            else if (ch < 0x20)
            {
                char esc[8];
                std::snprintf (esc, sizeof esc, "\\u%04x", ch);
                r += esc;
            }
            else
                r += (char) ch;
        }
        return r + "\"";
    }

    std::string number (double v)
    {
        char text[32];
        std::snprintf (text, sizeof text, "%.2f", v);
        return text;
    }
}

bool fromReaperTheme (const std::string& text, Colors& out, Settings& settings)
{
    // REAPER keeps colours as Windows COLORREF numbers (0x00bbggrr) in decimal; the
    // top byte can carry flags, which makes some of them negative.
    std::map<std::string, Rgb> reaper;
    std::istringstream lines (text);
    std::string line;
    while (std::getline (lines, line))
    {
        if (! line.empty() && line.back() == '\r')
            line.pop_back();
        const auto eq = line.find ('=');
        if (eq == std::string::npos || eq == 0)
            continue;
        const auto value = line.substr (eq + 1);
        if (value.empty() || value.find_first_not_of ("-0123456789") != std::string::npos)
            continue;
        long long n = 0;
        try { n = std::stoll (value); } catch (...) { continue; }
        const auto bits = (unsigned long long) n;
        reaper[line.substr (0, eq)] = { (double) (bits & 0xff) / 255.0, (double) ((bits >> 8) & 0xff) / 255.0,
                                        (double) ((bits >> 16) & 0xff) / 255.0, 1.0 };
    }

    // PHI key, then the REAPER keys to take it from, best first.
    static const std::vector<std::pair<const char*, std::vector<const char*>>> map {
        { "window", { "col_main_bg2", "col_main_bg" } },
        { "windowText", { "col_main_text2" } },
        { "base", { "col_main_editbk" } },
        { "text", { "col_main_text" } },
        { "alternateBase", { "col_tracklistbg" } },
        { "button", { "col_buttonbg" } },
        { "buttonText", { "col_toolbar_text", "col_main_text2" } },
        { "mid", { "col_tr1_divline", "col_main_3dsh" } },
        { "highlight", { "col_tr1_itembgsel", "col_tl_bgsel" } },
        { "accent", { "col_tl_bgsel", "playcursor_color" } },
        { "waveform", { "col_tr1_peaks" } },
        { "waveformBackground", { "col_mi_bg2", "col_mi_bg", "col_arrangebg" } },
        { "centerLine", { "col_gridlines" } },
        { "cursor", { "col_cursor" } },
        { "marker", { "marker" } },
        { "region", { "region" } },
        { "meterBackground", { "col_vuintcol" } },
        { "meterLow", { "col_vubot" } },
        { "meterMid", { "col_vumid" } },
        { "meterHigh", { "col_vutop" } },
    };

    Colors found;
    for (const auto& [key, sources] : map)
        for (const auto* source : sources)
            if (const auto it = reaper.find (source); it != reaper.end())
            {
                found[key] = it->second;
                break;
            }
    if (found.empty())
        return false;

    if (found.count ("waveform"))
        found["scope"] = found["waveform"];
    if (found.count ("highlight"))
    {
        auto s = found["highlight"];
        s.a = 0.25;
        found["selection"] = s;
    }

    settings.seed = found.count ("waveform") ? found["waveform"] : found.count ("highlight") ? found["highlight"] : settings.seed;
    if (found.count ("window"))
        settings.dark = luminance (found["window"]) < 0.2;
    out = found;
    return true;
}

const char* harmonyName (Harmony h)
{
    switch (h)
    {
        case Harmony::mono:               return "mono";
        case Harmony::analogous:          return "analogous";
        case Harmony::complementary:      return "complementary";
        case Harmony::splitComplementary: return "split-complementary";
        case Harmony::triadic:            return "triadic";
    }
    return "complementary";
}

bool harmonyFromName (const std::string& name, Harmony& out)
{
    for (auto h : { Harmony::mono, Harmony::analogous, Harmony::complementary, Harmony::splitComplementary, Harmony::triadic })
        if (name == harmonyName (h))
        {
            out = h;
            return true;
        }
    if (name == "monochrome")
    {
        out = Harmony::mono;
        return true;
    }
    return false;
}

std::string toJson (const std::string& name, const Result& theme, const Settings& settings)
{
    std::ostringstream j;
    j << "{\n  \"name\": " << quoted (name) << ",\n";
    j << "  \"base\": \"" << (theme.dark ? "dark" : "light") << "\",\n";
    j << "  \"dark\": " << (theme.dark ? "true" : "false") << ",\n";
    j << "  \"colors\": {\n";
    bool first = true;
    for (const auto& key : colorKeys())
    {
        const auto it = theme.colors.find (key);
        if (it == theme.colors.end())
            continue;
        j << (first ? "" : ",\n") << "    " << quoted (key) << ": \"" << toHex (it->second) << "\"";
        first = false;
    }
    j << "\n  },\n  \"spectrogram\": [";
    for (size_t i = 0; i < theme.spectrogram.size(); ++i)
        j << (i == 0 ? "" : ", ") << "\"" << toHex (theme.spectrogram[i]) << "\"";
    j << "],\n";
    j << "  \"generator\": {\n";
    j << "    \"seed\": \"" << toHex (settings.seed) << "\",\n";
    j << "    \"harmony\": \"" << harmonyName (settings.harmony) << "\",\n";
    j << "    \"saturation\": " << number (settings.saturation) << ",\n";
    j << "    \"contrast\": " << number (settings.contrast) << ",\n";
    j << "    \"tint\": " << number (settings.tint) << ",\n";
    j << "    \"meters\": \"" << (settings.meters == Meters::classic ? "classic" : "palette") << "\",\n";
    j << "    \"variation\": " << settings.variation << "\n";
    j << "  }\n}\n";
    return j.str();
}
}

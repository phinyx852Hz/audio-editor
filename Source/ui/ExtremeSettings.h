// SPDX-License-Identifier: AGPL-3.0-only
#pragma once

// The Extreme Stretch (Paulstretch) settings the editor remembers, shared with the mixer's
// clip Inspector so a clip stretches the way the editor last did.
#include "Settings.h"
#include "../core/Paulstretch.h"

#include <string>

// The Extreme Stretch settings, remembered between runs (the stretch itself is "extreme/ratio").
inline paulstretch::Settings loadExtremeSettings()
{
    const auto st = appSettings();
    auto v = [&st] (const char* key, double fallback) { return st.value (QStringLiteral ("extreme/") + QLatin1String (key), fallback).toDouble(); };
    paulstretch::Settings p;
    p.windowSeconds = v ("window", p.windowSeconds);
    p.onsets = v ("onsets", p.onsets);
    p.harmonics = (int) v ("harmonics", p.harmonics);
    p.harmonicsHz = v ("harmonicsHz", p.harmonicsHz);
    p.harmonicsCents = v ("harmonicsCents", p.harmonicsCents);
    p.harmonicsSoft = v ("harmonicsSoft", 1.0) != 0.0;
    p.tonalNoise = v ("tonalNoise", p.tonalNoise);
    p.frequencyShiftHz = v ("shift", p.frequencyShiftHz);
    p.pitchSemitones = v ("pitch", p.pitchSemitones);
    for (size_t r = 0; r < p.ratioLevels.size(); ++r)
        p.ratioLevels[r] = v (("ratioLevel" + std::to_string (r)).c_str(), p.ratioLevels[r]);
    p.spreadOctaves = v ("spread", p.spreadOctaves);
    p.lowHz = v ("lowHz", p.lowHz);
    p.highHz = v ("highHz", p.highHz);
    p.bandStop = v ("bandStop", 0.0) != 0.0;
    p.highDamp = v ("highDamp", p.highDamp);
    p.compress = v ("compress", p.compress);
    return p;
}

inline void saveExtremeSettings (const paulstretch::Settings& p)
{
    auto st = appSettings();
    auto put = [&st] (const char* key, double value) { st.setValue (QStringLiteral ("extreme/") + QLatin1String (key), value); };
    put ("window", p.windowSeconds);
    put ("onsets", p.onsets);
    put ("harmonics", p.harmonics);
    put ("harmonicsHz", p.harmonicsHz);
    put ("harmonicsCents", p.harmonicsCents);
    put ("harmonicsSoft", p.harmonicsSoft ? 1.0 : 0.0);
    put ("tonalNoise", p.tonalNoise);
    put ("shift", p.frequencyShiftHz);
    put ("pitch", p.pitchSemitones);
    for (size_t r = 0; r < p.ratioLevels.size(); ++r)
        put (("ratioLevel" + std::to_string (r)).c_str(), p.ratioLevels[r]);
    put ("spread", p.spreadOctaves);
    put ("lowHz", p.lowHz);
    put ("highHz", p.highHz);
    put ("bandStop", p.bandStop ? 1.0 : 0.0);
    put ("highDamp", p.highDamp);
    put ("compress", p.compress);
}

// The ratio last used (8 = eight times as long).
inline double extremeRatio() { return appSettings().value (QStringLiteral ("extreme/ratio"), 8.0).toDouble(); }

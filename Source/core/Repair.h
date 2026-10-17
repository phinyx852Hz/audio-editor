// SPDX-License-Identifier: AGPL-3.0-only
#pragma once

#include "AudioData.h"

#include <string>

// Spectral repair: edits on a time and frequency box, done on a short-time
// Fourier transform of the audio around it. Only the change is added back, so
// everything outside the box (and its feathered edge) stays bit-for-bit the same.
// Plain C++.
namespace repair
{
    // A time range, optionally limited to a frequency band. Without a band
    // (highHz <= lowHz) the edit covers every frequency.
    struct SpectralRegion
    {
        Frame start = 0, end = 0;
        double lowHz = 0.0, highHz = 0.0;

        bool hasBand() const { return highHz > lowHz; }
    };

    // Turns the region down (or up) by gainDb. At or below removeDb it is removed.
    inline constexpr double removeDb = -120.0;
    AudioDataPtr spectralGain (const AudioData&, const SpectralRegion&, double gainDb);

    // Auto Heal: rebuilds the region from the sound just before and after it, for
    // clicks, coughs, squeaks and phone rings. Returns null with `error` set when
    // the region cannot be healed (too long, or no audio around it).
    inline constexpr double maxHealSeconds = 4.0;
    AudioDataPtr autoHeal (const AudioData&, const SpectralRegion&, std::string& error);
}

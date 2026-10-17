// SPDX-License-Identifier: AGPL-3.0-only
#pragma once

#include "AudioData.h"

#include <atomic>
#include <string>
#include <vector>

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

    //==========================================================================
    // Restoration over a time range (the selection, or the whole file). Each
    // reports progress from 0 to 1 and stops early, returning null, when `cancel`
    // becomes true. Both may be null.
    struct Progress
    {
        std::atomic<float>* done = nullptr;
        const std::atomic<bool>* cancel = nullptr;
        bool cancelled() const { return cancel != nullptr && cancel->load(); }
        void report (float f) const { if (done != nullptr) done->store (f); }
    };

    // What a detect-and-fix tool found.
    struct Report
    {
        int fixed = 0;     // clicks or clipped stretches rebuilt
        int skipped = 0;   // found but too long to rebuild from their surroundings
    };

    // The average spectrum of background noise, learned from a stretch that holds only noise.
    struct NoiseProfile
    {
        double sampleRate = 0.0;
        int fftOrder = 11;
        std::vector<std::vector<float>> power;   // [channel][bin], mean power per bin
        Frame learnedFrom = 0;                   // frames of audio it was learned from

        bool isValid() const { return ! power.empty(); }
    };
    NoiseProfile learnNoise (const AudioData&, Frame start, Frame end);

    struct NoiseReductionSettings
    {
        double reductionDb = 12.0;   // how far the noise is turned down
        double sensitivityDb = 6.0;  // how far above the noise a sound must be to be kept
        double releaseMs = 100.0;    // how long the reduction takes to come back after a sound
    };
    AudioDataPtr reduceNoise (const AudioData&, Frame start, Frame end, const NoiseProfile&,
                              const NoiseReductionSettings&, Progress = {});

    // Finds clicks and crackle and rebuilds each from the audio around it.
    // Lower sensitivity values find more (it is a threshold in robust standard deviations).
    AudioDataPtr declick (const AudioData&, Frame start, Frame end, double sensitivity, Report&, Progress = {});

    // Rebuilds stretches of two or more samples stuck at the clipping level
    // (`levelPercent` of the loudest sample). The result may peak above 0 dBFS.
    AudioDataPtr declip (const AudioData&, Frame start, Frame end, double levelPercent, Report&, Progress = {});

    // Removes mains hum: narrow notches at the hum frequency and its harmonics.
    // frequency 0 finds 50 or 60 Hz (and the exact value) by itself.
    struct HumSettings
    {
        double frequency = 0.0;
        int harmonics = 8;
        double widthHz = 1.5;
    };
    double findHum (const AudioData&, Frame start, Frame end);   // 0 when there is no clear hum
    AudioDataPtr dehum (const AudioData&, Frame start, Frame end, const HumSettings&, Progress = {});

    // Turns down harsh "s" and "sh" sounds: above `thresholdDb` the band from
    // `fromHz` up is reduced, by at most `maxReductionDb`.
    struct DeEssSettings
    {
        double thresholdDb = -30.0;
        double fromHz = 5000.0;
        double maxReductionDb = 10.0;
    };
    AudioDataPtr deess (const AudioData&, Frame start, Frame end, const DeEssSettings&, Progress = {});

    // Fills x[gapStart, gapStart + gapLength) from an autoregressive model of the
    // samples around it (least-squares interpolation). x must hold enough audio
    // either side. Returns false when the gap is too long.
    bool interpolateGap (std::vector<float>& x, int gapStart, int gapLength);
}

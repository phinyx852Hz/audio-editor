// SPDX-License-Identifier: AGPL-3.0-only
#pragma once

#include "AudioData.h"

#include <array>
#include <atomic>
#include <functional>
#include <string>

// Extreme time stretching after Nasca Octavian Paul's Paulstretch (the algorithm
// behind PaulXStretch): long windows are read slowly through the source, each one's
// spectrum kept but its phases randomised, and the results overlapped, so a sound
// becomes a smooth, evolving texture at any ratio (8, 100, 1000 times longer).
// The spectral chain offers what PaulXStretch does (harmonics, tonal/noise, shifts,
// ratio mix, spread, filter, compressor, onsets). Written for PHI from the published
// algorithm; no Paulstretch code is used (it is GPL-2.0-only). Plain C++.
namespace paulstretch
{
    // The pitch ratios the ratio mix blends (1 is the stretched sound itself).
    constexpr std::array<double, 8> mixRatios { 0.25, 0.5, 2.0 / 3.0, 1.0, 1.5, 2.0, 3.0, 4.0 };

    // Each window's spectrum goes through these in order, as in PaulXStretch: harmonics,
    // tonal/noise, frequency shift, pitch shift, ratio mix, spread, filter, compressor.
    struct Settings
    {
        double windowSeconds = 0.25;   // longer: smoother and more smeared; shorter: more detail, more grain
        double onsets = 0.0;           // 0 to 1: how much hits are kept by passing them at normal speed

        int harmonics = 0;             // keep only this many harmonics of harmonicsHz (0: off)
        double harmonicsHz = 440.0;
        double harmonicsCents = 25.0;  // the width of each harmonic
        bool harmonicsSoft = true;     // gaussian edges, or hard ones
        double tonalNoise = 0.0;       // -1 keeps only the noise, +1 only the tones (spectral peaks), 0 both
        double frequencyShiftHz = 0.0; // adds the same number of Hz to every partial (inharmonic), -1000 to +1000
        double pitchSemitones = 0.0;   // moves every partial up or down, -24 to +24
        std::array<double, 8> ratioLevels { 0, 0, 0, 1, 0, 0, 0, 0 };   // the level of each of mixRatios
        double spreadOctaves = 0.0;    // smears each partial over this many octaves, 0 to 2: chorus to wash
        double lowHz = 0.0, highHz = 0.0;   // the band kept (or removed with bandStop); highHz 0: no filter
        bool bandStop = false;
        double highDamp = 0.0;         // 0 to 1: a gentle roll-off above 1 kHz, up to 6 dB an octave
        double compress = 0.0;         // 0 to 1: evens out loud and quiet windows
        unsigned seed = 1;             // the random phases: the same seed renders the same result
    };

    // The FFT size used for a window length: a power of two, 256 to 262144 frames.
    int windowFrames (double windowSeconds, double sampleRate);

    // The longest result render() makes (an hour), so a huge ratio cannot fill the memory.
    Frame maxResultFrames (double sampleRate);

    // Frames [start, end) stretched to `resultLength`. Every channel uses the same random
    // phases, so stereo and surround keep their image. `sourceAt` maps a result frame to
    // the source frame (both relative to start) for uneven stretches; even when empty
    // (then Settings::onsets pass the hits at normal speed and stretch the rest more).
    // The level of steady material stays about the same. Null (with `error`) when
    // cancelled or out of range.
    AudioDataPtr render (const AudioData&, Frame start, Frame end, Frame resultLength, const Settings&,
                         std::string& error, std::atomic<float>* progress = nullptr,
                         const std::atomic<bool>* cancel = nullptr, const std::function<double (double)>& sourceAt = {});

    // The whole range at one ratio (8 = eight times as long).
    AudioDataPtr stretch (const AudioData&, Frame start, Frame end, double ratio, const Settings&, std::string& error,
                          std::atomic<float>* progress = nullptr, const std::atomic<bool>* cancel = nullptr);
}

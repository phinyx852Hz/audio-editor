// SPDX-License-Identifier: AGPL-3.0-only
#pragma once

#include "AudioData.h"

#include <array>
#include <cmath>
#include <random>
#include <vector>

// Offline processing and measurement on AudioData. Plain C++.
namespace dsp
{
    inline double gainToDb (double g)   { return g > 0.0 ? 20.0 * std::log10 (g) : -200.0; }
    inline double dbToGain (double db)  { return std::pow (10.0, db / 20.0); }

    enum class FadeShape { linear, equalPower, exponential, sCurve };
    float fadeGain (double t, FadeShape);  // t in [0, 1], rising

    AudioDataPtr applyGain (const AudioData&, Frame start, Frame end, double gainDb);
    AudioDataPtr fadeIn (const AudioData&, Frame start, Frame end, FadeShape);
    AudioDataPtr fadeOut (const AudioData&, Frame start, Frame end, FadeShape);
    AudioDataPtr invert (const AudioData&, Frame start, Frame end);
    AudioDataPtr reverse (const AudioData&, Frame start, Frame end);
    AudioDataPtr removeDcOffset (const AudioData&, Frame start, Frame end);
    AudioDataPtr silence (int numChannels, double sampleRate, Frame length);

    // ITU-R BS.1770 K-weighting: high shelf followed by high pass.
    class KWeighting
    {
    public:
        void prepare (double sampleRate);
        float process (float x);
        void reset();

    private:
        struct Biquad
        {
            double b0 = 1, b1 = 0, b2 = 0, a1 = 0, a2 = 0, z1 = 0, z2 = 0;
            float process (float x);
        };

        Biquad shelf, highPass;
    };

    // 4x oversampled peak detector for ITU-R BS.1770 true peak.
    class TruePeakDetector
    {
    public:
        TruePeakDetector();
        float process (float x);  // returns the largest |value| of the 4 interpolated points
        void reset();

    private:
        static constexpr int tapsPerPhase = 12;
        std::array<std::array<float, tapsPerPhase>, 4> phases {};
        std::array<float, tapsPerPhase> history {};
        int writeIndex = 0;
    };

    // Channel weight for loudness summing (surround channels of 5.1 count more, LFE not at all).
    double loudnessChannelWeight (int channel, int numChannels);

    struct LoudnessReport
    {
        double integratedLufs = -200.0;
        double loudnessRange = 0.0;   // LU
        double samplePeakDb = -200.0;
        double truePeakDb = -200.0;
    };

    LoudnessReport measureLoudness (const AudioData&, Frame start, Frame end);

    // Integrated loudness from 100 ms block powers (K-weighted, channel-weighted mean squares).
    double integratedLoudness (const std::vector<double>& blockPowers100ms);

    // Adds triangular (TPDF) dither of one LSB at the given bit depth.
    void addTpdfDither (std::vector<std::vector<float>>& block, int bitDepth, std::mt19937& rng);
}

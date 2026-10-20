// SPDX-License-Identifier: AGPL-3.0-only
#pragma once

// Mid/Side Width: widens or narrows a stereo image by working on its mid (L + R)
// and side (L - R) separately, the way mastering wideners do. The side can be
// made mono below a crossover (tight bass), given made-up width from the mid
// (pure side, so it cancels when summed to mono), brightened with an "air" shelf,
// pushed wider on louder moments (lift), and the whole image rotated. Shared by
// Process/Spatial > Mid/Side Width and the live effect in the Master Section.
#include "AudioData.h"
#include "Dsp.h"

#include <string>
#include <vector>

namespace midside
{
    enum class Listen { stereo, mono, side, bass };

    struct Settings
    {
        double width = 1.0;          // the side level: 0 (mono) .. 2 (200 %), up to 3 with `huge`
        bool huge = false;           // allows up to 300 %, pulled back while the correlation is below -0.3
        double monoBelowHz = 0.0;    // 0: off; else the side is removed below this (20..500 Hz)
        double generate = 0.0;       // 0..1: made-up side from the mid above 150 Hz
        double airDb = 0.0;          // -6..+6: a high shelf (8 kHz) on the side only
        double rotation = 0.0;       // degrees, -45..45; positive turns the image to the right
        double lift = 0.0;           // 0..1: up to twice the width on louder moments
        bool matchLevel = true;      // keeps the output as loud as the input
        Listen listen = Listen::stereo;

        double maxWidth() const { return huge ? 3.0 : 2.0; }
    };

    struct Preset
    {
        std::string name;
        Settings settings;
    };
    const std::vector<Preset>& presets();

    // The streaming core. No allocation after prepare().
    class Processor
    {
    public:
        void prepare (double sampleRate);
        void set (const Settings&);   // safe between samples
        void process (float& left, float& right);
        void reset();
        double correlation() const;   // of the output, over about 300 ms

    private:
        struct Biquad
        {
            double b0 = 1, b1 = 0, b2 = 0, a1 = 0, a2 = 0, x1 = 0, x2 = 0, y1 = 0, y2 = 0;
            double run (double x)
            {
                const auto y = b0 * x + b1 * x1 + b2 * x2 - a1 * y1 - a2 * y2;
                x2 = x1; x1 = x; y2 = y1; y1 = y;
                return y;
            }
            void clear() { x1 = x2 = y1 = y2 = 0; }
        };
        void design();

        double rate = 48000.0;
        Settings settings, designed;
        bool fresh = true;
        Biquad sideHigh[2], bassLow[4], air;   // the crossover is two Butterworths in a row (Linkwitz-Riley)
        dsp::Stereoizer generator;
        double fast = 0, slow = 0, fastAttack = 0, fastRelease = 0, slowCoeff = 0;   // lift
        double ll = 0, rr = 0, lr = 0, avgCoeff = 0;                                     // correlation
        double inPower = 0, outPower = 0, match = 1, powerCoeff = 0, matchCoeff = 0;     // level match
        double guard = 1, guardCoeff = 0;                                                // huge
        double width = 1, widthCoeff = 0;                                                // smoothed
    };

    // A two-channel copy of the whole file with [start, end) processed (a mono file
    // plays the same on both sides). Listening modes other than stereo are kept.
    AudioDataPtr render (const AudioData&, Frame start, Frame end, const Settings&);

    struct Analysis
    {
        double correlation = 1.0;   // -1..1
        double monoSumDb = 0.0;     // the mono sum's level against the stereo level (0: no loss)
        double sideDb = -120.0;     // the side's level against the mid's
    };
    Analysis analyse (const AudioData&, Frame start, Frame end);
}

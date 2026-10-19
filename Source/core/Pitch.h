// SPDX-License-Identifier: AGPL-3.0-only
#pragma once

#include "AudioData.h"

#include <atomic>
#include <memory>
#include <vector>

// The pitch of the main voice or instrument over time: YIN (de Cheveigné and
// Kawahara, 2002) on the channels mixed to mono, with the difference function
// computed by FFT, then smoothed: octave slips corrected and stray frames
// dropped. Plain C++.
namespace pitch
{
    struct Point
    {
        Frame at = 0;             // centre of the analysis frame
        float hz = 0.0f;          // 0: no pitch here (silence, noise, a chord too dense)
        float confidence = 0.0f;  // 0 to 1
    };

    struct Settings
    {
        double lowestHz = 50.0;
        double highestHz = 2000.0;
        double threshold = 0.15;      // YIN's dip threshold: lower is stricter
        double silenceDb = -50.0;     // quieter frames have no pitch
        int hop = 256;                // frames between points
    };

    struct Track
    {
        double sampleRate = 0.0;
        int hop = 0;
        std::vector<Point> points;

        // The pitch at a frame (the nearest point), 0 where there is none.
        float at (Frame) const;
    };

    std::shared_ptr<Track> track (const AudioData&, const Settings& = {}, std::atomic<float>* progress = nullptr,
                                  const std::atomic<bool>* cancel = nullptr);
}

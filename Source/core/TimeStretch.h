// SPDX-License-Identifier: AGPL-3.0-only
#pragma once

#include "AudioData.h"
#include "BeatGrid.h"

#include <atomic>
#include <string>
#include <vector>

namespace beatgrid
{
    // The grid moved onto the hits in the audio: beat trackers place beats up to a few
    // tens of ms early or late, so the grid shifts by the median distance from each beat
    // to the sharpest rise in level near it. When `chooseBar1` is set, bar 1 also moves
    // to the beat of the bar with the most low-end punch (where the kick usually is).
    BeatGrid align (const AudioData&, BeatGrid, bool chooseBar1);
}

// Offline time stretching (Rubber Band; the beat grid is in BeatGrid.h). Plain C++.
namespace timestretch
{
    // How the audio is stretched. Drums keeps hits sharp, Melody suits single voices
    // and instruments, Full Mix whole songs, Full Mix + Voice keeps the character of
    // voices when pitch moves too; Varispeed changes speed and pitch together like tape.
    enum class Mode { drums, melody, fullMix, fullMixVoice, varispeed };
    const char* modeName (Mode);
    std::vector<Mode> modes();

    // A point in the audio and where it lands in the result (both in frames).
    struct Anchor
    {
        Frame source = 0;
        Frame target = 0;
    };

    // Frames [start, end) of the audio, stretched so each anchor's source lands on its
    // target; the audio between anchors is stretched evenly. Anchors are relative to
    // `start` and sorted; (0, 0) and (end - start, length of the result) are implied.
    // Every channel is stretched together, so stereo and surround stay coherent. Pitch
    // is kept except in Varispeed. Null (with `error`) when cancelled or out of range.
    AudioDataPtr render (const AudioData&, Frame start, Frame end, Frame resultLength, std::vector<Anchor> anchors,
                         Mode, std::string& error, std::atomic<float>* progress = nullptr,
                         const std::atomic<bool>* cancel = nullptr);

    // The whole range at one ratio (2 = twice as long).
    AudioDataPtr stretch (const AudioData&, Frame start, Frame end, double ratio, Mode, std::string& error,
                          std::atomic<float>* progress = nullptr, const std::atomic<bool>* cancel = nullptr);
}

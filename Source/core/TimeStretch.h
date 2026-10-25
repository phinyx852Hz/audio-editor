// SPDX-License-Identifier: AGPL-3.0-only
#pragma once

#include "AudioData.h"
#include "BeatGrid.h"
#include "Quantize.h"

#include <array>
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
    // Beats does not stretch at all: the audio is cut at every hit and each piece plays
    // from its new place at its own speed, its tail looped to fill a longer gap (Beats,
    // No Loop lets it ring out and fade instead), so hits stay exactly as recorded.
    // Texture overlaps short grains, scattered a little, for pads, drones and noise;
    // Smooth is a softer phase vocoder for sustained sounds with no sharp hits. Ambient
    // is Paulstretch (core/Paulstretch.h): everything smeared into an evolving wash, for
    // big ratios (Warp > Extreme Stretch goes far beyond 64 times and has its settings).
    // (The values are stored in settings and documents, so new modes go at the end.)
    enum class Mode { drums, melody, fullMix, fullMixVoice, varispeed, beats, beatsNoLoop, texture, smooth, ambient };
    const char* modeName (Mode);
    const char* modeDescription (Mode);
    std::vector<Mode> modes();

    // Where hits start: sharp rises in level, at least `minGapMs` apart (sensitivity 0 to 1).
    std::vector<Frame> transients (const AudioData&, double sensitivity = 0.5, double minGapMs = 50.0);

    // Quantizing hits to the grid (audioquantize::Settings: distances are fractions of one step).
    using Quantize = audioquantize::Settings;
    // Where each hit in [from, to) of the source should land: (source, target) pairs,
    // in order, at least 1 ms apart. Hits that do not move are left out.
    std::vector<std::pair<Frame, Frame>> quantize (const std::vector<Frame>& hits, const BeatGrid&, double sampleRate,
                                                   const Quantize&, Frame from, Frame to);

    // A point in the audio and where it lands in the result (both in frames).
    struct Anchor
    {
        Frame source = 0;
        Frame target = 0;
    };

    // Segments already rendered, so moving one warp marker re-renders only the two
    // segments beside it. Holds results for one source audio; cleared when it changes.
    class SegmentCache
    {
    public:
        AudioDataPtr find (const AudioData& source, Frame from, Frame to, Frame length, Mode) const;
        void add (const AudioData& source, Frame from, Frame to, Frame length, Mode, AudioDataPtr);
        void clear() { pieces.clear(); owner = nullptr; }

    private:
        const AudioData* owner = nullptr;
        std::vector<std::pair<std::array<long long, 4>, AudioDataPtr>> pieces;
    };

    // Frames [start, end) of the audio, stretched so each anchor's source lands on its
    // target; the audio between anchors is stretched evenly. Anchors are relative to
    // `start` and sorted; (0, 0) and (end - start, length of the result) are implied.
    // Every channel is stretched together, so stereo and surround stay coherent. Pitch
    // is kept except in Varispeed. Null (with `error`) when cancelled or out of range.
    // Segments that keep their length are the original audio, untouched. Beats cuts at
    // the anchors and at `hits` (relative to `start`; found in the range when empty).
    AudioDataPtr render (const AudioData&, Frame start, Frame end, Frame resultLength, std::vector<Anchor> anchors,
                         Mode, std::string& error, std::atomic<float>* progress = nullptr,
                         const std::atomic<bool>* cancel = nullptr, SegmentCache* = nullptr,
                         const std::vector<Frame>& hits = {});

    // The whole range at one ratio (2 = twice as long).
    AudioDataPtr stretch (const AudioData&, Frame start, Frame end, double ratio, Mode, std::string& error,
                          std::atomic<float>* progress = nullptr, const std::atomic<bool>* cancel = nullptr);
}

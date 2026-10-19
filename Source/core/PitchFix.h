// SPDX-License-Identifier: AGPL-3.0-only
#pragma once

#include "AudioData.h"
#include "Pitch.h"

#include <atomic>
#include <string>
#include <vector>

// Note-by-note pitch correction: moves a selected note to a target pitch
// (by default the nearest note) and, if asked, flattens its drift and vibrato
// towards that pitch. The pitch follows the pitch track point by point, and
// Rubber Band (R3 engine, formants kept) shifts the audio by a pitch that
// changes smoothly over time. The edges ease in and out, so the audio
// around the selection is untouched. Plain C++.
namespace pitchfix
{
    struct Settings
    {
        double targetHz = 0.0;      // 0: the nearest note to the note's own pitch
        double amount = 1.0;        // 0 to 1: how far to move the note towards the target
        double flatten = 0.0;       // 0 to 1: how much of the drift and vibrato to take out
        bool keepFormants = true;   // keep the voice's character (no chipmunk effect)
    };

    double nearestNoteHz (double hz);   // equal temperament, A4 = 440 Hz
    double cents (double fromHz, double toHz);

    // The note's own pitch over [start, end): the median of its voiced points, 0 if fewer than 3.
    double centreHz (const pitch::Track&, Frame start, Frame end);

    // The shift in cents at each point of the track from `start` to `end` (one value per hop,
    // starting at the first point at or after `start`), eased in and out at the edges.
    std::vector<double> shiftCurve (const pitch::Track&, Frame start, Frame end, const Settings&);

    // The corrected audio for [start, end), the same length. Empty (with `error` set)
    // when the selection has no steady pitch.
    AudioDataPtr correct (const AudioData&, Frame start, Frame end, const pitch::Track&, const Settings&,
                          std::string& error, std::atomic<float>* progress = nullptr);
}

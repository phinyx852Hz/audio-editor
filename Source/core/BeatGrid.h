// SPDX-License-Identifier: AGPL-3.0-only
#pragma once

#include <vector>

// The beat grid of a file (implemented in TimeStretch.cpp). Plain C++.
//
// The grid is a steady tempo, a bar line it is aligned to, and a time signature;
// bars and beats are counted from that bar line (bar 1 is the first whole bar).
struct BeatGrid
{
    double bpm = 0.0;          // 0: no grid
    double downbeat = 0.0;     // seconds: the start of a bar (kept within the first bar)
    int beatsPerBar = 4;

    bool isSet() const { return bpm > 0.0; }
    double beatSeconds() const { return bpm > 0.0 ? 60.0 / bpm : 0.0; }
    double barSeconds() const { return beatSeconds() * beatsPerBar; }
    double beatAt (double seconds) const;        // beats from the downbeat, fractional
    double secondsAt (double beat) const;
    // The nearest grid line, `division` beats apart (1 = beats, 0.25 = sixteenths, beatsPerBar = bars).
    double snap (double seconds, double division) const;
    // Keeps the downbeat within the first bar (a bar line before it is the same grid).
    void normalise();

    bool operator== (const BeatGrid&) const = default;
};

namespace beatgrid
{
    // A grid fitted to beat times: the steady tempo through them (least squares over
    // the beats, numbered by the median gap so a missed beat does not shift the rest)
    // and the downbeat from the first beat marked as beat 1 of a bar (`beatInBar`,
    // 1-based; empty or all 0: the first beat). No grid with fewer than 4 beats.
    BeatGrid fit (const std::vector<double>& beatSeconds, const std::vector<int>& beatInBar, int beatsPerBar = 4);
}

// SPDX-License-Identifier: AGPL-3.0-only
#pragma once

#include <string>
#include <vector>

// What a Φ effect's window draws above its knobs (Source/ui/PhiWindow.cpp), filled in
// by the effect itself from its settings and, for some, from what it hears.
struct DeviceDisplay
{
    enum class Kind { none, frequency, taps, curve, steps };
    Kind kind = Kind::none;

    // frequency: `front` in dB (lowDb at the bottom to highDb at the top, drawn in gold)
    //   and, if not empty, `back`: the level heard in dB full scale (grey), both at the
    //   frequencies 20 Hz × 1000^(i / (n - 1)), 20 Hz to 20 kHz.
    // taps: pairs of (position across, 0 to 1; level, 0 to 1) in `front`.
    // curve: values from 0 (bottom) to 1 (top), evenly across, in `front`.
    // steps: 1 (a hit) or 0 per step in `front`; `back` the same length, 1 on the step playing.
    std::vector<float> front, back;
    float lowDb = -24.0f, highDb = 24.0f;
    std::string axis;      // taps and curve: what across means, "0 to 2.4 s"
    std::string caption;   // a line in the display's corner: "fundamental 58.9 Hz, cut at 476 Hz"
};

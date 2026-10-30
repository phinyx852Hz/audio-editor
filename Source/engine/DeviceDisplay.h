// SPDX-License-Identifier: AGPL-3.0-only
#pragma once

#include <string>
#include <utility>
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

    // frequency: nodes on the curve that the mouse moves (Φ EQ's bands and cuts). Each
    // names the parameters (by index, -1 for none) it moves: dragged across, `hzParameter`
    // is set from text in Hz ("off" past the end of `grid` on the `offSide`); dragged up
    // and down, `dbParameter` from text in dB; the wheel steps `qParameter`; a right click
    // picks `typeParameter` from `types`; a double click sets `reset` (index, 0..1 value).
    struct Handle
    {
        float hz = 1000.0f, db = 0.0f;
        int hzParameter = -1, dbParameter = -1, qParameter = -1, typeParameter = -1;
        int offSide = 0;   // -1: a low cut, off at the left; 1: a high cut, off at the right
        bool on = true;
        std::string label;
        std::vector<std::pair<int, float>> reset;
        std::vector<int> knobs;   // the parameters shown as knobs while the node is chosen
    };
    std::vector<Handle> handles;
    std::vector<float> grid;          // the frequencies the nodes snap to, drawn as ticks
    std::vector<std::string> types;   // the names of typeParameter's choices
    std::vector<int> knobs;           // with handles: the knobs always shown, after the chosen node's

    // Pages (Φ Modulation's Flow, Space, Grain and Matrix): tabs above the knobs, each
    // naming the knobs shown while it is chosen. Choosing one sets `pageParameter` to it
    // (a choice of as many pages), and the picture is the effect's for that page.
    std::vector<std::string> pages;
    std::vector<std::vector<int>> pageKnobs;
    int pageParameter = -1, page = 0;
};

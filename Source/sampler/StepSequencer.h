// SPDX-License-Identifier: AGPL-3.0-only
#pragma once

// The Slicer's step sequencer, after Cubase's Pattern Editor and FL Studio's Channel Rack:
// patterns of lanes, one lane for each of the kit's pads. Each lane runs at its own length,
// rate and direction, so lanes of different lengths run against each other (polymeter);
// each step has its own velocity, gate, pitch, chance, repeats and time shift. The
// Slicer's engine plays them (SlicerEngine.h); this is the data and the arithmetic of
// when a step falls. Plain C++, no JUCE or Qt.
#include <array>
#include <cstdint>

namespace slicer
{
    constexpr int numLanes = 128;   // one for each pad
    constexpr int maxSteps = 64;
    constexpr int numPatterns = 16;
    constexpr int maxChain = 64;
    constexpr int maxRepeats = 8;

    enum class Direction : uint8_t { forward, backward, alternate, random };

    // How long a step is: from a quarter note down to a 64th, with dotted and triplet values.
    enum class Rate : uint8_t
    {
        quarter, quarterDotted, quarterTriplet, eighth, eighthDotted, eighthTriplet,
        sixteenth, sixteenthDotted, sixteenthTriplet, thirtySecond, thirtySecondTriplet, sixtyFourth, count
    };
    double rateBeats (Rate);   // in quarter notes

    struct Step
    {
        bool on = false;
        uint8_t velocity = 100;   // 1..127
        uint8_t gate = 50;        // how long the note is held, % of the step (1..100); Gate pads stop then
        int8_t pitch = 0;         // semitones, -24..24
        uint8_t chance = 100;     // % of the times it plays, 0..100
        uint8_t repeats = 1;      // hits within the step, 1..maxRepeats
        int8_t ramp = 0;          // the repeats' velocity from the first to the last, -100..100 %
        int8_t shift = 0;         // early or late, -50..50 % of a step
    };

    struct Lane
    {
        uint8_t steps = 16;       // 1..maxSteps
        Rate rate = Rate::sixteenth;
        Direction direction = Direction::forward;
        uint8_t offset = 0;       // the step it starts on
        uint8_t swing = 0;        // 0..100 %, on top of the pattern's
        bool mute = false, solo = false;
        std::array<Step, maxSteps> step {};

        bool empty() const;       // no step on
    };

    struct Pattern
    {
        std::array<Lane, numLanes> lanes {};
        uint8_t bars = 1;         // how long before the next pattern of a chain, 1..16
        uint8_t swing = 0;        // 0..100 %: every other step later, by up to half a step

        bool empty() const;
    };

    struct Song
    {
        std::array<Pattern, numPatterns> patterns {};
        std::array<int8_t, maxChain> chain {};   // patterns in the order a chain plays them
        int chainLength = 0;
        bool chainOn = false;
        double bpm = 120.0;
        int beatsPerBar = 4;
    };

    // Which of a lane's steps the k-th step of time plays (k counts from the pattern's start).
    // Random draws the same step for the same k, so a pattern sounds the same each time through.
    int stepIndex (const Lane&, int64_t k);
    // When the lane's k-th step of time falls, in beats from the pattern's start, with swing
    // (every other step) and the step's own shift.
    double stepTime (const Lane&, const Pattern&, int64_t k);
}

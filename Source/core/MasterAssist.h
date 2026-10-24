// SPDX-License-Identifier: AGPL-3.0-only
#pragma once

#include "AudioData.h"

#include <array>
#include <functional>
#include <string>
#include <vector>

// The Master Assistant's core: it never builds a chain, only turns the knobs of
// the one you made. It measures what the chain gives (loudness, true peak,
// peak-to-loudness ratio, tonal balance in octaves, stereo width), compares that
// with a target (a reference track or a preset), and moves the parameters you
// allow until the two are as close as it can get them, rendering a short excerpt
// for every try. The way it compares tone and loudness to a reference follows
// Matchering (Sergree, GPL-3.0); the search is a Levenberg-Marquardt fit on
// finite differences, with Broyden updates between full ones. Plain C++.
namespace assist
{
    // What is measured in a piece of audio.
    struct Profile
    {
        static constexpr int numBands = 10;                   // octaves centred on 31.5 Hz to 16 kHz
        static const std::array<double, numBands>& bandCentres();

        std::array<double, numBands> bandsDb {};              // each octave's level against the mean of all ten, dB
        double lufs = -200.0;                                 // integrated, BS.1770
        double truePeakDb = -200.0;
        double plr = 0.0;                                     // true peak minus integrated loudness, dB
        double lra = 0.0;                                     // loudness range, LU
        double sideToMidDb = -200.0;                          // side energy against mid; -200 for mono
        bool valid = false;                                   // false for silence
    };

    Profile measure (const AudioData&, Frame start, Frame end);

    // Tonal targets for when there is no reference. `keep` asks nothing of the tone.
    enum class Tone { keep, balanced, warm, bright };
    std::array<double, Profile::numBands> toneCurve (Tone);

    // Where the assistant aims. A weight of zero leaves that measure alone.
    struct Target
    {
        double lufs = -14.0, loudnessWeight = 1.0;
        double truePeakDb = -1.0, peakWeight = 2.0;          // only counts above the ceiling
        double plr = 0.0, dynamicsWeight = 0.0;
        std::array<double, Profile::numBands> bandsDb {};
        double toneWeight = 0.0;                              // per octave
        double sideToMidDb = -200.0, widthWeight = 0.0;

        // The same target for an excerpt: what the excerpt differed by from the
        // whole song before processing is taken off, so a target meant for the
        // whole song (as a reference's whole-song loudness is) still fits.
        Target forExcerpt (const Profile& wholeSource, const Profile& excerptSource) const;
    };

    // Everything measured of a reference, at its own loudness and ceiling.
    Target fromReference (const Profile& reference, double ceilingDb);

    // How far `got` is from the target, one value per measure (always the same length).
    std::vector<double> residuals (const Profile& got, const Target&);
    double distance (const Profile& got, const Target&);   // sum of squares of the residuals

    // The parts of [start, end) worth listening to: the loudest stretches and one
    // typical one, in their order in the song, joined with short fades. The
    // whole range when it is no longer than `seconds`.
    AudioDataPtr excerpt (const AudioData&, Frame start, Frame end, double seconds = 30.0);

    // What a parameter does, guessed from its name, and whether the assistant
    // moves it unless told otherwise (gains, EQ band gains, thresholds, ratios and
    // width do; frequencies, times, ceilings, modes and switches do not).
    enum class Role { level, eq, dynamics, time, ceiling, width, other };
    Role guessRole (const std::string& name);
    bool movedByDefault (const std::string& name, int numSteps, bool isBoolean);

    struct Settings
    {
        int maxEvaluations = 60;   // renders, the first one included
        double freedom = 0.3;      // how far (normalised) a parameter may move from where you left it
        double step = 0.02;        // the finite-difference step, normalised
        double stay = 0.3;         // the pull back towards your settings: a move of 0.1 costs as much as `stay` of error
        std::vector<double> freedomEach, stayEach;   // per parameter, in place of the two above when not empty
    };

    // How a ticked knob of this role is treated for this target: a level knob may go
    // anywhere to reach the loudness, and pulls back only lightly; an EQ knob moves
    // only when the tone is matched, a width knob only when the width is, so a
    // knob is never turned to fix something it is not for (widening to get louder).
    struct Policy
    {
        bool moves = true;
        double freedom = 0.3, stay = 0.3;
    };
    Policy policy (Role, const Target&, double freedom);

    // Renders the chain with these (normalised) values and gives the residuals;
    // false when it was cancelled or failed.
    using Evaluate = std::function<bool (const std::vector<double>& values, std::vector<double>& residuals)>;

    struct Result
    {
        std::vector<double> values;        // the best found; the start when nothing was better
        double startError = 0.0, error = 0.0;
        int evaluations = 0;
        bool stopped = false;              // cancelled or failed part way (values is still the best so far)
    };

    // `progress` gets 0..1 after each render.
    Result optimise (const std::vector<double>& start, const Evaluate&, const Settings&,
                     const std::function<void (double)>& progress = {});
}

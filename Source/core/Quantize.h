// SPDX-License-Identifier: AGPL-3.0-only
#pragma once

#include "AudioData.h"
#include "BeatGrid.h"

#include <string>
#include <utility>
#include <vector>

// Audio quantizing (the Quantize panel): finding hitpoints, choosing which of them
// count, grooves, and where each hit should land. The stretching itself is
// timestretch::render (TimeStretch.h). Plain C++.
namespace audioquantize
{
    // How hitpoints are found. Level looks for sharp rises in loudness (drums,
    // percussion, plucks); Spectral looks for new energy appearing anywhere in the
    // spectrum, so it also finds notes that start without getting louder (legato bass,
    // vocals, pads changing chord); Combined keeps the hits either one finds.
    enum class Method { level, spectral, combined };
    const char* methodName (Method);

    struct Detection
    {
        Method method = Method::level;
        double sensitivity = 0.5;   // 0 to 1: more finds quieter hits
        double minGapMs = 50.0;     // hits closer than this to the one before are dropped
    };
    std::vector<Frame> hits (const AudioData&, const Detection&);

    // Only the hits on or near the lines of a grid `division` beats apart (Cubase's
    // "Beats" filter): within `tolerance` of a step either side. Division 0 keeps them all.
    std::vector<Frame> onGrid (const std::vector<Frame>& hits, const BeatGrid&, double sampleRate, double division,
                               double tolerance = 0.25);

    // A timing feel: where each step of a repeating cycle lands, as a fraction of a
    // step early (-) or late (+). An MPC-style "58 %" swing on sixteenths is
    // { division 0.25, offsets { 0, 0.16 } }.
    struct Groove
    {
        std::string name;
        double division = 0.25;
        std::vector<double> offsets;
        bool isSet() const { return ! offsets.empty(); }
    };
    std::vector<Groove> builtInGrooves();
    // Swing as on drum machines: the second step of each pair at `percent` of the pair (50 straight, 66.7 triplet).
    Groove swing (double division, double percent);
    // The feel of a performance: for each step of a `steps`-long cycle starting at bar 1,
    // the median distance of the hits in [from, to) from that step's line. Steps no hit
    // came near stay on the grid. Unset when fewer than 4 hits were near a line.
    Groove extract (const std::vector<Frame>& hits, const BeatGrid&, double sampleRate, double division, int steps,
                    Frame from, Frame to);
    // "name|division|o1,o2,..." for settings files, and back (unset when unreadable).
    std::string toText (const Groove&);
    Groove fromText (const std::string&);

    struct Settings
    {
        double division = 0.25;     // beats per step: 1 quarters, 0.5 eighths, 0.25 sixteenths, 1/3 eighth triplets, 0.375 dotted sixteenths
        double strength = 1.0;      // 0 to 1: how far each hit moves towards its line
        double swing = 0.0;         // 0 to 1: every second step later, by up to a third of a step (1 = triplet feel)
        double leaveWithin = 0.0;   // "Non-Q": hits already this close to their line (fraction of a step) stay put
        double reach = 1.0;         // "Catch range" / max move: only hits this close to their line move (1 = all)
        double randomMs = 0.0;      // each moved hit lands up to this many ms either side of its target
        unsigned seed = 1;          // the same seed gives the same random offsets
        Groove groove;              // when set, its lines (division and offsets) replace the grid's and the swing
    };

    // A hit: where it is in the source audio, and where it is heard now (the same
    // unless it has been warped; quantizing again from there is Cubase's iterative quantize).
    struct Hit
    {
        Frame source = 0;
        Frame now = 0;
    };
    // Where the hits whose `now` is in [from, to) should land: (source, target) pairs in
    // order, targets rising, at least 1 ms apart. Hits that stay where they are are left out.
    std::vector<std::pair<Frame, Frame>> place (const std::vector<Hit>&, const BeatGrid&, double sampleRate, const Settings&,
                                                Frame from, Frame to);
    // The line a time (in beats from the downbeat) would be quantized to, in beats.
    double lineFor (double beat, const Settings&);
}

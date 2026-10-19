// SPDX-License-Identifier: AGPL-3.0-only
#pragma once

#include "AudioData.h"

#include <vector>

// Warp markers on a file (Warp menu). The audio shown and played is `source` with
// each marker's source frame moved to its target frame (core/TimeStretch.h), so
// every new move re-renders from the original and nothing is stretched twice.
// Plain C++.
struct WarpEdit
{
    struct Marker
    {
        Frame source = 0;
        Frame target = 0;
        bool operator== (const Marker&) const = default;
    };

    AudioDataPtr source;
    std::vector<Marker> markers;     // sorted by source; targets rise with them
    std::vector<Frame> transients;   // hits found in the source
    int mode = 0;                    // timestretch::Mode

    // Where a source frame lands, and where a result frame came from (straight lines between markers).
    Frame toTarget (Frame source) const;
    Frame toSource (Frame target) const;
};

// SPDX-License-Identifier: AGPL-3.0-only
#pragma once

#include "AudioDocument.h"

#include <string>

// iXML (production sound metadata in a WAV "iXML" chunk) to and from Tags.
// Every element is kept by its path ("SPEED/TIMECODE_RATE",
// "TRACK_LIST/TRACK[2]/NAME"), so fields PHI has no name for survive too.
// Plain C++.
namespace ixml
{
    bool read (const std::string& xml, Tags&, std::string& error);
    // The chunk's XML for the iXML values in tags; empty when there are none.
    std::string write (const Tags&);
}

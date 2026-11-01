// SPDX-License-Identifier: AGPL-3.0-only
#pragma once

#include "DawProject.h"

#include <string>

// Ableton Live Sets (.als, Live 9 to 12): gzipped XML that Ableton does not document. Read into
// the same project a DAWproject gives (DawProject.h), so the mixer opens both the same way:
// the tracks with their volume, pan, mute, solo and sends, the returns, the arrangement's audio
// and MIDI clips, the Session view's clips as the launcher's scenes, the tempo, and VST3
// plugins with their settings. A Drum Rack becomes the track's pads (Track::kit) for PHI's
// Slicer, each pad's Simpler with its sample, start and end, level, pan, transpose, choke group
// and note; a Simpler alone becomes the track's Sampler sound. Live's own effects, automation,
// warping, groups and the rest are named in Project::notes. Plain C++.
namespace ableton
{
    bool read (const std::string& path, dawproject::Project&, std::string& error);
    // The XML of a set whose folder is `setFolder` (where its samples are looked for).
    bool parse (const std::string& xml, const std::string& setFolder, dawproject::Project&, std::string& error);
}

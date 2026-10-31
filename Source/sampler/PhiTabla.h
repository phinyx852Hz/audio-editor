// SPDX-License-Identifier: AGPL-3.0-only
#pragma once

// PHI tabla (owner, 2026-10-31: "a set of tabla sounds that I plan to incorporate into PHI ... as a
// drum instrument of sorts ... connect the slicer and this particular set of tabla sounds (of my own
// playing) and the bol processor"; "I also want a playable drum pad ... 32 pads total, playable on
// the push"). The owner's own recorded strokes (share/phi-tabla) as a Slicer kit of 32 pads, banks A
// and B, so Push plays it: a pad a stroke and take, compounds layered (dha is tā with ge), the
// phrases whole, and the two Sa references. Softer hits are darker as well as quieter. The Bol
// Processor and Compose play it by bol: noteFor() gives each use of a bol the next take, so a run
// of the same bol does not repeat one sound. Plain C++; the sounds are loaded by whoever calls kit().
#include "SlicerEngine.h"

#include <functional>
#include <string>
#include <vector>

namespace phitabla
{
    struct Pad
    {
        int pad;                              // 0..31: banks A and B
        const char* name;                     // as the pad shows it: "dha", "tā 2", "phrase: dhātete (bharī)"
        const char* bol;                      // the bol it plays ("dha"), or "" for a phrase or a reference
        std::vector<const char*> files;       // in share/phi-tabla, without ".wav"; more than one sound together
    };
    const std::vector<Pad>& pads();

    // The bol a syllable is played as ("dhee" and "dhi" as dhi, "ne" as tā), or "" when it is no bol.
    std::string bolOf (const std::string& syllable);
    // The pad for the `use`-th time a bol is played (its takes in turn), or -1.
    int padFor (const std::string& syllable, int use);
    // The MIDI note that pad answers in the kit (Push's layout).
    int noteFor (const std::string& syllable, int use);

    // The kit, its sounds the files in `folder` (share/phi-tabla) as `load` reads them (a path to the
    // sound, or null); the files it could not load go in `missing`.
    slicer::Kit kit (const std::string& folder, const std::function<sampler::SamplePtr (const std::string&)>& load,
                     std::vector<std::string>* missing = nullptr);
}

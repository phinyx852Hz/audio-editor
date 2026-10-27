// SPDX-License-Identifier: AGPL-3.0-only
#pragma once

// The modal and tuning systems of the owner's research (Mode Research, 2026-10-26),
// added to the list in Tunings.cpp, and the atlas that places every system of that list under
// its region and its people or culture (owner, 2026-10-27: "organized in the Mode menu under
// melodic, then by geographic region, then by individual composer and/or national entity
// (including past empires); then another submenu, tuning, with the same system"). docs/modes-and-tunings.md
// has the sources, and what the research names that has no pitches to play yet.
#include "Tunings.h"

namespace mixer::atlas
{
    void add (std::vector<TuningSystem>&);     // the new systems, before the equal steps
    void place (std::vector<TuningSystem>&);   // every system's origins and kinds, at the end
}

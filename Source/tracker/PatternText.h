// SPDX-License-Identifier: GPL-3.0-or-later
// Adapted from MilkyTracker's tracker/PatternTools (Copyright 2009 Peter Barth), see PatternText.cpp.
#pragma once

#include <string>

struct TXMPattern;

// How one pattern cell reads in FastTracker 2 style, as MilkyTracker shows it:
// note, instrument, volume column, effect and operand.
class PatternText
{
public:
    struct Cell
    {
        std::string note, instrument, volume, effect, operand;   // UTF-8; "···" style dots when empty
        bool empty = true;
    };
    static Cell cell (const TXMPattern&, int row, int channel);

    static void convertEffectsToFT2 (int& eff, int& op);
    static int getVolumeFromEffect (int eff, int op);
    static void getVolumeName (char* name, unsigned int volume);
};

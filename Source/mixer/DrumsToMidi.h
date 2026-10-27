// SPDX-License-Identifier: AGPL-3.0-only
#pragma once

// Drums to MIDI (owner, 2026-10-26: "audio drum loops can become MIDI loops for a drum
// machine"): each hit of a drum loop as a note of the General MIDI kit on channel 10, a kick
// (36), a snare (38) or a closed hi-hat (42) by where the hit's energy lies, at the loudness it
// was played. Our own code. Plain C++, no JUCE or Qt.
#include "MixerEngine.h"
#include "Rhythms.h"

#include <vector>

namespace mixer
{
    namespace gm
    {
        constexpr int kick = 36, snare = 38, hat = 42;
    }

    struct DrumHit
    {
        Frame at = 0;   // in the clip as it plays, from its first frame
        int note = gm::kick;
        int velocity = 100;
    };

    struct DrumsSettings
    {
        double sensitivity = 0.5;   // 0 to 1: more finds quieter hits
        double minGapMs = 40.0;     // hits closer than this are one
    };

    // The clip's audio as it plays (its offset, length and reversal).
    std::vector<DrumHit> findDrums (const Clip&, double sampleRate, const DrumsSettings& = {});
    // The hits as a MIDI layer for the clip: as long as it, at `bpm`, each note a 1/16 long or
    // up to the next hit of its kind.
    std::shared_ptr<MidiClip> drumsToMidi (const Clip&, double sampleRate, double bpm, const DrumsSettings& = {});

    // A break (owner, 2026-10-27: "I can add the breaks as needed"): a drum loop cut to whole
    // bars, as a rhythm of the Mode menu's "my breaks". Its tempo from its length and hits
    // (beatgrid::fromLoop: 1 to 16 bars of 4/4), each hit of findDrums on its nearest 16th as a
    // kick, snare or hat, accented (X) from velocity 110, soft (o) under 70. False, with why,
    // when no such tempo fits the hits.
    bool breakRhythm (const AudioClipPtr&, double sampleRate, const std::string& name, rhythm::Rhythm&, std::string& error);
}

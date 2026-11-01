// SPDX-License-Identifier: AGPL-3.0-only
#pragma once

// PHI frame drum (owner, 2026-11-01: "a new instrument like the tabla drum Slicer instrument: the
// one based on my framedrum samples ... I'd prefer to keep them separate"; "add the velocity
// variations to each one-shot like you did the tabla kit"; "a Frame drum processor like the Tabla
// bol processor ... prioritize Iqa rhythms, and maybe include other rhythm generators"). The
// owner's own frame drum recordings (share/phi-frame-drum, August 2019, CC BY-SA 4.0) as a Slicer
// kit Push plays: the one-shots on the bottom half of Push's grid, a row a stroke (dum, tak, ka),
// and the 120 BPM loops and phrases on the top half. Softer hits are darker as well as quieter.
//
// The frame drum processor: PHI's rhythms (Mode > rhythm, the iqāʿāt first, and the rhythm
// generator) and the Bol Processor play on it by stroke. dum is the Bass, tak the Rimshot, ka (the
// lighter tak) the Side knock, pa the Flat hand; each use of a stroke plays its next take, so a run
// of taks does not repeat one sound. Plain C++; the sounds are loaded by whoever calls kit().
#include "SlicerEngine.h"
#include "../mixer/MidiFile.h"

#include <functional>
#include <string>
#include <vector>

namespace phiframedrum
{
    struct Sound
    {
        const char* file;                     // in share/phi-frame-drum, without ".wav"
        Frame start = 0, end = -1;            // the part played (in the file's frames); -1: to the end
        double gainDb = 0.0;
    };
    struct Pad
    {
        int pad;                              // 0..63: Push's grid, banks A to D
        const char* name;                     // as the pad shows it
        const char* stroke;                   // "dum", "tak", "ka", "pa", "snap", "sweep", or "" for a loop or phrase
        Sound sound;
    };
    const std::vector<Pad>& pads();

    // The pad at row (0 the bottom) and column (0 the left) of Push's 8x8 grid.
    int gridPad (int row, int column);

    // The stroke a syllable is played as ("doum" and "dm" as dum, "tek" as tak), or "" when it is none.
    std::string strokeOf (const std::string& syllable);
    // The pad for the `use`-th time a stroke is played (its takes in turn), or -1.
    int padFor (const std::string& syllable, int use);
    // The MIDI note that pad answers in the kit (Push's layout), or -1.
    int noteFor (const std::string& syllable, int use);

    // The stroke a General MIDI drum is played as: the low drums (kicks, low congas and toms) dum,
    // the high ones (snares, high congas, timbales) tak, and a soft tak (velocity under 70, the
    // rhythms' ghost notes and the iqāʿāt's ka) ka; the side stick, hats, woodblocks and bells ka,
    // the muted conga's slap pa, the clap a finger snap, shakers and cymbals a hand sweep.
    std::string strokeForDrum (int note, int velocity);
    // A song of General MIDI drums (a rhythm, the rhythm generator's) on PHI frame drum: each drum
    // note on channel 10 its stroke's next take, everything else as it was. `moved` counts the notes
    // it put on the frame drum.
    midifile::SongPtr fromGeneralMidi (const midifile::Song&, int* moved = nullptr);

    // The kit, its sounds the files in `folder` (share/phi-frame-drum) as `load` reads them (a path
    // to the sound, or null); the files it could not load go in `missing`.
    slicer::Kit kit (const std::string& folder, const std::function<sampler::SamplePtr (const std::string&)>& load,
                     std::vector<std::string>* missing = nullptr);
}

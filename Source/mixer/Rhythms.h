// SPDX-License-Identifier: AGPL-3.0-only
#pragma once

// Rhythmic modes (owner, 2026-10-26): the cycles and grooves of many traditions as drum
// patterns the mixer, the Tracker and a MIDI file can play. The North Indian tāls with their
// ṭhekās, the South Indian sūḷādi sapta tāḷas in all five jātis and the cāpu tāḷas, the classic
// breakbeats, trap, the six rhythmic modes of Notre-Dame, and timelines and grooves of the
// Caribbean, South America, West and North Africa and Korea. Every name is lower case
// (owner), transliterated: IAST for Sanskrit and Hindi, IJMES for Arabic, the Revised
// Romanization for Korean, each language's own spelling for the rest. docs/rhythms.md cites
// the sources. Our own code and transcriptions. Plain C++, no JUCE or Qt.
#include "MidiFile.h"

#include <string>
#include <vector>

namespace rhythm
{
    // One sound of a pattern: a General MIDI drum on channel 10, or a pitched note.
    // `hits` has a character a step, perBeat steps to the beat: X accented, x played, o soft
    // (a ghost note, a finger count), - the note before held on, . silent. Spaces and | are
    // only there to read it by and are skipped.
    struct Lane
    {
        std::string name;   // "bāyāṃ (dhā, ge)", "kick"
        int note = 36;
        int perBeat = 4;
        std::string hits;
        int channel = 9;    // 0-based: 9 is General MIDI's drum channel
    };

    struct Rhythm
    {
        std::string name;       // as shown, lower case: "tīntāl", "caturaśra jāti tripuṭa tāḷam (ādi tāḷam)"
        std::string plain;      // ASCII, for searching and saving: "tintal"
        std::string group;      // the Mode menu's submenu: "north indian tāl"
        std::string subgroup;   // a submenu inside it, or empty
        std::string ipa;        // how an English name is said, or empty
        int beats = 4;          // in one cycle: mātrās, akṣaras, beats of the bar or bars
        std::vector<int> sections;   // vibhāgs, aṅgas, halves of a clave... in beats, adding up to `beats`
        std::string counting;   // the cycle as it is spoken or counted: the ṭhekā's bols, kriyās, strokes
        std::string about;
        double bpm = 100.0;     // a usual tempo, in beats
        std::vector<Lane> lanes;
    };

    const std::vector<Rhythm>& rhythms();
    int findRhythm (const std::string& plain);   // its index, or -1
    std::vector<std::string> groups();           // in menu order

    struct Hit
    {
        int lane = 0;
        int step = 0;       // in the lane's own steps, from the start of the cycle
        int velocity = 96;
        int length = 1;     // steps, with the holds after it
    };
    int steps (const Lane&);                         // the lane's hits without the spacing
    std::vector<Hit> hits (const Rhythm&);           // every lane's, lane by lane
    int rowsPerBeat (const Rhythm&);                 // the steps every lane falls on: their perBeats' least common multiple
    bool valid (const Rhythm&);                      // every lane `beats` long, the sections adding up

    // `cycles` of the rhythm as a type 0 MIDI file at `bpm`, 480 ticks a beat.
    midifile::SongPtr toSong (const Rhythm&, int cycles, double bpm);

    // General MIDI's name for a drum note ("hand clap"), or empty.
    std::string drumName (int note);
}

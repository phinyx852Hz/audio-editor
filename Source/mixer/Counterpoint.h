// SPDX-License-Identifier: AGPL-3.0-only
#pragma once

// Species counterpoint (owner, 2026-10-25): new voices written against a melody, the cantus
// firmus, by the rules of Fux's Gradus ad Parnassum, in any of the mixer's modal systems.
// Our own code (no port of another generator). Plain C++, no JUCE or Qt.
//
// Each note of the melody is a bar. The species sets the new voice's rhythm in it: one note
// (first), two (second), four (third), a note tied over the bar line (fourth, suspensions), or
// a mix of all of them (fifth, florid). A beam search picks the pitches: the rules a voice must
// keep (consonance on the beat, no parallel fifths or octaves, dissonance only passing, as a
// neighbour or as a prepared suspension that falls by step, perfect consonances to begin and
// end, the last note reached by step) cost so much that they are only broken when nothing
// else fits, and the ones of taste (steps before leaps, a leap turned back by step, contrary
// motion, one high point) cost a little. A seed varies the choice among equally good lines.
//
// The modes: the keys a mode uses and the cents each plays (a maqam's E half-flat on E, a
// raga's just Ga, the steps of an equal temperament), so consonance is heard as the tuning
// plays it: a neutral third is a third. Ragas can give their āroha and avaroha, the notes
// used going up and coming down. Church modes raise the leading note at cadences (musica ficta).
#include "MidiFile.h"

#include <cstdint>
#include <string>
#include <vector>

namespace mixer { struct TuningSystem; }

namespace counterpoint
{
    enum class Species { first = 1, second, third, fourth, fifth };
    enum class Placement { above, below, around };   // around: above, then below, then above again

    struct Mode
    {
        int root = 0;                     // 0 (C) .. 11 (B): its keys count from 60 + root
        int period = 12;                  // keys to the octave: 12, an equal temperament's steps, or a long scale's degrees
        std::vector<double> keyCents;     // what each key of the period plays, in cents above the root
        std::vector<bool> scale;          // the keys of the period the mode uses
        std::vector<bool> up, down;       // a raga's āroha and avaroha (empty: the scale both ways)
        bool ficta = false;               // at a cadence, the note a whole step below where it lands may be raised

        static Mode chromatic (int root);   // every key, in 12-TET
        // A tuning system of mixer/Tunings.h on `root` (0 C .. 11 B), as its keys play it.
        static Mode fromTuning (const mixer::TuningSystem&, int root);
        bool valid() const;
    };

    struct Note
    {
        int64_t start = 0, end = 0;   // ticks
        int pitch = 60, velocity = 90;
    };
    using Line = std::vector<Note>;

    // The melody of a MIDI file as a cantus firmus: the highest note at each onset (drums left
    // out), each held until the next starts unless a rest of an eighth or more comes between;
    // a rest ends a phrase, which then cadences on its own.
    Line cantus (const midifile::Song&);
    // A mode the melody fits: the diatonic one on its last note that holds all its notes (with
    // ficta), else the notes it uses.
    Mode guessMode (const Line& cantus);

    struct Settings
    {
        Species species = Species::first;
        int voices = 1;                    // 1..3 new voices
        Placement placement = Placement::above;
        // Fux: in three and four parts one voice takes the species and the others go note
        // against note. True: every new voice in the species.
        bool allInSpecies = false;
        uint32_t seed = 0;
        Mode mode;                         // invalid (empty): guessMode
    };

    struct Result
    {
        std::vector<Line> voices;          // nearest the melody first: above, below, then further out
        std::vector<std::string> names;    // "above", "below", "high above"...
        int bent = 0;                      // the strict rules broken where nothing else fitted
    };

    Result generate (const Line& cantus, const Settings&);

    // A free part, as a fugue's voices are written (mixer/Fugue.h): one voice against lines
    // already fixed, in `species`, a bar of it to each note of `grid`. A grid note given a
    // pitch in `pinned` (-1: free) is that note as it is, held, and is left out of the line,
    // so the part leads out of and into the voice's own material. Each run of grid notes
    // without a gap is a phrase; only the last, with `cadence`, ends by the rules of an ending.
    // The first of `against` is heard as the melody (a grid note's pitch is what it sounds).
    struct FreePart
    {
        Line grid;
        std::vector<int> pinned;           // one for each grid note, or empty
        std::vector<Line> against;
        std::vector<int> ranks;            // one for each of `against`: higher is above, as `rank`
        int rank = 0;
        Species species = Species::fifth;
        int centre = 60, range = 9;        // the keys the part keeps to: centre - range .. centre + range
        int parts = 2;                     // the voices in all, this one too
        bool lowest = false;               // the bass: a fourth above it is a dissonance
        bool cadence = false;
    };
    Line freeVoice (const FreePart&, const Mode&, uint32_t seed, int& bent);

    // A voice as a type 0 MIDI file with `like`'s timing and tempo map, on `channel`.
    midifile::SongPtr toSong (const midifile::Song& like, const Line&, int channel);
}

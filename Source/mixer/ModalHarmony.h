// SPDX-License-Identifier: AGPL-3.0-only
#pragma once

// Harmonize in the mode (owner, 2026-10-29): an accompaniment, and a line if wanted, made from
// the Mode menu's mode in its own tuning, for every melodic tradition PHI has. Modal harmony's
// rules (docs/harmony.md, "modal harmony"): the final held by its chord or a pedal, no V–I,
// chords moving by step, the chords chosen carrying the mode's characteristic note and no
// tritone. Not every tradition harmonizes with chords, and not every mode sits in 12-TET, so:
// chords are judged by the cents the PHI synth really plays (keyCents), each interval against
// the nearest just consonance, and each tradition starts from its own texture: chords for the
// church, jazz and composers' modes, a drone for rāgas and echoi, heterophony for maqāmāt,
// dastgāhs and the East Asian modes, layering for gamelan, dyads for mbira, clusters for gagaku.
// When the chords change is the harmonic rhythm: steady, or a Mode ▸ rhythm cycle's sections
// (a tāl's vibhāgs, a clave's halves), struck on its strokes if wanted. Plain C++, no JUCE or Qt.
#include "Harmonies.h"
#include "Rhythms.h"
#include "Tunings.h"

#include <string>
#include <utility>
#include <vector>

namespace modalharmony
{
    enum class Texture { chords, quartal, clusters, drone, parallel, heterophony, layered, dyads };
    std::vector<Texture> textures();          // in the dialog's order
    std::string textureName (Texture);        // "chords (thirds)", "drone", ...
    bool usesChords (Texture);
    // What the mode's tradition does, by its family: chords for the church modes, a drone for a rāga...
    Texture traditional (const mixer::TuningSystem&);

    // A degree of the mode as the synth plays it: the key it is on (0..11 above the final) and its cents.
    struct Degree
    {
        int key = 0;
        double cents = 0.0;
    };
    std::vector<Degree> degrees (const mixer::TuningSystem&);
    // The keys that make the mode itself and not major or natural minor, for a seven-note mode
    // (dorian's major sixth, lydian's raised fourth); else none.
    std::vector<int> characteristic (const std::vector<Degree>&);
    // How far `cents` is from the nearest just consonance (unison, 6/5, 5/4, 4/3, 3/2, 8/5, 5/3,
    // octave), folded into the octave: 0 is pure, a neutral third about 35, a major second about 115.
    double roughness (double cents);

    // A chord: degree indices from the bottom, counting on past the octave (n is the final an
    // octave up), and how rough it is (the worst of its intervals).
    struct Chord
    {
        std::vector<int> steps;
        double roughness = 0.0;
        bool characteristic = false;   // carries the mode's characteristic note
    };
    // The chords for `changes` changes of one cycle, the first on the final. Empty when the mode
    // has no chord of `texture` within the roughness allowed (a drone serves instead).
    // `notes` a chord: 3, or 4 for the jazz modes' sevenths.
    std::vector<Chord> progression (const std::vector<Degree>&, Texture, int changes, int notes = 3);
    std::string describe (const std::vector<Degree>&, const std::vector<Chord>&);   // "1 (0·386·702 ¢) → 7 (0·316·702 ¢)"

    // The harmonic rhythm of one cycle: its length, where the chord changes and where it is struck
    // (beats from the cycle's start, with a velocity).
    struct Timing
    {
        double beats = 16.0;
        std::vector<double> changes;
        std::vector<std::pair<double, int>> strikes;
    };
    Timing steady (double beatsPerChord, int chords);
    // A rhythm's cycle: a change at each of its sections, held, or struck on its first sound's strokes.
    Timing fromRhythm (const rhythm::Rhythm&, bool onStrokes);

    struct Settings
    {
        Texture texture = Texture::chords;
        bool pedal = true;     // the final held under it
        bool line = true;      // a line through the mode over it
        int cycles = 4;
        unsigned variation = 1;
    };
    // Every cycle written out, its voices in semitones above the final (harmony::toSong plays it).
    harmony::Harmony generate (const mixer::TuningSystem&, const Timing&, const Settings&);
}

// SPDX-License-Identifier: AGPL-3.0-only
#pragma once

// The modal tunings a clip, a scene or a track can play in (owner, 2026-10-25): the major
// Hindustani ragas in just intonation, the church modes and their hypo-modes in Pythagorean
// tuning, jazz modes, Arabic maqams and Persian dastgahs with their quarter steps, and 2 to 21
// equal steps to the octave. Names are transliterated (IAST for the ragas, IJMES for Arabic,
// a common Persian romanisation), with a plain spelling beside each for searching and saving.
// The PHI synth plays them (mixer::Instrument::setTuning); other instruments stay in 12-TET.
#include "../core/PhiTuning.h"

#include <array>
#include <string>
#include <vector>

namespace mixer
{
    struct TuningSystem
    {
        std::string name;     // as shown: "Darbārī Kānaḍā", "Ḥijāz", "Rāst-Panjgāh"
        std::string plain;    // ASCII, also the key a mix saves: "Darbari Kanada"
        std::string family;   // "Raga", "Church mode", "Hypo-mode", "Jazz mode", "Maqam", "Dastgah", "Equal"
        std::vector<double> cents;   // the degrees above the tonic, from 0, within the period
        int divisions = 0;           // an equal temperament: this many steps to the octave (else 0)
    };

    const std::vector<TuningSystem>& tuningSystems();
    int findTuning (const std::string& plain);   // its index, or -1

    // What each of the twelve keys above the root plays, in cents above the root. A key the
    // scale has a degree on (the degree rounded to the nearest key, a quarter step up to the
    // key above, as E half-flat is spelled on E) plays that degree; the others stay where 12-TET
    // has them.
    std::array<double, 12> keyCents (const TuningSystem&);

    // The tuning for an instrument: `root` 0 (C) to 11 (B) is the tonic, Sa or the final, at its
    // 12-TET pitch with A4 at 440 Hz. An equal temperament walks its steps up and down the keys
    // from the root (middle C's octave).
    phi::Tuning makeTuning (const TuningSystem&, int root);

    // A clip's, scene's or track's choice: system -1 is none (the next level decides).
    struct TuningChoice
    {
        int system = -1;
        int root = 0;
        bool operator== (const TuningChoice&) const = default;
    };
    // The clip's, else the scene's, else the track's.
    TuningChoice tuningInForce (const TuningChoice& clip, const TuningChoice& scene, const TuningChoice& track);
}

// SPDX-License-Identifier: AGPL-3.0-only
#pragma once

// The modal tunings a clip, a scene or a track can play in (owner, 2026-10-25): the major
// Hindustani ragas in just intonation (and each in 12-TET), the church modes and their hypo-modes in Pythagorean
// tuning, jazz modes, Arabic maqams and Persian dastgahs with their quarter steps, and 2 to 21
// equal steps to the octave. Names are transliterated (IAST for the ragas, IJMES for Arabic,
// a common Persian romanisation), with a plain spelling beside each for searching and saving.
// The PHI synth plays them (mixer::Instrument::setTuning); other instruments stay in 12-TET.
#include "../core/PhiTuning.h"

#include <array>
#include <cstdint>
#include <string>
#include <vector>

namespace mixer
{
    struct TuningSystem
    {
        std::string name;     // as shown, all lower case (owner): "darbārī kānaḍā", "ḥijāz", "rāst-panjgāh"
        std::string plain;    // ASCII, also the key a mix saves: "darbari kanada"
        std::string family;   // "raga", "raga, equal tempered", "church mode", "hypo-mode", "jazz mode", "maqam", "dastgah", "equal"
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
    // The keys it has a degree on, as keyCents rounds them (bit n: n semitones above the root).
    uint16_t modeKeys (const TuningSystem&);

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

    // Hypo (owner, 2026-10-26): a mode's range moved off its final, the final kept, as the
    // Gregorian plagal modes do: from the fifth (the octave from a fourth below the final) or
    // from the fourth (the octave from a fifth below it). For the traditions built on a tonic
    // and its fifth or fourth (ragas' Sa with Pa or Ma, a maqam's tonic and ghammāz, a dastgah's
    // shāhed) that have no plagal forms of their own: the church modes have their hypo-modes,
    // and jazz modes and equal steps no such hierarchy.
    enum class Hypo { none, fifth, fourth };
    bool hypoFamily (const std::string& family);   // a TuningSystem's, or a midifile::Scale's
    // The mode has that degree: a fifth or fourth within 30 cents of a pure one.
    bool hasHypo (const std::vector<double>& cents, Hypo);
    bool hasHypo (uint16_t notes, Hypo);           // a midifile::Scale's notes
    bool hasHypo (const TuningSystem&, Hypo);      // its family, and the degree
    int hypoBelow (Hypo);   // keys from the bottom of the range up to the final: 5, 7 (none 0)
}

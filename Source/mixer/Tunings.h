// SPDX-License-Identifier: AGPL-3.0-only
#pragma once

// The modal tunings a clip, a scene or a track can play in (owner, 2026-10-25): the major
// Hindustani ragas in just intonation (and each in 12-TET), the church modes and their hypo-modes in Pythagorean
// tuning, jazz modes, Arabic maqams and Persian dastgahs with their quarter steps, Javanese,
// Balinese and Sundanese gamelan laras and their pathet in measured cents, the Shona mbira
// tunings of Zimbabwe in their measured cents, Chinese lü and modes from the treatises (and in
// 12-TET), Japanese gagaku and folk modes, and Korean court modes and folk tori (2026-10-26), and
// 2 to 21 equal steps to the octave. Names are transliterated (IAST for the ragas, IJMES for
// Arabic, Encyclopædia Iranica's for Persian, Indonesian spelling for gamelan, pinyin with its tones,
// Hepburn, McCune-Reischauer), with a plain spelling beside each for searching and saving.
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
        std::string family;   // "raga", "raga, equal tempered", "church mode", "hypo-mode", "jazz mode", "maqam", "dastgah",
                              // "javanese gamelan", "balinese gamelan", "sundanese gamelan", "mbira dzavadzimu",
                              // "nyunga nyunga", "matepe", "chinese tuning", "chinese mode", "chinese mode, equal tempered",
                              // "chinese regional mode", "gagaku mode", "japanese mode", "korean tuning", "korean mode",
                              // "korean folk mode", "equal"
        std::vector<double> cents;   // the degrees above the tonic, from 0, within the period
        int divisions = 0;           // an equal temperament: this many steps to the octave (else 0)
        // The tradition's name for each degree, when the family's own names (NoteEditor.cpp) can't
        // be worked out from the cents: the kepatihan number of each gamelan tone. Else empty.
        std::vector<std::string> names;

        // Where it is from, for the Mode menu's atlas (owner, 2026-10-27; Atlas.cpp):
        // the people, cultures, nations or empires it belongs to (origins()' names), whether it
        // is a mode or scale (melodic), a tuning, or both, and what it repeats at. Set after the
        // entry is made, never in its braces, so the list can take new fields.
        std::vector<std::string> from;
        unsigned kinds = 0;          // melodic | tuning; 0 until the atlas places it
        double period = 1200.0;      // cents: an octave, else a fourth (Hajibeyov's chains), a tritave (Bohlen-Pierce), a step (Carlos)
        std::string note;            // a line for the menu's tooltip: the source, or what is approximate
    };
    enum TuningKind : unsigned { melodic = 1, tuning = 2 };

    const std::vector<TuningSystem>& tuningSystems();
    int findTuning (const std::string& plain);   // its index, or -1

    // The atlas: the regions, in the menu's order, and the people and cultures in them. A
    // person's `year` is their birth (or floruit) and a culture's the start of its tradition,
    // for the order within a region; `dates` is what the menu shows beside the name.
    const std::vector<std::string>& regions();   // "europe", "west asia", ... "the americas", "oceania"
    struct Origin
    {
        std::string name;     // lower case, transliterated: "ṣafī al-dīn al-urmawī", "ottoman empire"
        std::string region;
        int year = 0;
        std::string dates;    // "1517–1590", "6th c. bce", or empty
        bool person = false;
        std::string shown;    // as the menu shows it, a proper noun capitalized (owner, 2026-10-27): "Ṣafī al-Dīn al-Urmawī", "Ottoman Empire (makam)"
    };
    // A region as the menu shows it: "West Asia", "the Americas".
    std::string regionTitle (const std::string& region);
    const std::vector<Origin>& origins();
    const Origin* findOrigin (const std::string& name);
    // The indices of tuningSystems() placed under `origin` that are of `kind`, in the list's order.
    std::vector<int> systemsFrom (const std::string& origin, unsigned kind);

    // The degrees within one octave, from 0: the cents themselves, or for a scale that repeats at
    // another period, its degrees walked up from the tonic until the octave.
    std::vector<double> octaveCents (const TuningSystem&);

    // What each of the twelve keys above the root plays, in cents above the root. A key the
    // scale has a degree on (the degree rounded to the nearest key, a quarter step up to the
    // key above, as E half-flat is spelled on E) plays that degree; the others stay where 12-TET
    // has them.
    std::array<double, 12> keyCents (const TuningSystem&);
    // The keys it has a degree on, as keyCents rounds them (bit n: n semitones above the root).
    uint16_t modeKeys (const TuningSystem&);

    // The tuning for an instrument: `root` 0 (C) to 11 (B) is the tonic, Sa or the final, at its
    // 12-TET pitch with A4 at 440 Hz. An equal temperament walks its steps up and down the keys
    // from the root (middle C's octave), and so does a scale of more than twelve degrees (Jing
    // Fang's 60 lü), a degree a key.
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

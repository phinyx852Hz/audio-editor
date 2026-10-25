// SPDX-License-Identifier: AGPL-3.0-only
#pragma once

// PHI rāga (owner, 2026-10-25): a Hindustani raga instrument. Each raga is a sheet the owner and
// their sitar teacher write: its thāṭ, svaras and their tuning, āroha and avaroha, vādī and
// samvādī, pakaḍ, chalan, the svaras sung with āndolan, its time and rasa. The sheets are plain
// text so they can be written and corrected without code:
//
//     raga: darbārī kānaḍā
//     plain: darbari kanada
//     thaat: āsāvarī
//     svaras: S R g=32/27 m P d=128/81 n
//     aroha: n, S R g m P d n S'
//     avaroha: S' d n P m P g m R S
//     vadi: R
//     samvadi: P
//     pakad: g m R S | d n P
//     andolan: g 20 0.5, d 20 0.5
//     time: late night
//
// Svaras are S r R g G m M P d D n N (lower case komal, M tīvra ma); "," after one is the lower
// octave (mandra), "'" the upper (tār); a dot below or above (ṇ, Ṡ) is read the same way. A
// svara's tuning is a ratio (g=32/27) or cents (g=294c) above Sa; one without is in five-limit
// just intonation. Andolan is the svara, the swing's depth in cents and its rate in Hz.
// Plain C++, in phi_dsp with the tunings, for the PHI synth.
#include <array>
#include <string>
#include <vector>

namespace phi
{
    struct Svara
    {
        int semitone = 0;   // above Sa, 0 to 11
        int octave = 0;     // -1 mandra, 0 madhya, 1 tār
        int key() const { return semitone + 12 * octave; }   // semitones above the middle Sa
        bool operator== (const Svara&) const = default;
    };
    using Phrase = std::vector<Svara>;

    struct Andolan
    {
        int semitone = 0;
        double depthCents = 20.0;   // the swing below the svara
        double rateHz = 0.5;
    };

    struct Raga
    {
        std::string name;    // IAST, lower case (owner): "darbārī kānaḍā"
        std::string plain;   // ASCII, the key a mix saves: "darbari kanada"
        std::string thaat;
        std::array<bool, 12> uses {};      // the svaras the raga has
        std::array<double, 12> cents {};   // each one's tuning above Sa (those it has)
        Phrase aroha, avaroha;
        int vadi = -1, samvadi = -1;       // semitones, -1 not given
        std::vector<Phrase> pakad, chalan;
        std::vector<Andolan> andolan;
        std::string time, rasa;

        std::vector<double> scaleCents() const;   // the svaras it uses, from Sa up
        const Andolan* andolanOn (int semitone) const;
    };

    // The sheets in `text`, in order. A sheet with a mistake (a svara the raga doesn't have in its
    // āroha, a field it doesn't know) is reported in `errors` as "line N: why", and left out.
    std::vector<Raga> parseRagas (const std::string& text, std::vector<std::string>& errors);
    std::string writeRagas (const std::vector<Raga>&);   // the sheets again, as parseRagas reads them

    // The sheets PHI comes with (PhiRagas.cpp).
    const std::vector<Raga>& builtInRagas();
    const std::string& builtInRagaSheets();

    // "n, S R g" -> svaras; false (and `bad` the token) on anything else.
    bool parsePhrase (const std::string& text, Phrase& out, std::string* bad = nullptr);
    std::string phraseText (const Phrase&);
    double justCents (int semitone);   // the five-limit just svara: 9/8 for R, 3/2 for P
}

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
        // The svaras (semitones above Sa) used going up (in its āroha) and coming down (avaroha);
        // all it uses when the sheet gives no āroha or avaroha.
        std::array<bool, 12> upSet() const;
        std::array<bool, 12> downSet() const;
        const Andolan* andolanOn (int semitone) const;
    };

    // The sheets in `text`, in order. A sheet with a mistake (a svara the raga doesn't have in its
    // āroha, a field it doesn't know) is reported in `errors` as "line N: why", and left out.
    std::vector<Raga> parseRagas (const std::string& text, std::vector<std::string>& errors);
    std::string writeRagas (const std::vector<Raga>&);   // the sheets again, as parseRagas reads them

    // Plays keys as the raga: a key goes to the svara the raga uses in the direction the melody is
    // moving (its āroha going up, its avaroha coming down), the nearest one when the key is not
    // among them, ties going the way the melody moves; so khamāj plays N going up and n coming
    // down from the same key. Each svara is tuned as the sheet says, and one sung with āndolan
    // swings below its pitch. Not thread-safe: one player per voice allocator.
    class RagaPlayer
    {
    public:
        struct Voice
        {
            int inKey = -1;          // the key played
            int outKey = -1;         // the key of the svara it became
            Svara svara;             // that svara, its octave counted from the Sa key
            double hz = 0.0;         // its pitch, before ornaments
            double swingCents = 0.0, swingHz = 0.0;   // āndolan, 0 when none
        };

        // `raga` must outlive the player (or be set again); null plays keys as they are, in 12-TET.
        void setRaga (const Raga* raga, int saKey = 60, double saHz = 261.6255653005986);
        void setOrnaments (double amount) { ornaments = amount; }   // 0 none to 1 as the sheet says
        Voice noteOn (int key);
        void reset() { lastKey = -1; rising = true; }
        // The voice's pitch `seconds` after it started, ornaments and all.
        double hzAt (const Voice&, double seconds) const;
        bool goingUp() const { return rising; }
        int voiceSaKey() const { return saKey; }

    private:
        const Raga* raga = nullptr;
        int saKey = 60;
        double saHz = 261.6255653005986;
        double ornaments = 1.0;
        std::array<bool, 12> up {}, down {};
        int lastKey = -1;
        bool rising = true;
    };

    // The sheets PHI comes with (PhiRagas.cpp).
    const std::vector<Raga>& builtInRagas();
    const std::string& builtInRagaSheets();

    // "n, S R g" -> svaras; false (and `bad` the token) on anything else.
    bool parsePhrase (const std::string& text, Phrase& out, std::string* bad = nullptr);
    std::string phraseText (const Phrase&);
    double justCents (int semitone);   // the five-limit just svara: 9/8 for R, 3/2 for P
}

// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2023 euwbah (Xen Tuner); the C++ port (C) 2026 phinyx852Hz (ΦNYX) and contributors.
#pragma once

// Xen Tuner's tuning configs in PHI (owner, 2026-10-29: "Xen Tuner is the one to use"): the
// notation half of Xen Tuner (github.com/euwbah/musescore-xen-tuner, GPL-3.0-or-later), ported
// from its `Xen Tuner/fns.js` (parseTuningConfig, parseSymbolsDeclaration, parseCentsOrRatio and
// the tables it builds) to plain C++, with no MuseScore. A config names a reference note and its
// frequency, the nominals (the letters on the staff, as many as the system has, each a staff step)
// and the equave they repeat at, and chains of accidentals (sharps and flats, comma arrows,
// Sagittal's symbols) with what each degree of a chain adds:
//
//     A4: 440
//     0c 200c 300c 500c 700c 800c 1000c 1200c
//     bb b (100c) # x
//
// Xen Tuner reads a staff's notes and works out their pitches; PHI goes the other way: a pitch, as
// the MIDI editor has it in cents, is spelled as the nominal and accidentals of the config that
// come nearest (the Speller), with the enharmonic spellings beside it, and what is left over in
// cents. Ligatures (HEJI's combined arrows), secondary accidentals (sec(): HEJI's higher primes)
// and override() are read; aux(), displaycents(), independent() and the like are MuseScore input
// and display settings, read past. Plain C++ in phi_dsp, no Qt.
#include <optional>
#include <string>
#include <vector>

namespace phi::xen
{
    // An accidental symbol: one of Xen Tuner's symbol codes (a SMuFL glyph), or quoted text.
    struct Symbol
    {
        int code = 0;          // 1.. when a SMuFL symbol, else 0
        std::string text;      // the text of a text accidental ('+', 'hi')
        bool operator== (const Symbol&) const = default;
    };
    int symbolCount();                         // Xen Tuner's codes run 1 .. symbolCount() - 1
    char32_t codePoint (int code);             // the SMuFL code point, 0 when none
    std::string smuflName (int code);          // "accidentalSharp"
    std::string textCode (int code);           // "#", "\\!)"; empty when it has none
    int symbolCode (const std::string& codeOrText);   // a text code or a number, -1 when neither
    // The accidentals as text, for a font without SMuFL: text codes, else the SMuFL name.
    std::string asText (const std::vector<Symbol>&);

    // An accidental chain: the symbols of each degree (empty at the central one, degree 0) and what
    // each degree adds in cents.
    struct Chain
    {
        std::vector<std::vector<Symbol>> symbols;
        std::vector<double> cents;
        int central = 0;
        int lowest() const { return -central; }
        int highest() const { return (int) cents.size() - 1 - central; }
    };

    struct Ligature
    {
        std::vector<int> chains;          // the chains it combines, from 0
        bool weak = false, important = false;
        struct Form { std::vector<int> degrees; std::vector<Symbol> symbols; };
        std::vector<Form> forms;
    };

    // A secondary accidental (sec()): symbols outside the chains that still move a note.
    struct Secondary
    {
        std::vector<Symbol> symbols;
        std::vector<double> cents;        // one for every nominal, or one for each
        double centsOn (int nominal) const;
    };

    struct Config
    {
        std::string name;                 // the file's, or as given
        char letter = 'a';                // the reference note: its letter (a..g) and octave
        int octave = 4;
        double hz = 440.0;
        std::vector<std::optional<double>> nominals;   // cents above the reference; none: not used
        double equave = 1200.0;
        std::vector<Chain> chains;
        std::vector<Ligature> ligatures;
        std::vector<Secondary> secondary;
        struct Override { int nominal; std::vector<int> degrees; double cents; };
        std::vector<Override> overrides;
        int displaySteps = 0;             // displaysteps(n, ...): the steps it counts an equave in

        // The reference note on the staff, a step a line or space: C0 is 0, middle C 28.
        int referenceStaff() const;
        // Its pitch in the MIDI editor's cents (above key 0 in 12-TET; A4 at 440 Hz is 6900).
        double referencePitch() const;
    };

    // Fills `out` (keeping its name) and says false, with the reason, when the text is not a config.
    bool parse (const std::string& text, Config& out, std::string& error);
    bool read (const std::string& path, Config& out, std::string& error);

    // A pitch spelled.
    struct Spelling
    {
        int staff = 0;                    // as referenceStaff(): C0 0, middle C 28
        int nominal = 0;                  // of the config's nominals
        int equave = 0;                   // equaves above the reference
        std::vector<int> degrees;         // on each chain
        int secondary = -1;               // the secondary accidental added, -1 none
        std::vector<Symbol> symbols;      // left to right, as written before the note
        double pitch = 0.0;               // what it notates, in cents (as Note::pitch)
        double off = 0.0;                 // the pitch spelled minus that
    };

    class Speller
    {
    public:
        Speller() = default;
        explicit Speller (Config);
        const Config& config() const { return conf; }
        bool valid() const { return ! entries.empty(); }
        bool tooLarge() const { return large; }   // more spellings than it keeps (fewer are kept)

        // The spellings of a pitch, the best first: the nearest, and of those equally near the
        // simplest (fewest accidentals, sharps before flats unless `flats`). The others are its
        // enharmonic spellings. Secondary accidentals come in when no chain spelling is within
        // half a cent.
        std::vector<Spelling> spell (double pitch, bool flats = false) const;
        Spelling best (double pitch, bool flats = false) const;

        // The config moved as a whole so that `pitch` (a system's tonic) is notated exactly, as
        // Xen Tuner's reference changes do: a raga's Sa in HEJI, or C in 31-EDO, then sits on its
        // letter with nothing over.
        Speller anchoredAt (double pitch) const;

        // Up and down as Xen Tuner's arrow keys: the next pitch the config notates above (or below).
        double next (double pitch, int direction) const;
        int stepsPerEquave() const { return (int) steps.size(); }

    private:
        struct Entry
        {
            int nominal = 0;
            std::vector<int> degrees;
            double cents = 0.0;           // above the reference, unwrapped
            std::vector<Symbol> symbols;
            double weight = 0.0;          // what its accidentals cost, in cents: lower first
            int direction = 0;            // the sign of its first chain's degree
        };
        Spelling make (const Entry&, double pitch, int secondary) const;

        Config conf;
        std::vector<Entry> entries;
        std::vector<double> steps;        // the distinct pitches in an equave, from 0, sorted
        bool large = false;
    };
}

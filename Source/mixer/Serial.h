// SPDX-License-Identifier: AGPL-3.0-only
#pragma once

// Twelve-tone serial music (owner, 2026-10-27): Arnold Schoenberg's method of composing with
// twelve tones related only to one another. Our own code (no port of another program); the
// historical rows are the ones music21's serial module lists (BSD licence, see Serial.cpp).
// Plain C++, no JUCE or Qt.
//
// - The row: the twelve pitch classes in an order, 0 (c) to 11 (b).
// - Its forms, labelled as the matrix labels them: p (prime), i (inversion), r (retrograde)
//   and ri (retrograde inversion), each at a level 0..11, semitones up from the row as written
//   (p0 is the row itself, i0 its inversion on the same first note, r0 and ri0 those backwards).
// - The matrix: the 12 x 12 square whose rows read left to right are the p forms, right to left
//   the r forms, its columns down the i forms and up the ri forms.
// - What a row is like: its intervals, whether it is an all-interval row, and the forms whose
//   first hexachord holds the six notes the row's own leaves out (hexachordal
//   combinatoriality, as Schoenberg used it from op. 25 on: two such forms together make
//   aggregates, all twelve notes, each hexachord against its complement).
// - A piece: one to four voices, each a chain of row forms (a plan the user writes, or a walk
//   through the matrix that favours forms joined by a pivot note or a combinatorial pair), the
//   voices independent, in canon, in mirror canon or combinatorial pairs; the row a line or cut
//   into chords; durations even, serial (each pitch class the length of its place in p0) or
//   free; registers close, wide (pointillist) or fixed (a frozen register, each pitch class in
//   one octave). The voices are whole statements of the row only, as many as fit in the bars
//   asked for (one at least), and the piece is whole bars of 4/4, so it loops.
// - Milton Babbitt's methods: rows derived from a cell, his six all-combinatorial source sets,
//   time-point rhythm and arrays (each below).
// Everything comes from one seed: the same settings and seed write the same piece.
#include <array>
#include <cstdint>
#include <memory>
#include <string>
#include <vector>

namespace midifile { struct Song; }

namespace serial
{
    using Row = std::array<int, 12>;

    const char* pitchName (int pc);   // lower case: "c", "c♯", "e♭"...

    // True if `pcs` is twelve different pitch classes, 0..11.
    bool isRow (const std::vector<int>&);
    // A row written as numbers ("0 11 7 8 3 1 2 10 6 5 4 9", t and e or a and b for 10 and 11)
    // or note names ("e f g c♯ f♯ e♭ a♭ d b c a b♭"; # and b for sharp and flat), separated by
    // spaces or commas. False, with `error` set, when it is not twelve different notes.
    bool parse (const std::string&, Row&, std::string& error);
    // The same for any number of different notes (a cell).
    bool parseNotes (const std::string&, std::vector<int>& pcs, std::string& error);
    std::string text (const Row&, bool names);   // numbers: "4 5 7 1 6 3 8 2 11 0 9 10"

    enum class Form { p, i, r, ri };
    struct Statement
    {
        Form form = Form::p;
        int level = 0;   // 0..11
        bool operator== (const Statement&) const = default;
    };
    Row transform (const Row& prime, Statement);
    std::string label (Statement);   // "p0", "i5", "r11", "ri3"
    // "p0 ri3 i5, r0": false, with `error` set, when a label does not read.
    bool parseStatements (const std::string&, std::vector<Statement>&, std::string& error);
    std::string text (const std::vector<Statement>&);

    // The matrix: [i][j] is the j-th note of row i, the p form beginning on the i-th note of i0.
    std::array<Row, 12> matrix (const Row&);
    int primeLevelOfRow (const Row&, int i);       // row i of the matrix is p<this>
    int inversionLevelOfColumn (const Row&, int j);   // column j is i<this>

    std::array<int, 11> intervals (const Row&);   // each step up, mod 12: 1..11
    bool allInterval (const Row&);                // each of the eleven intervals once
    // The forms (other than p0 itself) whose first hexachord is the complement of p0's, inversions first.
    std::vector<Statement> combinatorial (const Row&);
    // The same for any statement, the first of them (r at the same level is always one).
    Statement partner (const Row&, Statement);
    std::string describe (const Row&);   // "all-interval; combinatorial with i5, ri5 (inversional)"

    Row randomRow (uint32_t seed);
    Row randomAllInterval (uint32_t seed);
    // A row from a melody: its first twelve different pitch classes, in order, the ones it never
    // plays after them from the lowest. `missing`: how many had to be added.
    Row rowOf (const std::vector<int>& pitches, int& missing);

    // Milton Babbitt ("Set Structure as a Compositional Determinant", 1961). A row derived from
    // a cell of three or four notes: the cell, in its order, and transpositions and inversions of
    // it that fill out the twelve notes (the cell's forms, a seed's choice among those that do).
    // False, with `error` set, when no forms of it fill the aggregate.
    bool derive (const std::vector<int>& cell, uint32_t seed, Row&, std::string& error);
    // His six all-combinatorial source sets, the hexachords whose rows are combinatorial in all
    // four forms: a (chromatic), b, c (diatonic), d, e (hexatonic) and f (whole tone).
    struct SourceSet
    {
        const char* name;   // "source set a (chromatic)"
        std::array<int, 6> pcs;
    };
    const std::array<SourceSet, 6>& sourceSets();
    // A row on a source set by chance: its hexachord (transposed) in some order, then the other six.
    Row fromSourceSet (int set, uint32_t seed);
    int sourceSetOf (const Row&);   // the source set the row's first hexachord is, 0..5, else -1

    struct Historical
    {
        const char* name;   // lower case: "schoenberg: suite for piano, op. 25"
        Row row;
    };
    const std::vector<Historical>& historical();

    // A path through the matrix, `count` statements from p0: each next form joined to the last
    // by a pivot (its first note the last one's last) or as its combinatorial partner when it
    // can be, never the same form twice running.
    std::vector<Statement> walk (const Row&, int count, uint32_t seed);

    //==========================================================================
    // array: Babbitt's arrays, each form of the plan shared out among the voices a segment each
    // (12 / voices notes), the segments turning from voice to voice block by block, so every
    // block of the piece is an aggregate, all twelve notes across the voices.
    enum class Voices { independent, canon, mirror, combinatorial, array };
    // timePoint: Babbitt's time-point system ("Twelve-Tone Rhythmic Structure and the Electronic
    // Medium", 1962): a bar of twelve units is the modulus, and each note starts on the time point
    // its pitch class names (counted from the row's first note), the next one after the last.
    enum class Rhythm { even, serial, free, timePoint };
    enum class Register { close, wide, fixed };
    enum class Dynamics { even, terraced, serial };

    struct Settings
    {
        Row row { 0, 1, 2, 3, 4, 5, 6, 7, 8, 9, 10, 11 };
        // The first voice's statements, repeated as long as the piece is; empty: a walk.
        std::vector<Statement> plan;
        int voices = 1;                      // 1..4
        Voices relation = Voices::independent;
        int delay = 8;                       // a canon's entries, sixteenths apart
        int chord = 1;                       // notes at once: 1 (a line), 2, 3, 4 or 6
        Rhythm rhythm = Rhythm::even;
        int unit = 2;                        // sixteenths: each note (even), the shortest (serial, free), a time point
        double rests = 0.2;                  // free: the chance of a rest after a note
        Register reg = Register::close;
        int low = 48, high = 84;             // the range of all the voices, MIDI keys (at least 12 apart)
        Dynamics dynamics = Dynamics::even;
        int velocity = 80;
        int bars = 4;                        // as long as this, in bars of 4/4, in whole statements
        int ticksPerQuarter = 480;
        uint32_t seed = 1;
    };

    struct Note
    {
        int64_t start = 0, end = 0;   // ticks
        int pitch = 60, velocity = 80;
    };

    struct Voice
    {
        std::string name;                    // "voice 1", or "soprano"... in a canon "dux", "comes"
        std::vector<Statement> statements;
        std::vector<Note> notes;             // by start
    };

    struct Result
    {
        std::vector<Voice> voices;
        int64_t length = 0;                  // ticks, the bars asked for (or whole bars more): the loop
        std::string commentary;              // each voice's statements
        std::string error;
    };

    Result compose (const Settings&);

    // A voice as a type 0 MIDI file at `bpm` on `channel`, `length` ticks long (so it loops).
    std::shared_ptr<const midifile::Song> toSong (const Voice&, int64_t length, int ticksPerQuarter, double bpm, int channel);
}

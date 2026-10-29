// SPDX-License-Identifier: AGPL-3.0-only
#pragma once

// The notations that write a melody beat by beat instead of on a staff (owner, 2026-10-29: "all of
// these open source notation software traditions incorporated into PHI", Kaufmann's Musical
// Notations of the Orient the reference): a clip's notes written out in the cycle they count in, as
// each tradition writes them. One shared model (noteedit::Sequence: pitch in cents, time, the pitch
// curve that carries a mīṇḍ or a gamaka, the lyric and the mark of each note, the cycle) and a
// writer a tradition, each giving the same Writing: beats grouped into sections and lines, each beat
// holding the signs written in it.
//
// - Bhatkhande's Hindustani sargam (after Swara Notebook, Studio Kalavati, AGPL-3.0, and its
//   sargam-spec, GPL-2.0-or-later): the tāl's vibhāgs between bars, × sam, 0 khālī and the tālī
//   numbers under their first mātrā, komal svaras underlined, tīvra Ma with a stroke above, the
//   octave in dots (mandra below, tār above), several svaras in one mātrā joined by an arc beneath,
//   a held mātrā "ऽ" (the avagraha), a rest "–", the kaṇ before a svara small above it, a mīṇḍ an arc over the
//   svaras it joins, the bandish's words under them. Devanagari, Bengali or Roman.
// - Karnatak svaras (after Note-a-Bhairavi, ragumagu, GPL-3.0): the tāḷam's aṅgas between | and the
//   āvartanam closed with ||, the svaras with their variant (R₁ R₂ G₂...) as the rāga spells them,
//   ',' for each held unit, the speed in lines over (one for two to an akṣara, two for four), the
//   sāhityam under. Roman, Devanagari, Kannada, Tamil, Telugu or Malayalam.
// - Jianpu, China's numbered notation (after jianpu-ly, Silas S. Brown, Apache-2.0): 1 to 7 from
//   the tonic, ♯ and ♭ before, octave dots, a dash for each beat a note holds on, lines under for
//   quavers and semiquavers, beamed beat by beat, a dot after for a dotted note, 0 a rest, bars by
//   the cycle or 4/4. The Indonesian not angka writes the same, with kepatihan's numbers for gamelan
//   and dots for the held beats.
// - Jeongganbo, Korea's mensural grid (after Jeongganbo Editor, Dongha Hwang, and Jeonggan,
//   Tteoieonire, both GPL-3.0): a box (jeonggan) a beat, read down the column (gak) and the columns
//   right to left, thick lines between the jangdan's sections (daegang), the twelve yulmyeong in
//   hanja with the octave in their radical (淸 氵 above, 倍 亻 below), a beat's notes split in rows
//   and columns, "－" a held beat, "△" a rest, the sigimsae from the note's mark.
//
// Plain C++, no Qt; MidiStaff paints a Writing.
#include "NoteEditor.h"

#include <cstdint>
#include <string>
#include <vector>

namespace notation
{
    // The script svaras are written in.
    enum class Script { roman, devanagari, bengali, kannada, tamil, telugu, malayalam };
    const char* scriptName (Script);   // "Roman", "Devanagari"...

    // One sign written in a beat.
    struct Sign
    {
        enum class Kind { note, hold, rest };
        Kind kind = Kind::note;
        std::string text;          // the name: "सा", "R₂", "5", "黃"; for a hold or rest its sign
        std::string before;        // written before it: jianpu's ♯ and ♭
        int octave = 0;            // dots above (up) or below (down); jeongganbo's radical instead
        bool lowered = false;      // a line under: Bhatkhande's komal
        bool raised = false;       // a stroke above: Bhatkhande's tīvra Ma
        int lines = 0;             // jianpu's lines under (1 a quaver, 2 a semiquaver); Karnatak speed lines over
        int dots = 0;              // jianpu's dots after (a dotted note)
        std::string grace;         // a kaṇ or grace notes before it, written small (Bhatkhande above, jianpu before)
        bool glide = false;        // a mīṇḍ or slide from it to the next note: an arc over to there
        std::string off;           // the cents it is off its degree, when it is
        std::string lyric;         // the syllable under it
        std::string mark;          // its mark (a stroke, a technique, a sigimsae)
        int note = -1;             // its note's index in the sequence, -1 for a hold or a rest
        int64_t tick = 0;          // where it starts
        int row = 0, column = 0;   // jeongganbo: where in its box (rows down, columns across)
    };

    struct Beat
    {
        int64_t tick = 0, length = 0;
        std::vector<Sign> signs;   // in time order
        bool grouped = false;      // more than one sign: Bhatkhande's arc beneath, jianpu's beamed lines
        std::string sign;          // its section's sign, on a section's first beat: ×, 0, 2; I₄, O, U
        bool sectionStart = false; // a vibhāg, an aṅga, a daegang begins here
        bool cycleStart = false;   // an āvartan, a bar, a jangdan begins here
        int rows = 1, columns = 1; // jeongganbo: how the box is divided
    };

    struct Writing
    {
        std::vector<Beat> beats;
        std::string legend;        // what it counts in: "tīntāl: 16 mātrās, 4+4+4+4"
        int beatsPerLine = 16;     // a cycle a line where it fits, else this many
        enum Layout { lines, columns };   // across in lines; down in columns, right to left (jeongganbo)
        Layout layout = lines;
        const char* holdSign = "–";
        bool doubleBarAtCycle = false;    // || at the end of a cycle (Karnatak), else | before each section
        int64_t start = 0, beat = 480;    // the counting's: where a cycle starts, and a beat's ticks
    };

    // What a writing counts in: a rhythm of rhythm::rhythms() (its beats and sections, and their
    // signs), or the bars of 4/4 when the clip has no cycle.
    struct Counting
    {
        int64_t start = 0;         // the first cycle's first beat
        int64_t beat = 480;        // ticks
        int beats = 4;             // a cycle
        std::vector<int> sections { 4 };
        std::vector<std::string> marks;
        std::string legend;
    };
    Counting counting (const noteedit::Sequence&, int64_t tick);

    // The melody to write: a note at a time, the highest of a chord, drums left out.
    std::vector<int> melody (const noteedit::Sequence&);

    // The writers, over the beats from `from` to `to` (ticks). Each note in the system in force
    // where it starts (`before` when the clip has none).
    Writing bhatkhande (const noteedit::Sequence&, const noteedit::PitchSystem& before, Script, int64_t from, int64_t to);
    Writing carnatic (const noteedit::Sequence&, const noteedit::PitchSystem& before, Script, int64_t from, int64_t to);
    // `angka`: Indonesia's not angka, the held beats dots, and the degrees named as the system names
    // them (kepatihan for gamelan).
    Writing jianpu (const noteedit::Sequence&, const noteedit::PitchSystem& before, bool angka, int64_t from, int64_t to);
    Writing jeongganbo (const noteedit::Sequence&, const noteedit::PitchSystem& before, int64_t from, int64_t to);

    // The names, for the views' tooltips and tests.
    // A svara by its semitone above Sa (0..11): Bhatkhande's name in the script, komal or tīvra.
    struct Svara { std::string name; bool komal = false, tivra = false; };
    Svara hindustani (int semitone, Script);
    // A Karnatak svara by its semitone above Sa as `letter` (0 S .. 6 N) of the rāga: "R₂", "ரி₂".
    std::string karnatak (int semitone, int letter, Script);
    // The letters (0 S .. 6 N) a rāga's degrees take: in order for a sampūrṇa rāga that keeps Sa
    // and Pa, else each by its semitone's commonest name. `semitones` are the degrees' (0..11).
    std::vector<int> karnatakLetters (const std::vector<int>& semitones);
    // The yulmyeong of a pitch (cents, as Note::pitch): the twelve lü from hwangjong at E♭4
    // (modern gugak's), the octave in its radical; `octave` is set to the octaves from the middle.
    std::string yulmyeong (double pitch, int& octave);

    // Jeongganbo's sigimsae (Jeonggan's table): its key (the degrees it moves through, 3 the note),
    // the text the TteoiJeonggan font draws as its sign, and whether it is written after the note
    // (else over it). A note's mark holds the text.
    struct Sigimsae { std::string key, text; bool after = true; };
    const std::vector<Sigimsae>& sigimsae();

    // A note's pitch curve read as an ornament: a glide to the next note (mīṇḍ), a grace note at
    // its start (kaṇ), or a shake (gamak, āndolan). Cents and ticks as Note::curve.
    struct Ornament { bool glide = false; double grace = 0.0; bool shake = false; };
    Ornament ornamentOf (const noteedit::Note&);
}

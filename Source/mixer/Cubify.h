// SPDX-License-Identifier: AGPL-3.0-only
#pragma once

// cubify (owner, 2026-10-26): a clip launcher scene, or one MIDI clip, arranged by Iannis
// Xenakis's cube method in Nomos Alpha (1965-66), as Formalized Music (chapter VIII) gives it and
// as composition teachers pass it on. Our own code (no port of another program). Plain C++, no
// JUCE or Qt.
//
// - The group: the 24 rotations of a cube, each a way the eight corners change places,
//   labelled as Xenakis labels them (I, A, B, C, D, D2, E, E2, G, G2, L, L2 keep each of the
//   cube's two tetrahedra, corners 1-4 and 5-8, in place; Q1..Q12 swap them), with his table of
//   where each rotation sends the corners (Formalized Music, figure VIII-6).
// - The path: each rotation is the product of the two before it (f(n) = f(n-2) · f(n-1)), as
//   Nomos Alpha's sections follow D, Q12, Q4, E, Q8, Q2...
// - The cubes: four cubes nested one in another, eight values on the corners of each: fixed
//   pitches (not pitch classes: never moved by an octave) taken from a sieve on a quarter-tone
//   grid, durations, dynamics (ppp..fff) and articulations. Each turns on a path of its own;
//   reading the corners in order gives a line of notes, each the four values that meet there.
// - The form: 24 sections, V1 V1 V1 V2 six times. V1 is the cubes; V2 is freer: a
//   three-quarter-tone scale, the extremes of each part's register, long held notes that
//   swell and slide (glissandi as pitch bend).
// - The ensemble: the tracks sit on the corners of a grand cube, similar ones apart (on one
//   tetrahedron: no two of its corners share an edge). The grand cube turns too, so a track
//   plays the cubes of the track whose corner it has turned onto, as an oboe plays a
//   trombone's cube. An audio loop plays in the sections its corner turns into the upper
//   tetrahedron and rests in the others.
// - The modifications, as a composer makes them: a loud attack asked for pianissimo (a Bartók
//   pizzicato) is played louder, or as tenuto; a legato too short to join becomes staccato; a
//   pitch out of a track's range (or a drum's cube on a melody) is replaced by the track's own
//   pitch at that corner, or a rest; a part too soft to be heard against the loudest is brought
//   up; a jump in dynamics on one line gets a hairpin; a wind part is given room to breathe;
//   every section ends on a bar line.
// - Messiaen (owner, 2026-10-26: all three): pitches from one of his modes of limited
//   transposition (each a sieve, on the clip's lowest note or a chosen first note); non-retrogradable rhythms (each
//   reading's durations a palindrome); and the "mode de valeurs" path (Mode de valeurs et
//   d'intensités, 1949), where the cubes are locked together, so each pitch keeps its own
//   duration, dynamic and attack and only their order turns.
// Everything comes from one seed: variation 1 follows Xenakis's own paths.
#include "Stochastic.h"

#include <array>
#include <cstdint>
#include <memory>
#include <string>
#include <vector>

namespace midifile { struct Song; }

namespace cubify
{
    //==========================================================================
    // The rotation group of the cube.
    struct Rotation
    {
        const char* label;          // "D", "Q12"
        std::array<int, 8> seq;     // corners 0..7: corner i reads what corner seq[i] held
        bool swaps;                 // Q: the two tetrahedra change places
    };
    const std::array<Rotation, 24>& group();   // I first
    int find (const std::string& label);       // its index, or -1
    int product (int p, int q);                // p then q: (p·q).seq[i] = q.seq[p.seq[i]]
    int inverse (int r);
    int power (int r, int n);                  // r turned n times (n >= 0)
    // Xenakis's path: a, b, a·b, b·(a·b)... `n` long.
    std::vector<int> chain (int a, int b, int n);

    //==========================================================================
    enum class Articulation { staccatissimo, staccato, portato, tenuto, legato, accent, marcato, sforzando };
    const char* articulationName (Articulation);   // lower case
    const char* dynamicName (int level);           // 0 ppp .. 7 fff

    struct Note
    {
        int64_t start = 0, end = 0;   // ticks
        int pitch = 60, velocity = 90;
    };

    // A track of the scene: a MIDI clip's notes, or an audio loop.
    struct Part
    {
        std::string name;
        std::vector<Note> notes;      // at the Settings' ticksPerQuarter
        bool drums = false;           // channel 10: its pitches are drums, kept as they are
        bool audio = false;           // an audio loop (no notes): it plays or rests by section
    };
    Part partOf (const midifile::Song&, const std::string& name, int ticksPerQuarter);

    enum class Pitches { sieve, clip, messiaen };
    enum class Path { nomos, modeDeValeurs };
    enum class OutOfRange { ownPitch, rest };

    struct Settings
    {
        int ticksPerQuarter = 480;
        Pitches pitches = Pitches::sieve;
        // On the quarter-tone grid (quarterTones) or semitones, counting from C: 24@0 is every C.
        // The Nomos Alpha sieve stochasticize offers, here on the quarter tones: period 143, no octaves in it.
        std::string sieve = "(13@3 | 13@5 | 13@7 | 13@9) & 11@2 | (11@4 & 13@5) | (13@9 & 11@8) | 13@0 | 13@1 | 13@6";
        int messiaenMode = 2;          // 1..7
        int root = -1;                 // the mode's first note, 0 (C) .. 11 (B); -1: each part's lowest note
        bool quarterTones = true;      // played by pitch bend (± 2 semitones), else semitones only
        Path path = Path::nomos;
        int readings = 2;              // times each V1 section reads the eight corners, 1..4
        bool clipDurations = true;     // the durations cube from the clip's own, else 1..16 sixteenths
        bool palindromes = false;      // Messiaen's non-retrogradable rhythms
        OutOfRange outOfRange = OutOfRange::ownPitch;
        bool balance = true, hairpins = true, breaths = false, glissandi = true;
        uint32_t seed = 1;             // 1: Xenakis's own paths
    };

    // What is written for each part, from tick 0.
    struct Line
    {
        std::vector<Note> notes;
        // Pitch bend (14 bits, 8192 the middle) and expression (CC 11) as they change.
        struct Control { int64_t tick; bool bend; int value; };
        std::vector<Control> controls;
        std::vector<std::pair<int64_t, int64_t>> plays;   // an audio part: the spans its loop plays
    };

    struct Section
    {
        int64_t start = 0, length = 0;   // ticks
        bool free = false;               // V2
        std::string rotations;           // "pitch D, durations E, dynamics D2, articulations Q12, grand L"
    };

    struct Modifications
    {
        int softAttacks = 0, shortLegatos = 0, range = 0, timbre = 0, balance = 0, hairpins = 0, breaths = 0, bars = 0;
        int total() const { return softAttacks + shortLegatos + range + timbre + balance + hairpins + breaths + bars; }
    };

    struct Result
    {
        std::vector<Line> lines;          // one for each part, in the parts' order
        std::vector<int> corners;         // each part's corner of the grand cube, 0..7
        std::vector<Section> sections;    // 24
        Modifications modified;
        int64_t length = 0;
        std::string error;                // when nothing could be written
        std::string commentary;           // the sections, cubes and modifications, as text
    };

    Result arrange (const std::vector<Part>&, const Settings&);

    // A part's line as a type 0 MIDI file at `bpm`, on `channel`.
    std::shared_ptr<const midifile::Song> toSong (const Line&, int ticksPerQuarter, double bpm, int channel);
}

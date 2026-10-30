// SPDX-License-Identifier: AGPL-3.0-only
#pragma once

// Architectonics (owner, 2026-10-29: "for designing a set in the clip launcher based on Phi
// (the golden ratio) that is generative"; its own menu). A set is planned from the launcher's
// scenes: its length cut by the golden ratio again and again, so every section is a Fibonacci
// number of bars when the set is; the climax where the first cut falls, at 1/φ of the way
// through (or 1/φ² for an early one); and the scenes put in order by the arch the climax
// makes, by the Fibonacci numbers, or by a walk of steps of 1/φ. A seed makes each set new:
// which part of a cut comes first, which sections are cut once more, and how the scenes are
// walked. docs/architectonics.md. Plain C++, no JUCE or Qt.
#include "MidiFile.h"

#include <array>
#include <cstdint>
#include <string>
#include <vector>

namespace rhythm { struct Rhythm; }

namespace architectonics
{
    constexpr double phi = 1.6180339887498948482;

    // 1, 2, 3, 5, 8, 13... up to `most`.
    std::vector<int> fibonacci (int most);
    bool isFibonacci (int n);

    // How the scenes follow one another.
    enum class Order
    {
        arch,        // by how full they are, rising to the climax and falling after it
        fibonacci,   // the k-th section takes the scene F(k) along, round and round
        phiWalk      // the k-th section takes the scene at frac (start + k/φ) of the way along
    };

    struct Settings
    {
        // Clips in each of the launcher's scenes, scene 0 first; empty ones are left out.
        std::vector<int> clips;
        int bars = 144;           // the set's length; a Fibonacci number keeps every section one
        int longest = 13;         // no section longer than this, in bars
        Order order = Order::arch;
        bool earlyClimax = false; // the climax at 1/φ² (0.382) of the way, the shorter part first
        int climaxScene = -1;     // -1: the fullest scene (the later of equals)
        double variety = 0.38;    // 0..1: how many sections are cut once more by φ
        double seed = 0.0;        // 0..1: where the set's walk starts
    };

    // What a section does in the set, in five parts by the golden ratio (the owner's blueprint,
    // 2026-10-29): emergence for the first 1/φ² of the rise to the climax, expansion after it,
    // disruption the last section before the climax (a contrast, when the rise has three or
    // more), the climax from the golden section for 1/φ of what is left, and dissolution after.
    enum class Function { emergence, expansion, disruption, climax, dissolution };
    const char* functionName (Function);   // "emergence"...

    struct Section
    {
        int scene = 0;
        int start = 0;            // bar, from 0
        int bars = 0;
        double energy = 0.0;      // 0..1: the arch at the section's middle (1 at the climax)
        bool climax = false;      // the section the climax starts with
        Function function = Function::emergence;
    };

    struct Plan
    {
        std::vector<Section> sections;   // in order, back to back from bar 0
        int bars = 0;
        int climaxBar = 0;               // where the climax section starts: the golden section
        int dissolutionBar = 0;          // where the climax gives way
        int climaxScene = -1;
    };

    // Empty when no scene has a clip, or bars < 2.
    Plan plan (const Settings&);

    // The section playing at `bar` (bars from the set's start), -1 past the end.
    int sectionAt (const Plan&, double bar);

    // A new seed from the last, by a step of √7: an irrational step, so seeds never repeat.
    double nextSeed (double seed);

    // Compose a Song (owner, 2026-10-29: "one massive tool that could generate a song structure
    // in the clip launcher. It would draw from the Selector, semi-randomly ... logic based on
    // architectonics that draws from the four major modes of PHI: rhythm, melody, tuning, and
    // harmony"). The form is the set's: a scene for every section, in order. Each section's
    // energy decides how many of the Selector's colours play in it, entering one by one as the
    // energy rises (pad, bass, drums, arpeggio, lead, then the Selector's sounds) and leaving
    // as it falls. A colour's role says what it plays: a sound dealt from the Selector, or MIDI
    // written from the modes: a rhythm from the library as dense as the energy (rhythm), the
    // chords' roots (bass), the chords held (chords) or broken (arpeggio), and a line on them
    // (melody), all in the Mode menu's mode on its final (melody and harmony); the scenes play
    // in its tuning. The harmony follows each part's formal function (Caplin): the final held
    // in the emergence, a progression in the expansion, an unstable turn ending on the dominant
    // in the disruption, the fullest progression at the climax and a plagal close in the
    // dissolution. Every section of a part takes that part's progression and motif, moved on
    // a chord each time it comes back, so the song's material recurs and varies.
    enum class Role { sound, rhythm, bass, chords, arpeggio, melody };
    const char* roleName (Role);
    constexpr int colours = 8;   // the Selector's, top first

    struct SongSettings
    {
        Settings form;                   // bars, longest, earlyClimax, variety and seed (clips, order and climaxScene are set here)
        std::array<Role, colours> roles { Role::sound, Role::chords, Role::arpeggio, Role::sound,
                                          Role::melody, Role::sound, Role::bass, Role::rhythm };   // Live Performance: FX PD ARP VOX LD PR BA DR
        std::array<bool, colours> present {};   // the colours that can play: a role that writes MIDI, or a sound dealt
        int loopBars = 4;                // each scene's MIDI loop
        int beatsPerBar = 4;
        double bpm = 120.0;
        uint16_t keys = 0xAB5;           // the mode's pitch classes above the final (bit 0 the final): major to start
        int final = 0;                   // 0 c .. 11 b
        std::vector<const rhythm::Rhythm*> rhythms;   // to choose from; empty: the four-on-the-floor of our own
    };

    struct Scene
    {
        int section = 0;                 // in the plan
        Function function = Function::emergence;
        double energy = 0.0;
        std::array<bool, colours> plays {};
        std::array<midifile::SongPtr, colours> midi {};   // for the colours whose role writes MIDI and that play
        std::vector<int> chords;         // the progression, a degree of the mode a bar
        std::string rhythm;              // the rhythm's name, or empty
    };

    struct Song
    {
        Plan plan;                       // its sections' scenes: 0, 1, 2... (the caller adds where they start)
        std::vector<Scene> scenes;       // one a section
    };

    Song compose (const SongSettings&);

    // The song written out in full for notation and the timeline (owner, 2026-10-29: "a staff
    // notation feature that would generate ... a composition based on architectonics function"):
    // each colour's part from the first bar to the last, its scene's loop repeated through each
    // section it plays in (null for a colour with no MIDI); and all of them in one file, a MIDI
    // channel a colour (its drums on channel 10), with a marker where each section starts
    // ("1 emergence", "12 climax").
    std::array<midifile::SongPtr, colours> parts (const Song&, const SongSettings&);
    midifile::SongPtr score (const Song&, const SongSettings&, const std::string& title);
}

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
#include <cstdint>
#include <vector>

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
}

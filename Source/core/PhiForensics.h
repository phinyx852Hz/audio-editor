// SPDX-License-Identifier: AGPL-3.0-only
#pragma once

// Forensic Φ: does a plugin's maker seem to have used the golden ratio? It reads the
// Plugin Analyzer's measurements (PluginMeasure.h) for numbers a designer chooses:
// the frequencies of peaks, notches and phase turns, echo times and how much
// quieter each echo is, the tones a pitch shifter adds, a compressor's ratio and
// its attack against its release, LFO rates, a gate's rhythm, the latency. Then it
// looks for golden ratios between them (φ^(n/2): √φ, φ, φ^1.5, φ², ... φ⁶) and for
// golden numerals (1.618, 2.618, 6.18 and Fibonacci numbers such as 13, 233 or 610).
//
// Measure enough numbers and some fall near φ by chance, so every match is
// weighed against chance:
// - each comparison's tolerance comes from how exactly its numbers are measured,
//   and with it the chance of a hit, so the hits expected from luck alone are
//   known; the chance of seeing as many as were found is a Poisson tail;
// - each kind of evidence (echo times, tones, peaks...) is weighed on its own, at
//   the full tolerance and at a fifth of it, and the most telling decides, its
//   chance multiplied by how many were tried, so a plugin whose response is a
//   jumble of peaks cannot drown out (or fake) its exact golden echoes;
// - the same search is run for 1000 other ratios from 1.40 to 1.90 (the control
//   scan): φ only stands out if it finds more than nearly all of them;
// - a ratio that is also a simple fraction (10/3 is 0.1 % from φ^2.5) proves
//   nothing about φ, so such targets are left out at that tolerance.
// Plain C++.
#include "PluginMeasure.h"

#include <string>
#include <vector>

namespace forensic
{
    struct Feature
    {
        std::string group;   // what kind of number: "frequency", "phase", "echo", "level", "tone", "dynamics", "rate", "rhythm", "latency"
        std::string label;   // e.g. "Peak at 129.4 Hz (L)"
        double value = 0.0;
        double sigma = 0.01; // how exactly it is known, relative (0.01 = 1 %)
        std::string unit;    // "Hz", "ms", "" (a ratio)
    };

    struct Match
    {
        std::string what;      // e.g. "Echo 2 / Echo 1"
        double value = 0.0;    // the ratio (or numeral)
        std::string target;    // e.g. "φ²"
        double errorPercent = 0.0;
        double chance = 0.0;   // how likely a hit this close was by luck
        bool exact = false;    // within 0.1 %
    };

    struct ScanPoint { double base = 0.0; int hits = 0; };

    // One kind of evidence weighed on its own (at the full tolerance, or a fifth of it).
    struct GroupScore
    {
        std::string name;    // the feature group, or "numbers"
        bool fine = false;   // at a fifth of the tolerance
        int comparisons = 0, hits = 0;
        double expected = 0.0, pValue = 1.0;
    };

    enum class Verdict { nothing, chance, suggestive, strong };

    struct Report
    {
        std::vector<Feature> features;
        std::vector<Match> matches;          // golden ratios between features (and features that are ratios)
        std::vector<Match> numerals;         // golden numbers and Fibonacci numbers among the features
        int comparisons = 0;                 // how many ratios and numerals were tried
        double expected = 0.0;               // hits expected by chance, all kinds together
        int hits = 0;                        // hits found (matches + numerals)
        std::vector<GroupScore> groups;      // each kind of evidence
        std::string decisive;                // the most telling kind (empty when none has two hits)
        double pValue = 1.0;                 // its chance by luck, times the number of kinds and tolerances tried
        std::vector<ScanPoint> scan;         // the control scan of the deciding kind: hits for each base
        int phiHits = 0;                     // φ's own score in the scan
        double percentile = 1.0;             // share of the other bases that score as well as φ
        Verdict verdict = Verdict::nothing;
        std::string summary;
    };

    // The features of a measurement (what is read from it), then the search.
    std::vector<Feature> features (const measure::Results&);
    Report analyse (const std::vector<Feature>&);
    inline Report analyse (const measure::Results& r) { return analyse (features (r)); }

    std::string verdictName (Verdict);
    std::string text (const Report&);   // a readable report with the caveats
}

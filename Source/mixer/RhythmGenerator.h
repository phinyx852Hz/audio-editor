// SPDX-License-Identifier: AGPL-3.0-only
#pragma once

// The rhythm generator (owner, 2026-10-27: "a combo of all of these as you see fit", after
// Music Pattern Generator, Stochas and Subsequence): drum lanes, each its own length, so they
// run against each other (polymeter) until they line up again.
// - Each lane is a Euclidean rhythm: `pulses` spread as evenly as they go over `steps`, by
//   Bjorklund's algorithm, then rotated. Toussaint (2005) showed many traditional timelines are
//   Euclidean: E(3,8) is the tresillo, E(5,8) the cinquillo, E(7,12) the bembé bell. A lane can
//   instead take a written pattern (a rhythmic mode's), or a sieve as a mask over its steps.
// - Each hit has a chance to play (Stochas), and the loudness and timing can be humanized.
// - Rules (Subsequence): a fill every N bars, a density that rises or falls across the clip,
//   every Nth hit skipped, and the lanes turning a step every N bars.
// Every chance is drawn from one seed with stochastic::Random, so the same settings and seed
// write the same groove. Our own code. Plain C++, no JUCE or Qt.
#include "MidiFile.h"

#include <cstdint>
#include <string>
#include <vector>

namespace rhythm { struct Rhythm; }

namespace rhythmgen
{
    struct Lane
    {
        std::string name = "kick";
        int note = 36;               // a General MIDI drum on channel 10
        int steps = 16;              // the lane's cycle
        int stepsPerBeat = 4;        // 4: 16ths; 3: 8th-note triplets (12/8)
        int pulses = 4;
        int rotation = 0;            // the cycle starts this many steps later
        // Written hits (X accent, x hit, o soft, . rest, as rhythm::Lane): when set they take
        // the Euclidean rhythm's place, and `steps` is their length.
        std::string pattern;
        // A Xenakis sieve over the lane's steps ("4@0|4@3"): only the steps it holds may play.
        std::string sieve;
        double chance = 1.0;         // each hit plays with this chance
        int velocity = 100;          // a hit; an accent is 120 and a soft one 60, before humanizing
        bool accentFirst = true;     // the cycle's first hit accented
        bool muted = false;
        double pushMs = 0.0;         // played this much late (+, laid back) or early (-, pushing)
    };

    struct Settings
    {
        std::vector<Lane> lanes;
        int beatsPerBar = 4;
        int bars = 0;                // 0: until the lanes line up again (at most 64 bars)
        double swing = 0.0;          // 0 .. 0.5: every second step pushed this part of a step late
        int swingPerBeat = 0;        // the steps swung: 2 8ths, 4 16ths; 0 each lane's own steps
        double humanizeVelocity = 0.0;   // the standard deviation, in velocity
        double humanizeMs = 0.0;         // the standard deviation of the timing, in ms
        // Rules.
        int fillEvery = 0;           // bars: the last bar of every N fills in (0: never)
        double fillChance = 0.35;    // a rest in a fill bar plays softly with this chance
        double densityFrom = 1.0, densityTo = 1.0;   // the chance scaled from one to the other across the clip
        int skipEvery = 0;           // every Nth hit of each lane left out (0: none)
        int rotateEvery = 0;         // bars: each lane turns a step later every N bars (0: never)
        uint32_t seed = 1;
    };

    // `pulses` over `steps` as evenly as they go (Bjorklund), started `rotation` steps in.
    std::vector<bool> euclid (int pulses, int steps, int rotation = 0);
    // A lane's cycle as X x o . (its pattern, or its Euclidean rhythm with the first hit accented).
    std::string cycle (const Lane&);
    // The clip's length in bars: Settings::bars, or the bars until every lane's cycle comes round
    // together.
    int lengthBars (const Settings&);

    // The groove as a type 0 MIDI file at `bpm`, 480 ticks a beat, drums on channel 10.
    // `error` is set (and the song empty) when a sieve does not read.
    midifile::SongPtr generate (const Settings&, double bpm, std::string& error);
    // The same on the grid only, without the humanized timing and with three loudnesses, as a
    // rhythm for the Tracker: every lane at the finest steps any of them has.
    bool toRhythm (const Settings&, const std::string& name, rhythm::Rhythm&, std::string& error);

    // A rhythmic mode's lanes as written patterns, to start from.
    Settings fromRhythm (const rhythm::Rhythm&);

    // Feels (owner, 2026-10-27, after Hydrogen's swing and humanizing): how far off the grid the
    // groove sits. Swing in the MPC's percentages (50 straight, 66 a triplet shuffle), the kick,
    // the snare and the cymbals each pushed or laid back, and humanized timing and loudness.
    // Our own settings, a place to start from, not measurements of any drummer.
    struct Feel
    {
        std::string name;    // lower case: "laid back"
        std::string about;
        double swingPercent = 50.0;
        int swingPerBeat = 4;
        double kickMs = 0.0, snareMs = 0.0, cymbalMs = 0.0;
        double humanizeMs = 0.0, humanizeVelocity = 0.0;
    };
    const std::vector<Feel>& feels();
    // The feel's swing and humanizing into the settings, and its pushes onto the lanes by their sound.
    void applyFeel (Settings&, const Feel&);

    // Euclidean rhythms of the world, from Toussaint (2005), each with a low drum on the beat.
    struct Preset
    {
        std::string name;    // lower case: "tresillo: e(3,8)"
        std::string about;
        Settings settings;
    };
    const std::vector<Preset>& presets();
}

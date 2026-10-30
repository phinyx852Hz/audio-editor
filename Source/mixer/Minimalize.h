// SPDX-License-Identifier: AGPL-3.0-only
#pragma once

// Minimalize (owner, 2026-10-26): an arrangement in the ways of Steve Reich and Philip Glass,
// made from a clip-launcher scene or from what is on the timeline (cubify's arrangement, a Φ
// deal), within the mixer's eight tracks. Step 4 of PHI's generative chain: the Selector's set,
// the MIDI generators, cubify, then minimalize. Our own code. Plain C++, no JUCE or Qt.
//
// Each track's material is a pattern (a scene's loop, or a stretch of the timeline cut into
// cells), and each track takes one process to it. Every stage of a process plays `repeats`
// times, as Reich and Glass mark a bar to be repeated.
//
// Reich's: phasing, a copy of the pattern a step further ahead at each stage until it is back
// in unison (Piano Phase, Clapping Music), against the pattern held still; canon at the unison,
// copies entering one after another a beat apart; build-up, the pattern's notes put in place of
// rests one a stage (Drumming); and a pulse, the harmony played in steady eighths that swell and
// fall like a breath (Music for 18 Musicians).
// Glass's: additive process, the figure's first note, then the first two, the first three...
// each stage a little longer (Music in Fifths, Einstein on the Beach); subtractive, notes taken
// off the front again; cycles, a figure a little shorter than the others looped against them, so
// the tracks part and meet again; and arpeggiated harmony, the scene's chords broken in eighths.
//
// The gradual processes (phase, canon, build-up, additive, subtractive) set how long a section
// lasts; the others loop through it, and a gradual one that ends early holds where it ended.
#include "MidiFile.h"

#include <cstdint>
#include <string>
#include <vector>

namespace minimalize
{
    constexpr int ticksPerQuarter = 480;   // in and out
    constexpr int maxTracks = 16;   // as the mixer (MixerEngine.h); freeTracks says how many are free

    struct Note
    {
        int64_t start = 0, end = 0;   // ticks
        int pitch = 60, velocity = 90, channel = 0;
    };
    using Notes = std::vector<Note>;

    struct Cell
    {
        Notes notes;            // from 0
        int64_t length = 0;     // the loop, ticks
    };

    // A track's material: one cell for a scene's slot, or the timeline cut into cells, one a section.
    struct Part
    {
        std::vector<Cell> cells;
        std::string name;
        bool drums = false;     // channel 10: left out of the harmony, never arpeggiated
    };

    enum class Process { automatic, hold, phase, canon, buildUp, pulse, additive, subtractive, cycles, arpeggio };
    const char* name (Process);   // lower case: "phase", "build-up"...
    enum class Style { reich, glass, both };

    struct Settings
    {
        Style style = Style::both;
        std::vector<Process> processes;   // one a part; missing or automatic: chosen by the style
        int repeats = 4;                  // times each stage plays, 1..32
        int stepTicks = ticksPerQuarter / 4;   // a phase or build-up step: a sixteenth
        int shifts = 0;                   // phase steps before it holds; 0: all the way back to unison
        bool newVoices = true;            // phase copies, canon voices and a pulse on the free tracks
        int freeTracks = maxTracks;       // how many tracks the mixer has free for them
        uint32_t seed = 0;
        int64_t maxTicks = (int64_t) ticksPerQuarter * 4 * 1000;   // a thousand bars at most
    };

    struct Track
    {
        Notes notes;
        std::string name;       // the part's, with the process: "bass (phase)"
        int source = 0;         // the part it came from
        Process process = Process::hold;
        bool added = false;     // a new voice, not one of the parts
    };

    struct Result
    {
        std::vector<Track> tracks;   // the parts first, in order, then the new voices
        std::vector<int64_t> sections;   // where each section starts, ticks
        int64_t length = 0;
    };

    Result arrange (const std::vector<Part>&, const Settings&);

    // A cell's notes cut from a longer line: those starting in [from, to), moved to 0.
    Cell cut (const Notes&, int64_t from, int64_t to);

    // A track as a type 0 MIDI file at `bpm`, each note on its own channel, `length` long.
    midifile::SongPtr toSong (const Notes&, double bpm, int64_t length);
}

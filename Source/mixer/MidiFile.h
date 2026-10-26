// SPDX-License-Identifier: AGPL-3.0-only
#pragma once

// Standard MIDI files (.mid, types 0 and 1; type 2 is read as one song) for the
// mixer's tracks: read and played, never edited. Plain C++, no JUCE or Qt.
#include <cstddef>
#include <cstdint>
#include <memory>
#include <string>
#include <vector>

namespace midifile
{
    // A channel message (note on and off, controllers, program, pressure, pitch bend).
    // Meta events and SysEx are left out, apart from tempo, which is in Song::tempos.
    struct Event
    {
        int64_t tick = 0;
        uint8_t status = 0;     // 0x80..0xEF: the kind in the top four bits, the channel in the bottom four
        uint8_t data1 = 0, data2 = 0;
        int channel() const { return status & 0x0F; }
        int kind() const    { return status & 0xF0; }
    };

    struct Tempo
    {
        int64_t tick = 0;
        double microsPerQuarter = 500000.0;   // 120 BPM
    };

    struct Song
    {
        int format = 0;              // 0, 1 or 2
        int ticksPerQuarter = 480;   // 0 with SMPTE time
        double ticksPerSecond = 0;   // SMPTE time only: frames per second times ticks per frame
        std::vector<Event> events;   // every track's, in time order (file order when at the same tick)
        std::vector<Tempo> tempos;   // in time order; at least one, at tick 0
        std::vector<std::string> trackNames;
        int64_t lastTick = 0;        // the last event of any kind, end of track included
        uint16_t channelsUsed = 0;   // a bit per channel that has notes

        double fileBpm() const;      // the first tempo
        // Where a tick falls in seconds: with the file's tempo map, or at a fixed tempo
        // (bpm > 0), which follows the mixer's tempo; SMPTE files ignore the tempo.
        double seconds (int64_t tick, double bpm = 0.0) const;
        double lengthSeconds (double bpm = 0.0) const { return seconds (lastTick, bpm); }
        int noteCount() const;
    };
    using SongPtr = std::shared_ptr<const Song>;

    SongPtr parse (const uint8_t* data, size_t size, std::string& error);
    SongPtr read (const std::string& path, std::string& error);

    // Written back as a type 0 file: the tempo map, the channel events and the end of track at
    // lastTick. Files in SMPTE time are left alone (empty bytes, false).
    std::vector<uint8_t> bytes (const Song&);
    bool write (const Song&, const std::string& path, std::string& error);

    // Fast edits (owner, 2026-10-25: "MIDI editing, but really fast"). Each gives a new song and
    // leaves the one given alone. The notes are taken as they sound (an on and its off); the
    // other channel events stay where they are. A loop is `span` long: lastTick, up to whole
    // bars of 4/4.
    int64_t span (const Song&);
    SongPtr reversed (const Song&);          // mirrored over lastTick, as the mixer plays it reversed
    // The notes' places in time dealt out again along the golden ratio: the same pitches,
    // velocities and lengths, and the same rhythm (the times notes start), but which notes
    // (chords stay together) start at which of those times changes. `turn` gives another
    // order each time (0, 1, 2...).
    SongPtr phiShuffle (const Song&, int turn);
    SongPtr duplicated (const Song&);        // the loop twice, as one song twice as long
    SongPtr halfDensity (const Song&);       // every other chord or note in time kept
    SongPtr doubleDensity (const Song&);     // each note played twice in its own time, half as long
    // Modal music (owner, 2026-10-25): the seven modes, then the major Hindustani ragas (the ten
    // thaats among them) as the notes they use, both ways up and down; some share their notes
    // (Darbari and Jaunpuri with Asavari), and each is listed by its own name. Bit n: n semitones
    // above the root (Sa).
    struct Scale
    {
        const char* name;
        const char* family;   // "Mode" or "Raga"
        uint16_t notes;
    };
    const std::vector<Scale>& scales();
    // Every note moved to the nearest note of scales()[scale] on `root` (0 C .. 11 B), down when it
    // is between two; drums (channel 10) left alone.
    SongPtr toMode (const Song&, int root, int scale);
    // Every note (drums left alone) folded by octaves into the octave that starts `below` keys
    // under the final `root` (0 C .. 11 B), the one nearest the notes' middle: a hypo-mode's
    // range (mixer::Hypo), the final kept.
    SongPtr inRange (const Song&, int root, int below);
}

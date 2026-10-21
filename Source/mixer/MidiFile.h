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
}

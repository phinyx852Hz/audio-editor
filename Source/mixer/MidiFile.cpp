// SPDX-License-Identifier: AGPL-3.0-only
#include "MidiFile.h"

#include <algorithm>
#include <fstream>
#include <iterator>

namespace midifile
{
    namespace
    {
        struct Reader
        {
            const uint8_t* p;
            const uint8_t* end;

            bool has (size_t n) const { return (size_t) (end - p) >= n; }
            uint32_t be (int bytes)
            {
                uint32_t v = 0;
                for (int i = 0; i < bytes; ++i)
                    v = (v << 8) | *p++;
                return v;
            }
            // A variable-length quantity: seven bits a byte, at most four bytes.
            bool vlq (uint32_t& v)
            {
                v = 0;
                for (int i = 0; i < 4; ++i)
                {
                    if (p >= end)
                        return false;
                    const auto b = *p++;
                    v = (v << 7) | (b & 0x7F);
                    if ((b & 0x80) == 0)
                        return true;
                }
                return false;
            }
        };

        // Bytes after the status byte for a channel message.
        int dataBytes (uint8_t status) { const auto k = status & 0xF0; return k == 0xC0 || k == 0xD0 ? 1 : 2; }
    }

    double Song::fileBpm() const
    {
        return tempos.empty() ? 120.0 : 60.0e6 / tempos.front().microsPerQuarter;
    }

    double Song::seconds (int64_t tick, double bpm) const
    {
        if (ticksPerSecond > 0.0)
            return (double) tick / ticksPerSecond;
        const auto tpq = (double) std::max (1, ticksPerQuarter);
        if (bpm > 0.0)
            return (double) tick / tpq * 60.0 / bpm;

        double at = 0.0;
        int64_t from = 0;
        double micros = 500000.0;
        for (const auto& t : tempos)
        {
            if (t.tick >= tick)
                break;
            at += (double) (t.tick - from) / tpq * micros * 1.0e-6;
            from = t.tick;
            micros = t.microsPerQuarter;
        }
        return at + (double) (tick - from) / tpq * micros * 1.0e-6;
    }

    int Song::noteCount() const
    {
        return (int) std::count_if (events.begin(), events.end(), [] (const Event& e) { return e.kind() == 0x90 && e.data2 > 0; });
    }

    SongPtr parse (const uint8_t* data, size_t size, std::string& error)
    {
        Reader r { data, data + size };

        // RIFF-wrapped MIDI (.rmi): the SMF is in the "data" chunk.
        if (r.has (20) && std::equal (data, data + 4, "RIFF") && std::equal (data + 8, data + 12, "RMID"))
        {
            r.p += 12;
            while (r.has (8))
            {
                const bool isData = std::equal (r.p, r.p + 4, "data");
                r.p += 4;
                const auto len = (uint32_t) r.p[0] | (uint32_t) r.p[1] << 8 | (uint32_t) r.p[2] << 16 | (uint32_t) r.p[3] << 24;
                r.p += 4;
                if (isData)
                {
                    r.end = r.p + std::min<size_t> (len, (size_t) (r.end - r.p));
                    break;
                }
                r.p += std::min<size_t> (len + (len & 1), (size_t) (r.end - r.p));
            }
        }

        if (! r.has (14) || ! std::equal (r.p, r.p + 4, "MThd"))
        {
            error = "This is not a standard MIDI file.";
            return nullptr;
        }
        r.p += 4;
        const auto headerLength = r.be (4);
        if (headerLength < 6 || ! r.has (headerLength))
        {
            error = "The MIDI file's header is damaged.";
            return nullptr;
        }
        auto song = std::make_shared<Song>();
        song->format = (int) r.be (2);
        const auto tracks = (int) r.be (2);
        const auto division = r.be (2);
        r.p += headerLength - 6;
        if (division & 0x8000)
        {
            // SMPTE: a negative frame rate (24, 25, 29 for 29.97 drop, 30) and ticks per frame.
            const auto fps = 256 - (int) (division >> 8);
            song->ticksPerQuarter = 0;
            song->ticksPerSecond = (fps == 29 ? 29.97 : (double) fps) * (double) (division & 0xFF);
            if (song->ticksPerSecond <= 0.0)
            {
                error = "The MIDI file's time division is damaged.";
                return nullptr;
            }
        }
        else
            song->ticksPerQuarter = std::max<int> (1, (int) division);

        struct Timed { Event e; int order; };
        std::vector<Timed> all;
        int order = 0;
        int64_t trackOffset = 0;   // type 2: each track plays after the one before

        for (int t = 0; t < tracks && r.has (8); ++t)
        {
            const bool isTrack = std::equal (r.p, r.p + 4, "MTrk");
            r.p += 4;
            const auto length = r.be (4);
            const auto* trackEnd = r.p + std::min<size_t> (length, (size_t) (r.end - r.p));
            if (! isTrack)
            {
                r.p = trackEnd;
                --t;
                continue;
            }
            Reader tr { r.p, trackEnd };
            r.p = trackEnd;

            int64_t tick = trackOffset;
            uint8_t running = 0;
            std::string name;
            while (tr.p < tr.end)
            {
                uint32_t delta = 0;
                if (! tr.vlq (delta) || tr.p >= tr.end)
                    break;
                tick += delta;
                auto status = *tr.p;
                if (status < 0x80)
                {
                    if (running == 0)
                        break;   // data with no status before it: the rest is unreadable
                    status = running;
                }
                else
                    ++tr.p;

                if (status == 0xFF)
                {
                    if (! tr.has (1))
                        break;
                    const auto type = *tr.p++;
                    uint32_t len = 0;
                    if (! tr.vlq (len) || ! tr.has (len))
                        break;
                    if (type == 0x51 && len == 3)
                    {
                        Tempo tempo;
                        tempo.tick = tick;
                        tempo.microsPerQuarter = std::max (1.0, (double) ((uint32_t) tr.p[0] << 16 | (uint32_t) tr.p[1] << 8 | tr.p[2]));
                        song->tempos.push_back (tempo);
                    }
                    else if (type == 0x03 && name.empty())
                        name.assign ((const char*) tr.p, len);
                    tr.p += len;
                    song->lastTick = std::max (song->lastTick, tick);
                    if (type == 0x2F)
                        break;
                    continue;
                }
                if (status == 0xF0 || status == 0xF7)
                {
                    uint32_t len = 0;
                    if (! tr.vlq (len) || ! tr.has (len))
                        break;
                    tr.p += len;
                    running = 0;
                    continue;
                }
                if (status >= 0xF0)
                    break;   // system messages do not belong in a file

                running = status;
                const auto n = dataBytes (status);
                if (! tr.has ((size_t) n))
                    break;
                Event e;
                e.tick = tick;
                e.status = status;
                e.data1 = tr.p[0] & 0x7F;
                e.data2 = n > 1 ? tr.p[1] & 0x7F : 0;
                tr.p += n;
                // A note-on at velocity 0 is a note-off.
                if (e.kind() == 0x90 && e.data2 == 0)
                    e.status = (uint8_t) (0x80 | e.channel());
                if (e.kind() == 0x90)
                    song->channelsUsed |= (uint16_t) (1u << e.channel());
                all.push_back ({ e, order++ });
                song->lastTick = std::max (song->lastTick, tick);
            }
            song->trackNames.push_back (name);
            if (song->format == 2)
                trackOffset = song->lastTick;
        }

        if (all.empty() && song->trackNames.empty())
        {
            error = "The MIDI file has no tracks.";
            return nullptr;
        }

        // In time order; at the same tick a note-off goes before a note-on, so a note
        // repeated with no gap is not cut by its own earlier end.
        std::stable_sort (all.begin(), all.end(), [] (const Timed& a, const Timed& b)
        {
            if (a.e.tick != b.e.tick)
                return a.e.tick < b.e.tick;
            const auto offA = a.e.kind() == 0x80, offB = b.e.kind() == 0x80;
            if (offA != offB)
                return offA;
            return a.order < b.order;
        });
        song->events.reserve (all.size());
        for (const auto& t : all)
            song->events.push_back (t.e);

        std::stable_sort (song->tempos.begin(), song->tempos.end(), [] (const Tempo& a, const Tempo& b) { return a.tick < b.tick; });
        if (song->tempos.empty() || song->tempos.front().tick > 0)
            song->tempos.insert (song->tempos.begin(), Tempo {});
        return song;
    }

    SongPtr read (const std::string& path, std::string& error)
    {
        std::ifstream in (path, std::ios::binary);
        if (! in)
        {
            error = "The file could not be opened.";
            return nullptr;
        }
        const std::vector<uint8_t> bytes ((std::istreambuf_iterator<char> (in)), std::istreambuf_iterator<char>());
        return parse (bytes.data(), bytes.size(), error);
    }
}

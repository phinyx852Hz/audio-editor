// SPDX-License-Identifier: AGPL-3.0-only
#include "MidiFile.h"

#include <algorithm>
#include <cmath>
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

namespace midifile
{
    namespace
    {
        void putVlq (std::vector<uint8_t>& out, uint32_t v)
        {
            uint8_t buffer[5];
            int n = 0;
            buffer[n++] = (uint8_t) (v & 0x7F);
            while ((v >>= 7) != 0)
                buffer[n++] = (uint8_t) ((v & 0x7F) | 0x80);
            while (n > 0)
                out.push_back (buffer[--n]);
        }

        void putBe (std::vector<uint8_t>& out, uint32_t v, int bytes)
        {
            for (int i = bytes - 1; i >= 0; --i)
                out.push_back ((uint8_t) (v >> (8 * i)));
        }

        struct Note
        {
            int64_t start = 0, end = 0;
            uint8_t channel = 0, pitch = 0, velocity = 0;
        };

        // The notes (each on with its off) and every other channel event.
        void split (const Song& song, std::vector<Note>& notes, std::vector<Event>& others)
        {
            std::vector<std::vector<size_t>> open (16 * 128);
            for (const auto& e : song.events)
            {
                const auto key = (size_t) e.channel() * 128 + (e.data1 & 0x7F);
                if (e.kind() == 0x90 && e.data2 > 0)
                {
                    open[key].push_back (notes.size());
                    notes.push_back ({ e.tick, -1, (uint8_t) e.channel(), (uint8_t) (e.data1 & 0x7F), e.data2 });
                }
                else if (e.kind() == 0x80 || e.kind() == 0x90)
                {
                    if (! open[key].empty())
                    {
                        notes[open[key].front()].end = e.tick;   // first on, first off
                        open[key].erase (open[key].begin());
                    }
                }
                else
                    others.push_back (e);
            }
            for (auto& n : notes)
                if (n.end < 0)
                    n.end = std::max (n.start + 1, song.lastTick);   // held to the end
        }

        SongPtr join (const Song& like, const std::vector<Note>& notes, std::vector<Event> others, int64_t lastTick)
        {
            auto out = std::make_shared<Song> (like);
            out->format = 0;
            out->events.clear();
            out->channelsUsed = 0;
            struct Timed { Event e; int order; };
            std::vector<Timed> all;
            for (const auto& e : others)
                all.push_back ({ e, 1 });
            for (const auto& n : notes)
            {
                all.push_back ({ { n.start, (uint8_t) (0x90 | n.channel), n.pitch, n.velocity }, 2 });
                all.push_back ({ { std::max (n.end, n.start + 1), (uint8_t) (0x80 | n.channel), n.pitch, 0 }, 0 });
                out->channelsUsed = (uint16_t) (out->channelsUsed | (1u << n.channel));
            }
            // At the same tick: note-offs, then the rest, then note-ons (as parse keeps them).
            std::stable_sort (all.begin(), all.end(), [] (const Timed& a, const Timed& b)
            {
                return a.e.tick != b.e.tick ? a.e.tick < b.e.tick : a.order < b.order;
            });
            for (const auto& t : all)
                out->events.push_back (t.e);
            out->lastTick = lastTick;
            for (const auto& e : out->events)
                out->lastTick = std::max (out->lastTick, e.tick);
            return out;
        }

        // Notes grouped by when they start (a chord is one group), in time order.
        std::vector<std::vector<Note>> groups (std::vector<Note> notes)
        {
            std::stable_sort (notes.begin(), notes.end(), [] (const Note& a, const Note& b) { return a.start < b.start; });
            std::vector<std::vector<Note>> out;
            for (const auto& n : notes)
            {
                if (out.empty() || out.back().front().start != n.start)
                    out.emplace_back();
                out.back().push_back (n);
            }
            return out;
        }

        constexpr double goldenRatio = 1.6180339887498949;
    }

    std::vector<uint8_t> bytes (const Song& song)
    {
        std::vector<uint8_t> out;
        if (song.ticksPerQuarter <= 0)
            return out;
        std::vector<uint8_t> track;
        struct Item { int64_t tick; int order; std::vector<uint8_t> data; };
        std::vector<Item> items;
        for (const auto& t : song.tempos)
        {
            const auto micros = (uint32_t) std::clamp (std::llround (t.microsPerQuarter), 1LL, 0xFFFFFFLL);
            items.push_back ({ t.tick, 0, { 0xFF, 0x51, 0x03, (uint8_t) (micros >> 16), (uint8_t) (micros >> 8), (uint8_t) micros } });
        }
        for (const auto& e : song.events)
        {
            Item item { e.tick, 1, { e.status, e.data1 } };
            if (e.kind() != 0xC0 && e.kind() != 0xD0)
                item.data.push_back (e.data2);
            items.push_back (std::move (item));
        }
        std::stable_sort (items.begin(), items.end(), [] (const Item& a, const Item& b)
        {
            return a.tick != b.tick ? a.tick < b.tick : a.order < b.order;
        });
        int64_t at = 0;
        for (const auto& item : items)
        {
            putVlq (track, (uint32_t) std::max<int64_t> (0, item.tick - at));
            at = std::max (at, item.tick);
            track.insert (track.end(), item.data.begin(), item.data.end());
        }
        putVlq (track, (uint32_t) std::max<int64_t> (0, song.lastTick - at));
        track.insert (track.end(), { 0xFF, 0x2F, 0x00 });

        out.insert (out.end(), { 'M', 'T', 'h', 'd' });
        putBe (out, 6, 4);
        putBe (out, 0, 2);
        putBe (out, 1, 2);
        putBe (out, (uint32_t) std::min (song.ticksPerQuarter, 0x7FFF), 2);
        out.insert (out.end(), { 'M', 'T', 'r', 'k' });
        putBe (out, (uint32_t) track.size(), 4);
        out.insert (out.end(), track.begin(), track.end());
        return out;
    }

    bool write (const Song& song, const std::string& path, std::string& error)
    {
        const auto data = bytes (song);
        if (data.empty())
        {
            error = "A MIDI file in SMPTE time cannot be written back.";
            return false;
        }
        std::ofstream file (path, std::ios::binary);
        file.write ((const char*) data.data(), (std::streamsize) data.size());
        if (! file)
        {
            error = "The MIDI file could not be written.";
            return false;
        }
        return true;
    }

    int64_t span (const Song& song)
    {
        if (song.ticksPerQuarter <= 0)
            return std::max<int64_t> (1, song.lastTick);
        const int64_t bar = 4 * (int64_t) song.ticksPerQuarter;
        return std::max<int64_t> (bar, (song.lastTick + bar - 1) / bar * bar);
    }

    SongPtr reversed (const Song& song)
    {
        std::vector<Note> notes;
        std::vector<Event> others;
        split (song, notes, others);
        const auto length = song.lastTick;
        for (auto& n : notes)
        {
            const auto start = std::max<int64_t> (0, length - n.end), end = std::max<int64_t> (0, length - n.start);
            n.start = start;
            n.end = end;
        }
        for (auto& e : others)
            if (e.tick != 0)
                e.tick = std::max<int64_t> (0, length - e.tick);
        return join (song, notes, std::move (others), length);
    }

    SongPtr phiShuffle (const Song& song, int turn)
    {
        std::vector<Note> notes;
        std::vector<Event> others;
        split (song, notes, others);
        auto chords = groups (notes);
        const auto count = chords.size();
        const auto length = span (song);
        if (count < 2)
            return join (song, notes, std::move (others), length);

        std::vector<int64_t> starts;
        for (const auto& c : chords)
            starts.push_back (c.front().start);
        // A golden walk: chord i goes where its key falls among all the keys, each turn starting further on.
        std::vector<size_t> order (count);
        for (size_t i = 0; i < count; ++i)
            order[i] = i;
        const auto offset = std::fmod ((double) (turn + 1) * goldenRatio * goldenRatio, 1.0);
        auto key = [&] (size_t i) { return std::fmod (offset + (double) (i + 1) * goldenRatio, 1.0); };
        std::stable_sort (order.begin(), order.end(), [&] (size_t a, size_t b) { return key (a) < key (b); });
        bool same = true;
        for (size_t i = 0; i < count; ++i)
            same = same && order[i] == i;
        if (same)
            std::rotate (order.begin(), order.begin() + 1, order.end());   // never as it was

        std::vector<Note> out;
        for (size_t slot = 0; slot < count; ++slot)
            for (auto n : chords[order[slot]])
            {
                const auto length0 = n.end - n.start;
                n.start = starts[slot];
                n.end = std::min (n.start + length0, length);
                out.push_back (n);
            }
        return join (song, out, std::move (others), length);
    }

    SongPtr duplicated (const Song& song)
    {
        std::vector<Note> notes;
        std::vector<Event> others;
        split (song, notes, others);
        const auto length = span (song);
        const auto n = notes.size();
        for (size_t i = 0; i < n; ++i)
        {
            auto copy = notes[i];
            copy.start += length;
            copy.end += length;
            notes.push_back (copy);
        }
        const auto m = others.size();
        for (size_t i = 0; i < m; ++i)
        {
            auto copy = others[i];
            copy.tick += length;
            others.push_back (copy);
        }
        return join (song, notes, std::move (others), 2 * length);
    }

    SongPtr halfDensity (const Song& song)
    {
        std::vector<Note> notes;
        std::vector<Event> others;
        split (song, notes, others);
        const auto chords = groups (notes);
        std::vector<Note> kept;
        for (size_t i = 0; i < chords.size(); i += 2)
            kept.insert (kept.end(), chords[i].begin(), chords[i].end());
        return join (song, kept, std::move (others), span (song));
    }

    SongPtr doubleDensity (const Song& song)
    {
        std::vector<Note> notes;
        std::vector<Event> others;
        split (song, notes, others);
        std::vector<Note> out;
        for (const auto& n : notes)
        {
            const auto half = (n.end - n.start) / 2;
            if (half < 1)
            {
                out.push_back (n);
                continue;
            }
            auto first = n, second = n;
            first.end = n.start + half;
            second.start = n.start + half;
            out.push_back (first);
            out.push_back (second);
        }
        return join (song, out, std::move (others), span (song));
    }
}

namespace midifile
{
    const std::vector<Scale>& scales()
    {
        auto bits = [] (std::initializer_list<int> degrees) { uint16_t m = 0; for (auto d : degrees) m = (uint16_t) (m | (1u << d)); return m; };
        static const std::vector<Scale> all {
            { "Ionian (Major)", "Mode", bits ({ 0, 2, 4, 5, 7, 9, 11 }) },
            { "Dorian", "Mode", bits ({ 0, 2, 3, 5, 7, 9, 10 }) },
            { "Phrygian", "Mode", bits ({ 0, 1, 3, 5, 7, 8, 10 }) },
            { "Lydian", "Mode", bits ({ 0, 2, 4, 6, 7, 9, 11 }) },
            { "Mixolydian", "Mode", bits ({ 0, 2, 4, 5, 7, 9, 10 }) },
            { "Aeolian (Minor)", "Mode", bits ({ 0, 2, 3, 5, 7, 8, 10 }) },
            { "Locrian", "Mode", bits ({ 0, 1, 3, 5, 6, 8, 10 }) },
            // The ten thaats, by their best-known raga where it is named after one.
            { "Bilawal", "Raga", bits ({ 0, 2, 4, 5, 7, 9, 11 }) },
            { "Yaman (Kalyan)", "Raga", bits ({ 0, 2, 4, 6, 7, 9, 11 }) },
            { "Khamaj", "Raga", bits ({ 0, 2, 4, 5, 7, 9, 10 }) },
            { "Kafi", "Raga", bits ({ 0, 2, 3, 5, 7, 9, 10 }) },
            { "Asavari", "Raga", bits ({ 0, 2, 3, 5, 7, 8, 10 }) },
            { "Bhairavi", "Raga", bits ({ 0, 1, 3, 5, 7, 8, 10 }) },
            { "Bhairav", "Raga", bits ({ 0, 1, 4, 5, 7, 8, 11 }) },
            { "Marwa", "Raga", bits ({ 0, 1, 4, 6, 9, 11 }) },
            { "Purvi", "Raga", bits ({ 0, 1, 4, 6, 7, 8, 11 }) },
            { "Todi", "Raga", bits ({ 0, 1, 3, 6, 7, 8, 11 }) },
            // The major ragas besides.
            { "Darbari Kanada", "Raga", bits ({ 0, 2, 3, 5, 7, 8, 10 }) },
            { "Jaunpuri", "Raga", bits ({ 0, 2, 3, 5, 7, 8, 10 }) },
            { "Bageshri", "Raga", bits ({ 0, 2, 3, 5, 7, 9, 10 }) },
            { "Bhimpalasi", "Raga", bits ({ 0, 2, 3, 5, 7, 9, 10 }) },
            { "Desh", "Raga", bits ({ 0, 2, 4, 5, 7, 9, 10, 11 }) },
            { "Bihag", "Raga", bits ({ 0, 2, 4, 5, 6, 7, 9, 11 }) },
            { "Kedar", "Raga", bits ({ 0, 2, 4, 5, 6, 7, 9, 11 }) },
            { "Miyan ki Malhar", "Raga", bits ({ 0, 2, 3, 5, 7, 9, 10, 11 }) },
            { "Megh", "Raga", bits ({ 0, 2, 5, 7, 10 }) },
            { "Bhupali", "Raga", bits ({ 0, 2, 4, 7, 9 }) },
            { "Durga", "Raga", bits ({ 0, 2, 5, 7, 9 }) },
            { "Hamsadhwani", "Raga", bits ({ 0, 2, 4, 7, 11 }) },
            { "Malkauns", "Raga", bits ({ 0, 3, 5, 8, 10 }) },
            { "Jog", "Raga", bits ({ 0, 3, 4, 5, 7, 10 }) },
            { "Ahir Bhairav", "Raga", bits ({ 0, 1, 4, 5, 7, 9, 10 }) },
            { "Lalit", "Raga", bits ({ 0, 1, 4, 5, 6, 8, 11 }) },
            { "Puriya Dhanashri", "Raga", bits ({ 0, 1, 4, 6, 7, 8, 11 }) },
            { "Shree", "Raga", bits ({ 0, 1, 4, 6, 7, 8, 11 }) },
            { "Multani", "Raga", bits ({ 0, 1, 3, 6, 7, 8, 11 }) },
            { "Charukeshi", "Raga", bits ({ 0, 2, 4, 5, 7, 8, 10 }) },
            // Gamelan laras and pathet on the keys their tones round to (mixer/Tunings.cpp has them
            // in their own cents); sléndro's three pathet all fall on the same keys.
            { "sléndro", "Gamelan", bits ({ 0, 2, 5, 7, 10 }) },
            { "pélog", "Gamelan", bits ({ 0, 1, 3, 5, 7, 8, 9 }) },
            { "pélog pathet lima", "Gamelan", bits ({ 0, 1, 3, 7, 8 }) },
            { "pélog pathet nem", "Gamelan", bits ({ 0, 4, 5, 7, 11 }) },
            { "pélog pathet barang", "Gamelan", bits ({ 0, 1, 6, 7, 8 }) },
            { "selisir", "Gamelan", bits ({ 0, 1, 3, 7, 8 }) },
            { "degung", "Gamelan", bits ({ 0, 1, 3, 7, 8 }) },
            // Shona mbira tunings (mixer/Tunings.cpp has their cents), as the keys they play on; Shona
            // names in lower case, as the tunings have them (owner).
            { "nyamaropa", "Mbira", bits ({ 0, 2, 4, 5, 7, 9, 10 }) },
            { "dambatsoko", "Mbira", bits ({ 0, 2, 4, 5, 7, 9, 11 }) },
            { "katsanzaira", "Mbira", bits ({ 0, 2, 3, 5, 7, 9, 10 }) },
            { "mavembe (gandanga)", "Mbira", bits ({ 0, 1, 3, 5, 7, 8, 10 }) },
            { "dindinyongwe", "Mbira", bits ({ 0, 1, 3, 5, 6, 8, 10 }) },
            { "nyunga nyunga", "Mbira", bits ({ 0, 2, 4, 7, 9, 11 }) },
            { "matepe", "Mbira", bits ({ 0, 2, 4, 5, 7, 8, 10 }) },
            // The five Chinese modes (wusheng), Japanese scales and Korean modes, as in mixer/Tunings.cpp.
            { "gōng", "Chinese", bits ({ 0, 2, 4, 7, 9 }) },
            { "shāng", "Chinese", bits ({ 0, 2, 5, 7, 10 }) },
            { "jué", "Chinese", bits ({ 0, 3, 5, 8, 10 }) },
            { "zhǐ", "Chinese", bits ({ 0, 2, 5, 7, 9 }) },
            { "yǔ", "Chinese", bits ({ 0, 3, 5, 7, 10 }) },
            { "miyako-bushi (in)", "Japanese", bits ({ 0, 1, 5, 7, 8 }) },
            { "ritsu (yō)", "Japanese", bits ({ 0, 2, 5, 7, 9 }) },
            { "min'yō", "Japanese", bits ({ 0, 3, 5, 7, 10 }) },
            { "ryūkyū", "Japanese", bits ({ 0, 4, 5, 7, 11 }) },
            { "hirajōshi", "Japanese", bits ({ 0, 2, 3, 7, 8 }) },
            { "iwato", "Japanese", bits ({ 0, 1, 5, 6, 10 }) },
            { "p'yŏngjo", "Korean", bits ({ 0, 2, 5, 7, 9 }) },
            { "kyemyŏnjo", "Korean", bits ({ 0, 3, 5, 7, 10 }) },
            { "kyŏngt'ori", "Korean", bits ({ 0, 2, 5, 7, 9 }) },
            { "menarit'ori", "Korean", bits ({ 0, 3, 5, 8, 10 }) },
            { "yukchabaegit'ori", "Korean", bits ({ 0, 5, 7, 8 }) },
            { "susimgat'ori", "Korean", bits ({ 0, 2, 5, 7, 10 }) },
        };
        return all;
    }

    SongPtr toMode (const Song& song, int root, int scale)
    {
        const auto& list = scales();
        const auto notesOf = list[(size_t) std::clamp (scale, 0, (int) list.size() - 1)].notes;
        root = ((root % 12) + 12) % 12;
        bool in[12] {};
        for (int d = 0; d < 12; ++d)
            if (notesOf & (1u << d))
                in[(d + root) % 12] = true;
        std::vector<Note> notes;
        std::vector<Event> others;
        split (song, notes, others);
        for (auto& n : notes)
        {
            if (n.channel == 9)
                continue;
            for (int step = 0; step < 12; ++step)
            {
                if (n.pitch - step >= 0 && in[(n.pitch - step) % 12])
                {
                    n.pitch = (uint8_t) (n.pitch - step);
                    break;
                }
                if (n.pitch + step <= 127 && in[(n.pitch + step) % 12])
                {
                    n.pitch = (uint8_t) (n.pitch + step);
                    break;
                }
            }
        }
        return join (song, notes, std::move (others), song.lastTick);
    }
}

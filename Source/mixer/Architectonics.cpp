// SPDX-License-Identifier: AGPL-3.0-only
#include "Architectonics.h"
#include "Rhythms.h"

#include <algorithm>
#include <cmath>
#include <map>

namespace architectonics
{
    namespace
    {
        double frac (double x) { return x - std::floor (x); }

        // Steps of 1/φ from the seed: the most evenly spread sequence there is (Weyl's), as the
        // Φ random selector walks its favourites.
        struct Walk
        {
            double x;
            double next() { return x = frac (x + 1.0 / phi); }
        };

        // A length cut by the golden ratio: the longer part round (len/φ), so a Fibonacci number
        // splits into the two before it (144 into 89 and 55).
        std::pair<int, int> golden (int len)
        {
            const auto major = std::clamp ((int) std::lround (len / phi), 1, len - 1);
            return { major, len - major };
        }

        // `len` bars from `start` into sections no longer than `longest`, each cut's longer part
        // first or second as the walk says; and then, now and then (variety), cut once more.
        void cut (int start, int len, int longest, double variety, Walk& walk, std::vector<Section>& out)
        {
            if (len > longest && len >= 2)
            {
                const auto [major, minor] = golden (len);
                const bool majorFirst = walk.next() < 1.0 / phi;
                cut (start, majorFirst ? major : minor, longest, variety, walk, out);
                cut (start + (majorFirst ? major : minor), majorFirst ? minor : major, longest, variety, walk, out);
                return;
            }
            if (len >= 3 && walk.next() < variety)
            {
                const auto [major, minor] = golden (len);
                const bool majorFirst = walk.next() < 1.0 / phi;
                out.push_back ({ 0, start, majorFirst ? major : minor });
                out.push_back ({ 0, start + (majorFirst ? major : minor), majorFirst ? minor : major });
                return;
            }
            out.push_back ({ 0, start, len });
        }

        // The arch (Kurth's energy, rising and spending itself): from a quarter to the full at the
        // climax, held near it through the climax, then falling to near nothing at the end.
        double arch (double bar, int climax, int dissolution, int bars)
        {
            if (bar < climax)
                return 0.25 + 0.75 * bar / std::max (1, climax);
            if (bar < dissolution)
                return 1.0 - 0.1 * (bar - climax) / std::max (1, dissolution - climax);
            return 0.9 - 0.75 * (bar - dissolution) / std::max (1, bars - dissolution);
        }
    }

    std::vector<int> fibonacci (int most)
    {
        std::vector<int> out;
        for (int a = 1, b = 2; a <= most; )
        {
            out.push_back (a);
            const auto c = a + b;
            a = b;
            b = c;
        }
        return out;
    }

    bool isFibonacci (int n)
    {
        const auto all = fibonacci (n);
        return ! all.empty() && all.back() == n;
    }

    Plan plan (const Settings& s)
    {
        Plan p;
        std::vector<int> scenes;   // those with clips
        for (size_t i = 0; i < s.clips.size(); ++i)
            if (s.clips[i] > 0)
                scenes.push_back ((int) i);
        if (scenes.empty() || s.bars < 2)
            return p;

        p.bars = s.bars;
        p.climaxScene = s.climaxScene >= 0 && s.climaxScene < (int) s.clips.size() && s.clips[(size_t) s.climaxScene] > 0
                            ? s.climaxScene
                            : *std::max_element (scenes.begin(), scenes.end(), [&] (int a, int b)
                              {
                                  return std::pair (s.clips[(size_t) a], a) < std::pair (s.clips[(size_t) b], b);
                              });

        // The first cut is the golden section, and the climax: the longer part first, unless early.
        Walk walk { frac (s.seed) };
        const auto longest = std::max (1, s.longest);
        const auto variety = std::clamp (s.variety, 0.0, 1.0);
        const auto [major, minor] = golden (s.bars);
        p.climaxBar = s.earlyClimax ? minor : major;
        cut (0, p.climaxBar, longest, variety, walk, p.sections);
        const auto climaxIndex = p.sections.size();
        cut (p.climaxBar, s.bars - p.climaxBar, longest, variety, walk, p.sections);
        p.dissolutionBar = p.climaxBar + (int) std::lround ((s.bars - p.climaxBar) / phi);

        const auto emergence = p.climaxBar / (phi * phi);
        double low = 1.0, high = 0.0;
        for (size_t i = 0; i < p.sections.size(); ++i)
        {
            auto& section = p.sections[i];
            section.climax = i == climaxIndex;
            section.function = section.start >= p.dissolutionBar && ! section.climax ? Function::dissolution
                             : section.start >= p.climaxBar ? Function::climax
                             : i + 1 == climaxIndex && climaxIndex >= 3 ? Function::disruption
                             : section.start < emergence || i == 0 ? Function::emergence
                             : Function::expansion;
            // The disruption is a contrast: the energy drops back before the climax.
            section.energy = section.climax ? 1.0
                           : section.function == Function::disruption ? 0.5
                           : arch (section.start + section.bars * 0.5, p.climaxBar, p.dissolutionBar, s.bars);
            if (! section.climax)
            {
                low = std::min (low, section.energy);
                high = std::max (high, section.energy);
            }
        }

        // The other scenes: by fullness for the arch, else in the launcher's order.
        std::vector<int> others;
        for (auto i : scenes)
            if (i != p.climaxScene)
                others.push_back (i);
        if (s.order == Order::arch)
            std::stable_sort (others.begin(), others.end(), [&] (int a, int b) { return s.clips[(size_t) a] < s.clips[(size_t) b]; });
        const auto m = (int) others.size();
        const auto offset = (int) std::floor (frac (s.seed) * std::max (1, m));
        int f0 = 0, f1 = 1;   // F(k) and F(k+1), mod m
        int k = 0, last = -1;
        for (auto& section : p.sections)
        {
            if (section.climax || m == 0)
            {
                section.scene = p.climaxScene;
                last = -1;
                continue;
            }
            ++k;
            int at = 0;
            switch (s.order)
            {
                case Order::arch:
                {
                    const auto e = high > low ? (section.energy - low) / (high - low) : 0.5;
                    const auto w = walk.next();   // now and then a step off the arch
                    at = (int) std::lround (e * (m - 1)) + (w < 0.19 ? -1 : w > 0.81 ? 1 : 0);
                    break;
                }
                case Order::fibonacci:
                {
                    const auto f2 = (f0 + f1) % m;
                    f0 = f1;
                    f1 = f2;
                    at = (offset + f0) % m;
                    break;
                }
                case Order::phiWalk:
                    at = (int) std::floor (m * frac (s.seed + k / phi));
                    break;
            }
            at = std::clamp (at, 0, m - 1);
            if (m > 1 && at == last)   // never the same scene twice running
                at = at == m - 1 ? at - 1 : at + 1;
            section.scene = others[(size_t) at];
            last = at;
        }
        return p;
    }

    const char* functionName (Function f)
    {
        switch (f)
        {
            case Function::emergence:   return "emergence";
            case Function::expansion:   return "expansion";
            case Function::disruption:  return "disruption";
            case Function::climax:      return "climax";
            case Function::dissolution: return "dissolution";
        }
        return "";
    }

    int sectionAt (const Plan& p, double bar)
    {
        if (bar < 0.0)
            return -1;
        for (size_t i = 0; i < p.sections.size(); ++i)
            if (bar < p.sections[i].start + p.sections[i].bars)
                return (int) i;
        return -1;
    }

    // A step of √7, as the Φ random selector's second colour walks: not 1/φ, which would only
    // shift the last set's walk along by one.
    double nextSeed (double seed) { return frac (seed + std::sqrt (7.0)); }

    const char* roleName (Role r)
    {
        switch (r)
        {
            case Role::sound:    return "a sound from the Selector";
            case Role::rhythm:   return "rhythm";
            case Role::bass:     return "bass";
            case Role::chords:   return "chords";
            case Role::arpeggio: return "arpeggio";
            case Role::melody:   return "melody";
        }
        return "";
    }

    namespace
    {
        constexpr int tpq = 480;

        // The mode's degrees above the final, in semitones; and degree d (any whole number) as
        // semitones, an octave for every pass round the mode.
        struct Scale
        {
            std::vector<int> steps;
            explicit Scale (uint16_t keys)
            {
                for (int k = 0; k < 12; ++k)
                    if ((keys & (1u << k)) != 0)
                        steps.push_back (k);
                if (steps.empty() || steps.front() != 0)
                    steps.insert (steps.begin(), 0);
            }
            int size() const { return (int) steps.size(); }
            int pitch (int degree) const
            {
                const auto n = size();
                const auto octave = (int) std::floor ((double) degree / n);
                return 12 * octave + steps[(size_t) (degree - octave * n)];
            }
            // The chord on a degree: every other note of the mode, as a triad's thirds are.
            std::vector<int> chord (int root, int notes) const
            {
                std::vector<int> out;
                for (int i = 0; i < notes; ++i)
                    out.push_back (pitch (root + 2 * i));
                return out;
            }
        };

        // A song being written: notes on a channel, then made a type 0 file a loop long.
        struct Writer
        {
            struct Timed { int64_t tick; int order; midifile::Event e; };
            std::vector<Timed> events;
            uint16_t channels = 0;
            void note (double beat, double beats, int key, int velocity, int channel)
            {
                const auto start = (int64_t) std::llround (beat * tpq);
                const auto end = std::max (start + 1, (int64_t) std::llround ((beat + beats) * tpq) - 1);
                const auto k = (uint8_t) std::clamp (key, 0, 127);
                const auto v = (uint8_t) std::clamp (velocity, 1, 127);
                events.push_back ({ start, 1, { start, (uint8_t) (0x90 | channel), k, v } });
                events.push_back ({ end, 0, { end, (uint8_t) (0x80 | channel), k, 0 } });
                channels |= (uint16_t) (1u << channel);
            }
            midifile::SongPtr song (const std::string& name, double bpm, double lengthBeats)
            {
                auto out = std::make_shared<midifile::Song>();
                out->format = 0;
                out->ticksPerQuarter = tpq;
                out->tempos.push_back ({ 0, 60.0e6 / std::max (1.0, bpm) });
                out->trackNames.push_back (name);
                std::stable_sort (events.begin(), events.end(), [] (const Timed& a, const Timed& b)
                {
                    return a.tick != b.tick ? a.tick < b.tick : a.order < b.order;
                });
                for (const auto& t : events)
                    out->events.push_back (t.e);
                out->channelsUsed = channels;
                out->lastTick = (int64_t) std::llround (lengthBeats * tpq);
                return out;
            }
        };

        // Each part's progressions, a degree a bar (0 the final, 3 the fourth, 4 the fifth): the
        // seed picks one for the part, and the part keeps it.
        const std::vector<std::vector<int>>& progressions (Function f)
        {
            static const std::map<Function, std::vector<std::vector<int>>> all {
                { Function::emergence,   { { 0, 0, 0, 0 }, { 0, 0, 3, 0 }, { 0, 5, 0, 0 } } },
                { Function::expansion,   { { 0, 3, 4, 0 }, { 0, 5, 3, 4 }, { 0, 4, 5, 3 }, { 0, 2, 3, 4 } } },
                { Function::disruption,  { { 5, 1, 5, 4 }, { 1, 6, 1, 4 }, { 2, 5, 1, 4 } } },
                { Function::climax,      { { 0, 5, 3, 4 }, { 3, 4, 5, 0 }, { 0, 3, 5, 4 } } },
                { Function::dissolution, { { 3, 0, 3, 0 }, { 5, 3, 0, 0 }, { 3, 3, 0, 0 } } } };
            return all.at (f);
        }

        // Who enters first as the energy rises: the harmony's ground, then the beat, then the
        // figures and the line, then the Selector's sounds.
        int entry (Role r)
        {
            switch (r)
            {
                case Role::chords:   return 0;
                case Role::bass:     return 1;
                case Role::rhythm:   return 2;
                case Role::arpeggio: return 3;
                case Role::melody:   return 4;
                case Role::sound:    return 5;
            }
            return 5;
        }

        double density (const rhythm::Rhythm& r) { return (double) rhythm::hits (r).size() / std::max (1, r.beats); }
    }

    Song compose (const SongSettings& ss)
    {
        Song song;
        auto form = ss.form;
        form.clips = { 1 };
        form.climaxScene = 0;
        song.plan = plan (form);
        if (song.plan.sections.empty())
            return song;
        for (size_t i = 0; i < song.plan.sections.size(); ++i)
            song.plan.sections[i].scene = (int) i;
        song.plan.climaxScene = -1;
        for (const auto& x : song.plan.sections)
            if (x.climax)
                song.plan.climaxScene = x.scene;

        const Scale scale (ss.keys);
        const auto bar = std::max (1, ss.beatsPerBar);
        const auto loop = std::max (1, ss.loopBars);
        const auto length = (double) loop * bar;
        const auto root = 48 + std::clamp (ss.final, 0, 11);   // the final in the octave below middle c

        // The colours that can play, in the order they enter.
        std::vector<int> order;
        for (int c = 0; c < colours; ++c)
            if (ss.present[(size_t) c])
                order.push_back (c);
        Walk walk { frac (form.seed + 1.0 / (phi * phi)) };
        std::stable_sort (order.begin(), order.end(), [&] (int a, int b) { return entry (ss.roles[(size_t) a]) < entry (ss.roles[(size_t) b]); });

        // The rhythms by how many hits a beat; those that go round in whole bars and fit the loop
        // a whole number of times, or the loop fits them (a cycle of eight bars over a loop of four).
        std::vector<const rhythm::Rhythm*> pool;
        const auto loopBeats = loop * bar;
        for (const auto* r : ss.rhythms)
            if (r != nullptr && r->beats > 0 && r->beats % bar == 0 && (loopBeats % r->beats == 0 || r->beats % loopBeats == 0))
                pool.push_back (r);
        std::stable_sort (pool.begin(), pool.end(), [] (const rhythm::Rhythm* a, const rhythm::Rhythm* b) { return density (*a) < density (*b); });

        // Each part's progression and motif, chosen once: the material that recurs.
        std::map<Function, std::vector<int>> progression;
        std::map<Function, std::vector<int>> motif;   // degrees above the chord's root, an eighth each; -100 a rest
        std::map<Function, int> visits;
        for (auto f : { Function::emergence, Function::expansion, Function::disruption, Function::climax, Function::dissolution })
        {
            const auto& list = progressions (f);
            progression[f] = list[(size_t) std::floor (walk.next() * (double) list.size())];
            std::vector<int> m;
            int at = 0;
            for (int e = 0; e < 2 * bar; ++e)
            {
                const auto w = walk.next();
                if (e > 0 && w < 0.3)
                {
                    m.push_back (-100);
                    continue;
                }
                at += w < 0.5 ? 1 : w < 0.7 ? -1 : w < 0.85 ? 2 : -2;   // mostly steps, now and then a leap
                at = std::clamp (at, -3, 6);
                m.push_back (e == 0 ? 0 : at);
            }
            motif[f] = m;
        }

        // Each part's rhythm, as dense as the part's energy (the disruption a contrast: as sparse
        // as its energy is high), kept for the whole part as its progression is.
        std::map<Function, const rhythm::Rhythm*> groove;
        if (! pool.empty())
        {
            std::map<Function, std::pair<double, int>> energy;
            for (const auto& x : song.plan.sections)
            {
                energy[x.function].first += x.energy * x.bars;
                energy[x.function].second += x.bars;
            }
            for (const auto& [f, sum] : energy)
            {
                const auto mean = sum.first / std::max (1, sum.second);
                const auto target = f == Function::disruption ? 1.0 - mean : mean;
                const auto j = walk.next();
                const auto at = (int) std::lround (target * (double) (pool.size() - 1)) + (j < 0.19 ? -1 : j > 0.81 ? 1 : 0);
                groove[f] = pool[(size_t) std::clamp (at, 0, (int) pool.size() - 1)];
            }
        }

        for (size_t i = 0; i < song.plan.sections.size(); ++i)
        {
            const auto& x = song.plan.sections[i];
            Scene sc;
            sc.section = (int) i;
            sc.function = x.function;
            sc.energy = x.energy;
            const auto e = x.energy;
            const auto visit = visits[x.function]++;

            // Who plays: as many as the energy calls for, all of them at the climax.
            const auto playing = x.climax ? (int) order.size() : std::max (1, (int) std::ceil (e * order.size() - 1.0e-9));
            for (int k = 0; k < (int) order.size() && k < playing; ++k)
                sc.plays[(size_t) order[(size_t) k]] = true;

            // The part's progression, moved on a chord each time the part comes back.
            const auto& prog = progression[x.function];
            for (int b = 0; b < loop; ++b)
                sc.chords.push_back (prog[(size_t) ((b + visit) % (int) prog.size())]);
            if (x.function == Function::disruption || (i + 1 < song.plan.sections.size() && song.plan.sections[i + 1].climax))
                sc.chords.back() = 4;   // onto the dominant, before the climax
            if (i + 1 == song.plan.sections.size())
                sc.chords.back() = 0;   // and home at the end

            const auto velocity = (int) std::lround (56 + 56 * e);
            const auto lift = x.function == Function::climax ? 7 : x.function == Function::emergence || x.function == Function::dissolution ? -2 : 0;
            for (int c = 0; c < colours; ++c)
            {
                const auto role = ss.roles[(size_t) c];
                if (! sc.plays[(size_t) c] || role == Role::sound)
                    continue;
                Writer w;
                std::string name = std::string (functionName (x.function)) + " " + roleName (role);
                switch (role)
                {
                    case Role::rhythm:
                    {
                        if (pool.empty())
                        {
                            // Four on the floor, the hats as dense as the energy.
                            for (int beat = 0; beat < (int) length; ++beat)
                            {
                                w.note (beat, 0.25, 36, velocity, 9);
                                if (e > 0.45 && beat % 2 == 1)
                                    w.note (beat, 0.25, 38, velocity, 9);
                                const auto per = e > 0.75 ? 4 : e > 0.4 ? 2 : 1;
                                for (int h = 0; h < per; ++h)
                                    w.note (beat + (double) h / per + (per == 1 ? 0.5 : 0.0), 0.125, 42, velocity - 20, 9);
                            }
                            sc.rhythm = "four on the floor";
                            sc.midi[(size_t) c] = w.song (name, ss.bpm, length);
                            continue;
                        }
                        const auto& r = *groove[x.function];
                        const auto cycles = std::max (1, loopBeats / r.beats);
                        sc.rhythm = r.name;
                        auto made = rhythm::toSong (r, cycles, ss.bpm);
                        if (made != nullptr)
                        {
                            auto copy = std::make_shared<midifile::Song> (*made);
                            copy->trackNames = { name + ": " + r.name };
                            copy->lastTick = (int64_t) cycles * r.beats * tpq;   // whole cycles, so whole bars
                            sc.midi[(size_t) c] = copy;
                        }
                        continue;
                    }
                    case Role::bass:
                        for (int b = 0; b < loop; ++b)
                        {
                            const auto chord = scale.chord (sc.chords[(size_t) b], 3);
                            const auto low = root - 12 + chord[0];
                            const auto per = e < 0.4 ? 1 : e < 0.7 ? 2 : 2 * bar;   // whole bars, halves, eighths
                            const auto step = (double) bar / per;
                            for (int n = 0; n < per; ++n)
                            {
                                const auto key = per > 2 && n % 4 == 2 ? low + (chord[2] - chord[0]) : per > 2 && n % 4 == 3 ? low + 12 : low;
                                w.note (b * bar + n * step, step * 0.9, key, velocity, 0);
                            }
                        }
                        break;
                    case Role::chords:
                        for (int b = 0; b < loop; ++b)
                        {
                            const auto chord = scale.chord (sc.chords[(size_t) b], e > 0.8 ? 4 : 3);
                            const auto strikes = e > 0.8 ? 2 : 1;
                            for (int s = 0; s < strikes; ++s)
                                for (auto p : chord)
                                    w.note (b * bar + s * (double) bar / strikes, (double) bar / strikes, root + p, velocity - 10, 0);
                        }
                        break;
                    case Role::arpeggio:
                    {
                        const auto per = e < 0.5 ? 2 : 4;
                        for (int b = 0; b < loop; ++b)
                        {
                            const auto chord = scale.chord (sc.chords[(size_t) b], 3);
                            std::vector<int> up;
                            for (int o = 0; o < 2; ++o)
                                for (auto p : chord)
                                    up.push_back (root + 12 + p + 12 * o);
                            const auto figure = (int) up.size();
                            for (int n = 0; n < bar * per; ++n)
                            {
                                const auto k = n % (2 * figure - 2);   // up and down again
                                w.note (b * bar + (double) n / per, 1.0 / per, up[(size_t) (k < figure ? k : 2 * figure - 2 - k)], velocity - 8, 0);
                            }
                        }
                        break;
                    }
                    case Role::melody:
                    {
                        // The part's motif on each bar's chord (Caplin's basic idea, repeated and
                        // sequenced), sparser as the energy falls; the loop's last note held, a cadence.
                        const auto& m = motif[x.function];
                        for (int b = 0; b < loop; ++b)
                        {
                            const auto chordRoot = sc.chords[(size_t) b];
                            for (int n = 0; n < (int) m.size(); ++n)
                            {
                                if (m[(size_t) n] == -100 || (n % 2 == 1 && walk.next() > e + 0.2))
                                    continue;
                                const auto last = b == loop - 1 && n == (int) m.size() - 1;
                                const auto degree = last ? chordRoot + scale.size() : chordRoot + m[(size_t) n] + lift;
                                w.note (b * bar + n * 0.5, last ? 0.5 : 0.45, root + 12 + scale.pitch (degree), velocity, 0);
                            }
                        }
                        break;
                    }
                    case Role::sound:
                        break;
                }
                sc.midi[(size_t) c] = w.song (name, ss.bpm, length);
            }
            song.scenes.push_back (std::move (sc));
        }
        return song;
    }
}

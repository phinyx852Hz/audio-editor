// SPDX-License-Identifier: AGPL-3.0-only
#include "Minimalize.h"

#include <algorithm>
#include <cmath>
#include <numeric>
#include <random>

namespace minimalize
{
    namespace
    {
        constexpr int64_t bar = (int64_t) ticksPerQuarter * 4;
        constexpr int64_t eighth = ticksPerQuarter / 2;

        // A stage: a cell played `repeats` times. A plan is its stages; the last one holds.
        using Plan = std::vector<Cell>;

        int64_t roundUp (int64_t t, int64_t to) { return to <= 0 ? t : (t + to - 1) / to * to; }

        int64_t snapped (int64_t t, int64_t step) { return step <= 0 ? t : std::max (step, (t + step / 2) / step * step); }

        // The cell's notes in groups that start together (chords stay whole), in time order.
        std::vector<Notes> groups (const Cell& c)
        {
            auto sorted = c.notes;
            std::stable_sort (sorted.begin(), sorted.end(), [] (const Note& a, const Note& b) { return a.start < b.start; });
            std::vector<Notes> out;
            for (const auto& n : sorted)
            {
                if (out.empty() || out.back().front().start != n.start)
                    out.emplace_back();
                out.back().push_back (n);
            }
            return out;
        }

        // Turned round in its loop: each note `shift` later (earlier when negative), wrapping.
        Cell rotated (const Cell& c, int64_t shift)
        {
            Cell out { {}, c.length };
            if (c.length <= 0)
                return out;
            for (auto n : c.notes)
            {
                const auto length = std::min (n.end - n.start, c.length);
                n.start = ((n.start + shift) % c.length + c.length) % c.length;
                n.end = n.start + std::max<int64_t> (1, length);
                out.notes.push_back (n);
            }
            return out;
        }

        Plan holdPlan (const Cell& c) { return { c }; }

        // Reich: the copy a step ahead each stage, until it is round to unison again (or `shifts` steps).
        Plan phasePlan (const Cell& c, const Settings& s)
        {
            const auto step = std::max<int64_t> (1, s.stepTicks);
            const auto round = std::max<int64_t> (1, c.length / step);
            const auto steps = s.shifts > 0 ? std::min<int64_t> (s.shifts, round) : round;
            Plan p;
            for (int64_t k = 0; k <= steps; ++k)
                p.push_back (rotated (c, -k * step));
            return p;
        }

        // Canon at the unison: voice `k` waits k stages, then plays the pattern k beats behind.
        Plan canonPlan (const Cell& c, int k)
        {
            Plan p;
            for (int i = 0; i < k; ++i)
                p.push_back ({ {}, c.length });
            p.push_back (rotated (c, (int64_t) k * ticksPerQuarter));
            return p;
        }

        // Reich's build-up: a note (a chord) in place of a rest each stage, the downbeat's first.
        Plan buildUpPlan (const Cell& c, std::mt19937& rng)
        {
            auto g = groups (c);
            if (g.size() < 2)
                return holdPlan (c);
            std::vector<size_t> order (g.size());
            std::iota (order.begin(), order.end(), 0);
            std::shuffle (order.begin() + 1, order.end(), rng);
            Plan p;
            Cell growing { {}, c.length };
            for (const auto i : order)
            {
                growing.notes.insert (growing.notes.end(), g[i].begin(), g[i].end());
                p.push_back (growing);
            }
            return p;
        }

        // Glass's additive process: the first group, then the first two... each stage as long as it
        // takes to reach the next, then the whole figure.
        Plan additivePlan (const Cell& c, int64_t step)
        {
            const auto g = groups (c);
            if (g.size() < 2)
                return holdPlan (c);
            Plan p;
            Cell growing;
            for (size_t k = 0; k < g.size(); ++k)
            {
                growing.notes.insert (growing.notes.end(), g[k].begin(), g[k].end());
                growing.length = k + 1 < g.size() ? snapped (g[k + 1].front().start, step) : c.length;
                for (auto& n : growing.notes)
                    n.end = std::min (n.end, std::max (growing.length, n.start + 1));
                p.push_back (growing);
            }
            return p;
        }

        // Subtractive: the whole figure, then a group less off the front each stage, to its last.
        Plan subtractivePlan (const Cell& c, int64_t step)
        {
            const auto g = groups (c);
            if (g.size() < 2)
                return holdPlan (c);
            Plan p;
            for (size_t k = 0; k < g.size(); ++k)
            {
                const auto from = g[k].front().start;
                Cell less { {}, std::max (step, snapped (c.length - from, step)) };
                for (size_t i = k; i < g.size(); ++i)
                    for (auto n : g[i])
                    {
                        n.start -= from;
                        n.end -= from;
                        less.notes.push_back (n);
                    }
                p.push_back (less);
            }
            return p;
        }

        // Glass's cycles: the figure a group shorter (or a little longer, when it is short), so it
        // drifts against the others and meets them again where the lengths' multiples meet.
        Plan cyclesPlan (const Cell& c, int64_t step)
        {
            const auto g = groups (c);
            Cell cycle;
            if (g.size() >= 3)
            {
                cycle.length = snapped (g.back().front().start, step);
                for (size_t k = 0; k + 1 < g.size(); ++k)
                    cycle.notes.insert (cycle.notes.end(), g[k].begin(), g[k].end());
            }
            else
            {
                cycle = c;
                cycle.length = c.length + 2 * step;
            }
            if (cycle.length <= 0 || cycle.length == c.length)
                return holdPlan (c);
            for (auto& n : cycle.notes)
                n.end = std::min (n.end, n.start + cycle.length);
            return { cycle };
        }

        // The harmony each bar: the pitches sounding in it on every part but the drums.
        using Harmony = std::vector<std::vector<int>>;

        Harmony harmonyOf (const std::vector<const Cell*>& cells, const std::vector<bool>& drums)
        {
            int64_t length = 0;
            for (const auto* c : cells)
                if (c != nullptr)
                    length = std::max (length, c->length);
            const auto bars = std::max<int64_t> (1, roundUp (length, bar) / bar);
            Harmony h ((size_t) bars);
            for (size_t i = 0; i < cells.size(); ++i)
                if (cells[i] != nullptr && ! drums[i])
                    for (const auto& n : cells[i]->notes)
                        for (int64_t b = std::max<int64_t> (0, n.start / bar); b < bars && b * bar < n.end; ++b)
                            h[(size_t) b].push_back (n.pitch);
            for (auto& chord : h)
            {
                std::sort (chord.begin(), chord.end());
                chord.erase (std::unique (chord.begin(), chord.end()), chord.end());
            }
            // A bar with nothing sounding keeps the harmony before it (or after, at the start).
            for (size_t b = 1; b < h.size(); ++b)
                if (h[b].empty())
                    h[b] = h[b - 1];
            for (size_t b = h.size(); b-- > 1;)
                if (h[b - 1].empty())
                    h[b - 1] = h[b];
            return h;
        }

        // The chord's notes in an octave around `centre`, at least three of them.
        std::vector<int> voiced (const std::vector<int>& chord, int centre)
        {
            std::vector<int> tones;
            const auto low = std::clamp (centre - 6, 12, 108);
            for (const auto p : chord)
            {
                auto t = low + ((p - low) % 12 + 12) % 12;
                tones.push_back (t);
            }
            std::sort (tones.begin(), tones.end());
            tones.erase (std::unique (tones.begin(), tones.end()), tones.end());
            for (size_t i = 0; tones.size() < 3 && ! tones.empty(); ++i)
                tones.push_back (tones[i] + 12);
            return tones;
        }

        int centreOf (const Cell& c, int otherwise)
        {
            if (c.notes.empty())
                return otherwise;
            double sum = 0;
            for (const auto& n : c.notes)
                sum += n.pitch;
            return (int) std::lround (sum / (double) c.notes.size());
        }

        int velocityOf (const Cell& c)
        {
            if (c.notes.empty())
                return 80;
            double sum = 0;
            for (const auto& n : c.notes)
                sum += n.velocity;
            return std::clamp ((int) std::lround (sum / (double) c.notes.size()), 30, 120);
        }

        int channelOf (const Cell& c) { return c.notes.empty() ? 0 : c.notes.front().channel; }

        // Glass's arpeggiated harmony: each bar's chord broken in eighths, in a figure the seed picks.
        Plan arpeggioPlan (const Cell& c, const Harmony& h, int centre, int velocity, int channel, int figure)
        {
            static const std::vector<std::vector<int>> figures {
                { 0, 1, 2, 3 },             // up
                { 0, 1, 2, 3, 2, 1 },       // up and down
                { 0, 2, 1, 2 },             // Alberti
                { 0, 1, 2, 0, 1, 2, 0, 1 }, // three and three and two (Einstein on the Beach)
            };
            const auto& f = figures[(size_t) figure % figures.size()];
            Cell out { {}, std::max (c.length, eighth) };
            int i = 0;
            for (int64_t t = 0; t < out.length; t += eighth, ++i)
            {
                const auto& chord = h[(size_t) ((t / bar) % (int64_t) h.size())];
                if (chord.empty())
                    return holdPlan (c);
                const auto tones = voiced (chord, centre);
                const auto at = (size_t) f[(size_t) i % f.size()];
                const auto pitch = tones[at % tones.size()] + 12 * (int) (at / tones.size());
                const bool accent = i % (int) f.size() == 0;
                out.notes.push_back ({ t, std::min (t + eighth, out.length), std::clamp (pitch, 0, 127),
                                       std::clamp (velocity + (accent ? 10 : -5), 1, 127), channel });
            }
            return { out };
        }

        // Reich's pulse: the chord in steady eighths. Its breath (the swell) is put on in render.
        Plan pulsePlan (int64_t length, const Harmony& h, int channel)
        {
            Cell out { {}, std::max (length, bar) };
            for (int64_t t = 0; t < out.length; t += eighth)
            {
                const auto& chord = h[(size_t) ((t / bar) % (int64_t) h.size())];
                const auto tones = voiced (chord, 66);
                for (size_t k = 0; k < std::min<size_t> (tones.size(), 4); ++k)
                    out.notes.push_back ({ t, t + eighth - eighth / 8, tones[k], 80, channel });
            }
            return { out };
        }

        bool gradual (Process p)
        {
            return p == Process::phase || p == Process::canon || p == Process::buildUp || p == Process::additive || p == Process::subtractive;
        }

        int64_t planLength (const Plan& p, int repeats)
        {
            int64_t sum = 0;
            for (const auto& c : p)
                sum += c.length * repeats;
            return sum;
        }

        // The plan from `at` for `length`: each stage `repeats` times, the last held to the end.
        void render (const Plan& plan, int repeats, int64_t at, int64_t length, Notes& out)
        {
            if (plan.empty())
                return;
            int64_t t = 0;
            size_t stage = 0;
            int played = 0;
            while (t < length)
            {
                const auto& c = plan[stage];
                if (c.length <= 0)
                    break;
                for (const auto& n : c.notes)
                    if (t + n.start < length)
                        out.push_back ({ at + t + n.start, at + std::min (t + n.end, length), n.pitch, n.velocity, n.channel });
                t += c.length;
                if (++played >= repeats && stage + 1 < plan.size())
                {
                    ++stage;
                    played = 0;
                }
            }
        }

        // The automatic choice for a part, by the style and its place among the parts.
        Process chosen (Style style, int index, bool drums)
        {
            if (style == Style::reich)
            {
                if (drums)
                    return index == 0 ? Process::phase : Process::buildUp;
                static const Process order[] = { Process::phase, Process::buildUp, Process::canon, Process::hold };
                return order[index % 4];
            }
            if (style == Style::glass)
            {
                if (drums)
                    return Process::cycles;
                static const Process order[] = { Process::additive, Process::arpeggio, Process::cycles, Process::subtractive };
                return order[index % 4];
            }
            if (drums)
                return Process::buildUp;
            static const Process order[] = { Process::phase, Process::arpeggio, Process::additive, Process::cycles, Process::buildUp, Process::subtractive };
            return order[index % 6];
        }
    }

    const char* name (Process p)
    {
        switch (p)
        {
            case Process::automatic:   return "automatic";
            case Process::hold:        return "hold";
            case Process::phase:       return "phase";
            case Process::canon:       return "canon";
            case Process::buildUp:     return "build-up";
            case Process::pulse:       return "pulse";
            case Process::additive:    return "additive";
            case Process::subtractive: return "subtractive";
            case Process::cycles:      return "cycles";
            case Process::arpeggio:    return "arpeggio";
        }
        return "";
    }

    Cell cut (const Notes& notes, int64_t from, int64_t to)
    {
        Cell c { {}, std::max<int64_t> (0, to - from) };
        for (auto n : notes)
            if (n.start >= from && n.start < to)
            {
                n.start -= from;
                n.end = std::min (n.end - from, c.length);
                c.notes.push_back (n);
            }
        return c;
    }

    Result arrange (const std::vector<Part>& parts, const Settings& settings)
    {
        Result result;
        if (parts.empty())
            return result;
        const auto repeats = std::clamp (settings.repeats, 1, 32);
        const auto step = std::max<int64_t> (1, settings.stepTicks);
        std::mt19937 rng (settings.seed * 2654435761u + 17u);

        // The tracks: the parts with their processes, then the new voices on the tracks left.
        struct Voice { int part; Process process; int canon = 0; bool added = false; };
        std::vector<Voice> voices;
        const int count = std::min ((int) parts.size(), maxTracks);
        int melodic = 0;
        for (int i = 0; i < count; ++i)
        {
            auto p = i < (int) settings.processes.size() ? settings.processes[(size_t) i] : Process::automatic;
            if (p == Process::automatic)
                p = chosen (settings.style, parts[(size_t) i].drums ? i : melodic, parts[(size_t) i].drums);
            if (parts[(size_t) i].drums && (p == Process::arpeggio || p == Process::pulse))
                p = Process::buildUp;
            if (! parts[(size_t) i].drums)
                ++melodic;
            voices.push_back ({ i, p });
        }
        // Phasing and canon want the pattern held still on its own track and the moving copies
        // on new ones; without room the part itself moves, against the others.
        int room = settings.newVoices ? std::clamp (std::min (maxTracks - count, settings.freeTracks), 0, maxTracks) : 0;
        voices.reserve (voices.size() + (size_t) room);   // `v` below must outlive the new voices pushed after it
        for (int i = 0; i < count; ++i)
        {
            auto& v = voices[(size_t) i];
            if (v.process == Process::phase && room > 0)
            {
                voices.push_back ({ i, Process::phase, 0, true });
                v.process = Process::hold;
                --room;
            }
            else if (v.process == Process::canon)
            {
                const int entries = std::min (room, 2);
                for (int k = 1; k <= entries; ++k)
                    voices.push_back ({ i, Process::canon, k, true });
                room -= entries;
                if (entries > 0)
                    v.process = Process::hold;
                else
                    v.canon = 1;
            }
        }
        const bool anyMelodic = std::any_of (parts.begin(), parts.begin() + count, [] (const Part& p) { return ! p.drums; });
        if (room > 0 && anyMelodic && std::none_of (voices.begin(), voices.end(), [] (const Voice& v) { return v.process == Process::pulse; }))
        {
            const int lead = (int) (std::find_if (parts.begin(), parts.end(), [] (const Part& p) { return ! p.drums; }) - parts.begin());
            voices.push_back ({ lead, settings.style == Style::glass ? Process::arpeggio : Process::pulse, 0, true });
            --room;
        }

        for (const auto& v : voices)
        {
            Track t;
            t.source = v.part;
            t.process = v.process;
            t.added = v.added;
            const auto& base = parts[(size_t) v.part].name;
            t.name = v.added ? base + " (" + name (v.process) + (v.canon > 0 ? " " + std::to_string (v.canon + 1) : std::string()) + ")"
                             : base + " (" + name (v.process) + ")";
            result.tracks.push_back (t);
        }
        const auto figure = (int) (rng() % 4);

        // Section by section: each part's cell (its last again when it has fewer), every
        // track's plan, the section as long as its longest gradual process.
        size_t sections = 0;
        for (int i = 0; i < count; ++i)
            sections = std::max (sections, parts[(size_t) i].cells.size());
        int64_t at = 0;
        for (size_t s = 0; s < sections && at < settings.maxTicks; ++s)
        {
            std::vector<const Cell*> cells;
            std::vector<bool> drums;
            int64_t longest = 0;
            for (int i = 0; i < count; ++i)
            {
                const auto& cs = parts[(size_t) i].cells;
                cells.push_back (cs.empty() ? nullptr : &cs[std::min (s, cs.size() - 1)]);
                drums.push_back (parts[(size_t) i].drums);
                if (cells.back() != nullptr)
                    longest = std::max (longest, cells.back()->length);
            }
            if (longest <= 0)
                continue;
            const auto harmony = harmonyOf (cells, drums);

            std::vector<Plan> plans;
            int64_t length = 0;
            for (const auto& v : voices)
            {
                const auto* c = cells[(size_t) v.part];
                if (c == nullptr || c->length <= 0)
                {
                    plans.emplace_back();
                    continue;
                }
                Plan plan;
                switch (v.process)
                {
                    case Process::automatic:
                    case Process::hold:        plan = holdPlan (*c); break;
                    case Process::phase:       plan = phasePlan (*c, settings); break;
                    case Process::canon:       plan = canonPlan (*c, std::max (1, v.canon)); break;
                    case Process::buildUp:     plan = buildUpPlan (*c, rng); break;
                    case Process::additive:    plan = additivePlan (*c, step); break;
                    case Process::subtractive: plan = subtractivePlan (*c, step); break;
                    case Process::cycles:      plan = cyclesPlan (*c, step); break;
                    case Process::arpeggio:
                        plan = arpeggioPlan (*c, harmony, centreOf (*c, 60) + (v.added ? 12 : 0), velocityOf (*c), channelOf (*c), figure);
                        break;
                    case Process::pulse:       plan = pulsePlan (longest, harmony, channelOf (*c)); break;
                }
                if (gradual (v.process))
                    length = std::max (length, planLength (plan, repeats));
                plans.push_back (std::move (plan));
            }
            if (length <= 0)
                length = longest * repeats * 2;
            length = roundUp (length, bar);
            length = std::min (length, settings.maxTicks - at);

            result.sections.push_back (at);
            for (size_t k = 0; k < voices.size(); ++k)
            {
                auto& out = result.tracks[k].notes;
                const auto before = out.size();
                render (plans[k], repeats, at, length, out);
                // The pulse breathes: each stage swells from soft to loud and falls back.
                if (voices[k].process == Process::pulse)
                {
                    const auto breath = (double) std::max<int64_t> (bar, longest * repeats);
                    for (auto i = before; i < out.size(); ++i)
                    {
                        const auto x = std::fmod ((double) (out[i].start - at), breath) / breath;
                        out[i].velocity = std::clamp ((int) std::lround (35.0 + 70.0 * std::sin (x * 3.14159265358979)), 1, 127);
                    }
                }
            }
            at += length;
        }
        result.length = at;
        return result;
    }

    midifile::SongPtr toSong (const Notes& notes, double bpm, int64_t length)
    {
        auto out = std::make_shared<midifile::Song>();
        out->format = 0;
        out->ticksPerQuarter = ticksPerQuarter;
        out->tempos = { { 0, 60000000.0 / std::max (1.0, bpm) } };
        struct Timed { midifile::Event e; int order; };
        std::vector<Timed> all;
        for (const auto& n : notes)
        {
            const auto channel = (uint8_t) std::clamp (n.channel, 0, 15);
            const auto pitch = (uint8_t) std::clamp (n.pitch, 0, 127);
            all.push_back ({ { n.start, (uint8_t) (0x90 | channel), pitch, (uint8_t) std::clamp (n.velocity, 1, 127) }, 1 });
            all.push_back ({ { std::max (n.end, n.start + 1), (uint8_t) (0x80 | channel), pitch, 0 }, 0 });
            out->channelsUsed |= (uint16_t) (1u << channel);
        }
        std::stable_sort (all.begin(), all.end(), [] (const Timed& a, const Timed& b)
        {
            return a.e.tick != b.e.tick ? a.e.tick < b.e.tick : a.order < b.order;   // offs before ons
        });
        for (const auto& t : all)
            out->events.push_back (t.e);
        out->lastTick = length;
        for (const auto& e : out->events)
            out->lastTick = std::max (out->lastTick, e.tick);
        return out;
    }
}

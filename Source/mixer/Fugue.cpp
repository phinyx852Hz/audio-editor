// SPDX-License-Identifier: AGPL-3.0-only
#include "Fugue.h"

#include <algorithm>
#include <cmath>

namespace fugue
{
    using counterpoint::Line;
    using counterpoint::Mode;
    using counterpoint::Note;

    namespace
    {
        int floorDiv (int a, int b) { return a >= 0 ? a / b : -((-a + b - 1) / b); }
        int64_t roundUp (int64_t x, int64_t step) { return std::max<int64_t> (step, (x + step - 1) / step * step); }

        // The mode's notes across the keyboard, to move a line by its degrees.
        struct Scale
        {
            explicit Scale (const Mode& m) : mode (m)
            {
                for (int p = 0; p < 128; ++p)
                {
                    if (in (p))
                        notes.push_back (p);
                    below[p] = (int) notes.size() - 1;
                }
                perOctave = (int) std::count (m.scale.begin(), m.scale.end(), true);
                tonic = std::clamp (60 + m.root, 0, 127);
            }
            int key (int p) const
            {
                const auto rel = p - (60 + mode.root);
                return rel - floorDiv (rel, mode.period) * mode.period;
            }
            bool in (int p) const { return mode.scale[(size_t) key (p)]; }
            double cents (int p) const
            {
                const auto rel = p - (60 + mode.root);
                return floorDiv (rel, mode.period) * 1200.0 + mode.keyCents[(size_t) key (p)];
            }
            // Where a key falls among the mode's notes: the one at or below it, and how far above that it is.
            std::pair<int, int> place (int p) const
            {
                p = std::clamp (p, 0, 127);
                const auto k = std::max (0, below[p]);
                return { k, p - notes[(size_t) k] };
            }
            int at (int index, int off) const { return std::clamp (notes[(size_t) std::clamp (index, 0, (int) notes.size() - 1)] + off, 0, 127); }
            int shift (int p, int degrees) const
            {
                const auto [k, off] = place (p);
                return at (k + degrees, off);
            }
            // The degrees up from the tonic nearest `target` cents: a fifth's, a third's.
            int degreesFor (double target) const
            {
                int best = 1;
                double bestOff = 1.0e9;
                for (int d = 1; d <= 2 * perOctave; ++d)
                {
                    const auto off = std::abs (cents (shift (tonic, d)) - cents (tonic) - target);
                    if (off < bestOff - 1.0e-9)
                    {
                        bestOff = off;
                        best = d;
                    }
                }
                return best;
            }

            const Mode& mode;
            std::vector<int> notes;
            int below[128] {};
            int perOctave = 7, tonic = 60;
        };

        Line moved (const Scale& s, const Line& l, int degrees, int64_t by)
        {
            Line out = l;
            for (auto& x : out)
            {
                x.pitch = s.shift (x.pitch, degrees);
                x.start += by;
                x.end += by;
            }
            return out;
        }
        // Upside down about its first note, degree for degree.
        Line inverted (const Scale& s, const Line& l)
        {
            Line out = l;
            const auto k0 = s.place (l.front().pitch).first;
            for (auto& x : out)
            {
                const auto [k, off] = s.place (x.pitch);
                x.pitch = s.at (2 * k0 - k, -off);
            }
            return out;
        }
        // The answer at the fifth. Tonal: until the subject leaves its tonic and dominant, the
        // tonic is answered by the dominant and the dominant by the tonic, a fourth up.
        Line answered (const Scale& s, const Line& subject, int fifth, bool tonal)
        {
            const auto dominant = s.key (s.shift (s.tonic, fifth));
            Line out = subject;
            bool head = tonal;
            for (auto& x : out)
            {
                const auto k = s.key (x.pitch);
                head = head && (k == 0 || k == dominant);
                x.pitch = s.shift (x.pitch, head && k == dominant ? s.perOctave - fifth : fifth);
            }
            return out;
        }
        // A raga's āroha going up and its avaroha coming down: a note the way leaves out moves to
        // the nearest it keeps, still going the same way.
        void keepDirections (const Scale& s, Line& l)
        {
            if (s.mode.up.empty() && s.mode.down.empty())
                return;
            for (size_t i = 1; i < l.size(); ++i)
            {
                const auto prev = l[i - 1].pitch, p = l[i].pitch;
                if (p == prev)
                    continue;
                const bool up = p > prev;
                const auto& way = up ? s.mode.up : s.mode.down;
                if (way.empty() || way[(size_t) s.key (p)])
                    continue;
                for (int d : { 1, -1, 2, -2 })
                {
                    const auto q = s.shift (p, up ? d : -d);
                    if (q != prev && (q > prev) == up && way[(size_t) s.key (q)])
                    {
                        l[i].pitch = q;
                        break;
                    }
                }
            }
        }

        enum class Kind { octave, fifth, fourth, imperfect, dissonant };
        Kind classify (double centsApart)
        {
            const auto r = std::fmod (std::abs (centsApart), 1200.0);
            constexpr double tolerance = 30.0;
            if (r < tolerance || r > 1200.0 - tolerance)
                return Kind::octave;
            if (std::abs (r - 702.0) < tolerance)
                return Kind::fifth;
            if (std::abs (r - 498.0) < tolerance)
                return Kind::fourth;
            if ((r >= 250.0 && r <= 450.0) || (r >= 750.0 && r <= 950.0))
                return Kind::imperfect;
            return Kind::dissonant;
        }

        int sounding (const Line& line, int64_t t)
        {
            auto it = std::upper_bound (line.begin(), line.end(), t, [] (int64_t x, const Note& n) { return x < n.start; });
            if (it == line.begin())
                return -1;
            --it;
            return t < it->end ? it->pitch : -1;
        }

        // How badly two fixed lines sound together: dissonances struck (on the beat the worst),
        // fourths, parallel fifths and octaves, unisons, and the voices crossing (`above`: 1 if
        // `a` should be the higher, -1 the lower).
        double clash (const Scale& s, const Line& a, const Line& b, int64_t t0, int64_t beat, int above)
        {
            if (a.empty() || b.empty())
                return 0.0;
            const auto from = std::max (a.front().start, b.front().start), to = std::min (a.back().end, b.back().end);
            std::vector<int64_t> times;
            for (const auto* l : { &a, &b })
                for (const auto& x : *l)
                    if (x.start >= from && x.start < to)
                        times.push_back (x.start);
            std::sort (times.begin(), times.end());
            times.erase (std::unique (times.begin(), times.end()), times.end());
            double bad = 0.0;
            int pa0 = -1, pb0 = -1;
            for (auto t : times)
            {
                const auto pa = sounding (a, t), pb = sounding (b, t);
                if (pa < 0 || pb < 0)
                {
                    pa0 = pb0 = -1;
                    continue;
                }
                const auto k = classify (s.cents (pa) - s.cents (pb));
                const bool strong = (t - t0) % beat == 0;
                if (k == Kind::dissonant)
                    bad += strong ? 3.0 : 0.5;
                else if (k == Kind::fourth)
                    bad += strong ? 1.0 : 0.2;
                if (pa0 >= 0 && pa != pa0 && pb != pb0)
                {
                    const auto k0 = classify (s.cents (pa0) - s.cents (pb0));
                    if ((k == Kind::octave || k == Kind::fifth) && k == k0)
                        bad += 4.0;
                }
                if (pa == pb)
                    bad += 0.5;
                if ((above > 0 && pa < pb) || (above < 0 && pa > pb))
                    bad += 0.5;
                pa0 = pa;
                pb0 = pb;
            }
            return bad;
        }

        int median (const Line& l)
        {
            std::vector<int> p;
            for (const auto& x : l)
                p.push_back (x.pitch);
            std::nth_element (p.begin(), p.begin() + (long) p.size() / 2, p.end());
            return p[p.size() / 2];
        }
        bool inRange (const Line& l)
        {
            return std::all_of (l.begin(), l.end(), [] (const Note& x) { return x.pitch >= 24 && x.pitch <= 100; });
        }

        // Each voice's home: whether its first entry is the answer, and its octave from the subject's.
        struct Home
        {
            bool answer;
            int octave;
        };
        const Home homes[3][4] {
            { { true, 0 }, { false, 0 } },
            { { true, 0 }, { false, 0 }, { false, -1 } },
            { { true, 0 }, { false, 0 }, { true, -1 }, { false, -1 } },
        };
        // The voices in the order they enter, top first counting 0: alto, soprano, bass, tenor.
        const std::vector<int> orders[3] { { 1, 0 }, { 1, 0, 2 }, { 1, 0, 3, 2 } };
    }

    Line subject (const Line& melody, int ticksPerQuarter)
    {
        if (melody.empty() || ticksPerQuarter <= 0)
            return {};
        const auto limit = melody.front().start + 16 * (int64_t) ticksPerQuarter;
        Line phrase, all;
        bool rest = false;
        for (const auto& x : melody)
        {
            if (x.start >= limit)
                break;
            if (! all.empty() && x.start > all.back().end)
                rest = true;
            all.push_back (x);
            all.back().end = std::min (all.back().end, limit);
            if (! rest)
                phrase.push_back (all.back());
        }
        if (phrase.size() >= 3)
            return phrase;
        return all.size() >= 2 ? all : Line {};
    }

    Result generate (const Line& subjectIn, int ticksPerQuarter, const Settings& settings)
    {
        Result result;
        if (subjectIn.size() < 2 || ticksPerQuarter <= 0)
            return result;
        const auto mode = settings.mode.valid() ? settings.mode : counterpoint::guessMode (subjectIn);
        const Scale s (mode);
        if (s.notes.empty())
            return result;
        const int V = std::clamp (settings.voices, 2, 4);
        const int N = s.perOctave;
        const auto tpq = (int64_t) ticksPerQuarter;

        // The subject from its first note, lifted an octave or so if it is too high or low for the voices around it.
        const auto t0 = subjectIn.front().start;
        Line subject = moved (s, subjectIn, 0, -t0);
        for (int lift = 0; lift < 4 && median (subject) > 76; ++lift)
            subject = moved (s, subject, -N, 0);
        for (int lift = 0; lift < 4 && median (subject) < 53; ++lift)
            subject = moved (s, subject, N, 0);
        std::vector<int64_t> lengths;
        int velocity = 0;
        for (const auto& x : subject)
        {
            lengths.push_back (x.end - x.start);
            velocity += x.velocity;
        }
        velocity = std::clamp (velocity / (int) subject.size(), 1, 127);
        std::nth_element (lengths.begin(), lengths.begin() + (long) lengths.size() / 2, lengths.end());
        // The beat the free voices count: a half note under a subject of quarters and longer, else a quarter.
        const auto unit = lengths[lengths.size() / 2] >= tpq ? 2 * tpq : tpq;
        const auto Ls = roundUp (subject.back().end, unit);
        const auto fifth = s.degreesFor (702.0);
        auto answer = answered (s, subject, fifth, settings.answer == Answer::tonal);
        keepDirections (s, answer);

        const auto& home = homes[V - 2];
        const auto& order = orders[V - 2];
        const auto mid = median (subject);
        std::vector<int> centre (V);
        for (int v = 0; v < V; ++v)
            centre[(size_t) v] = (home[v].answer ? s.shift (mid, fifth) : mid) + home[v].octave * mode.period;
        const auto range = (int) std::lround (9.0 * mode.period / 12.0);
        auto rank = [V] (int v) { return V - v; };

        // The material each voice is given, as the plan lays it out; the free voices are written after.
        std::vector<Line> fixed ((size_t) V);
        std::vector<std::vector<std::pair<int64_t, int64_t>>> blocks ((size_t) V);
        Line lead;   // what the fugue is about at each moment: the entries and episodes
        auto place = [&] (int v, const Line& l, const std::string& what, bool isLead, bool plan = true)
        {
            if (l.empty())
                return;
            auto& f = fixed[(size_t) v];
            // A raga's āroha and avaroha kept from the voice's note before, the subject's own notes too.
            Line kept;
            if (! f.empty() && f.back().end <= l.front().start)
                kept.push_back (f.back());
            kept.insert (kept.end(), l.begin(), l.end());
            keepDirections (s, kept);
            f.insert (f.end(), kept.end() - (long) l.size(), kept.end());
            std::sort (f.begin(), f.end(), [] (const Note& a, const Note& b) { return a.start < b.start; });
            blocks[(size_t) v].push_back ({ l.front().start, l.back().end });
            if (isLead)
                lead.insert (lead.end(), kept.end() - (long) l.size(), kept.end());
            if (plan)
                result.plan.push_back ({ what, v, l.front().start, l.back().end });
        };
        auto freeAt = [&] (int v, int64_t a, int64_t b)
        {
            for (const auto& [x, y] : blocks[(size_t) v])
                if (x < b && y > a)
                    return false;
            return true;
        };
        auto entered = [&] (int v, int64_t t)
        {
            for (const auto& [x, y] : blocks[(size_t) v])
                if (x < t)
                    return true;
            return false;
        };
        const auto seed = settings.seed * 2654435761u + 977u;
        int bent = 0;

        // The countersubject, written once against the answer and then moved where each entry needs it.
        Line cs;
        int64_t csAt = 0;
        int csDegrees = 0;
        int lastVoice = -1;
        auto placeCountersubject = [&] (const Line& entry, int degrees, int64_t at, int v)
        {
            if (cs.empty())
                return;
            Line best;
            int bestVoice = -1;
            double bestCost = 1.0e9, bestClash = 1.0e9;
            for (int w = 0; w < V; ++w)
            {
                if (w == v || ! entered (w, at))
                    continue;
                for (int o = -2; o <= 2; ++o)
                {
                    auto line = moved (s, cs, degrees - csDegrees + o * N, at - csAt);
                    keepDirections (s, line);
                    if (! inRange (line) || ! freeAt (w, line.front().start, line.back().end)
                        || std::abs (median (line) - centre[(size_t) w]) > range + 3)
                        continue;
                    const auto c = clash (s, line, entry, t0, unit, w < v ? 1 : -1);
                    const auto cost = c + 0.3 * std::abs (median (line) - centre[(size_t) w]) + (w == lastVoice ? 0.0 : 1.0);
                    if (cost < bestCost)
                    {
                        bestCost = cost;
                        bestClash = c;
                        best = std::move (line);
                        bestVoice = w;
                    }
                }
            }
            if (bestVoice >= 0 && bestClash <= 4.0)
                place (bestVoice, best, "countersubject", false);
        };

        // The exposition.
        int64_t t = t0;
        for (int k = 0; k < V; ++k)
        {
            const auto v = order[(size_t) k];
            const auto& h = home[v];
            const auto degrees = (h.answer ? fifth : 0) + h.octave * N;
            const auto entry = moved (s, h.answer ? answer : subject, h.octave * N, t);
            place (v, entry, h.answer ? "answer" : "subject", true);
            if (k >= 1 && settings.countersubject)
            {
                if (cs.empty())
                {
                    // Written by the voice that had the subject, against the answer alone.
                    const auto u = order[(size_t) k - 1];
                    const auto& before = fixed[(size_t) u];
                    counterpoint::FreePart part;
                    part.grid.push_back (before.back());
                    part.pinned.push_back (before.back().pitch);
                    for (auto x = before.back().end; x < t + Ls;)
                    {
                        auto y = std::min (t + Ls, t0 + ((x - t0) / unit + 1) * unit);
                        if (y - x < unit / 4 && y < t + Ls)
                            y = std::min (t + Ls, y + unit);
                        const auto sounds = sounding (entry, x);
                        part.grid.push_back ({ x, y, sounds >= 0 ? sounds : centre[(size_t) u], velocity });
                        part.pinned.push_back (-1);
                        x = y;
                    }
                    part.against = { entry };
                    part.ranks = { rank (v) };
                    part.rank = rank (u);
                    part.species = settings.species;
                    part.centre = centre[(size_t) u];
                    part.range = range;
                    part.parts = 2;
                    part.lowest = rank (u) < rank (v);
                    cs = counterpoint::freeVoice (part, mode, seed + 11u, bent);
                    if (! cs.empty())
                    {
                        place (u, cs, "countersubject", false);
                        csAt = t;
                        csDegrees = degrees;
                    }
                }
                else
                    placeCountersubject (entry, degrees, t, v);
            }
            lastVoice = v;
            t += Ls;
        }

        // Episodes: the subject's head, its first bar or third at most, in sequence a degree at a time in one voice.
        Line head;
        const auto headLength = std::clamp<int64_t> (Ls / 3, unit, 4 * tpq);
        for (const auto& x : subject)
            if (head.size() < 2 || x.start < headLength)
                head.push_back (x);
        head.back().end = std::min (head.back().end, std::max (headLength, head.back().start + unit / 2));
        const auto F = roundUp (head.back().end, unit);
        auto episode = [&] (int e, int avoid)
        {
            const int steps = F * 3 <= 2 * Ls ? 3 : 2;
            auto w = e % V;
            if (w == avoid || w == lastVoice)
                w = (w + 1) % V;
            if (w == avoid || w == lastVoice)
                w = (w + 1) % V;
            const auto& h = home[w];
            const int dir = e % 2 == 0 ? -1 : 1;   // down by step, then up
            const auto from = t;
            for (int j = 0; j < steps; ++j)
            {
                auto line = moved (s, head, (h.answer ? fifth : 0) + h.octave * N + dir * j, t);
                keepDirections (s, line);
                place (w, line, "episode", true, false);
                t += F;
            }
            result.plan.push_back ({ "episode", w, from, t });
        };

        // Middle entries, each after an episode, on other degrees of the mode.
        static const double degreesOf[] { 350.0, 850.0, 498.0, 200.0 };   // the mediant, the submediant, the subdominant, the supertonic
        const auto middle = std::clamp (settings.middleEntries, 0, 4);
        for (int m = 0; m < middle; ++m)
        {
            auto v = order[(size_t) (m + 2) % (size_t) V];
            if (v == lastVoice)
                v = order[(size_t) (m + 3) % (size_t) V];
            episode (m, v);
            const bool upsideDown = settings.inversion && m % 2 == 1;
            const auto& base = upsideDown ? inverted (s, subject) : subject;
            auto degrees = s.degreesFor (degreesOf[m]) + home[v].octave * N;
            auto line = moved (s, base, degrees, t);
            for (int o = 0; o < 3 && median (line) - centre[(size_t) v] > mode.period / 2; ++o)
                line = moved (s, base, degrees -= N, t);
            for (int o = 0; o < 3 && centre[(size_t) v] - median (line) > mode.period / 2; ++o)
                line = moved (s, base, degrees += N, t);
            keepDirections (s, line);
            place (v, line, upsideDown ? "middle entry, inverted" : "middle entry", true);
            if (! upsideDown && settings.countersubject)
                placeCountersubject (line, degrees, t, v);
            lastVoice = v;
            t += Ls;
        }

        // The last episode, then the stretto: every voice enters again, each before the one
        // before has finished, at the distance where they sound best together.
        episode (middle, order[0]);
        bool stretto = false;
        if (settings.stretto && Ls >= 2 * unit)
        {
            double bestBad = 1.0e9;
            int64_t bestDelay = 0;
            std::vector<Line> bestLines;
            // As close as they go: within half the subject if anywhere sounds well enough there.
            for (auto delay = unit; delay <= Ls - unit && (delay <= Ls / 2 || bestBad > 5.0 * (V - 1)); delay += unit)
            {
                std::vector<Line> lines;
                for (int k = 0; k < V; ++k)
                {
                    const auto v = order[(size_t) k];
                    lines.push_back (moved (s, home[v].answer ? answer : subject, home[v].octave * N, t + k * delay));
                }
                double bad = 0.0;
                for (int i = 0; i < V; ++i)
                    for (int j = i + 1; j < V; ++j)
                        bad += clash (s, lines[(size_t) i], lines[(size_t) j], t0, unit, order[(size_t) i] < order[(size_t) j] ? 1 : -1);
                if (bad < bestBad - 1.0e-9)
                {
                    bestBad = bad;
                    bestDelay = delay;
                    bestLines = std::move (lines);
                }
            }
            if (! bestLines.empty() && bestBad <= 5.0 * (V - 1))
            {
                for (int k = 0; k < V; ++k)
                    place (order[(size_t) k], bestLines[(size_t) k], "stretto", true);
                result.strettoDelay = bestDelay;
                t += (V - 1) * bestDelay + Ls;
                stretto = true;
            }
        }
        if (! stretto)
        {
            // The last entry in the bass.
            const auto v = V - 1;
            const auto line = moved (s, subject, home[v].octave * N, t);
            place (v, line, "final entry", true);
            if (settings.countersubject)
                placeCountersubject (line, home[v].octave * N, t, v);
            t += Ls;
        }

        // The close: the tonic held in the bass, the nearest at or below its last note.
        {
            const auto v = V - 1;
            const auto last = fixed[(size_t) v].back().pitch;
            int tonic = last;
            for (int p = last + 2; p >= std::max (0, last - mode.period); --p)
                if (s.key (p) == 0)
                {
                    tonic = p;
                    if (p <= last)
                        break;
                }
            const auto length = std::max (2 * unit, 4 * tpq);
            place (v, { { t, t + length, tonic, velocity } }, "close", true);
            result.end = t + length;
        }

        // What the fugue is about at each moment: the latest of the lead to come in.
        std::stable_sort (lead.begin(), lead.end(), [] (const Note& a, const Note& b) { return a.start < b.start; });
        for (size_t i = 0; i + 1 < lead.size(); ++i)
            lead[i].end = std::min (lead[i].end, lead[i + 1].start);
        lead.erase (std::remove_if (lead.begin(), lead.end(), [] (const Note& x) { return x.end <= x.start; }), lead.end());

        // The free voices, wherever a voice that has come in has nothing given: the bass first,
        // then down from the top, each against all written before it.
        std::vector<Line> whole = fixed;
        std::vector<int> writing { V - 1 };
        for (int v = 0; v < V - 1; ++v)
            writing.push_back (v);
        for (auto v : writing)
        {
            // Where the voice has nothing given: between its blocks of material, and after the last.
            auto own = blocks[(size_t) v];
            std::sort (own.begin(), own.end());
            std::vector<std::pair<int64_t, int64_t>> merged;
            for (const auto& x : own)
                if (! merged.empty() && x.first <= merged.back().second)
                    merged.back().second = std::max (merged.back().second, x.second);
                else
                    merged.push_back (x);
            const auto& f = fixed[(size_t) v];
            auto noteFrom = [&] (int64_t start) -> const Note* { for (const auto& x : f) if (x.start == start) return &x; return nullptr; };
            auto noteTo = [&] (int64_t end) -> const Note* { for (const auto& x : f) if (x.end == end) return &x; return nullptr; };
            counterpoint::FreePart part;
            bool last = false;
            for (size_t b = 0; b < merged.size(); ++b)
            {
                const auto a = merged[b].second;
                const auto to = b + 1 < merged.size() ? merged[b + 1].first : result.end;
                if (to <= a)
                    continue;
                // Out of the voice's own last note, held as it is.
                if (const auto* in = noteTo (a); in != nullptr && (part.grid.empty() || part.grid.back().start != in->start))
                {
                    part.grid.push_back (*in);
                    part.pinned.push_back (in->pitch);
                }
                for (auto x = a; x < to;)
                {
                    auto y = std::min (to, t0 + ((x - t0) / unit + 1) * unit);
                    if (y - x < unit / 4 && y < to)
                        y = std::min (to, y + unit);
                    const auto sounds = sounding (lead, x);
                    part.grid.push_back ({ x, y, sounds >= 0 ? sounds : centre[(size_t) v], velocity });
                    part.pinned.push_back (-1);
                    x = y;
                }
                // Into its next.
                if (b + 1 >= merged.size())
                    last = true;
                else if (const auto* out = noteFrom (to); out != nullptr)
                {
                    part.grid.push_back (*out);
                    part.pinned.push_back (out->pitch);
                }
            }
            if (part.grid.empty())
                continue;
            part.against = { lead };
            part.ranks = { rank (v) };
            for (int w = 0; w < V; ++w)
                if (w != v && ! whole[(size_t) w].empty())
                {
                    part.against.push_back (whole[(size_t) w]);
                    part.ranks.push_back (rank (w));
                }
            part.rank = rank (v);
            part.species = settings.species;
            part.centre = centre[(size_t) v];
            part.range = range;
            part.parts = V;
            part.lowest = v == V - 1;
            part.cadence = last;
            const auto line = counterpoint::freeVoice (part, mode, seed + 101u * (uint32_t) (v + 1), bent);
            auto& w = whole[(size_t) v];
            w.insert (w.end(), line.begin(), line.end());
            std::sort (w.begin(), w.end(), [] (const Note& x, const Note& y) { return x.start < y.start; });
        }

        static const std::vector<std::string> names[3] { { "upper", "lower" }, { "soprano", "alto", "bass" }, { "soprano", "alto", "tenor", "bass" } };
        result.voices = std::move (whole);
        result.names = names[V - 2];
        result.bent = bent;
        std::stable_sort (result.plan.begin(), result.plan.end(), [] (const Section& a, const Section& b) { return a.start < b.start; });
        return result;
    }
}

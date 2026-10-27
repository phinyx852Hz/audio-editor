// SPDX-License-Identifier: AGPL-3.0-only
#include "Counterpoint.h"
#include "Tunings.h"

#include <algorithm>
#include <cmath>
#include <unordered_set>

namespace counterpoint
{
    namespace
    {
        constexpr double hard = 1000.0;   // a strict rule broken: only when nothing else fits
        constexpr double tolerance = 30.0;   // cents a consonance may be out and still be heard as one

        int floorDiv (int a, int b) { return a >= 0 ? a / b : -((-a + b - 1) / b); }

        // Where a key falls in a mode: the octave and the key of the period.
        struct Place
        {
            int octave, key;
        };
        Place place (const Mode& m, int pitch)
        {
            const auto rel = pitch - (60 + m.root);
            const auto octave = floorDiv (rel, m.period);
            return { octave, rel - octave * m.period };
        }
        double cents (const Mode& m, int pitch)
        {
            const auto p = place (m, pitch);
            return p.octave * 1200.0 + m.keyCents[(size_t) p.key];
        }
        // The scale degree, counting up from the root's octave; -1 off the scale.
        int degree (const Mode& m, int pitch)
        {
            const auto p = place (m, pitch);
            if (! m.scale[(size_t) p.key])
                return -1;
            int below = 0, count = 0;
            for (int k = 0; k < m.period; ++k)
                if (m.scale[(size_t) k])
                {
                    ++count;
                    below += k < p.key ? 1 : 0;
                }
            return (p.octave + 100) * count + below;   // kept positive
        }

        // The mode worked out for every key once, as the search asks it millions of times.
        struct Keys
        {
            explicit Keys (const Mode& m) : mode (m)
            {
                for (int p = 0; p < 128; ++p)
                {
                    cents[p] = counterpoint::cents (m, p);
                    degree[p] = counterpoint::degree (m, p);
                    key[p] = place (m, p).key;
                }
            }
            const Mode& mode;
            double cents[128];
            int degree[128];
            int key[128];
        };
        double cents (const Keys& k, int pitch) { return k.cents[pitch]; }
        bool inScale (const Keys& k, int pitch) { return k.degree[pitch] >= 0; }
        bool isStep (const Keys& k, int a, int b)
        {
            if (a == b)
                return false;
            const auto da = k.degree[a], db = k.degree[b];
            if (da >= 0 && db >= 0)
                return std::abs (da - db) == 1;
            return std::abs (k.cents[a] - k.cents[b]) <= 250.0;   // a raised leading note
        }

        enum class Interval { unison, octave, fifth, fourth, imperfect, dissonant };
        Interval classify (double centsApart)
        {
            const auto d = std::abs (centsApart);
            if (d < tolerance)
                return Interval::unison;
            const auto r = std::fmod (d, 1200.0);
            if (r < tolerance || r > 1200.0 - tolerance)
                return Interval::octave;
            if (std::abs (r - 702.0) < tolerance)
                return Interval::fifth;
            if (std::abs (r - 498.0) < tolerance)
                return Interval::fourth;
            if ((r >= 250.0 && r <= 450.0) || (r >= 750.0 && r <= 950.0))
                return Interval::imperfect;   // thirds and sixths, the neutral ones of the maqams too
            return Interval::dissonant;
        }
        bool perfect (Interval i) { return i == Interval::unison || i == Interval::octave || i == Interval::fifth; }
        // Parallels: unisons and octaves count as one kind.
        int kind (Interval i) { return i == Interval::fifth ? 5 : i == Interval::unison || i == Interval::octave ? 8 : 0; }

        // What a melodic move costs, `hard` if it is not sung: a tritone, a seventh, more than an
        // octave, a major sixth, or a minor sixth down.
        double melodic (const Keys& m, int from, int to, bool& leap)
        {
            const auto d = cents (m, to) - cents (m, from);
            const auto a = std::abs (d);
            leap = ! isStep (m, from, to) && from != to;
            if (a > 1200.0 + tolerance)
                return hard;
            const auto r = std::fmod (a, 1200.0);
            if (a > tolerance && std::abs (r - 600.0) < 40.0)
                return hard;
            if (a >= 950.0 && a <= 1200.0 - tolerance)
                return hard;
            if (a > 830.0 && a < 950.0)
                return hard;
            if (a >= 750.0 && a <= 830.0)
                return d > 0 ? 4.0 : hard;
            if (! leap)
                return 0.0;
            if (a <= 450.0)
                return 1.0;
            if (a <= 750.0)
                return 2.5;
            return 4.0;   // the octave
        }

        struct Slot
        {
            int64_t start = 0, end = 0;
            bool strong = false;        // on the bar line
            bool tied = false;          // held over from the note before (fourth species)
            bool phraseStart = false, phraseEnd = false, penultimate = false;
            int bar = 0;
            int cadence = -1;           // the melody's last pitch of the phrase (for ficta)
            int pinned = -1;            // a free part's note given as it is (freeVoice)
            bool strictEnds = true;     // the phrase begins and ends by the rules (a free part: its cadence only)
        };

        // The voice's rhythm against each note of the melody.
        std::vector<Slot> rhythm (const Line& cf, Species species, uint32_t seed, const FreePart* part = nullptr)
        {
            std::vector<Slot> slots;
            uint32_t random = seed * 2654435761u + 12345u;
            auto next = [&random] { random = random * 1664525u + 1013904223u; return random >> 8; };
            size_t first = 0;
            for (size_t i = 0; i < cf.size(); ++i)
            {
                if (i + 1 < cf.size() && cf[i + 1].start <= cf[i].end)
                    continue;   // the phrase goes on
                // cf[first..i] is a phrase.
                const auto lastPitch = cf[i].pitch;
                for (size_t b = first; b <= i; ++b)
                {
                    const auto s = cf[b].start, e = cf[b].end, d = e - s;
                    const bool firstBar = b == first, lastBar = b == i, penultimate = b + 1 == i;
                    std::vector<std::pair<double, bool>> parts;   // (where in the bar, tied); a negative place: a rest to there
                    if (lastBar)
                        parts = { { 0.0, false } };
                    else
                        switch (species)
                        {
                            case Species::first:  parts = { { 0.0, false } }; break;
                            case Species::second: parts = { { 0.0, false }, { 0.5, false } }; break;
                            case Species::third:  parts = { { 0.0, false }, { 0.25, false }, { 0.5, false }, { 0.75, false } }; break;
                            case Species::fourth:
                                parts = firstBar ? std::vector<std::pair<double, bool>> { { 0.5, false } }
                                                 : std::vector<std::pair<double, bool>> { { 0.0, true }, { 0.5, false } };
                                break;
                            case Species::fifth:
                            {
                                if (firstBar)
                                {
                                    parts = next() % 2 == 0 ? std::vector<std::pair<double, bool>> { { 0.5, false } }
                                                            : std::vector<std::pair<double, bool>> { { 0.0, false }, { 0.5, false } };
                                    break;
                                }
                                static const std::vector<std::vector<std::pair<double, bool>>> patterns {
                                    { { 0.0, false }, { 0.5, false } },
                                    { { 0.0, false }, { 0.25, false }, { 0.5, false }, { 0.75, false } },
                                    { { 0.0, false }, { 0.25, false }, { 0.5, false } },
                                    { { 0.0, false }, { 0.5, false }, { 0.75, false } },
                                    { { 0.0, true }, { 0.5, false } },
                                    { { 0.0, true }, { 0.5, false }, { 0.75, false } },
                                };
                                // The cadence: a suspension into the last bar, as Fux ends his.
                                parts = penultimate ? patterns[4] : patterns[next() % patterns.size()];
                                break;
                            }
                        }
                    const auto pinned = part != nullptr && b < part->pinned.size() ? part->pinned[b] : -1;
                    if (d < 4 || pinned >= 0)
                        parts = { { 0.0, false } };   // too short to divide, or given
                    for (size_t k = 0; k < parts.size(); ++k)
                    {
                        Slot slot;
                        slot.start = s + (int64_t) std::llround (parts[k].first * (double) d);
                        slot.end = k + 1 < parts.size() ? s + (int64_t) std::llround (parts[k + 1].first * (double) d) : e;
                        slot.strong = parts[k].first == 0.0;
                        slot.tied = parts[k].second;
                        slot.bar = (int) b;
                        slot.penultimate = penultimate;
                        slot.cadence = lastPitch;
                        slot.pinned = pinned;
                        slot.strictEnds = part == nullptr;
                        if (slot.end > slot.start)
                            slots.push_back (slot);
                    }
                }
                // The phrase's first and last notes.
                for (auto& sl : slots)
                    if (sl.bar == (int) first)
                    {
                        sl.phraseStart = true;
                        sl.tied = false;
                        break;
                    }
                slots.back().phraseEnd = true;
                first = i + 1;
            }
            if (part != nullptr && part->cadence && ! slots.empty())
                slots.back().strictEnds = true;
            return slots;
        }

        int sounding (const Line& line, int64_t t)
        {
            auto it = std::upper_bound (line.begin(), line.end(), t, [] (int64_t x, const Note& n) { return x < n.start; });
            if (it == line.begin())
                return -1;
            --it;
            return t < it->end ? it->pitch : -1;
        }

        uint32_t hash (uint32_t a, uint32_t b, uint32_t c)
        {
            auto h = a * 0x9E3779B1u ^ (b + 0x7F4A7C15u) * 0x85EBCA77u ^ (c + 0x165667B1u) * 0xC2B2AE3Du;
            h ^= h >> 15;
            h *= 0x2C1B3C6Du;
            h ^= h >> 12;
            return h;
        }

        struct Node
        {
            int pitch = -1;
            int parent = -1;
            double cost = 0.0;
            int dir = 0;              // the last move: -1 down, 0 none, 1 up
            double lastMove = 0.0;    // its size in cents, signed
            bool lastLeap = false;
            double leapRun = 0.0;     // cents leapt in one direction without turning
            int pending = 0;          // what the note after a dissonance must do (see below)
            int parallelRun = 0;      // thirds or sixths in a row against the melody
            int high = -1, highCount = 0;
            int lastStrong = -1;
            int bent = 0;
        };
        // pending: 0 nothing; 9 and 11 go on by step down or up (a passing note); 20 by step
        // either way (passing or neighbour); 30 fall a step to a consonance (a suspension).

        struct Voice
        {
            int rank = 1;             // above the melody 1, 2, 3; below -1, -2, -3
            Species species = Species::first;
        };

        Line write (const Line& cf, const std::vector<Line>& others, const std::vector<int>& ranks, const Voice& voice,
                    const Mode& mode, int newVoices, bool lowest, uint32_t seed, int& bent, const FreePart* part = nullptr)
        {
            const Keys m (mode);
            const auto slots = rhythm (cf, voice.species, seed + (uint32_t) (voice.rank + 8) * 7919u, part);
            const auto n = slots.size();
            if (n == 0)
                return {};

            // The register: around the melody's middle, a fifth (and more) above or a sixth (and more) below.
            std::vector<int> pitches;
            for (const auto& x : cf)
                pitches.push_back (x.pitch);
            std::nth_element (pitches.begin(), pitches.begin() + (long) pitches.size() / 2, pitches.end());
            const auto middle = pitches[pitches.size() / 2];
            const auto scale = mode.period / 12.0;
            const auto r = std::abs (voice.rank);
            const auto centre = part != nullptr ? part->centre : middle + (int) std::lround (scale * (voice.rank > 0 ? 7.0 * r : -(8.0 * r + 1.0)));
            const auto half = part != nullptr ? std::max (2, part->range) : (int) std::lround (scale * 9.0);
            auto lo = std::clamp (centre - half, 21, 108), hi = std::clamp (centre + half, 21, 108);
            if (part == nullptr && mode.below > 0 && mode.period == 12)
            {
                // A hypo range: the octave from `below` keys under a final, its middle nearest the centre.
                const auto bottomKey = ((mode.root - mode.below) % 12 + 12) % 12;
                auto bottom = centre - 6 - ((centre - 6 - bottomKey) % 12 + 12) % 12;
                if (centre - (bottom + 6) > 6)
                    bottom += 12;
                lo = std::clamp (bottom - 2, 21, 108);
                hi = std::clamp (bottom + 14, 21, 108);
            }

            // What the other voices sound at each slot.
            // And just before, as a voice moving within the slot before is heard moving into this one.
            std::vector<std::vector<int>> at (n, std::vector<int> (others.size(), -1)), justBefore = at;
            std::vector<int> prevStrong (n, -1);
            for (size_t i = 0, last = (size_t) -1; i < n; ++i)
            {
                for (size_t o = 0; o < others.size(); ++o)
                {
                    at[i][o] = sounding (others[o], slots[i].start);
                    justBefore[i][o] = i > 0 ? sounding (others[o], slots[i].start - 1) : -1;
                }
                prevStrong[i] = last == (size_t) -1 || slots[i].phraseStart ? -1 : (int) last;
                if (slots[i].strong)
                    last = i;
                if (slots[i].phraseStart)
                    prevStrong[i] = -1;
            }

            constexpr size_t beam = 256;
            std::vector<std::vector<Node>> layers;
            layers.push_back ({ Node {} });   // before the first note
            for (size_t i = 0; i < n; ++i)
            {
                const auto& slot = slots[i];
                std::vector<int> candidates;
                for (int p = lo; p <= hi; ++p)
                    if (inScale (m, p) && slot.pinned < 0)
                        candidates.push_back (p);
                if (slot.pinned >= 0)
                    candidates.push_back (std::clamp (slot.pinned, 0, 127));
                // Musica ficta: at a cadence the note a whole step below where it lands, raised.
                if (mode.ficta && mode.period == 12 && slot.pinned < 0 && (slot.penultimate || slot.phraseEnd))
                    for (int p = lo; p <= hi; ++p)
                    {
                        const auto pc = ((p - slot.cadence) % 12 + 12) % 12;
                        if (pc == 11 && ! inScale (m, p) && inScale (m, p - 1))
                            candidates.push_back (p);
                    }

                std::vector<Node> next;
                const auto& previous = layers.back();
                for (size_t pi = 0; pi < previous.size(); ++pi)
                {
                    const auto& from = previous[pi];
                    const bool fresh = slot.phraseStart || from.pitch < 0;
                    const auto pp = from.pitch;
                    for (auto p : candidates)
                    {
                        Node node;
                        node.pitch = p;
                        node.parent = (int) pi;
                        node.high = from.high;
                        node.highCount = from.highCount;
                        node.lastStrong = slot.strong ? p : from.lastStrong;
                        double cost = 0.0;
                        int broke = 0;
                        auto rule = [&] (bool broken) { if (broken) { cost += hard; ++broke; } };

                        bool leap = false;
                        double move = 0.0;
                        const bool tie = slot.tied && ! fresh && p == pp;
                        if (! fresh)
                        {
                            move = cents (m, p) - cents (m, pp);
                            if (slot.tied && ! tie)
                                cost += voice.species == Species::fourth ? 3.0 : 1.0;   // the syncopation broken
                            if (! tie)
                            {
                                const auto c = melodic (m, pp, p, leap);
                                rule (c >= hard);
                                cost += c < hard ? c : 0.0;
                                if (p == pp)
                                    cost += voice.species == Species::first ? 4.0 : 10.0;
                                // A leap of more than a fourth is turned back by step.
                                if (from.lastLeap && std::abs (from.lastMove) > 520.0 && ! (! leap && move * from.lastMove < 0.0))
                                    cost += 6.0;
                                if (leap && from.lastLeap && move * from.lastMove > 0.0)
                                {
                                    cost += 4.0;
                                    rule (std::abs (from.leapRun) + std::abs (move) > 1200.0 + tolerance);
                                }
                                // A raga's āroha going up, its avaroha coming down.
                                const auto key = (size_t) m.key[p];
                                if (p > pp && ! mode.up.empty())
                                    rule (! mode.up[key]);
                                if (p < pp && ! mode.down.empty())
                                    rule (! mode.down[key]);
                            }
                            switch (from.pending)
                            {
                                case 9: case 11: rule (! isStep (m, pp, p) || (p - pp > 0 ? 11 : 9) != from.pending); break;
                                case 20: rule (! isStep (m, pp, p)); break;
                                default: break;
                            }
                        }
                        node.dir = tie || fresh ? (fresh ? 0 : from.dir) : (move > 0.0 ? 1 : move < 0.0 ? -1 : 0);
                        node.lastMove = tie ? from.lastMove : move;
                        node.lastLeap = tie ? from.lastLeap : leap;
                        node.leapRun = leap ? (from.lastLeap && move * from.lastMove > 0.0 ? from.leapRun + move : move) : 0.0;

                        // Against the other voices.
                        int bass = p;
                        for (auto q : at[i])
                            if (q >= 0)
                                bass = std::min (bass, q);
                        bool dissonant = false;
                        Interval withCantus = Interval::unison;
                        bool cantusSounds = false;
                        for (size_t o = 0; o < others.size(); ++o)
                        {
                            const auto q = at[i][o];
                            if (q < 0)
                                continue;
                            auto iv = classify (cents (m, p) - cents (m, q));
                            if (iv == Interval::fourth && std::min (p, q) == bass)
                                iv = Interval::dissonant;   // a fourth over the bass
                            if (iv == Interval::fourth && newVoices == 1)
                                iv = Interval::dissonant;   // in two parts the bass is always one of them
                            dissonant = dissonant || iv == Interval::dissonant;
                            if (o == 0)
                            {
                                withCantus = iv;
                                cantusSounds = true;
                            }
                            // Unisons only at the ends.
                            if (iv == Interval::unison && slot.strong && ! slot.phraseStart && ! slot.phraseEnd)
                            {
                                if (newVoices == 1 && voice.species == Species::first)
                                    rule (true);
                                else
                                    cost += 5.0;
                            }
                            // Voices keep their places.
                            if ((voice.rank > ranks[o] && p < q) || (voice.rank < ranks[o] && p > q))
                                cost += 8.0;

                            if (fresh || tie)
                                continue;
                            const auto qp = justBefore[i][o];
                            if (qp >= 0 && pp >= 0)
                            {
                                const auto before = classify (cents (m, pp) - cents (m, qp));
                                const bool bothMove = pp != p && qp != q;
                                const bool similar = bothMove && (p - pp > 0) == (q - qp > 0);
                                // Parallel fifths and octaves, and octaves by contrary motion.
                                if (bothMove && kind (iv) != 0 && kind (iv) == kind (before))
                                    rule (true);
                                // Hidden fifths and octaves: into a perfect consonance by similar motion with a leap.
                                else if (similar && perfect (iv) && leap)
                                    cost += 4.0;
                                if (o == 0)
                                {
                                    cost += ! bothMove ? 0.3 : similar ? (iv == before ? 1.0 : 0.8) : 0.0;
                                    node.parallelRun = similar && iv == Interval::imperfect && before == Interval::imperfect ? from.parallelRun + 1 : 0;
                                    if (node.parallelRun > 3)
                                        cost += 2.0;
                                }
                            }
                            // Fifths or octaves on beat after beat (Fux's "ottava battuta") sound parallel too.
                            if (slot.strong && voice.species != Species::first && prevStrong[i] >= 0 && from.lastStrong >= 0)
                            {
                                const auto qs = at[(size_t) prevStrong[i]][o];
                                if (qs >= 0 && qs != q && from.lastStrong != p
                                    && kind (iv) != 0 && kind (iv) == kind (classify (cents (m, from.lastStrong) - cents (m, qs))))
                                    cost += 4.0;
                            }
                        }

                        // Dissonance: passing or neighbouring on the weak beats, or prepared and resolved on the strong.
                        if (! fresh && from.pending == 30)
                            rule (! (! tie && p < pp && isStep (m, pp, p) && ! dissonant));
                        if (dissonant)
                        {
                            cost += 0.5;
                            if (slot.phraseStart || slot.phraseEnd || fresh)
                                rule (true);
                            else if (slot.strong)
                            {
                                const bool suspension = tie && (voice.species == Species::fourth || voice.species == Species::fifth);
                                rule (! suspension);
                                node.pending = 30;
                            }
                            else
                            {
                                rule (tie || ! isStep (m, pp, p) || voice.species == Species::fourth);
                                node.pending = voice.species == Species::second ? (p > pp ? 11 : 9) : 20;
                            }
                        }
                        else if (slot.strong && cantusSounds && perfect (withCantus) && ! slot.phraseStart && ! slot.phraseEnd)
                            cost += 1.2;   // imperfect consonances in the middle

                        // Beginning and ending.
                        if (slot.phraseStart && cantusSounds && slot.strictEnds)
                        {
                            // A fifth below would put the music in another mode; the bass starts on the melody's note.
                            const bool ok = withCantus == Interval::unison || withCantus == Interval::octave
                                            || (withCantus == Interval::fifth && voice.rank > 0);
                            if (! ok)
                            {
                                if (! lowest && (std::abs (voice.rank) > 1 || (newVoices > 1 && withCantus == Interval::imperfect)))
                                    cost += 2.0;
                                else
                                    rule (true);
                            }
                        }
                        if (slot.phraseEnd && cantusSounds && slot.strictEnds)
                        {
                            const bool octave = withCantus == Interval::unison || withCantus == Interval::octave;
                            if (! octave)
                            {
                                if (lowest || newVoices == 1)
                                    rule (true);
                                else
                                    cost += withCantus == Interval::fifth ? 2.0 : withCantus == Interval::imperfect ? 4.0 : hard;
                            }
                            if (! fresh && ! isStep (m, pp, p))
                                cost += 30.0;   // the last note reached by step
                        }

                        // One high point.
                        if (p > node.high)
                        {
                            node.high = p;
                            node.highCount = 1;
                        }
                        else if (p == node.high && ! tie)
                        {
                            ++node.highCount;
                            cost += 1.5;
                        }
                        cost += 0.15 * std::abs (p - centre) / std::max (scale, 0.25);
                        cost += (hash (seed, (uint32_t) i, (uint32_t) p) & 0xFFFF) / 65535.0;   // the seed's taste
                        if (slot.pinned >= 0)
                        {
                            // Given: what it costs against the others is the same whichever way it is
                            // reached, so only the way in counts; it is not this voice's to bend, and
                            // owes nothing after it.
                            broke = 0;
                            node.pending = 0;
                        }
                        node.cost = from.cost + cost;
                        node.bent = from.bent + broke;
                        next.push_back (node);
                    }
                }
                // The best few, one for each pitch and what it still owes.
                std::sort (next.begin(), next.end(), [] (const Node& a, const Node& b) { return a.cost < b.cost; });
                std::vector<Node> kept;
                std::unordered_set<int> seen;
                for (const auto& x : next)
                {
                    const auto key = x.pitch * 4096 + x.pending * 64 + (x.dir + 1) * 16 + std::min (x.parallelRun, 4) * 2 + (x.lastLeap ? 1 : 0);
                    if (! seen.insert (key).second)
                        continue;
                    kept.push_back (x);
                    if (kept.size() >= beam)
                        break;
                }
                layers.push_back (std::move (kept));
            }

            // Back from the best end, tied notes joined.
            const auto& last = layers.back();
            size_t best = 0;
            for (size_t k = 1; k < last.size(); ++k)
                if (last[k].cost < last[best].cost)
                    best = k;
            bent += last[best].bent;
            std::vector<int> chosen (n);
            for (size_t i = n, k = best; i-- > 0;)
            {
                chosen[i] = layers[i + 1][k].pitch;
                k = (size_t) layers[i + 1][k].parent;
            }
            int velocity = 0;
            for (const auto& x : cf)
                velocity += x.velocity;
            velocity = std::clamp (velocity / (int) cf.size(), 1, 127);
            Line out;
            for (size_t i = 0; i < n; ++i)
            {
                if (slots[i].pinned >= 0)
                    continue;
                if (slots[i].tied && ! slots[i].phraseStart && ! out.empty() && out.back().pitch == chosen[i] && out.back().end == slots[i].start)
                    out.back().end = slots[i].end;
                else
                    out.push_back ({ slots[i].start, slots[i].end, chosen[i], velocity });
            }
            return out;
        }
    }

    Mode Mode::chromatic (int root)
    {
        Mode m;
        m.root = ((root % 12) + 12) % 12;
        for (int k = 0; k < 12; ++k)
        {
            m.keyCents.push_back (k * 100.0);
            m.scale.push_back (true);
        }
        return m;
    }

    Mode Mode::fromTuning (const mixer::TuningSystem& t, int root)
    {
        Mode m;
        m.root = ((root % 12) + 12) % 12;
        if (t.divisions > 0)
        {
            m.period = t.divisions;
            for (int k = 0; k < t.divisions; ++k)
            {
                m.keyCents.push_back (1200.0 * k / t.divisions);
                m.scale.push_back (true);
            }
            return m;
        }
        if (t.cents.size() > 12)   // a degree a key, as the tuning plays it
        {
            m.period = (int) t.cents.size();
            m.keyCents = t.cents;
            m.scale.assign (t.cents.size(), true);
            return m;
        }
        const auto keys = mixer::keyCents (t);
        m.keyCents.assign (keys.begin(), keys.end());
        m.scale.assign (12, false);
        for (auto c : mixer::octaveCents (t))   // a scale repeating at a fourth: its degrees in the octave
            m.scale[(size_t) (((int) std::floor (c / 100.0 + 0.5) % 12 + 12) % 12)] = true;
        m.ficta = t.family == "church mode" || t.family == "hypo-mode";
        return m;
    }

    bool Mode::valid() const
    {
        return period > 0 && (int) keyCents.size() == period && (int) scale.size() == period
               && std::find (scale.begin(), scale.end(), true) != scale.end()
               && (up.empty() || (int) up.size() == period) && (down.empty() || (int) down.size() == period);
    }

    Line cantus (const midifile::Song& song)
    {
        // The notes, each on with its off.
        Line notes;
        std::vector<std::vector<size_t>> open (16 * 128);
        for (const auto& e : song.events)
        {
            if (e.channel() == 9)
                continue;
            const auto key = (size_t) e.channel() * 128 + (e.data1 & 0x7F);
            if (e.kind() == 0x90 && e.data2 > 0)
            {
                open[key].push_back (notes.size());
                notes.push_back ({ e.tick, -1, e.data1 & 0x7F, e.data2 });
            }
            else if ((e.kind() == 0x80 || e.kind() == 0x90) && ! open[key].empty())
            {
                notes[open[key].front()].end = e.tick;
                open[key].erase (open[key].begin());
            }
        }
        for (auto& x : notes)
            if (x.end < 0)
                x.end = std::max (x.start + 1, song.lastTick);
        std::stable_sort (notes.begin(), notes.end(), [] (const Note& a, const Note& b)
        {
            return a.start != b.start ? a.start < b.start : a.pitch > b.pitch;
        });
        // The top note at each onset, held to the next unless a rest of an eighth or more comes between.
        const int64_t gap = std::max<int64_t> (1, song.ticksPerQuarter > 0 ? song.ticksPerQuarter / 2 : 1);
        Line line;
        for (const auto& x : notes)
        {
            if (! line.empty() && line.back().start == x.start)
                continue;
            if (! line.empty())
            {
                auto& before = line.back();
                if (x.start - before.end < gap || before.end > x.start)
                    before.end = x.start;
            }
            line.push_back (x);
        }
        line.erase (std::remove_if (line.begin(), line.end(), [] (const Note& x) { return x.end <= x.start; }), line.end());
        return line;
    }

    Mode guessMode (const Line& cf)
    {
        if (cf.empty())
            return Mode::chromatic (0);
        bool used[12] {};
        for (const auto& x : cf)
            used[x.pitch % 12] = true;
        const auto final = cf.back().pitch % 12;
        // The diatonic sets, fewest sharps and flats first.
        static const int major[7] { 0, 2, 4, 5, 7, 9, 11 };
        for (int t : { 0, 7, 5, 2, 10, 9, 3, 4, 8, 11, 1, 6 })
        {
            bool in[12] {};
            for (auto d : major)
                in[(t + d) % 12] = true;
            bool all = true;
            for (int pc = 0; pc < 12; ++pc)
                all = all && (! used[pc] || in[pc]);
            if (! all)
                continue;
            auto m = Mode::chromatic (final);
            for (int k = 0; k < 12; ++k)
                m.scale[(size_t) k] = in[(final + k) % 12];
            m.ficta = true;
            return m;
        }
        auto m = Mode::chromatic (final);
        for (int k = 0; k < 12; ++k)
            m.scale[(size_t) k] = used[(final + k) % 12];
        return m;
    }

    Result generate (const Line& cf, const Settings& settings)
    {
        Result result;
        if (cf.empty())
            return result;
        const auto mode = settings.mode.valid() ? settings.mode : guessMode (cf);
        const auto count = std::clamp (settings.voices, 1, 3);

        // Each new voice's place, nearest the melody first.
        std::vector<int> places;
        switch (settings.placement)
        {
            case Placement::above: for (int k = 1; k <= count; ++k) places.push_back (k); break;
            case Placement::below: for (int k = 1; k <= count; ++k) places.push_back (-k); break;
            case Placement::around: places = std::vector<int> { 1, -1, 2 }; places.resize ((size_t) count); break;
        }
        std::vector<Voice> voices;
        for (size_t k = 0; k < places.size(); ++k)
            voices.push_back ({ places[k], k == 0 || settings.allInSpecies ? settings.species : Species::first });

        // The voices note against note first, the bass before them, so the one in the species is
        // written against them all.
        std::vector<size_t> order (voices.size());
        for (size_t k = 0; k < order.size(); ++k)
            order[k] = k;
        std::stable_sort (order.begin(), order.end(), [&] (size_t a, size_t b)
        {
            const bool fa = voices[a].species == Species::first, fb = voices[b].species == Species::first;
            return fa != fb ? fa : voices[a].rank < 0 && voices[a].rank < voices[b].rank;
        });
        // Each voice is written against those before it, so one can leave the next nothing that
        // keeps the rules: then all are written again with another seed, and the best kept.
        const int attempts = cf.size() > 64 ? 3 : 8;   // a long melody takes a while each time
        for (int attempt = 0; attempt < attempts; ++attempt)
        {
            std::vector<Line> lines (voices.size());
            int bent = 0;
            for (auto k : order)
            {
                std::vector<int> otherRanks { 0 };
                std::vector<Line> written { cf };
                for (size_t j = 0; j < voices.size(); ++j)
                    if (j != k && ! lines[j].empty())
                    {
                        written.push_back (lines[j]);
                        otherRanks.push_back (voices[j].rank);
                    }
                // The lowest voice below the melody is the bass: it starts and ends on the melody's note.
                const bool lowest = voices[k].rank < 0 && std::all_of (voices.begin(), voices.end(), [&] (const Voice& v) { return v.rank >= voices[k].rank; });
                lines[k] = write (cf, written, otherRanks, voices[k], mode, count, lowest, settings.seed + (uint32_t) attempt * 1000003u, bent);
            }
            if (attempt == 0 || bent < result.bent)
            {
                result.voices = std::move (lines);
                result.bent = bent;
            }
            if (result.bent == 0)
                break;
        }
        for (auto& v : voices)
        {
            static const char* above[] { "above", "high above", "highest" };
            static const char* below[] { "below", "low below", "lowest" };
            result.names.push_back (v.rank > 0 ? above[v.rank - 1] : below[-v.rank - 1]);
        }
        return result;
    }

    Line freeVoice (const FreePart& part, const Mode& mode, uint32_t seed, int& bent)
    {
        if (part.grid.empty() || ! mode.valid() || part.against.empty() || part.ranks.size() != part.against.size())
            return {};
        return write (part.grid, part.against, part.ranks, { part.rank, part.species }, mode, std::max (1, part.parts - 1),
                      part.lowest, seed, bent, &part);
    }

    midifile::SongPtr toSong (const midifile::Song& like, const Line& line, int channel)
    {
        auto out = std::make_shared<midifile::Song> (like);
        out->format = 0;
        out->events.clear();
        out->trackNames.clear();
        channel = std::clamp (channel, 0, 15);
        struct Timed { midifile::Event e; int order; };
        std::vector<Timed> all;
        for (const auto& x : line)
        {
            all.push_back ({ { x.start, (uint8_t) (0x90 | channel), (uint8_t) std::clamp (x.pitch, 0, 127), (uint8_t) std::clamp (x.velocity, 1, 127) }, 1 });
            all.push_back ({ { std::max (x.end, x.start + 1), (uint8_t) (0x80 | channel), (uint8_t) std::clamp (x.pitch, 0, 127), 0 }, 0 });
        }
        std::stable_sort (all.begin(), all.end(), [] (const Timed& a, const Timed& b)
        {
            return a.e.tick != b.e.tick ? a.e.tick < b.e.tick : a.order < b.order;   // offs before ons
        });
        for (const auto& t : all)
            out->events.push_back (t.e);
        out->channelsUsed = line.empty() ? 0 : (uint16_t) (1u << channel);
        for (const auto& e : out->events)
            out->lastTick = std::max (out->lastTick, e.tick);
        return out;
    }
}

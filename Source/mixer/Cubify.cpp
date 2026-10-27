// SPDX-License-Identifier: AGPL-3.0-only
#include "Cubify.h"

#include "MidiFile.h"

#include <algorithm>
#include <cmath>
#include <map>
#include <numeric>
#include <set>
#include <sstream>

namespace cubify
{
    namespace
    {
        // Formalized Music, figure VIII-6: where each rotation sends the first tetrahedron's
        // corners (1-4); the second's (5-8) follow, each four on. Here from 0.
        struct Entry { const char* label; std::array<int, 4> half; };
        constexpr Entry table[24] {
            { "I", { 1, 2, 3, 4 } }, { "A", { 2, 1, 4, 3 } }, { "B", { 3, 4, 1, 2 } }, { "C", { 4, 3, 2, 1 } },
            { "D2", { 2, 3, 1, 4 } }, { "D", { 3, 1, 2, 4 } }, { "E2", { 2, 4, 3, 1 } }, { "E", { 4, 1, 3, 2 } },
            { "G2", { 3, 2, 4, 1 } }, { "G", { 4, 2, 1, 3 } }, { "L2", { 1, 3, 4, 2 } }, { "L", { 1, 4, 2, 3 } },
            { "Q7", { 7, 8, 6, 5 } }, { "Q2", { 7, 6, 5, 8 } }, { "Q3", { 8, 6, 7, 5 } }, { "Q11", { 6, 7, 8, 5 } },
            { "Q8", { 6, 8, 5, 7 } }, { "Q6", { 6, 5, 7, 8 } }, { "Q1", { 8, 7, 5, 6 } }, { "Q5", { 7, 5, 8, 6 } },
            { "Q9", { 5, 8, 7, 6 } }, { "Q10", { 5, 7, 6, 8 } }, { "Q4", { 8, 5, 6, 7 } }, { "Q12", { 5, 6, 8, 7 } },
        };

        std::array<Rotation, 24> build()
        {
            std::array<Rotation, 24> all {};
            for (int r = 0; r < 24; ++r)
            {
                all[(size_t) r].label = table[r].label;
                for (int i = 0; i < 4; ++i)
                {
                    const auto to = table[r].half[(size_t) i] - 1;
                    all[(size_t) r].seq[(size_t) i] = to;
                    all[(size_t) r].seq[(size_t) i + 4] = to < 4 ? to + 4 : to - 4;
                }
                all[(size_t) r].swaps = table[r].half[0] > 4;
            }
            return all;
        }

        int indexOf (const std::array<int, 8>& seq)
        {
            const auto& all = group();
            for (int r = 0; r < 24; ++r)
                if (all[(size_t) r].seq == seq)
                    return r;
            return 0;
        }

        constexpr int velocities[8] { 20, 33, 46, 60, 74, 90, 106, 122 };
        constexpr double gates[8] { 0.12, 0.35, 0.7, 0.96, 1.0, 0.8, 0.5, 0.9 };
        constexpr int defaultSixteenths[8] { 1, 2, 3, 4, 6, 8, 12, 16 };

        constexpr double pi = 3.14159265358979323846;

        bool loudAttack (Articulation a) { return a == Articulation::accent || a == Articulation::marcato || a == Articulation::sforzando; }

        // Eight values a cube holds on its corners, and what its track can play.
        struct Cubes
        {
            std::array<int, 8> pitch {};       // quarter steps above MIDI key 0 (key * 2)
            std::array<int, 8> duration {};    // sixteenths
            std::array<int, 8> dynamic {};     // 0 ppp .. 7 fff
            std::array<Articulation, 8> articulation {};
            bool drums = false;
        };

        // Sieve points, as quarter steps, from `low` to `high`.
        std::vector<int> pitchesIn (const Settings& s, const stochastic::Sieve& sieve, int root, int low, int high)
        {
            std::vector<int> out;
            low = std::max (0, low);
            high = std::min (254, high);
            for (int q = low; q <= high; ++q)
            {
                if (s.pitches == Pitches::messiaen || ! s.quarterTones)
                {
                    if (q % 2 != 0)
                        continue;
                    const auto key = q / 2;
                    if (sieve.contains (s.pitches == Pitches::messiaen ? key - root : key))
                        out.push_back (q);
                }
                else if (sieve.contains (q))
                    out.push_back (q);
            }
            return out;
        }

        // Eight spread evenly over `from` (sorted), starting `phase` (0..1) of a step in.
        std::array<int, 8> spread (const std::vector<int>& from, double phase)
        {
            std::array<int, 8> out {};
            const auto n = from.size();
            for (size_t k = 0; k < 8; ++k)
                out[k] = from[std::min (n - 1, (size_t) std::floor (((double) k + phase) * (double) n / 8.0))];
            return out;
        }

        std::string pitchName (int q)
        {
            static const char* names[12] { "C", "C♯", "D", "E♭", "E", "F", "F♯", "G", "A♭", "A", "B♭", "B" };
            const auto key = q / 2;
            return std::string (names[key % 12]) + std::to_string (key / 12 - 1) + (q % 2 != 0 ? "↑" : "");
        }

        // A drawn note, before it is rendered.
        struct Drawn
        {
            int64_t start = 0, ioi = 0;   // ticks: when, and until the next
            int pitch = 0;                // quarter steps
            int dynamic = 4;
            Articulation articulation = Articulation::tenuto;
            bool rest = false;
            // V2: a swell, and a slide over the second half by this many cents.
            bool swell = false;
            int glide = 0;
        };

        int bendFor (double cents) { return std::clamp ((int) std::lround (8192.0 + cents / 200.0 * 8191.0), 0, 16383); }

        std::vector<int> pathFrom (stochastic::Random& random, int fallbackA, int fallbackB, bool xenakis)
        {
            if (xenakis)
                return chain (fallbackA, fallbackB, 18);
            for (int attempt = 0; attempt < 64; ++attempt)
            {
                const auto a = 1 + (int) (random.next() % 23);
                const auto b = 1 + (int) (random.next() % 23);
                auto path = chain (a, b, 18);
                if (std::set<int> (path.begin(), path.end()).size() >= 8)
                    return path;
            }
            return chain (fallbackA, fallbackB, 18);
        }
    }

    //==========================================================================
    const std::array<Rotation, 24>& group()
    {
        static const auto all = build();
        return all;
    }

    int find (const std::string& label)
    {
        for (int r = 0; r < 24; ++r)
            if (label == table[r].label)
                return r;
        return -1;
    }

    int product (int p, int q)
    {
        const auto& all = group();
        std::array<int, 8> seq {};
        for (size_t i = 0; i < 8; ++i)
            seq[i] = all[(size_t) q].seq[(size_t) all[(size_t) p].seq[i]];
        return indexOf (seq);
    }

    int inverse (int r)
    {
        std::array<int, 8> seq {};
        const auto& s = group()[(size_t) r].seq;
        for (int i = 0; i < 8; ++i)
            seq[(size_t) s[(size_t) i]] = i;
        return indexOf (seq);
    }

    int power (int r, int n)
    {
        int out = 0;
        for (int i = 0; i < n; ++i)
            out = product (out, r);
        return out;
    }

    std::vector<int> chain (int a, int b, int n)
    {
        std::vector<int> out;
        for (int i = 0; i < n; ++i)
        {
            out.push_back (a);
            const auto next = product (a, b);
            a = b;
            b = next;
        }
        return out;
    }

    const char* articulationName (Articulation a)
    {
        static const char* names[8] { "staccatissimo", "staccato", "portato", "tenuto", "legato", "accent", "marcato", "sforzando" };
        return names[(int) a];
    }

    const char* dynamicName (int level)
    {
        static const char* names[8] { "ppp", "pp", "p", "mp", "mf", "f", "ff", "fff" };
        return names[std::clamp (level, 0, 7)];
    }

    Part partOf (const midifile::Song& song, const std::string& name, int ticksPerQuarter)
    {
        Part part;
        part.name = name;
        const auto scale = song.ticksPerQuarter > 0 ? (double) ticksPerQuarter / song.ticksPerQuarter : 1.0;
        std::map<std::pair<int, int>, std::vector<std::pair<int64_t, int>>> held;   // (channel, key): starts, velocities
        int drumNotes = 0, notes = 0;
        for (const auto& e : song.events)
        {
            const auto key = std::make_pair (e.channel(), (int) e.data1);
            if (e.kind() == 0x90 && e.data2 > 0)
                held[key].push_back ({ e.tick, e.data2 });
            else if ((e.kind() == 0x80 || (e.kind() == 0x90 && e.data2 == 0)) && ! held[key].empty())
            {
                const auto [start, velocity] = held[key].front();
                held[key].erase (held[key].begin());
                part.notes.push_back ({ (int64_t) std::llround ((double) start * scale), (int64_t) std::llround ((double) e.tick * scale), e.data1, velocity });
                ++notes;
                if (e.channel() == 9)
                    ++drumNotes;
            }
        }
        part.drums = notes > 0 && drumNotes * 2 > notes;
        std::sort (part.notes.begin(), part.notes.end(), [] (const Note& a, const Note& b) { return a.start < b.start; });
        return part;
    }

    //==========================================================================
    Result arrange (const std::vector<Part>& parts, const Settings& s)
    {
        Result result;
        const auto n = (int) parts.size();
        if (n == 0 || n > 8)
        {
            result.error = n == 0 ? "nothing to cubify: the scene has no clips" : "a cube has eight corners: cubify takes at most eight tracks";
            return result;
        }
        std::string why;
        auto sieve = s.pitches == Pitches::messiaen ? stochastic::Sieve {} : stochastic::Sieve::parse (s.sieve, why);
        if (s.pitches == Pitches::messiaen)
        {
            static const uint16_t modes[7] { 0x555, 0x6db, 0xddd, 0x9e7, 0x8e3, 0xd75, 0xbef };
            sieve = stochastic::Sieve::fromKeys (modes[std::clamp (s.messiaenMode, 1, 7) - 1]);
        }
        else if (sieve.empty())
        {
            result.error = "the sieve does not read: " + why;
            return result;
        }
        stochastic::Random random (s.seed * 0x9E3779B97F4A7C15ull + 1);
        const int unit = std::max (1, s.ticksPerQuarter / 4);   // a sixteenth
        const int64_t bar = 4LL * s.ticksPerQuarter;
        const auto readings = std::clamp (s.readings, 1, 4);

        // The grand cube: the lower half of the parts by register on the first tetrahedron, the
        // upper on the second, so like sits with like and no two of them share an edge.
        std::vector<int> order ((size_t) n);
        std::iota (order.begin(), order.end(), 0);
        auto registerOf = [&] (int p)
        {
            const auto& part = parts[(size_t) p];
            if (part.audio)
                return 1000.0;
            if (part.drums || part.notes.empty())
                return -1.0;
            double sum = 0;
            for (const auto& x : part.notes)
                sum += x.pitch;
            return sum / (double) part.notes.size();
        };
        std::stable_sort (order.begin(), order.end(), [&] (int a, int b) { return registerOf (a) < registerOf (b); });
        result.corners.assign ((size_t) n, 0);
        std::array<int, 8> partAt;
        partAt.fill (-1);
        const auto lower = (n + 1) / 2;
        for (int i = 0; i < n; ++i)
        {
            const auto corner = i < lower ? i : 4 + (i - lower);
            result.corners[(size_t) order[(size_t) i]] = corner;
            partAt[(size_t) corner] = order[(size_t) i];
        }

        // Each corner's cubes: its track's, or for a corner with none (or an audio loop), a
        // ghost of the nearest track with notes, a fifth lower or higher.
        int anyMelodic = -1;
        for (int p = 0; p < n; ++p)
            if (! parts[(size_t) p].audio && ! parts[(size_t) p].notes.empty() && (anyMelodic < 0 || parts[(size_t) anyMelodic].drums))
                anyMelodic = p;
        std::array<Cubes, 8> cubes;
        for (int c = 0; c < 8; ++c)
        {
            auto source = partAt[(size_t) c];
            int shift = 0;
            if (source < 0 || parts[(size_t) source].audio || parts[(size_t) source].notes.empty())
            {
                source = -1;
                for (int d = 1; d < 8 && source < 0; ++d)
                    for (const auto other : { c - d, c + d })
                        if (other >= 0 && other < 8 && partAt[(size_t) other] >= 0)
                        {
                            const auto& o = parts[(size_t) partAt[(size_t) other]];
                            if (! o.audio && ! o.notes.empty())
                            {
                                source = partAt[(size_t) other];
                                break;
                            }
                        }
                if (source < 0)
                    source = anyMelodic;
                shift = (c < 4 ? -1 : 1) * (10 + 4 * (c % 4));   // a fourth, fifth, sixth or seventh away
            }
            auto& cube = cubes[(size_t) c];
            for (int k = 0; k < 8; ++k)
                cube.dynamic[(size_t) k] = k;
            const auto turn = (int) (random.next() % 8);
            for (int k = 0; k < 8; ++k)
                cube.articulation[(size_t) k] = (Articulation) ((k + turn) % 8);
            for (int k = 0; k < 8; ++k)
                cube.duration[(size_t) k] = defaultSixteenths[k];
            if (source < 0)
            {
                for (int k = 0; k < 8; ++k)
                    cube.pitch[(size_t) k] = 120 + 4 * k;
                continue;
            }
            const auto& part = parts[(size_t) source];
            cube.drums = part.drums;
            int lowKey = 127, highKey = 0;
            for (const auto& x : part.notes)
            {
                lowKey = std::min (lowKey, x.pitch);
                highKey = std::max (highKey, x.pitch);
            }

            if (part.drums)
            {
                // A drum part keeps its own drums: its eight most played.
                std::map<int, int> count;
                for (const auto& x : part.notes)
                    ++count[x.pitch];
                std::vector<std::pair<int, int>> byCount;
                for (const auto& [key, times] : count)
                    byCount.push_back ({ -times, key });
                std::sort (byCount.begin(), byCount.end());
                for (size_t k = 0; k < 8; ++k)
                    cube.pitch[k] = 2 * byCount[k % byCount.size()].second;
            }
            else
            {
                // Pitches from the sieve (or the mode) over the track's register, two octaves at least.
                const auto centre = lowKey + highKey + shift;   // quarter steps
                const auto half = std::max (24, highKey - lowKey + 2);
                int low = centre - half, high = centre + half;
                std::vector<int> found;
                if (s.pitches == Pitches::clip)
                {
                    std::set<int> own;
                    for (const auto& x : part.notes)
                        own.insert (2 * x.pitch + shift);
                    found.assign (own.begin(), own.end());
                }
                if (found.size() < 8)
                {
                    auto widen = 0;
                    std::vector<int> more;
                    const auto root = s.root >= 0 ? s.root % 12 : lowKey;
                    while ((more = pitchesIn (s, sieve, root, low - widen, high + widen)).size() + found.size() < 8 && widen < 256)
                        widen += 4;
                    if (s.pitches == Pitches::clip)
                    {
                        // The clip's own, and the sieve's nearest to them.
                        std::sort (more.begin(), more.end(), [&] (int a, int b) { return std::abs (a - centre) < std::abs (b - centre); });
                        for (auto q : more)
                            if (found.size() < 8 && std::find (found.begin(), found.end(), q) == found.end())
                                found.push_back (q);
                        std::sort (found.begin(), found.end());
                    }
                    else
                        found = more;
                }
                if (found.empty())
                    for (int k = 0; k < 8; ++k)
                        found.push_back (std::clamp (centre - 16 + 4 * k, 0, 254));
                cube.pitch = spread (found, random.uniform());
            }

            if (s.clipDurations)
            {
                // The clip's own durations, from one onset to the next, on the sixteenths.
                std::set<int> found;
                for (size_t i = 1; i < part.notes.size(); ++i)
                    if (const auto gap = part.notes[i].start - part.notes[i - 1].start; gap > 0)
                        found.insert (std::clamp ((int) std::lround ((double) gap / unit), 1, 16));
                std::vector<int> list (found.begin(), found.end());
                if (list.size() > 8)
                {
                    const auto eight = spread (list, 0.0);
                    list.assign (eight.begin(), eight.end());
                }
                for (int k = 0; k < 8 && list.size() < 8; ++k)
                    if (std::find (list.begin(), list.end(), defaultSixteenths[k]) == list.end())
                        list.push_back (defaultSixteenths[k]);
                std::sort (list.begin(), list.end());
                for (size_t k = 0; k < 8; ++k)
                    cube.duration[k] = list[k];
            }
        }
        // A part's own range: its clip's register, a fifth beyond each end.
        std::vector<std::pair<int, int>> ranges ((size_t) n, { 0, 254 });
        for (int p = 0; p < n; ++p)
        {
            const auto& part = parts[(size_t) p];
            if (part.notes.empty() || part.drums)
                continue;
            int lowKey = 127, highKey = 0;
            for (const auto& x : part.notes)
            {
                lowKey = std::min (lowKey, x.pitch);
                highKey = std::max (highKey, x.pitch);
            }
            const auto& own = cubes[(size_t) result.corners[(size_t) p]];
            ranges[(size_t) p] = { std::min (2 * lowKey - 14, own.pitch[0]), std::max (2 * highKey + 14, own.pitch[7]) };
        }

        // The paths: Xenakis's own for the first variation (Nomos Alpha's D, Q12, Q4... for the
        // pitches; E, G2, L2... in the first group alone for the durations), drawn for the others.
        const bool xenakis = s.seed <= 1;
        const auto pitchPath = pathFrom (random, find ("D"), find ("Q12"), xenakis);
        const auto durationPath = pathFrom (random, find ("E"), find ("G2"), xenakis);
        const auto articulationPath = pathFrom (random, find ("Q12"), find ("D"), xenakis);
        const auto grandPath = pathFrom (random, find ("L"), find ("Q7"), xenakis);
        const auto& all = group();

        std::vector<std::vector<Drawn>> drawn ((size_t) n);
        result.lines.resize ((size_t) n);
        std::ostringstream commentary;
        commentary << "cubify: " << n << " track" << (n == 1 ? "" : "s") << ", 24 sections (V1 V1 V1 V2, six times)\n";
        commentary << "path: " << (s.path == Path::nomos ? "nomos alpha, the cubes turning each their own way" : "mode de valeurs, the cubes locked together")
                   << (xenakis ? " (Xenakis's own)" : ", variation " + std::to_string (s.seed)) << "\n\n";
        for (int c = 0; c < 8; ++c)
        {
            const auto p = partAt[(size_t) c];
            commentary << "corner " << c + 1 << ": " << (p >= 0 ? parts[(size_t) p].name : std::string ("(a ghost)")) << "\n  pitches";
            for (auto q : cubes[(size_t) c].pitch)
                commentary << " " << (cubes[(size_t) c].drums ? "drum " + std::to_string (q / 2) : pitchName (q));
            commentary << "\n  durations (sixteenths)";
            for (auto d : cubes[(size_t) c].duration)
                commentary << " " << d;
            commentary << "\n  articulations";
            for (auto a : cubes[(size_t) c].articulation)
                commentary << " " << articulationName (a);
            commentary << "\n";
        }
        commentary << "\n";

        int64_t at = 0;
        int v1 = 0;
        std::vector<int64_t> groupLengths;
        for (int section = 0; section < 24; ++section)
        {
            Section info;
            info.start = at;
            info.free = section % 4 == 3;
            std::vector<std::vector<Drawn>> here ((size_t) n);
            int64_t longest = 0;
            if (! info.free)
            {
                const auto j = v1++;
                const auto& grand = all[(size_t) grandPath[(size_t) j]];
                info.rotations = std::string ("pitch ") + all[(size_t) pitchPath[(size_t) j]].label
                               + (s.path == Path::nomos ? std::string (", durations ") + all[(size_t) durationPath[(size_t) j]].label
                                                          + ", dynamics " + all[(size_t) inverse (pitchPath[(size_t) j])].label
                                                          + ", articulations " + all[(size_t) articulationPath[(size_t) j]].label
                                                        : std::string (" (all four locked)"))
                               + ", grand " + grand.label;
                for (int p = 0; p < n; ++p)
                {
                    const auto& part = parts[(size_t) p];
                    if (part.audio)
                        continue;
                    const auto c = result.corners[(size_t) p];
                    const auto m = grand.seq[(size_t) c];       // the corner whose cubes it plays
                    const auto& from = cubes[(size_t) m];
                    const auto& own = cubes[(size_t) c];
                    int64_t t = 0;
                    for (int r = 0; r < readings; ++r)
                    {
                        const auto pr = power (pitchPath[(size_t) ((j + m) % 18)], r + 1);
                        const auto dr = s.path == Path::nomos ? power (durationPath[(size_t) ((j + m) % 18)], r + 1) : pr;
                        const auto vr = s.path == Path::nomos ? inverse (pr) : pr;
                        const auto ar = s.path == Path::nomos ? power (articulationPath[(size_t) ((j + m) % 18)], r + 1) : pr;
                        std::array<int, 8> durations {};
                        for (size_t k = 0; k < 8; ++k)
                            durations[k] = from.duration[(size_t) all[(size_t) dr].seq[k]];
                        if (s.palindromes)
                            for (size_t k = 4; k < 8; ++k)
                                durations[k] = durations[7 - k];
                        for (size_t k = 0; k < 8; ++k)
                        {
                            Drawn d;
                            const auto corner = all[(size_t) pr].seq[k];
                            d.start = t;
                            d.ioi = (int64_t) durations[k] * unit;
                            d.pitch = from.pitch[(size_t) corner];
                            d.dynamic = from.dynamic[(size_t) all[(size_t) vr].seq[k]];
                            d.articulation = from.articulation[(size_t) all[(size_t) ar].seq[k]];
                            t += d.ioi;
                            // The modifications.
                            if (from.drums != part.drums)
                            {
                                d.pitch = own.pitch[(size_t) corner];   // an oboe's key clicks for a triangle
                                ++result.modified.timbre;
                            }
                            else if (! part.drums && (d.pitch < ranges[(size_t) p].first || d.pitch > ranges[(size_t) p].second))
                            {
                                if (s.outOfRange == OutOfRange::rest)
                                    d.rest = true;
                                else
                                    d.pitch = own.pitch[(size_t) corner];
                                ++result.modified.range;
                            }
                            if (loudAttack (d.articulation) && d.dynamic <= 1)
                            {
                                if ((j + (int) k + r) % 2 == 0)
                                    d.dynamic = 4;
                                else
                                    d.articulation = Articulation::tenuto;
                                ++result.modified.softAttacks;
                            }
                            if ((d.articulation == Articulation::legato || d.articulation == Articulation::tenuto) && durations[k] == 1)
                            {
                                d.articulation = Articulation::staccato;
                                ++result.modified.shortLegatos;
                            }
                            here[(size_t) p].push_back (d);
                        }
                    }
                    longest = std::max (longest, t);
                }
                if (longest == 0)
                    longest = 2 * bar;
                info.length = (longest + bar - 1) / bar * bar;
                if (info.length != longest)
                    ++result.modified.bars;
                groupLengths.push_back (info.length);

                // Balance: no part more than three steps below the loudest it sounds against.
                if (s.balance)
                    for (int p = 0; p < n; ++p)
                        for (auto& d : here[(size_t) p])
                        {
                            int loudest = -1;
                            for (int o = 0; o < n; ++o)
                                if (o != p)
                                    for (const auto& e : here[(size_t) o])
                                        if (! e.rest && e.start <= d.start && d.start < e.start + e.ioi)
                                            loudest = std::max (loudest, e.dynamic);
                            if (! d.rest && loudest - d.dynamic > 3)
                            {
                                d.dynamic = loudest - 3;
                                ++result.modified.balance;
                            }
                        }

                // Audio loops play while their corner is turned into the upper tetrahedron.
                for (int p = 0; p < n; ++p)
                    if (parts[(size_t) p].audio && grand.seq[(size_t) result.corners[(size_t) p]] >= 4)
                        result.lines[(size_t) p].plays.push_back ({ at, info.length });
            }
            else
            {
                // V2: freer. A three-quarter-tone scale from each part's lowest pitch, at the
                // extreme of its register (the lower tetrahedron's parts low, the upper's high),
                // in long notes that swell and slide.
                int64_t mean = 0;
                for (size_t g = groupLengths.size() - 3; g < groupLengths.size(); ++g)
                    mean += groupLengths[g];
                info.length = std::max (2 * bar, (mean / 3 + bar / 2) / bar * bar);
                info.rotations = "free: three-quarter tones, extreme registers, held notes";
                for (int p = 0; p < n; ++p)
                {
                    const auto& part = parts[(size_t) p];
                    if (part.audio || part.notes.empty())
                        continue;
                    const auto c = result.corners[(size_t) p];
                    const auto& own = cubes[(size_t) c];
                    if (part.drums)
                    {
                        // A roll, swelling and dying away, over the first half.
                        const auto steps = info.length / 2 / unit;
                        for (int64_t i = 0; i < steps; ++i)
                        {
                            Drawn d;
                            d.start = i * unit;
                            d.ioi = unit;
                            d.pitch = own.pitch[0];
                            d.dynamic = (int) std::lround (3.0 * std::sin (pi * (double) i / (double) steps));
                            d.articulation = Articulation::staccato;
                            here[(size_t) p].push_back (d);
                        }
                        continue;
                    }
                    const auto step = s.quarterTones ? 3 : 6;
                    const auto base = own.pitch[0];
                    const auto [low, high] = ranges[(size_t) p];
                    const auto from = c < 4 ? std::max (0, low - 24) : std::max (0, high - 4);
                    const auto to = c < 4 ? std::min (254, low + 4) : std::min (254, high + 24);
                    std::vector<int> scale;
                    for (int q = from; q <= to; ++q)
                        if (((q - base) % step + step) % step == 0)
                            scale.push_back (q);
                    if (scale.empty())
                        scale.push_back (std::clamp (base, 0, 254));
                    const auto count = 1 + (int) (random.next() % 3);
                    const auto each = std::max ((int64_t) unit, info.length / count / unit * unit);
                    auto place = (int) (random.next() % scale.size());
                    for (int i = 0; i < count; ++i)
                    {
                        Drawn d;
                        d.start = i * each;
                        d.ioi = i == count - 1 ? info.length - d.start : each;
                        d.pitch = scale[(size_t) place];
                        d.dynamic = 1 + (int) (random.next() % 3);
                        d.articulation = Articulation::legato;
                        d.swell = s.hairpins;
                        if (s.glissandi && random.next() % 2 == 0)
                            d.glide = (random.next() % 2 == 0 ? 1 : -1) * (int) (75 + random.next() % 76);
                        here[(size_t) p].push_back (d);
                        place = std::clamp (place + (int) (random.next() % 5) - 2, 0, (int) scale.size() - 1);
                    }
                }
            }
            for (int p = 0; p < n; ++p)
                for (auto d : here[(size_t) p])
                {
                    if (d.start >= info.length)
                        continue;
                    d.ioi = std::min (d.ioi, info.length - d.start);
                    d.start += at;
                    drawn[(size_t) p].push_back (d);
                }
            commentary << "section " << section + 1 << (info.free ? " V2" : " V1") << ", bar " << at / bar + 1 << ", "
                       << info.length / bar << " bar" << (info.length / bar == 1 ? "" : "s") << ": " << info.rotations << "\n";
            result.sections.push_back (info);
            at += info.length;
        }
        result.length = at;

        // Rendered: each articulation's length and attack, quarter tones and slides by pitch
        // bend, hairpins and swells by expression (CC 11), and room to breathe.
        for (int p = 0; p < n; ++p)
        {
            auto& line = result.lines[(size_t) p];
            const auto& list = drawn[(size_t) p];
            int bend = 8192, expression = -1;
            int64_t phrase = list.empty() ? 0 : list.front().start, lastEnd = -1;
            auto control = [&] (int64_t tick, bool isBend, int value)
            {
                auto& current = isBend ? bend : expression;
                if (current == value)
                    return;
                current = value;
                line.controls.push_back ({ tick, isBend, value });
            };
            for (size_t i = 0; i < list.size(); ++i)
            {
                const auto& d = list[i];
                if (d.rest)
                    continue;
                auto end = d.start + std::max<int64_t> (1, (int64_t) std::llround ((double) d.ioi * gates[(int) d.articulation]));
                if (lastEnd >= 0 && d.start - lastEnd >= 2 * unit)
                    phrase = d.start;
                if (s.breaths && end - phrase > 2 * bar && d.ioi > 2 * unit)
                {
                    end = std::min (end, d.start + d.ioi - 2 * unit);
                    phrase = d.start + d.ioi;
                    ++result.modified.breaths;
                }
                lastEnd = end;
                auto velocity = velocities[std::clamp (d.dynamic, 0, 7)];
                if (d.articulation == Articulation::accent)
                    velocity += 10;
                else if (d.articulation == Articulation::marcato)
                    velocity += 16;
                else if (d.articulation == Articulation::sforzando)
                    velocity = 127;
                const bool drums = parts[(size_t) p].drums;
                if (! drums)
                    control (d.start, true, bendFor (d.pitch % 2 != 0 ? 50.0 : 0.0));
                if (d.swell)
                {
                    control (d.start, false, 40);
                    const auto length = end - d.start;
                    for (int64_t t = unit; t < length; t += unit)
                        control (d.start + t, false, (int) std::lround (40.0 + 80.0 * std::sin (pi * (double) t / (double) length)));
                }
                else if (d.articulation == Articulation::sforzando)
                {
                    control (d.start, false, 127);
                    if (end - d.start > unit)
                        control (d.start + unit, false, 64);
                }
                else
                {
                    control (d.start, false, 100);
                    // A hairpin towards the next note when the dynamics jump.
                    if (s.hairpins && i + 1 < list.size() && ! list[i + 1].rest && std::abs (list[i + 1].dynamic - d.dynamic) >= 3
                        && list[i + 1].start == d.start + d.ioi && end - d.start >= 2 * unit)
                    {
                        const auto target = list[i + 1].dynamic > d.dynamic ? 127 : 60;
                        const auto length = end - d.start;
                        for (int64_t t = unit; t < length; t += unit)
                            control (d.start + t, false, (int) std::lround (100.0 + (target - 100.0) * (double) t / (double) length));
                        ++result.modified.hairpins;
                    }
                }
                if (d.glide != 0 && ! drums)
                {
                    const auto from = d.pitch % 2 != 0 ? 50.0 : 0.0;
                    const auto half = (end - d.start) / 2;
                    for (int64_t t = half; t <= end - d.start; t += unit)
                        control (d.start + t, true, bendFor (from + d.glide * (double) (t - half) / (double) std::max<int64_t> (1, end - d.start - half)));
                }
                line.notes.push_back ({ d.start, end, std::clamp (d.pitch / 2, 0, 127), std::clamp (velocity, 1, 127) });
            }
        }
        const auto& m = result.modified;
        commentary << "\nmodifications: " << m.total() << "\n"
                   << "  loud attacks asked for pianissimo, played louder or as tenuto: " << m.softAttacks << "\n"
                   << "  legatos too short to join, played staccato: " << m.shortLegatos << "\n"
                   << "  pitches out of a track's range, " << (s.outOfRange == OutOfRange::rest ? "rests" : "its own pitch at that corner") << ": " << m.range << "\n"
                   << "  a drum's cube on a melody or a melody's on drums, the track's own: " << m.timbre << "\n"
                   << "  parts too soft to be heard, brought up: " << m.balance << "\n"
                   << "  hairpins between jumps in dynamics: " << m.hairpins << "\n"
                   << "  breaths: " << m.breaths << "\n"
                   << "  sections filled out to the bar line: " << m.bars << "\n";
        result.commentary = commentary.str();
        return result;
    }

    std::shared_ptr<const midifile::Song> toSong (const Line& line, int ticksPerQuarter, double bpm, int channel)
    {
        auto out = std::make_shared<midifile::Song>();
        out->format = 0;
        out->ticksPerQuarter = ticksPerQuarter;
        out->tempos = { { 0, 60000000.0 / std::max (1.0, bpm) } };
        channel = std::clamp (channel, 0, 15);
        struct Timed { midifile::Event e; int order; };
        std::vector<Timed> all;
        // Bend in the middle and expression full to begin.
        all.push_back ({ { 0, (uint8_t) (0xE0 | channel), 0, 64 }, 0 });
        for (const auto& c : line.controls)
            all.push_back ({ c.bend ? midifile::Event { c.tick, (uint8_t) (0xE0 | channel), (uint8_t) (c.value & 0x7F), (uint8_t) (c.value >> 7) }
                                    : midifile::Event { c.tick, (uint8_t) (0xB0 | channel), 11, (uint8_t) std::clamp (c.value, 0, 127) }, 1 });
        for (const auto& x : line.notes)
        {
            all.push_back ({ { x.start, (uint8_t) (0x90 | channel), (uint8_t) x.pitch, (uint8_t) x.velocity }, 2 });
            all.push_back ({ { std::max (x.end, x.start + 1), (uint8_t) (0x80 | channel), (uint8_t) x.pitch, 0 }, -1 });
        }
        std::stable_sort (all.begin(), all.end(), [] (const Timed& a, const Timed& b)
        {
            return a.e.tick != b.e.tick ? a.e.tick < b.e.tick : a.order < b.order;   // offs, then controls, then ons
        });
        for (const auto& t : all)
            out->events.push_back (t.e);
        out->channelsUsed = line.notes.empty() ? 0 : (uint16_t) (1u << channel);
        for (const auto& e : out->events)
            out->lastTick = std::max (out->lastTick, e.tick);
        return out;
    }
}

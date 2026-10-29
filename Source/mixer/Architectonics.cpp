// SPDX-License-Identifier: AGPL-3.0-only
#include "Architectonics.h"

#include <algorithm>
#include <cmath>

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
}

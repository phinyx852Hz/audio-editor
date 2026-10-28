// SPDX-License-Identifier: AGPL-3.0-only
#include "StepSequencer.h"

#include <algorithm>
#include <cmath>

namespace slicer
{
    double rateBeats (Rate r)
    {
        static constexpr double beats[] { 1.0, 1.5, 2.0 / 3.0, 0.5, 0.75, 1.0 / 3.0, 0.25, 0.375, 1.0 / 6.0, 0.125, 1.0 / 12.0, 0.0625 };
        const auto i = (size_t) r;
        return i < std::size (beats) ? beats[i] : 0.25;
    }

    bool Lane::empty() const
    {
        return std::none_of (step.begin(), step.end(), [] (const Step& s) { return s.on; });
    }

    bool Pattern::empty() const
    {
        return std::all_of (lanes.begin(), lanes.end(), [] (const Lane& l) { return l.empty(); });
    }

    int stepIndex (const Lane& lane, int64_t k)
    {
        const int64_t n = std::clamp<int> (lane.steps, 1, maxSteps);
        const auto at = k + lane.offset;
        auto wrap = [] (int64_t a, int64_t m) { return (int) (((a % m) + m) % m); };
        switch (lane.direction)
        {
            case Direction::backward:
                return (int) n - 1 - wrap (at, n);
            case Direction::alternate:
            {
                if (n == 1)
                    return 0;
                const auto p = wrap (at, 2 * n - 2);
                return p < n ? p : (int) (2 * n - 2) - p;
            }
            case Direction::random:
            {
                // A hash of k: the same draw every time round.
                auto x = (uint64_t) at * 0x9E3779B97F4A7C15ull;
                x ^= x >> 31;
                x *= 0xBF58476D1CE4E5B9ull;
                x ^= x >> 29;
                return (int) (x % (uint64_t) n);
            }
            case Direction::forward:
            default:
                return wrap (at, n);
        }
    }

    double stepTime (const Lane& lane, const Pattern& pattern, int64_t k)
    {
        const auto length = rateBeats (lane.rate);
        const auto swing = std::min (100, (int) lane.swing + (int) pattern.swing) / 100.0;
        const auto& s = lane.step[(size_t) stepIndex (lane, k)];
        auto t = (double) k * length + std::clamp<int> (s.shift, -50, 50) / 100.0 * length;
        if ((k & 1) != 0)
            t += swing * 0.5 * length;
        return t;
    }
}

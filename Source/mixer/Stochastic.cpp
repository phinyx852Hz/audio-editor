// SPDX-License-Identifier: AGPL-3.0-only
#include "Stochastic.h"
#include "Counterpoint.h"
#include "MidiFile.h"
#include "Rhythms.h"

#include <algorithm>
#include <cctype>
#include <cmath>
#include <numeric>
#include <set>

namespace stochastic
{
    //==========================================================================
    // splitmix64: one multiply-xorshift step a number, the same everywhere.
    Random::Random (uint64_t seed) : state (seed * 0x9E3779B97F4A7C15ull + 0x632BE59BD9B4E019ull) {}

    uint64_t Random::next()
    {
        auto z = (state += 0x9E3779B97F4A7C15ull);
        z = (z ^ (z >> 30)) * 0xBF58476D1CE4E5B9ull;
        z = (z ^ (z >> 27)) * 0x94D049BB133111EBull;
        return z ^ (z >> 31);
    }

    double Random::uniform() { return (double) (next() >> 11) * (1.0 / 9007199254740992.0); }

    const char* lawName (Law law)
    {
        switch (law)
        {
            case Law::uniform:     return "uniform";
            case Law::linear:      return "linear";
            case Law::exponential: return "exponential";
            case Law::gaussian:    return "gaussian";
            case Law::cauchy:      return "cauchy";
            case Law::logistic:    return "logistic";
            case Law::arcsine:     return "arcsine";
        }
        return "";
    }

    double draw (Law law, double centre, double spread, Random& rng)
    {
        constexpr double pi = 3.14159265358979323846;
        const auto u = rng.uniform();
        const auto side = [&rng] { return rng.uniform() < 0.5 ? -1.0 : 1.0; };
        switch (law)
        {
            case Law::uniform:
                return centre + spread * (2.0 * u - 1.0);
            case Law::linear:   // f(j) = 2/a (1 - j/a) on [0, a]: small steps likeliest
                return centre + side() * spread * (1.0 - std::sqrt (1.0 - u));
            case Law::exponential:
                return centre + side() * spread * -std::log1p (-u);
            case Law::gaussian:
                return centre + spread * std::sqrt (-2.0 * std::log (1.0 - u)) * std::cos (2.0 * pi * rng.uniform());
            case Law::cauchy:   // its tails are endless: kept within twenty widths
                return centre + spread * std::clamp (std::tan (pi * (u - 0.5)), -20.0, 20.0);
            case Law::logistic:
                return centre + spread * std::log ((u + 1e-12) / (1.0 - u + 1e-12));
            case Law::arcsine:  // the ends likeliest
                return centre + spread * std::sin (pi * (u - 0.5));
        }
        return centre;
    }

    //==========================================================================
    struct Sieve::Node
    {
        enum Kind { residue, unite, intersect, complement } kind = residue;
        int64_t modulus = 1, shift = 0;
        std::shared_ptr<const Node> a, b;
    };

    namespace
    {
        using NodePtr = std::shared_ptr<const Sieve::Node>;

        struct Parser
        {
            const std::string& s;
            size_t at = 0;
            std::string error;

            void space()
            {
                while (at < s.size() && std::isspace ((unsigned char) s[at]))
                    ++at;
            }
            bool take (char c)
            {
                space();
                if (at < s.size() && s[at] == c)
                {
                    ++at;
                    return true;
                }
                return false;
            }
            bool number (int64_t& out)
            {
                space();
                const auto from = at;
                while (at < s.size() && std::isdigit ((unsigned char) s[at]) && at - from < 9)
                    ++at;
                if (at == from)
                    return false;
                out = std::stoll (s.substr (from, at - from));
                return true;
            }
            NodePtr join (Sieve::Node::Kind kind, NodePtr a, NodePtr b)
            {
                auto n = std::make_shared<Sieve::Node>();
                n->kind = kind;
                n->a = std::move (a);
                n->b = std::move (b);
                return n;
            }
            NodePtr expr()
            {
                auto left = term();
                while (left != nullptr && take ('|'))
                {
                    auto right = term();
                    if (right == nullptr)
                        return nullptr;
                    left = join (Sieve::Node::unite, left, right);
                }
                return left;
            }
            NodePtr term()
            {
                auto left = factor();
                while (left != nullptr && take ('&'))
                {
                    auto right = factor();
                    if (right == nullptr)
                        return nullptr;
                    left = join (Sieve::Node::intersect, left, right);
                }
                return left;
            }
            NodePtr factor()
            {
                if (take ('-'))
                {
                    auto inner = factor();
                    return inner == nullptr ? nullptr : join (Sieve::Node::complement, inner, nullptr);
                }
                if (take ('('))
                {
                    auto inner = expr();
                    if (inner != nullptr && ! take (')'))
                    {
                        error = "a ( is not closed";
                        return nullptr;
                    }
                    return inner;
                }
                int64_t m = 0, r = 0;
                if (! number (m))
                {
                    error = at < s.size() ? "expected a residue class such as 3@0 at \"" + s.substr (at, 8) + "\"" : "the sieve ends too soon";
                    return nullptr;
                }
                if (! take ('@') || ! number (r))
                {
                    error = "a residue class is a modulus, @ and a shift: 3@0";
                    return nullptr;
                }
                if (m < 1)
                {
                    error = "a modulus is 1 or more";
                    return nullptr;
                }
                auto n = std::make_shared<Sieve::Node>();
                n->modulus = m;
                n->shift = r % m;
                return n;
            }
        };

        bool has (const Sieve::Node& n, int64_t x)
        {
            switch (n.kind)
            {
                case Sieve::Node::residue:    return ((x % n.modulus) + n.modulus) % n.modulus == n.shift;
                case Sieve::Node::unite:      return has (*n.a, x) || has (*n.b, x);
                case Sieve::Node::intersect:  return has (*n.a, x) && has (*n.b, x);
                case Sieve::Node::complement: return ! has (*n.a, x);
            }
            return false;
        }

        int64_t lcmOf (const Sieve::Node& n)
        {
            constexpr int64_t cap = 1 << 20;
            if (n.kind == Sieve::Node::residue)
                return n.modulus;
            const auto a = lcmOf (*n.a);
            const auto b = n.b != nullptr ? lcmOf (*n.b) : 1;
            return std::min (cap, std::lcm (a, b));
        }
    }

    Sieve Sieve::parse (const std::string& text, std::string& error)
    {
        Parser p { text, 0, {} };
        Sieve out;
        auto root = p.expr();
        p.space();
        if (root != nullptr && p.at < text.size())
        {
            p.error = "unexpected \"" + text.substr (p.at, 8) + "\"";
            root = nullptr;
        }
        error = root == nullptr ? (p.error.empty() ? std::string ("the sieve is empty") : p.error) : std::string();
        if (root != nullptr)
        {
            out.root = root;
            out.written = text;
        }
        return out;
    }

    Sieve Sieve::fromPoints (const std::vector<int>& points, int period)
    {
        period = std::max (1, period);
        std::vector<bool> in ((size_t) period, false);
        for (auto p : points)
            in[(size_t) (((p % period) + period) % period)] = true;
        std::vector<int> divisors;
        for (int m = 1; m <= period; ++m)
            if (period % m == 0)
                divisors.push_back (m);
        auto whole = [&] (int m, int r)
        {
            for (int k = r; k < period; k += m)
                if (! in[(size_t) k])
                    return false;
            return true;
        };
        // Each point by the smallest modulus whose whole class is in the set.
        std::vector<std::pair<int, int>> classes;
        for (int p = 0; p < period; ++p)
            if (in[(size_t) p])
                for (auto m : divisors)
                    if (whole (m, p % m))
                    {
                        if (std::find (classes.begin(), classes.end(), std::pair { m, p % m }) == classes.end())
                            classes.push_back ({ m, p % m });
                        break;
                    }
        // A class whose points the others cover too is dropped, the largest moduli first.
        std::stable_sort (classes.begin(), classes.end());
        for (auto i = (int) classes.size() - 1; i >= 0; --i)
        {
            std::vector<int> count ((size_t) period, 0);
            for (size_t j = 0; j < classes.size(); ++j)
                if ((int) j != i)
                    for (int k = classes[j].second; k < period; k += classes[j].first)
                        ++count[(size_t) k];
            bool needed = false;
            for (int k = classes[(size_t) i].second; k < period; k += classes[(size_t) i].first)
                needed = needed || count[(size_t) k] == 0;
            if (! needed)
                classes.erase (classes.begin() + i);
        }
        std::string text;
        for (const auto& [m, r] : classes)
            text += (text.empty() ? "" : " | ") + std::to_string (m) + "@" + std::to_string (r);
        if (text.empty())
            return {};
        std::string error;
        return parse (text, error);
    }

    Sieve Sieve::fromKeys (uint16_t keys)
    {
        std::vector<int> points;
        for (int k = 0; k < 12; ++k)
            if (keys & (1u << k))
                points.push_back (k);
        return fromPoints (points, 12);
    }

    bool Sieve::contains (int64_t n) const { return root != nullptr && has (*root, n); }

    std::vector<int64_t> Sieve::points (int64_t from, int64_t to) const
    {
        std::vector<int64_t> out;
        for (auto n = from; n < to && root != nullptr; ++n)
            if (has (*root, n))
                out.push_back (n);
        return out;
    }

    int64_t Sieve::period() const { return root != nullptr ? lcmOf (*root) : 1; }
    std::string Sieve::text() const { return written; }

    //==========================================================================
    namespace
    {
        struct Section
        {
            int64_t from = 0, to = 0;
            double density = 1.0;   // notes a beat
            int low = 0, high = 127;
            double velocity = 80.0;
        };

        std::vector<int> keysIn (const Settings& s, int low, int high)
        {
            std::vector<int> keys;
            for (int k = std::max (0, low); k <= std::min (127, high); ++k)
                if (s.pitches.empty() || s.pitches.contains (k - s.root))
                    keys.push_back (k);
            return keys;
        }

        int nearest (const std::vector<int>& keys, double x)
        {
            auto it = std::lower_bound (keys.begin(), keys.end(), (int) std::ceil (x));
            if (it == keys.end())
                return keys.back();
            if (it == keys.begin())
                return *it;
            const auto below = *(it - 1);
            return x - below <= *it - x ? below : *it;
        }

        // Folded back into [low, high] at its ends, as a leap past the range turns round.
        double reflect (double x, double low, double high)
        {
            const auto width = high - low;
            if (width <= 0)
                return low;
            auto y = std::fmod (x - low, 2.0 * width);
            if (y < 0)
                y += 2.0 * width;
            return low + (y <= width ? y : 2.0 * width - y);
        }
    }

    Result generate (const Settings& s)
    {
        Result result;
        const auto tpq = std::max (1, s.ticksPerQuarter);
        const auto low = std::clamp (std::min (s.low, s.high), 0, 127);
        const auto high = std::clamp (std::max (s.low, s.high), 0, 127);
        const auto allKeys = keysIn (s, low, high);
        if (allKeys.empty())
        {
            result.error = "the pitch sieve has no key between the lowest and highest note";
            return result;
        }
        if (s.length <= 0 || s.density <= 0)
            return result;
        Random rng (((uint64_t) s.seed << 8) ^ (uint64_t) s.method);

        // The sections: an ST cloud's, each with its own density, or the Markov screens.
        std::vector<Section> sections;
        const auto sectionTicks = std::max<int64_t> (1, (int64_t) std::llround (std::max (0.25, s.sectionBeats) * tpq));
        int state[3] { 0, 0, 0 };   // register (low, high), loudness (soft, loud), density (sparse, dense)
        for (int64_t from = 0, n = 0; from < s.length; from += sectionTicks, ++n)
        {
            Section c;
            c.from = from;
            c.to = std::min (s.length, from + sectionTicks);
            if (s.method == Method::cloud)
            {
                // ST: a section's density drawn from the exponential law about the mean.
                c.density = s.density * std::clamp (-std::log1p (-rng.uniform()), 0.25, 3.0);
                c.low = low;
                c.high = high;
                c.velocity = s.velocity;
            }
            else
            {
                for (int d = 0; d < 3; ++d)
                    if (n == 0)
                        state[d] = rng.uniform() < 0.5 ? 0 : 1;
                    else if (rng.uniform() >= std::clamp (s.stay[d], 0.0, 1.0))
                        state[d] = 1 - state[d];
                // The two registers overlap by a sixth of the range, so a line can cross over.
                const auto middle = (low + high) / 2, overlap = (high - low) / 6;
                c.low = state[0] == 0 ? low : middle - overlap;
                c.high = state[0] == 0 ? middle + overlap : high;
                c.velocity = std::clamp (s.velocity + (state[1] == 0 ? -1.5 : 1.5) * std::max (8, s.velocitySpread), 1.0, 127.0);
                c.density = s.density * (state[2] == 0 ? 0.5 : 2.0);
                result.screens += (result.screens.empty() ? "" : " ");
                result.screens += state[0] == 0 ? 'l' : 'h';
                result.screens += state[1] == 0 ? 'p' : 'f';
                result.screens += state[2] == 0 ? 's' : 'd';
            }
            sections.push_back (c);
        }

        // The onsets: a Poisson process in each section, or the grid's places taken by chance.
        std::vector<std::pair<int64_t, size_t>> onsets;
        if (s.grid.empty())
        {
            for (size_t i = 0; i < sections.size(); ++i)
            {
                const auto perTick = sections[i].density / tpq;
                for (double t = (double) sections[i].from + -std::log1p (-rng.uniform()) / perTick;
                     t < (double) sections[i].to; t += -std::log1p (-rng.uniform()) / perTick)
                    onsets.push_back ({ (int64_t) t, i });
            }
        }
        else
        {
            const auto& g = s.grid;
            for (size_t k = 0; k < g.size(); ++k)
            {
                if (g[k] < 0 || g[k] >= s.length)
                    continue;
                const auto i = (size_t) std::min<int64_t> ((int64_t) sections.size() - 1, g[k] / sectionTicks);
                const auto gap = k + 1 < g.size() ? g[k + 1] - g[k] : s.length - g[k] + g.front();
                const auto chance = std::min (1.0, sections[i].density * (double) std::max<int64_t> (1, gap) / tpq);
                if (rng.uniform() < chance)
                    onsets.push_back ({ g[k], i });
            }
        }

        // Each note: a leap from the last by the law of intervals, then a length and a loudness.
        double last = -1;
        std::vector<int> lastOf (128, -1);   // the note sounding on each key, cut short when struck again
        for (const auto& [start, i] : onsets)
        {
            const auto& c = sections[i];
            auto keys = keysIn (s, c.low, c.high);
            if (keys.empty())
                keys = allKeys;
            double x = last < 0 ? c.low + rng.uniform() * (c.high - c.low)
                                : draw (s.intervalLaw, last, std::max (0.5, s.intervalSpread), rng);
            x = reflect (x, keys.front(), keys.back());
            const auto pitch = nearest (keys, x);
            last = pitch;

            double beats = s.lengthLaw == Law::exponential ? s.lengthBeats * -std::log1p (-rng.uniform())
                                                           : draw (s.lengthLaw, s.lengthBeats, s.lengthBeats * 0.5, rng);
            beats = std::clamp (std::abs (beats), 1.0 / 32.0, 8.0);
            Note n;
            n.start = start;
            n.end = std::min (s.length, start + std::max<int64_t> (1, (int64_t) std::llround (beats * tpq)));
            n.pitch = pitch;
            n.velocity = std::clamp ((int) std::lround (draw (s.loudnessLaw, c.velocity, std::max (0, s.velocitySpread), rng)), 1, 127);
            if (n.end <= n.start)
                continue;
            if (const auto before = lastOf[(size_t) pitch]; before >= 0)
            {
                auto& b = result.notes[(size_t) before];
                if (b.start == n.start)
                    continue;   // the same key twice at once
                b.end = std::min (b.end, n.start);
            }
            lastOf[(size_t) pitch] = (int) result.notes.size();
            result.notes.push_back (n);
        }
        return result;
    }

    //==========================================================================
    std::vector<int64_t> gridFromSieve (const Sieve& sieve, int64_t stepTicks, int64_t length)
    {
        std::vector<int64_t> out;
        stepTicks = std::max<int64_t> (1, stepTicks);
        for (int64_t n = 0; n * stepTicks < length; ++n)
            if (sieve.contains (n))
                out.push_back (n * stepTicks);
        return out;
    }

    std::vector<int64_t> gridFromRhythm (const rhythm::Rhythm& r, int ticksPerQuarter, int64_t length)
    {
        std::set<int64_t> cycle;
        for (const auto& h : rhythm::hits (r))
            cycle.insert ((int64_t) h.step * ticksPerQuarter / std::max (1, r.lanes[(size_t) h.lane].perBeat));
        const auto period = (int64_t) std::max (1, r.beats) * ticksPerQuarter;
        std::vector<int64_t> out;
        for (int64_t from = 0; from < length && ! cycle.empty(); from += period)
            for (auto t : cycle)
                if (from + t < length)
                    out.push_back (from + t);
        return out;
    }

    Sieve sieveFromRhythm (const rhythm::Rhythm& r)
    {
        const auto rows = rhythm::rowsPerBeat (r);
        std::vector<int> points;
        for (const auto& h : rhythm::hits (r))
            points.push_back (h.step * rows / std::max (1, r.lanes[(size_t) h.lane].perBeat));
        return Sieve::fromPoints (points, std::max (1, r.beats) * rows);
    }

    Sieve rhythmSieveOf (const midifile::Song& song, int64_t stepTicks, int period)
    {
        std::vector<int> points;
        stepTicks = std::max<int64_t> (1, stepTicks);
        for (const auto& e : song.events)
            if (e.kind() == 0x90 && e.data2 > 0)
                points.push_back ((int) (((e.tick + stepTicks / 2) / stepTicks) % std::max (1, period)));
        return points.empty() ? Sieve() : Sieve::fromPoints (points, period);
    }

    Sieve pitchSieveOf (const midifile::Song& song, int root)
    {
        uint16_t keys = 0;
        for (const auto& e : song.events)
            if (e.kind() == 0x90 && e.data2 > 0 && e.channel() != 9)
                keys |= (uint16_t) (1u << (((e.data1 - root) % 12 + 12) % 12));
        return keys == 0 ? Sieve() : Sieve::fromKeys (keys);
    }

    std::shared_ptr<const midifile::Song> toSong (const midifile::Song& like, const std::vector<Note>& notes, int channel)
    {
        counterpoint::Line line;
        for (const auto& n : notes)
            line.push_back ({ n.start, n.end, n.pitch, n.velocity });
        return counterpoint::toSong (like, line, channel);
    }
}

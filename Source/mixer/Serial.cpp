// SPDX-License-Identifier: AGPL-3.0-only
#include "Serial.h"
#include "MidiFile.h"
#include "Stochastic.h"

#include <algorithm>
#include <cctype>
#include <numeric>

namespace serial
{
    namespace
    {
        int mod12 (int n) { return ((n % 12) + 12) % 12; }

        std::vector<std::string> tokens (const std::string& text)
        {
            std::vector<std::string> out;
            std::string word;
            for (const char c : text)
            {
                if (std::isspace ((unsigned char) c) || c == ',' || c == ';')
                {
                    if (! word.empty())
                        out.push_back (std::move (word));
                    word.clear();
                }
                else
                    word += (char) std::tolower ((unsigned char) c);
            }
            if (! word.empty())
                out.push_back (std::move (word));
            return out;
        }

        // A note name: a letter, then sharps and flats (#, ♯, b, ♭), an octave number after it ignored.
        bool noteName (const std::string& word, int& pc)
        {
            static const int letters[7] { 9, 11, 0, 2, 4, 5, 7 };   // a b c d e f g
            if (word.empty() || word[0] < 'a' || word[0] > 'g')
                return false;
            pc = letters[word[0] - 'a'];
            size_t at = 1;
            const std::string sharp = "♯", flat = "♭";
            while (at < word.size())
            {
                if (word[at] == '#')
                    ++pc, ++at;
                else if (word[at] == 'b')
                    --pc, ++at;
                else if (word.compare (at, sharp.size(), sharp) == 0)
                    ++pc, at += sharp.size();
                else if (word.compare (at, flat.size(), flat) == 0)
                    --pc, at += flat.size();
                else
                    break;
            }
            while (at < word.size() && (std::isdigit ((unsigned char) word[at]) || word[at] == '-'))
                ++at;
            pc = mod12 (pc);
            return at == word.size();
        }

        int indexOf (const Row& r, int pc)
        {
            return (int) (std::find (r.begin(), r.end(), mod12 (pc)) - r.begin());
        }

        unsigned hexachord (const Row& r, int half)   // a bit for each pitch class
        {
            unsigned bits = 0;
            for (int k = 0; k < 6; ++k)
                bits |= 1u << r[(size_t) (half * 6 + k)];
            return bits;
        }

        // Every statement, inversions first, then primes, retrograde inversions and retrogrades.
        std::vector<Statement> everyStatement()
        {
            std::vector<Statement> all;
            for (auto f : { Form::i, Form::p, Form::ri, Form::r })
                for (int n = 0; n < 12; ++n)
                    all.push_back ({ f, n });
            return all;
        }

        // The statements whose first hexachord is `bits`, other than `not`.
        std::vector<Statement> firstHexachordIs (const Row& row, unsigned bits, Statement not_)
        {
            std::vector<Statement> out;
            for (const auto s : everyStatement())
                if (! (s == not_) && hexachord (transform (row, s), 0) == bits)
                    out.push_back (s);
            return out;
        }

        size_t pick (const std::vector<double>& weights, stochastic::Random& rng)
        {
            const auto total = std::accumulate (weights.begin(), weights.end(), 0.0);
            auto x = rng.uniform() * total;
            for (size_t k = 0; k < weights.size(); ++k)
                if ((x -= weights[k]) < 0.0)
                    return k;
            return weights.size() - 1;
        }

        int below (stochastic::Random& rng, int n) { return (int) (rng.next() % (uint64_t) std::max (1, n)); }
    }

    const char* pitchName (int pc)
    {
        static const char* const names[12] { "c", "c♯", "d", "e♭", "e", "f", "f♯", "g", "a♭", "a", "b♭", "b" };
        return names[mod12 (pc)];
    }

    bool isRow (const std::vector<int>& pcs)
    {
        if (pcs.size() != 12)
            return false;
        unsigned seen = 0;
        for (auto pc : pcs)
        {
            if (pc < 0 || pc > 11 || (seen & (1u << pc)))
                return false;
            seen |= 1u << pc;
        }
        return true;
    }

    bool parse (const std::string& text, Row& row, std::string& error)
    {
        const auto words = tokens (text);
        // Numbers if any word is one: then t and e are 10 and 11, not the note e.
        const bool numbers = std::any_of (words.begin(), words.end(), [] (const std::string& w)
        {
            return ! w.empty() && std::all_of (w.begin(), w.end(), [] (char c) { return std::isdigit ((unsigned char) c); });
        });
        std::vector<int> pcs;
        for (const auto& w : words)
        {
            int pc = -1;
            if (numbers)
            {
                if (w == "t" || w == "a")
                    pc = 10;
                else if (w == "e" || w == "b")
                    pc = 11;
                else if (std::all_of (w.begin(), w.end(), [] (char c) { return std::isdigit ((unsigned char) c); }) && w.size() <= 2)
                    pc = std::stoi (w);
                if (pc < 0 || pc > 11)
                {
                    error = "“" + w + "” is not a pitch class (0 to 11, t and e for 10 and 11)";
                    return false;
                }
            }
            else if (! noteName (w, pc))
            {
                error = "“" + w + "” is not a note";
                return false;
            }
            if (std::find (pcs.begin(), pcs.end(), pc) != pcs.end())
            {
                error = std::string (pitchName (pc)) + " is in the row twice";
                return false;
            }
            pcs.push_back (pc);
        }
        if (pcs.size() != 12)
        {
            error = pcs.size() < 12 ? std::to_string (12 - pcs.size()) + " note(s) missing: a row has all twelve"
                                    : "a row has twelve notes";
            return false;
        }
        std::copy (pcs.begin(), pcs.end(), row.begin());
        return true;
    }

    std::string text (const Row& row, bool names)
    {
        std::string out;
        for (auto pc : row)
        {
            if (! out.empty())
                out += ' ';
            out += names ? std::string (pitchName (pc)) : std::to_string (pc);
        }
        return out;
    }

    Row transform (const Row& prime, Statement s)
    {
        Row out;
        const bool inverted = s.form == Form::i || s.form == Form::ri;
        for (size_t k = 0; k < 12; ++k)
            out[k] = mod12 ((inverted ? 2 * prime[0] - prime[k] : prime[k]) + s.level);
        if (s.form == Form::r || s.form == Form::ri)
            std::reverse (out.begin(), out.end());
        return out;
    }

    std::string label (Statement s)
    {
        static const char* const forms[4] { "p", "i", "r", "ri" };
        return forms[(int) s.form] + std::to_string (mod12 (s.level));
    }

    bool parseStatements (const std::string& text, std::vector<Statement>& out, std::string& error)
    {
        out.clear();
        for (const auto& w : tokens (text))
        {
            Statement s;
            size_t at = 1;
            if (w.rfind ("ri", 0) == 0)
                s.form = Form::ri, at = 2;
            else if (w[0] == 'p' || w[0] == 's')   // s: some write the prime as s
                s.form = Form::p;
            else if (w[0] == 'i')
                s.form = Form::i;
            else if (w[0] == 'r')
                s.form = Form::r;
            else
                at = 0;
            const auto digits = w.substr (at);
            if (at == 0 || digits.empty() || digits.size() > 2
                || ! std::all_of (digits.begin(), digits.end(), [] (char c) { return std::isdigit ((unsigned char) c); })
                || std::stoi (digits) > 11)
            {
                error = "“" + w + "” is not a row form (p, i, r or ri and a level 0 to 11: p0, ri5)";
                out.clear();
                return false;
            }
            s.level = std::stoi (digits);
            out.push_back (s);
        }
        return true;
    }

    std::string text (const std::vector<Statement>& plan)
    {
        std::string out;
        for (const auto& s : plan)
            out += (out.empty() ? "" : " ") + label (s);
        return out;
    }

    std::array<Row, 12> matrix (const Row& row)
    {
        std::array<Row, 12> m;
        for (int i = 0; i < 12; ++i)
            m[(size_t) i] = transform (row, { Form::p, primeLevelOfRow (row, i) });
        return m;
    }

    int primeLevelOfRow (const Row& row, int i) { return mod12 (row[0] - row[(size_t) i]); }
    int inversionLevelOfColumn (const Row& row, int j) { return mod12 (row[(size_t) j] - row[0]); }

    std::array<int, 11> intervals (const Row& row)
    {
        std::array<int, 11> out;
        for (size_t k = 0; k < 11; ++k)
            out[k] = mod12 (row[k + 1] - row[k]);
        return out;
    }

    bool allInterval (const Row& row)
    {
        unsigned seen = 0;
        for (auto i : intervals (row))
            seen |= 1u << i;
        return seen == 0xFFEu;   // 1..11
    }

    std::vector<Statement> combinatorial (const Row& row)
    {
        return firstHexachordIs (row, 0xFFFu & ~hexachord (row, 0), { Form::p, 0 });
    }

    Statement partner (const Row& row, Statement s)
    {
        const auto found = firstHexachordIs (row, 0xFFFu & ~hexachord (transform (row, s), 0), s);
        return found.empty() ? Statement { s.form == Form::p ? Form::r : s.form == Form::r ? Form::p : s.form == Form::i ? Form::ri : Form::i, s.level }
                             : found.front();
    }

    std::string describe (const Row& row)
    {
        std::string out = "intervals";
        for (auto i : intervals (row))
            out += " " + std::to_string (i);
        if (allInterval (row))
            out += "; an all-interval row";
        std::string kinds[4];
        for (const auto& s : combinatorial (row))
            if (! (s == Statement { Form::r, 0 }))   // r0 is the trivial one: every row has it
                kinds[(int) s.form] += (kinds[(int) s.form].empty() ? "" : ", ") + label (s);
        static const char* const names[4] { "prime", "inversional", "retrograde", "retrograde-inversional" };
        std::string with;
        for (int f : { 1, 0, 3, 2 })
            if (! kinds[f].empty())
                with += (with.empty() ? "" : "; ") + std::string (names[f]) + " with " + kinds[f];
        // Every row is retrograde-combinatorial (with r0): all-combinatorial is the other three too.
        const bool all = ! kinds[0].empty() && ! kinds[1].empty() && ! kinds[3].empty();
        if (with.empty())
            out += "; hexachordally combinatorial only with its own retrograde";
        else
            out += std::string ("; ") + (all ? "all-combinatorial: " : "hexachordally combinatorial, ") + with;
        return out;
    }

    Row randomRow (uint32_t seed)
    {
        stochastic::Random rng (seed);
        Row row;
        std::iota (row.begin(), row.end(), 0);
        for (int k = 11; k > 0; --k)
            std::swap (row[(size_t) k], row[(size_t) below (rng, k + 1)]);
        return row;
    }

    Row randomAllInterval (uint32_t seed)
    {
        stochastic::Random rng (seed);
        Row row {};
        row[0] = below (rng, 12);
        unsigned pcs = 1u << row[0], steps = 0;
        // Depth first, the intervals tried in an order of the seed's: there are 1928 such rows on each first note.
        auto search = [&] (auto&& self, int at) -> bool
        {
            if (at == 12)
                return true;
            std::array<int, 11> order;
            std::iota (order.begin(), order.end(), 1);
            for (int k = 10; k > 0; --k)
                std::swap (order[(size_t) k], order[(size_t) below (rng, k + 1)]);
            for (auto i : order)
            {
                const auto pc = mod12 (row[(size_t) at - 1] + i);
                if ((steps & (1u << i)) || (pcs & (1u << pc)))
                    continue;
                row[(size_t) at] = pc;
                steps |= 1u << i;
                pcs |= 1u << pc;
                if (self (self, at + 1))
                    return true;
                steps &= ~(1u << i);
                pcs &= ~(1u << pc);
            }
            return false;
        };
        search (search, 1);
        return row;
    }

    Row rowOf (const std::vector<int>& pitches, int& missing)
    {
        std::vector<int> pcs;
        for (auto p : pitches)
            if (pcs.size() < 12 && std::find (pcs.begin(), pcs.end(), mod12 (p)) == pcs.end())
                pcs.push_back (mod12 (p));
        missing = 12 - (int) pcs.size();
        for (int pc = 0; pc < 12 && pcs.size() < 12; ++pc)
            if (std::find (pcs.begin(), pcs.end(), pc) == pcs.end())
                pcs.push_back (pc);
        Row row;
        std::copy (pcs.begin(), pcs.end(), row.begin());
        return row;
    }

    const std::vector<Historical>& historical()
    {
        // The rows as music21's serial module lists them (music21/serial.py, historicalDict;
        // Copyright (c) 2006-2026 Michael Scott Asato Cuthbert, BSD 3-Clause licence: the
        // copyright notice, this list of conditions and the disclaimer are in docs/serial.md).
        static const std::vector<Historical> all {
            { "schoenberg: suite for piano, op. 25", { 4, 5, 7, 1, 6, 3, 8, 2, 11, 0, 9, 10 } },
            { "schoenberg: wind quintet, op. 26", { 3, 7, 9, 11, 1, 0, 10, 2, 4, 6, 8, 5 } },
            { "schoenberg: suite, op. 29", { 3, 7, 6, 10, 2, 11, 0, 9, 8, 4, 5, 1 } },
            { "schoenberg: third string quartet, op. 30", { 7, 4, 3, 9, 0, 5, 6, 11, 10, 1, 8, 2 } },
            { "schoenberg: variations for orchestra, op. 31", { 10, 4, 6, 3, 5, 9, 2, 1, 7, 8, 11, 0 } },
            { "schoenberg: von heute auf morgen, op. 32", { 2, 3, 9, 1, 11, 5, 8, 7, 4, 0, 10, 6 } },
            { "schoenberg: piano piece, op. 33a", { 10, 5, 0, 11, 9, 6, 1, 3, 7, 8, 2, 4 } },
            { "schoenberg: piano piece, op. 33b", { 11, 1, 5, 3, 9, 8, 6, 10, 7, 4, 0, 2 } },
            { "schoenberg: accompaniment to a film scene, op. 34", { 3, 6, 2, 4, 1, 0, 9, 11, 10, 8, 5, 7 } },
            { "schoenberg: violin concerto, op. 36", { 9, 10, 3, 11, 4, 6, 0, 1, 7, 8, 2, 5 } },
            { "schoenberg: fourth string quartet, op. 37", { 2, 1, 9, 10, 5, 3, 4, 0, 8, 7, 6, 11 } },
            { "schoenberg: ode to napoleon, op. 41", { 1, 0, 4, 5, 9, 8, 3, 2, 6, 7, 11, 10 } },
            { "schoenberg: piano concerto, op. 42", { 3, 10, 2, 5, 4, 0, 6, 8, 1, 9, 11, 7 } },
            { "schoenberg: string trio, op. 45", { 2, 10, 3, 9, 4, 1, 11, 8, 6, 7, 5, 0 } },
            { "schoenberg: a survivor from warsaw, op. 46", { 6, 7, 0, 8, 4, 3, 11, 10, 5, 9, 1, 2 } },
            { "schoenberg: phantasy for violin and piano, op. 47", { 10, 9, 1, 11, 5, 7, 3, 4, 0, 2, 8, 6 } },
            { "schoenberg: moses und aron", { 9, 10, 4, 2, 3, 1, 7, 5, 6, 8, 11, 0 } },
            { "berg: chamber concerto", { 11, 7, 5, 9, 2, 3, 6, 8, 0, 1, 4, 10 } },
            { "berg: lyric suite", { 5, 4, 0, 9, 7, 2, 8, 1, 3, 6, 10, 11 } },
            { "berg: der wein", { 2, 4, 5, 7, 9, 10, 1, 6, 8, 0, 11, 3 } },
            { "berg: lulu", { 0, 4, 5, 2, 7, 9, 6, 8, 11, 10, 3, 1 } },
            { "berg: violin concerto", { 7, 10, 2, 6, 9, 0, 4, 8, 11, 1, 3, 5 } },
            { "webern: string trio, op. 20", { 8, 7, 2, 1, 6, 5, 9, 10, 3, 4, 0, 11 } },
            { "webern: symphony, op. 21", { 5, 8, 7, 6, 10, 9, 3, 4, 0, 1, 2, 11 } },
            { "webern: quartet, op. 22", { 6, 3, 2, 5, 4, 8, 9, 10, 11, 1, 7, 0 } },
            { "webern: concerto for nine instruments, op. 24", { 11, 10, 2, 3, 7, 6, 8, 4, 5, 0, 1, 9 } },
            { "webern: variations for piano, op. 27", { 3, 11, 10, 2, 1, 0, 6, 4, 7, 5, 9, 8 } },
            { "webern: string quartet, op. 28", { 1, 0, 3, 2, 6, 7, 4, 5, 9, 8, 11, 10 } },
            { "webern: cantata i, op. 29", { 3, 11, 2, 1, 5, 4, 7, 6, 10, 9, 0, 8 } },
            { "webern: variations for orchestra, op. 30", { 9, 10, 1, 0, 11, 2, 3, 6, 5, 4, 7, 8 } },
            { "webern: cantata ii, op. 31", { 6, 9, 5, 4, 8, 3, 7, 11, 10, 2, 1, 0 } },
        };
        return all;
    }

    std::vector<Statement> walk (const Row& row, int count, uint32_t seed)
    {
        stochastic::Random rng (seed * 2654435761u + 17u);
        std::vector<Statement> out;
        if (count <= 0)
            return out;
        out.push_back ({ Form::p, 0 });
        const auto all = everyStatement();
        while ((int) out.size() < count)
        {
            const auto last = out.back();
            const auto notes = transform (row, last);
            // Across the join, the last hexachord and the next one's first make an aggregate.
            const auto join = 0xFFFu & ~hexachord (notes, 1);
            std::vector<double> weights;
            for (const auto& s : all)
            {
                const auto next = transform (row, s);
                double w = 1.0;
                if (s == last)
                    w = 0.0;
                else if (next[0] == notes[11])
                    w = 4.0;   // a pivot: the last note begins the next form
                else if (hexachord (next, 0) == join)
                    w = 3.0;
                else if (s.level == last.level)
                    w = 2.0;
                weights.push_back (w);
            }
            out.push_back (all[pick (weights, rng)]);
        }
        return out;
    }

    //==========================================================================
    namespace
    {
        // A statement's rhythm and loudness: for each of its chords (or notes), how long it is,
        // the rest after it and its velocity. Canons and combinatorial pairs share their leader's.
        struct Timing
        {
            std::vector<int64_t> lengths, rests;
            std::vector<int> velocities;
            int64_t total() const
            {
                return std::accumulate (lengths.begin(), lengths.end(), (int64_t) 0) + std::accumulate (rests.begin(), rests.end(), (int64_t) 0);
            }
        };

        Timing timing (const Settings& s, const Row& notes, stochastic::Random& rng)
        {
            const auto unit = std::max<int64_t> (1, (int64_t) s.unit * s.ticksPerQuarter / 4);
            const auto i0 = transform (s.row, { Form::i, 0 });
            static const int terraces[8] { 28, 40, 52, 64, 76, 88, 102, 118 };   // ppp .. fff
            const auto terrace = terraces[below (rng, 8)];
            Timing t;
            for (int k = 0; k < 12; k += s.chord)
            {
                const auto pc = notes[(size_t) k];
                int64_t length = unit;
                int64_t rest = 0;
                if (s.rhythm == Rhythm::serial)
                    length = unit * (indexOf (s.row, pc) + 1);   // 1 to 12 units, by the note's place in p0
                else if (s.rhythm == Rhythm::free)
                {
                    static const int lengths[7] { 1, 1, 2, 2, 3, 4, 6 };
                    length = unit * lengths[below (rng, 7)];
                    if (rng.uniform() < s.rests)
                        rest = unit * (1 + below (rng, 2));
                }
                int velocity = s.velocity;
                if (s.dynamics == Dynamics::terraced)
                    velocity = terrace;
                else if (s.dynamics == Dynamics::serial)
                    velocity = 20 + 9 * indexOf (i0, pc);   // by its place in i0: pp to fff
                t.lengths.push_back (length);
                t.rests.push_back (rest);
                t.velocities.push_back (std::clamp (velocity, 1, 127));
            }
            return t;
        }

        // Where a voice's pitch classes go: its range, what it last played, its frozen register.
        struct Placer
        {
            int low = 48, high = 84;
            int last = -1;
            std::array<int, 12> frozen {};
            Register reg = Register::close;

            Placer (Register r, int lo, int hi, stochastic::Random& rng) : low (lo), high (hi), reg (r)
            {
                for (int pc = 0; pc < 12; ++pc)
                {
                    const auto c = candidates (pc);
                    frozen[(size_t) pc] = c[(size_t) below (rng, (int) c.size())];
                }
            }

            std::vector<int> candidates (int pc) const
            {
                std::vector<int> out;
                for (int k = low; k <= high; ++k)
                    if (mod12 (k) == pc)
                        out.push_back (k);
                return out;
            }

            int place (int pc, stochastic::Random& rng, const std::vector<int>& taken)
            {
                auto c = candidates (pc);
                c.erase (std::remove_if (c.begin(), c.end(), [&taken] (int k) { return std::find (taken.begin(), taken.end(), k) != taken.end(); }), c.end());
                if (c.empty())
                    c = candidates (pc);
                int key = c.front();
                if (reg == Register::fixed)
                    key = frozen[(size_t) pc];
                else if (reg == Register::wide)
                    key = c[(size_t) below (rng, (int) c.size())];
                else
                {
                    // Close: nearest the last note (in a chord, the one below it), ties either way.
                    const auto from = ! taken.empty() ? taken.back() + 1 : last >= 0 ? last : (low + high) / 2;
                    int best = 1 << 20;
                    for (auto k : c)
                    {
                        const auto d = std::abs (k - from);
                        if (d < best || (d == best && rng.uniform() < 0.5))
                            best = d, key = k;
                    }
                }
                last = key;
                return key;
            }
        };

        Statement mirrored (Statement s)
        {
            static const Form m[4] { Form::i, Form::p, Form::ri, Form::r };
            return { m[(int) s.form], s.level };
        }
    }

    Result compose (const Settings& s)
    {
        Result result;
        if (! isRow (std::vector<int> (s.row.begin(), s.row.end())))
            return result.error = "the row needs all twelve notes, each once", result;
        if (s.chord != 1 && s.chord != 2 && s.chord != 3 && s.chord != 4 && s.chord != 6)
            return result.error = "a row divides into chords of 1, 2, 3, 4 or 6 notes", result;
        if (s.high - s.low < 11 || s.low < 0 || s.high > 127)
            return result.error = "the range needs an octave at least, to hold every pitch class", result;
        if (s.ticksPerQuarter <= 0)
            return result.error = "no timing", result;
        const auto V = std::clamp (s.voices, 1, 4);
        const auto tpq = s.ticksPerQuarter;
        const auto bar = (int64_t) tpq * 4;
        const auto target = (int64_t) std::max (1, s.bars) * bar;
        stochastic::Random rng (s.seed);

        // The statements a voice can use, more than enough to fill it.
        const auto shortest = std::max<int64_t> (1, (int64_t) std::max (1, s.unit) * tpq / 4 * (12 / s.chord));
        const auto most = (int) std::clamp<int64_t> (target / shortest + 2, 1, 4096);
        auto planFor = [&] (uint32_t seed)
        {
            if (! s.plan.empty())
            {
                std::vector<Statement> cycled;
                for (int k = 0; k < most; ++k)
                    cycled.push_back (s.plan[(size_t) k % s.plan.size()]);
                return cycled;
            }
            return walk (s.row, most, seed);
        };
        // A leader: whole statements while they fit in the piece (one at least).
        auto lead = [&] (const std::vector<Statement>& plan, std::vector<Statement>& used, std::vector<Timing>& timings)
        {
            int64_t at = 0;
            for (const auto& st : plan)
            {
                auto t = timing (s, transform (s.row, st), rng);
                if (! used.empty() && at + t.total() > target)
                    break;
                at += t.total();
                used.push_back (st);
                timings.push_back (std::move (t));
            }
        };

        std::vector<std::vector<Statement>> statements ((size_t) V);
        std::vector<std::vector<Timing>> timings ((size_t) V);
        std::vector<int64_t> delays ((size_t) V, 0);
        lead (planFor (s.seed), statements[0], timings[0]);
        for (int v = 1; v < V; ++v)
        {
            auto& mine = statements[(size_t) v];
            switch (s.relation)
            {
                case Voices::independent:
                    lead (walk (s.row, most, s.seed + 7919u * (uint32_t) v), mine, timings[(size_t) v]);
                    break;
                case Voices::canon:
                case Voices::mirror:
                    mine = statements[0];
                    if (s.relation == Voices::mirror && v % 2 == 1)
                        for (auto& st : mine)
                            st = mirrored (st);
                    timings[(size_t) v] = timings[0];
                    delays[(size_t) v] = (int64_t) v * std::max (0, s.delay) * tpq / 4;
                    break;
                case Voices::combinatorial:
                    if (v == 2)   // a second pair, on a walk of its own
                        lead (walk (s.row, most, s.seed + 104729u), mine, timings[2]);
                    else
                    {
                        for (const auto& st : statements[(size_t) v - 1])
                            mine.push_back (partner (s.row, st));
                        timings[(size_t) v] = timings[(size_t) v - 1];
                    }
                    break;
            }
        }

        // The voices' ranges: the whole range cut in overlapping parts, an octave each at least, the first highest.
        const auto span = s.high - s.low;
        const auto width = V == 1 ? span : std::max (11, span / V + 5);
        for (int v = 0; v < V; ++v)
        {
            const auto lo = V == 1 ? s.low : s.low + (int) ((int64_t) (V - 1 - v) * (span - width) / (V - 1));
            Placer placer (s.reg, lo, lo + width, rng);
            Voice voice;
            static const char* const canonNames[4] { "dux", "comes", "comes 2", "comes 3" };
            voice.name = V == 1 ? "voice" : s.relation == Voices::canon || s.relation == Voices::mirror ? canonNames[v] : "voice " + std::to_string (v + 1);
            voice.statements = statements[(size_t) v];
            auto at = delays[(size_t) v];
            for (size_t k = 0; k < voice.statements.size(); ++k)
            {
                const auto notes = transform (s.row, voice.statements[k]);
                const auto& t = timings[(size_t) v][k];
                for (size_t c = 0; c < t.lengths.size(); ++c)
                {
                    const auto length = t.lengths[c];
                    const auto sounding = std::max<int64_t> (1, length - length / 8);
                    std::vector<int> taken;
                    for (int n = 0; n < s.chord; ++n)
                    {
                        const auto key = placer.place (notes[c * (size_t) s.chord + (size_t) n], rng, taken);
                        taken.push_back (key);
                        voice.notes.push_back ({ at, at + sounding, key, t.velocities[c] });
                    }
                    if (s.chord > 1)
                        placer.last = taken.front();   // the next chord near this one's lowest
                    at += length + t.rests[c];
                }
            }
            result.length = std::max (result.length, at);
            result.commentary += (result.commentary.empty() ? "" : "\n") + voice.name + ": " + text (voice.statements);
            result.voices.push_back (std::move (voice));
        }
        result.length = std::max (target, (result.length + bar - 1) / bar * bar);   // what was asked, or whole bars more
        for (auto& voice : result.voices)
            std::stable_sort (voice.notes.begin(), voice.notes.end(), [] (const Note& a, const Note& b) { return a.start < b.start; });
        return result;
    }

    std::shared_ptr<const midifile::Song> toSong (const Voice& voice, int64_t length, int ticksPerQuarter, double bpm, int channel)
    {
        auto song = std::make_shared<midifile::Song>();
        song->format = 0;
        song->ticksPerQuarter = std::max (1, ticksPerQuarter);
        song->tempos.push_back ({ 0, 60.0e6 / std::max (1.0, bpm) });
        song->trackNames.push_back (voice.name);
        channel = std::clamp (channel, 0, 15);
        struct Timed { midifile::Event e; int order; };   // offs (0) before ons (1) at the same tick
        std::vector<Timed> all;
        for (const auto& n : voice.notes)
        {
            all.push_back ({ { n.start, (uint8_t) (0x90 | channel), (uint8_t) std::clamp (n.pitch, 0, 127), (uint8_t) std::clamp (n.velocity, 1, 127) }, 1 });
            all.push_back ({ { std::max (n.end, n.start + 1), (uint8_t) (0x80 | channel), (uint8_t) std::clamp (n.pitch, 0, 127), 0 }, 0 });
        }
        std::stable_sort (all.begin(), all.end(), [] (const Timed& a, const Timed& b)
        {
            return a.e.tick != b.e.tick ? a.e.tick < b.e.tick : a.order < b.order;
        });
        for (const auto& t : all)
        {
            song->events.push_back (t.e);
            song->lastTick = std::max (song->lastTick, t.e.tick);
        }
        song->lastTick = std::max (song->lastTick, length);
        song->channelsUsed = voice.notes.empty() ? 0 : (uint16_t) (1u << channel);
        return song;
    }
}

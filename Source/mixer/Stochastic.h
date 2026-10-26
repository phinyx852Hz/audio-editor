// SPDX-License-Identifier: AGPL-3.0-only
#pragma once

// Stochastic music (owner, 2026-10-26): new MIDI lines by Iannis Xenakis's methods in
// Formalized Music (1963, revised 1992) and his paper "Sieves" (Perspectives of New Music 28/1,
// 1990). Our own code (no port of another program). Plain C++, no JUCE or Qt.
//
// - ST clouds (ST/10, Achorripsis): the line is cut into sections, each with a density drawn
//   from an exponential law around the mean; inside a section the onsets come as a Poisson
//   process, each pitch a leap from the last by the linear law of intervals, f(j) = 2/a (1 - j/a),
//   and each length and loudness from a law of their own.
// - Sieves: sets of whole numbers built from residue classes, m@r (every n with n mod m == r),
//   joined by | (union), & (intersection) and - (complement), as Xenakis writes them. A pitch
//   sieve counts semitones from the root; a rhythm sieve counts steps of a grid from the
//   start. Any set that repeats can be written back as a sieve (Xenakis's own analysis), so a
//   mode or a rhythmic mode can be turned into one and edited.
// - Markov screens (Analogique A and B): the line is a book of screens, each a cell of three
//   dimensions with two states each (register low or high, soft or loud, sparse or dense); a
//   2x2 transition matrix for each dimension says what the next screen is likely to be, and
//   a cloud fills each screen.
// Everything is drawn from one seed, so the same settings and seed write the same line.
#include <cstdint>
#include <memory>
#include <string>
#include <vector>

namespace rhythm { struct Rhythm; }
namespace midifile { struct Song; }

namespace stochastic
{
    // A small random source of our own: the same numbers on every platform and compiler.
    class Random
    {
    public:
        explicit Random (uint64_t seed);
        uint64_t next();
        double uniform();   // [0, 1)
    private:
        uint64_t state;
    };

    // The laws Formalized Music uses (chapters I and V). Each is drawn about `centre`, `spread`
    // its scale: half the width (uniform, linear, arcsine), the mean distance (exponential, both
    // ways at random), the standard deviation (gaussian), or the scale parameter (cauchy,
    // logistic). Linear, exponential, cauchy and logistic favour small distances from the
    // centre; arcsine favours the ends.
    enum class Law { uniform, linear, exponential, gaussian, cauchy, logistic, arcsine };
    constexpr int numLaws = 7;
    const char* lawName (Law);   // lower case: "linear"
    double draw (Law, double centre, double spread, Random&);

    //==========================================================================
    class Sieve
    {
    public:
        Sieve() = default;
        // "3@0 | 4@1 & -5@2", "(8@0|8@1|8@7)&(5@1|5@3)". & binds tighter than |; - is the
        // complement of what follows. False, with `error` set, when it does not read.
        static Sieve parse (const std::string& text, std::string& error);
        // The union of residue classes, smallest moduli first, that gives exactly `points`
        // repeated every `period` (Xenakis 1990: each point covered by the smallest modulus,
        // a divisor of the period, whose whole class is in the set; classes another covers
        // dropped). Points are taken mod period.
        static Sieve fromPoints (const std::vector<int>& points, int period);
        // The keys of a mode (bit n: n semitones above the root), period 12.
        static Sieve fromKeys (uint16_t keys);

        bool empty() const { return root == nullptr; }
        bool contains (int64_t n) const;
        std::vector<int64_t> points (int64_t from, int64_t to) const;   // [from, to)
        int64_t period() const;         // the moduli's least common multiple (at most 1 << 20)
        std::string text() const;       // as written by parse, or as fromPoints built it

        struct Node;
    private:
        std::shared_ptr<const Node> root;
        std::string written;
    };

    //==========================================================================
    struct Note
    {
        int64_t start = 0, end = 0;   // ticks
        int pitch = 60, velocity = 90;
    };

    enum class Method { cloud, screens };

    struct Settings
    {
        Method method = Method::cloud;
        int ticksPerQuarter = 480;
        int64_t length = 480 * 16;           // ticks
        double density = 2.0;                // notes a beat, on average
        double sectionBeats = 4.0;           // a cloud's sections; a screen's length
        int low = 48, high = 84;             // the range, MIDI keys
        int root = 0;                        // 0 (C) .. 11 (B): where a pitch sieve counts from
        Sieve pitches;                       // empty: every key
        Law intervalLaw = Law::linear;
        double intervalSpread = 12.0;        // semitones
        Law lengthLaw = Law::exponential;
        double lengthBeats = 0.5;            // the mean
        Law loudnessLaw = Law::gaussian;
        int velocity = 80, velocitySpread = 18;
        // Where notes may start, in ticks from the start (sorted); empty: anywhere, as a Poisson
        // process. On a grid each place is taken with the chance the density gives it.
        std::vector<int64_t> grid;
        // Markov screens: the chance each dimension (register, loudness, density) stays in its
        // state from one screen to the next. Analogique's matrices change state more often than
        // not: 0.2 stays, 0.8 moves.
        double stay[3] { 0.2, 0.2, 0.2 };
        uint32_t seed = 1;
    };

    struct Result
    {
        std::vector<Note> notes;        // by start
        // Markov screens: each screen's states, register (l, h), loudness (p, f) and density
        // (s, d): "lps hfd hpd..."
        std::string screens;
        std::string error;              // when nothing could be written (no key of the sieve in range)
    };

    Result generate (const Settings&);

    // Grids for Settings::grid, `length` ticks long, the pattern repeated to fill it.
    // A sieve's points, a step `stepTicks` apart.
    std::vector<int64_t> gridFromSieve (const Sieve&, int64_t stepTicks, int64_t length);
    // A rhythmic mode's onsets, every lane's, at `ticksPerQuarter`.
    std::vector<int64_t> gridFromRhythm (const rhythm::Rhythm&, int ticksPerQuarter, int64_t length);
    // A rhythmic mode as a sieve on its finest grid (rowsPerBeat steps a beat), period one cycle.
    Sieve sieveFromRhythm (const rhythm::Rhythm&);
    // A MIDI file's note onsets (drums too) as a sieve of `stepTicks` steps over `period` steps,
    // and its pitch classes from `root` as a pitch sieve. Empty when it has no notes.
    Sieve rhythmSieveOf (const midifile::Song&, int64_t stepTicks, int period);
    Sieve pitchSieveOf (const midifile::Song&, int root);

    // The notes as a type 0 MIDI file with `like`'s timing and tempo map, on `channel`.
    std::shared_ptr<const midifile::Song> toSong (const midifile::Song& like, const std::vector<Note>&, int channel);
}

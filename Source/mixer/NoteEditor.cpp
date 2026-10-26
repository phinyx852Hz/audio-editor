// SPDX-License-Identifier: AGPL-3.0-only
#include "NoteEditor.h"

#include "Tunings.h"
#include "../core/Scala.h"

#include <algorithm>
#include <array>
#include <cmath>
#include <map>
#include <memory>
#include <numeric>

namespace noteedit
{
    namespace
    {
        int floorDiv (int a, int b) { return a >= 0 ? a / b : -((-a + b - 1) / b); }
        int mod (int a, int b) { return ((a % b) + b) % b; }
        int64_t roundTo (int64_t t, int64_t grid) { return grid <= 0 ? t : (int64_t) std::llround ((double) t / (double) grid) * grid; }

        // The names: the families' namers and which of them mark the period with dots.
        struct Namers
        {
            std::vector<std::pair<std::string, Namer>> namers;
            std::vector<std::string> dotted;
        };
        Namers& namers();

        const Namer* namerFor (const std::string& family)
        {
            const Namer* best = nullptr;
            size_t bestLength = 0;
            for (const auto& [prefix, n] : namers().namers)
                if (family.rfind (prefix, 0) == 0 && prefix.size() >= bestLength)
                {
                    best = &n;
                    bestLength = prefix.size();
                }
            return best;
        }

        bool isDotted (const std::string& family)
        {
            for (const auto& prefix : namers().dotted)
                if (family.rfind (prefix, 0) == 0)
                    return true;
            return false;
        }

        // Dots above for each period up, below for each down, on the name's last letter
        // (combining dot above U+0307, dot below U+0323): Ṡ, ṇ, 1̇.
        std::string dots (std::string name, int period)
        {
            for (int i = 0; i < std::abs (period); ++i)
                name += period > 0 ? "\xCC\x87" : "\xCC\xA3";
            return name;
        }

        // Sargam: the svaras by the semitone they are nearest (lower case komal, M tīvra ma), as
        // PHI rāga's sheets write them.
        std::string svara (const PitchSystem& s, int degree)
        {
            static const char* names[12] { "S", "r", "R", "g", "G", "m", "M", "P", "d", "D", "n", "N" };
            const auto semitone = mod ((int) std::lround (s.degrees[(size_t) degree] / 100.0), 12);
            return names[semitone];
        }

        // The five notes of the Chinese (and Korean) pentatonic named from gong, which is found as
        // the degree with the others at a major second, a major third, a fifth and a major sixth
        // above it; with seven notes, the two changing tones (biàn) and the qīng tones as well.
        std::string fiveNotes (const PitchSystem& s, int degree, const char* const* names, const char* const* extra)
        {
            const auto n = s.size();
            const auto near = [&] (double c, double target) { auto d = std::fmod (c - target + 2400.0, 1200.0); return std::min (d, 1200.0 - d) < 40.0; };
            const auto has = [&] (double c) { for (auto d : s.degrees) if (near (d, c)) return true; return false; };
            for (int g = 0; g < n; ++g)
            {
                const auto gong = s.degrees[(size_t) g];
                if (! (has (gong + 200) && has (gong + 400) && has (gong + 700) && has (gong + 900)))
                    continue;
                const auto c = s.degrees[(size_t) degree];
                static const double at[5] { 0, 200, 400, 700, 900 };
                for (int k = 0; k < 5; ++k)
                    if (near (c, gong + at[k]))
                        return names[k];
                static const double others[4] { 500, 600, 1000, 1100 };
                for (int k = 0; k < 4; ++k)
                    if (near (c, gong + others[k]))
                        return extra[k];
                return {};
            }
            return {};
        }

        Namers& namers()
        {
            static Namers all = []
            {
                Namers n;
                // Gamelan tones are named by Tunings.h (their kepatihan numbers), so need no namer.
                n.namers.push_back ({ "raga", svara });
                static const char* chinese[5] { "gōng", "shāng", "jué", "zhǐ", "yǔ" };
                static const char* chineseExtra[4] { "qīngjué", "biànzhǐ", "rùn", "biàngōng" };
                n.namers.push_back ({ "chinese", [] (const PitchSystem& s, int d) { return fiveNotes (s, d, chinese, chineseExtra); } });
                // 宮商角徵羽 read in Korean (McCune-Reischauer).
                static const char* korean[5] { "kung", "sang", "kak", "ch'i", "u" };
                static const char* koreanExtra[4] { "", "", "", "" };
                n.namers.push_back ({ "korean", [] (const PitchSystem& s, int d) { return fiveNotes (s, d, korean, koreanExtra); } });
                n.dotted = { "raga", "javanese gamelan", "balinese gamelan" };
                return n;
            }();
            return all;
        }

        //======================================================================
        struct ChannelState
        {
            double bendRange = 2.0;
            int bend = 8192;
            int rpnMsb = 127, rpnLsb = 127;
            int64_t bendTick = -1;          // when the last bend came
            size_t bendIndex = SIZE_MAX;    // its event
        };

        bool isRpnSetup (int msb, int lsb) { return msb == 0 && (lsb == 0 || lsb == 6); }
    }

    //==========================================================================
    bool PitchSystem::isTwelveTet() const
    {
        if (degrees.size() != 12 || std::abs (period - 1200.0) > 1.0e-6 || std::abs (std::fmod (tonic, 100.0)) > 1.0e-6)
            return false;
        for (size_t i = 0; i < 12; ++i)
            if (std::abs (degrees[i] - 100.0 * (double) i) > 1.0e-6)
                return false;
        return true;
    }

    int PitchSystem::degreeOf (int step) const { return degrees.empty() ? 0 : mod (step, size()); }
    int PitchSystem::periodOf (int step) const { return degrees.empty() ? 0 : floorDiv (step, size()); }

    double PitchSystem::pitchOf (int step) const
    {
        if (degrees.empty())
            return tonic + step * 100.0;
        return tonic + periodOf (step) * period + degrees[(size_t) degreeOf (step)];
    }

    int PitchSystem::nearestStep (double pitch) const
    {
        if (degrees.empty() || period <= 0.0)
            return (int) std::lround ((pitch - tonic) / 100.0);
        const auto rel = pitch - tonic;
        const auto periods = (int) std::floor (rel / period);
        const auto within = rel - periods * period;
        int best = 0;
        double bestDistance = 1.0e300;
        // The degrees of this period, and the tonic of the next (a note just under it is nearer to it).
        for (int i = 0; i <= size(); ++i)
        {
            const auto c = i == size() ? period : degrees[(size_t) i];
            const auto d = std::abs (within - c);
            if (d < bestDistance - 1.0e-9)
            {
                bestDistance = d;
                best = i;
            }
        }
        return periods * size() + best;
    }

    std::vector<int> PitchSystem::steps (double low, double high) const
    {
        std::vector<int> out;
        if (degrees.empty() || high < low)
            return out;
        auto s = nearestStep (low) - 1;
        while (pitchOf (s) >= low - 1.0e-9)
            --s;
        for (++s; pitchOf (s) <= high + 1.0e-9; ++s)
            if (pitchOf (s) >= low - 1.0e-9)
                out.push_back (s);
        return out;
    }

    PitchSystem twelveTet (int root)
    {
        PitchSystem s;
        for (int i = 0; i < 12; ++i)
            s.degrees.push_back (100.0 * i);
        s.tonic = 6000.0 + 100.0 * std::clamp (root, 0, 11);
        return s;
    }

    PitchSystem fromTuning (const mixer::TuningSystem& t, int root)
    {
        PitchSystem s;
        s.name = t.name;
        s.family = t.family;
        s.names = t.names;
        s.tonic = 6000.0 + 100.0 * std::clamp (root, 0, 11);
        // Sorted within the octave, the names kept with their degrees.
        std::vector<std::pair<double, std::string>> at;
        for (size_t i = 0; i < t.cents.size(); ++i)
        {
            auto c = std::fmod (t.cents[i], 1200.0);
            if (c < 0)
                c += 1200.0;
            at.push_back ({ c, i < t.names.size() ? t.names[i] : std::string() });
        }
        std::sort (at.begin(), at.end(), [] (const auto& a, const auto& b) { return a.first < b.first; });
        s.names.clear();
        for (const auto& [c, n] : at)
        {
            if (! s.degrees.empty() && std::abs (c - s.degrees.back()) < 1.0e-6)
                continue;
            s.degrees.push_back (c);
            s.names.push_back (n);
        }
        if (s.degrees.empty() || s.degrees.front() > 1.0e-6)
        {
            s.degrees.insert (s.degrees.begin(), 0.0);
            s.names.insert (s.names.begin(), std::string());
        }
        if (std::all_of (s.names.begin(), s.names.end(), [] (const std::string& n) { return n.empty(); }))
            s.names.clear();
        return s;
    }

    PitchSystem fromScala (const phi::scala::Scale& scl, const phi::scala::KeyboardMap& kbm, const std::string& name)
    {
        PitchSystem s;
        s.name = name.empty() ? scl.description : name;
        s.family = "scala";
        s.degrees = scl.degrees();
        s.period = scl.period() > 0.0 ? scl.period() : 1200.0;
        const auto hz = phi::scala::keyHz (scl, kbm, kbm.middleKey);
        s.tonic = hz > 0.0 ? 6900.0 + 1200.0 * std::log2 (hz / 440.0) : 100.0 * kbm.middleKey;
        return s;
    }

    //==========================================================================
    std::string centsText (double cents)
    {
        const auto r = std::round (cents * 10.0) / 10.0;
        if (std::abs (r) < 0.05)
            return "0";
        char buffer[32];
        const auto whole = std::abs (r - std::round (r)) < 0.05;
        std::snprintf (buffer, sizeof buffer, whole ? "%.0f" : "%.1f", std::abs (r));
        return std::string (r > 0 ? "+" : "\xE2\x88\x92") + buffer;
    }

    std::string letterName (double pitch, bool quarterTones)
    {
        static const char* keys[12] { "C", "C♯", "D", "E♭", "E", "F", "F♯", "G", "A♭", "A", "B♭", "B" };
        static const bool natural[12] { true, false, true, false, true, true, false, true, false, true, false, true };
        static const char* letters[12] { "C", "C", "D", "E", "E", "F", "F", "G", "A", "A", "B", "B" };
        const auto unit = quarterTones ? 50.0 : 100.0;
        const auto steps = (long) std::lround (pitch / unit);
        const auto off = pitch - steps * unit;
        std::string name;
        int octaveKey = 0;
        if (! quarterTones || steps % 2 == 0)
        {
            const auto key = (int) (quarterTones ? steps / 2 : steps);
            name = keys[mod (key, 12)];
            octaveKey = key;
        }
        else
        {
            // A quarter step: spelled from the natural beside it, as E half-flat is written on E.
            const auto below = (int) ((steps - 1) / 2), above = below + 1;
            if (natural[mod (above, 12)] && ! natural[mod (below, 12)])
            {
                name = std::string (letters[mod (above, 12)]) + "½♭";
                octaveKey = above;
            }
            else
            {
                name = std::string (letters[mod (below, 12)]) + (natural[mod (below, 12)] ? "½♯" : "♯½♯");
                octaveKey = below;
            }
        }
        name += std::to_string (floorDiv (octaveKey, 12) - 1);
        if (std::abs (off) >= 0.5)
            name += " " + centsText (off);
        return name;
    }

    std::string label (const PitchSystem& s, int step, Naming naming)
    {
        const auto pitch = s.pitchOf (step);
        if (s.degrees.empty())
            return letterName (pitch, false);
        const auto degree = s.degreeOf (step), period = s.periodOf (step);
        const auto tonicOctave = (int) std::floor (s.tonic / 1200.0) - 1;
        switch (naming)
        {
            case Naming::degrees:
            {
                auto n = std::to_string (degree + 1);
                for (int i = 0; i < std::abs (period); ++i)
                    n += period > 0 ? "'" : ",";
                return n;
            }
            case Naming::cents:
                return std::to_string ((int) std::lround (s.degrees[(size_t) degree])) + "¢ " + std::to_string (tonicOctave + period);
            case Naming::letters:
                break;
            case Naming::tradition:
            {
                const auto quarter = s.family == "maqam" || s.family == "dastgah";
                if (quarter)
                    return letterName (pitch, true);
                std::string name;
                if (const auto* namer = namerFor (s.family))
                    name = (*namer) (s, degree);
                if (name.empty() && (size_t) degree < s.names.size())
                    name = s.names[(size_t) degree];
                if (name.empty())
                    break;
                if (isDotted (s.family))
                    return dots (name, period);
                return name + " " + std::to_string (tonicOctave + period);
            }
        }
        return letterName (pitch, s.family == "maqam" || s.family == "dastgah");
    }

    void addNamer (const std::string& familyPrefix, Namer n) { namers().namers.push_back ({ familyPrefix, std::move (n) }); }
    void addDotted (const std::string& familyPrefix) { namers().dotted.push_back (familyPrefix); }

    //==========================================================================
    int Note::key() const { return std::clamp ((int) std::lround (pitch / 100.0), 0, 127); }

    void Sequence::sort()
    {
        std::stable_sort (notes.begin(), notes.end(), [] (const Note& a, const Note& b)
                          { return a.start != b.start ? a.start < b.start : a.pitch < b.pitch; });
    }

    int64_t Sequence::end() const
    {
        auto e = lastTick;
        for (const auto& n : notes)
            e = std::max (e, n.end());
        return e;
    }

    bool Sequence::anySelected() const
    {
        return std::any_of (notes.begin(), notes.end(), [] (const Note& n) { return n.selected; });
    }

    std::vector<size_t> Sequence::targets() const
    {
        std::vector<size_t> out;
        const auto picked = anySelected();
        for (size_t i = 0; i < notes.size(); ++i)
            if (! picked || notes[i].selected)
                out.push_back (i);
        return out;
    }

    //==========================================================================
    Sequence decode (const midifile::Song& song)
    {
        Sequence s;
        s.ticksPerQuarter = song.ticksPerQuarter > 0 ? song.ticksPerQuarter : 480;
        s.tempos = song.tempos.empty() ? std::vector<midifile::Tempo> { midifile::Tempo {} } : song.tempos;
        s.trackNames = song.trackNames;
        s.lastTick = song.lastTick;

        std::array<ChannelState, 16> ch {};
        int lowerMembers = 0;   // an MPE zone set up: its members bend ±48 unless RPN 0 says otherwise
        std::vector<bool> drop (song.events.size(), false);
        std::array<size_t, 16> rpnMsbAt;     // each channel's last CC 101
        rpnMsbAt.fill (SIZE_MAX);
        std::map<int, std::vector<size_t>> open;   // (channel << 7 | key): notes not let go yet, oldest first

        for (size_t i = 0; i < song.events.size(); ++i)
        {
            const auto& e = song.events[i];
            auto& c = ch[(size_t) e.channel()];
            switch (e.kind())
            {
                case 0xE0:
                    c.bend = e.data2 << 7 | e.data1;
                    c.bendTick = e.tick;
                    c.bendIndex = i;
                    break;
                case 0xB0:
                    if (e.data1 == 101)
                    {
                        c.rpnMsb = e.data2;
                        rpnMsbAt[(size_t) e.channel()] = i;
                    }
                    else if (e.data1 == 100)
                    {
                        // The bend range, the MPE zone, or the null RPN after them: rebuilt by encode().
                        c.rpnLsb = e.data2;
                        if ((c.rpnMsb == 127 && c.rpnLsb == 127) || isRpnSetup (c.rpnMsb, c.rpnLsb))
                        {
                            drop[i] = true;
                            if (rpnMsbAt[(size_t) e.channel()] != SIZE_MAX)
                                drop[rpnMsbAt[(size_t) e.channel()]] = true;
                        }
                        rpnMsbAt[(size_t) e.channel()] = SIZE_MAX;
                    }
                    else if ((e.data1 == 6 || e.data1 == 38) && isRpnSetup (c.rpnMsb, c.rpnLsb))
                    {
                        drop[i] = true;
                        if (c.rpnLsb == 0)
                            c.bendRange = e.data1 == 6 ? (double) e.data2 : std::floor (c.bendRange) + e.data2 / 100.0;
                        else if (e.data1 == 6 && e.channel() == 0)
                        {
                            lowerMembers = std::clamp ((int) e.data2, 0, 15);
                            for (int m = 1; m < 16; ++m)
                                ch[(size_t) m].bendRange = m <= lowerMembers ? 48.0 : 2.0;
                        }
                    }
                    break;
                case 0x90:
                    if (e.data2 > 0)
                    {
                        Note n;
                        n.start = e.tick;
                        n.channel = e.channel();
                        n.velocity = e.data2;
                        n.pitch = e.data1 * 100.0;
                        // The bend sent for this note, at its own tick: taken into its pitch.
                        if (c.bendTick == e.tick && c.bendIndex != SIZE_MAX)
                        {
                            n.pitch += (c.bend - 8192) / 8192.0 * c.bendRange * 100.0;
                            drop[c.bendIndex] = true;
                        }
                        open[e.channel() << 7 | e.data1].push_back (s.notes.size());
                        s.notes.push_back (n);
                        break;
                    }
                    [[fallthrough]];
                case 0x80:
                {
                    auto& list = open[e.channel() << 7 | e.data1];
                    if (! list.empty())
                    {
                        auto& n = s.notes[list.front()];
                        n.length = std::max<int64_t> (1, e.tick - n.start);
                        n.release = e.kind() == 0x80 ? e.data2 : 64;
                        list.erase (list.begin());
                    }
                    break;
                }
                default: break;
            }
        }
        // Notes never let go are held to the end.
        for (auto& [k, list] : open)
            for (auto at : list)
                s.notes[at].length = std::max<int64_t> (1, s.end() - s.notes[at].start);
        for (size_t i = 0; i < song.events.size(); ++i)
        {
            const auto k = song.events[i].kind();
            if (k != 0x90 && k != 0x80 && ! drop[i])
                s.other.push_back (song.events[i]);
        }
        s.sort();
        return s;
    }

    Encoding suggestedEncoding (const Sequence& s)
    {
        for (const auto& n : s.notes)
            if (std::abs (n.pitch - n.key() * 100.0) > 0.5)
                return Encoding::mpe;
        return Encoding::keys;
    }

    Encoded encode (const Sequence& s, const EncodeOptions& o)
    {
        struct Out { midifile::Event e; int rank; size_t order; };
        std::vector<Out> out;
        const auto add = [&] (int64_t tick, uint8_t status, int d1, int d2, int rank)
        { out.push_back ({ { tick, status, (uint8_t) std::clamp (d1, 0, 127), (uint8_t) std::clamp (d2, 0, 127) }, rank, out.size() }); };
        for (const auto& e : s.other)
            out.push_back ({ e, 1, out.size() });

        Encoded result;
        const auto range = std::clamp (o.bendRange, 1.0, 96.0);
        const auto bent = o.encoding != Encoding::keys;
        std::vector<int> pool;
        if (o.encoding == Encoding::mpe)
        {
            for (int c = 1; c < 16; ++c)
                if (! (o.keepDrums && c == 9))
                    pool.push_back (c);
        }
        else if (o.encoding == Encoding::bendChannels)
        {
            for (int c = 0; c < 16; ++c)
                if (! (o.keepDrums && c == 9))
                    pool.push_back (c);
        }

        struct Chan { int64_t busyUntil = 0; int bend = -1; std::vector<std::pair<int64_t, int>> sounding; bool used = false; };
        std::array<Chan, 16> chans {};
        auto notes = s.notes;
        std::stable_sort (notes.begin(), notes.end(), [] (const Note& a, const Note& b) { return a.start < b.start; });
        for (const auto& n : notes)
        {
            if (n.length <= 0)
                continue;
            const auto drum = o.keepDrums && n.channel == 9;
            auto key = n.key();
            if (! bent || drum)
            {
                add (n.start, (uint8_t) (0x90 | n.channel), key, std::clamp (n.velocity, 1, 127), 4);
                add (n.end(), (uint8_t) (0x80 | n.channel), key, n.release, 2);
                chans[(size_t) n.channel].used = true;
                continue;
            }
            const auto offset = n.pitch - key * 100.0;
            const auto bend = std::clamp ((int) std::lround (8192.0 + offset / (range * 100.0) * 8192.0), 0, 16383);
            for (auto& c : chans)
                std::erase_if (c.sounding, [&] (const auto& p) { return p.first <= n.start; });
            const auto free = [&] (int c) { return chans[(size_t) c].sounding.empty(); };
            const auto keyFree = [&] (int c) { for (auto& p : chans[(size_t) c].sounding) if (p.second == key) return false; return true; };
            int pick = -1;
            const auto earliest = [&] (auto&& ok)
            {
                int best = -1;
                for (auto c : pool)
                    if (ok (c) && (best < 0 || chans[(size_t) c].busyUntil < chans[(size_t) best].busyUntil))
                        best = c;
                return best;
            };
            if (o.encoding == Encoding::bendChannels)
            {
                // Share a channel already bent this way, else one free (the one free longest).
                pick = earliest ([&] (int c) { return ! free (c) && chans[(size_t) c].bend == bend && keyFree (c); });
                if (pick < 0)
                    pick = earliest ([&] (int c) { return free (c) && chans[(size_t) c].bend == bend; });
                if (pick < 0)
                    pick = earliest (free);
            }
            else
            {
                // MPE: a channel of its own, the one free longest (its last note's release has
                // rung out the most before the bend moves), else one bent the same.
                pick = earliest (free);
                if (pick < 0)
                    pick = earliest ([&] (int c) { return chans[(size_t) c].bend == bend && keyFree (c); });
            }
            if (pick < 0)
            {
                pick = earliest (keyFree);
                if (pick < 0)
                    pick = earliest ([] (int) { return true; });
                ++result.clashes;
            }
            if (pick < 0)
                continue;
            auto& c = chans[(size_t) pick];
            // The bend before every note, at its tick, so a reader knows it is the note's own.
            add (n.start, (uint8_t) (0xE0 | pick), bend & 0x7F, bend >> 7, 3);
            add (n.start, (uint8_t) (0x90 | pick), key, std::clamp (n.velocity, 1, 127), 4);
            add (n.end(), (uint8_t) (0x80 | pick), key, n.release, 2);
            c.bend = bend;
            c.sounding.push_back ({ n.end(), key });
            c.busyUntil = std::max (c.busyUntil, n.end());
            c.used = true;
        }

        // The set-up at the start: the MPE zone, then the bend range of each channel bent.
        if (bent)
        {
            const auto rpn = [&] (int channel, int lsb, double value)
            {
                const auto st = (uint8_t) (0xB0 | channel);
                add (0, st, 101, 0, 0);
                add (0, st, 100, lsb, 0);
                add (0, st, 6, (int) std::floor (value), 0);
                if (lsb == 0)
                    add (0, st, 38, (int) std::lround ((value - std::floor (value)) * 100.0), 0);
                add (0, st, 101, 127, 0);
                add (0, st, 100, 127, 0);
            };
            if (o.encoding == Encoding::mpe)
                rpn (0, 6, 15.0);
            for (auto c : pool)
                if (chans[(size_t) c].used)
                    rpn (c, 0, range);
        }

        std::stable_sort (out.begin(), out.end(), [] (const Out& a, const Out& b)
        {
            if (a.e.tick != b.e.tick)
                return a.e.tick < b.e.tick;
            if (a.rank != b.rank)
                return a.rank < b.rank;
            return a.order < b.order;
        });
        auto song = std::make_shared<midifile::Song>();
        song->format = 0;
        song->ticksPerQuarter = s.ticksPerQuarter;
        song->tempos = s.tempos;
        song->trackNames = s.trackNames;
        for (const auto& x : out)
        {
            song->events.push_back (x.e);
            if (x.e.kind() == 0x90)
                song->channelsUsed |= (uint16_t) (1u << x.e.channel());
        }
        song->lastTick = s.end();
        for (const auto& x : out)
            song->lastTick = std::max (song->lastTick, x.e.tick);
        for (auto& c : chans)
            result.channels += c.used ? 1 : 0;
        result.song = song;
        return result;
    }

    //==========================================================================
    void quantizeStarts (Sequence& s, int64_t grid, double strength, double swing)
    {
        if (grid <= 0)
            return;
        strength = std::clamp (strength, 0.0, 1.0);
        for (auto i : s.targets())
        {
            auto& n = s.notes[i];
            auto target = roundTo (n.start, grid);
            // Swing: every second grid line later, by up to a third of a line (triplet feel at 50%).
            if (swing > 0.0 && (target / grid) % 2 == 1)
                target += (int64_t) std::lround ((double) grid * std::clamp (swing, 0.0, 1.0) * 2.0 / 3.0);
            n.start = std::max<int64_t> (0, n.start + (int64_t) std::lround ((double) (target - n.start) * strength));
        }
        s.sort();
    }

    void quantizeLengths (Sequence& s, int64_t grid)
    {
        if (grid <= 0)
            return;
        for (auto i : s.targets())
            s.notes[i].length = std::max (grid, roundTo (s.notes[i].length, grid));
    }

    void quantizeEnds (Sequence& s, int64_t grid)
    {
        if (grid <= 0)
            return;
        for (auto i : s.targets())
        {
            auto& n = s.notes[i];
            n.length = std::max<int64_t> (1, roundTo (n.end(), grid) - n.start);
        }
    }

    void legato (Sequence& s, int64_t gap)
    {
        s.sort();
        const auto t = s.targets();
        for (size_t k = 0; k < t.size(); ++k)
        {
            auto& n = s.notes[t[k]];
            // The next start after this one among the targets (chords end together).
            for (size_t j = k + 1; j < t.size(); ++j)
                if (s.notes[t[j]].start > n.start)
                {
                    n.length = std::max<int64_t> (1, s.notes[t[j]].start - n.start - gap);
                    break;
                }
        }
    }

    void fixedLength (Sequence& s, int64_t length)
    {
        for (auto i : s.targets())
            s.notes[i].length = std::max<int64_t> (1, length);
    }

    void deleteOverlaps (Sequence& s, bool mono)
    {
        s.sort();
        const auto t = s.targets();
        for (size_t k = 0; k < t.size(); ++k)
        {
            auto& n = s.notes[t[k]];
            for (size_t j = k + 1; j < t.size(); ++j)
            {
                const auto& m = s.notes[t[j]];
                if (m.start >= n.end())
                    break;
                if (m.start > n.start && (mono || std::abs (m.pitch - n.pitch) < 0.5))
                {
                    n.length = m.start - n.start;
                    break;
                }
            }
        }
        // Notes on the same pitch at the same time: one kept.
        std::vector<bool> gone (s.notes.size(), false);
        for (size_t k = 0; k + 1 < t.size(); ++k)
            for (size_t j = k + 1; j < t.size() && s.notes[t[j]].start == s.notes[t[k]].start; ++j)
                if (! gone[t[k]] && (mono || std::abs (s.notes[t[j]].pitch - s.notes[t[k]].pitch) < 0.5))
                    gone[t[j]] = true;
        size_t w = 0;
        for (size_t i = 0; i < s.notes.size(); ++i)
            if (! gone[i])
                s.notes[w++] = s.notes[i];
        s.notes.resize (w);
    }

    void transposeCents (Sequence& s, double cents)
    {
        for (auto i : s.targets())
            s.notes[i].pitch = std::clamp (s.notes[i].pitch + cents, 0.0, 12700.0);
    }

    void transposeSteps (Sequence& s, const PitchSystem& system, int steps)
    {
        for (auto i : s.targets())
        {
            auto& n = s.notes[i];
            if (n.channel == 9)
                continue;
            n.pitch = std::clamp (system.pitchOf (system.nearestStep (n.pitch) + steps), 0.0, 12700.0);
        }
    }

    void snapToSystem (Sequence& s, const PitchSystem& system) { transposeSteps (s, system, 0); }

    void setVelocity (Sequence& s, int velocity)
    {
        for (auto i : s.targets())
            s.notes[i].velocity = std::clamp (velocity, 1, 127);
    }

    void scaleVelocity (Sequence& s, double factor, int add)
    {
        for (auto i : s.targets())
        {
            auto& n = s.notes[i];
            n.velocity = std::clamp ((int) std::lround (64.0 + (n.velocity - 64.0) * factor) + add, 1, 127);
        }
    }

    void rampVelocity (Sequence& s, int from, int to)
    {
        const auto t = s.targets();
        if (t.empty())
            return;
        int64_t first = s.notes[t.front()].start, last = first;
        for (auto i : t)
        {
            first = std::min (first, s.notes[i].start);
            last = std::max (last, s.notes[i].start);
        }
        for (auto i : t)
        {
            const auto x = last > first ? (double) (s.notes[i].start - first) / (double) (last - first) : 0.0;
            s.notes[i].velocity = std::clamp ((int) std::lround (from + (to - from) * x), 1, 127);
        }
    }

    void splitAt (Sequence& s, int64_t tick)
    {
        std::vector<Note> added;
        for (auto i : s.targets())
        {
            auto& n = s.notes[i];
            if (n.start < tick && n.end() > tick)
            {
                auto right = n;
                right.start = tick;
                right.length = n.end() - tick;
                n.length = tick - n.start;
                added.push_back (right);
            }
        }
        s.notes.insert (s.notes.end(), added.begin(), added.end());
        s.sort();
    }

    void glue (Sequence& s)
    {
        s.sort();
        auto t = s.targets();
        std::vector<bool> gone (s.notes.size(), false);
        for (size_t k = 0; k < t.size(); ++k)
        {
            if (gone[t[k]])
                continue;
            auto& n = s.notes[t[k]];
            for (size_t j = k + 1; j < t.size(); ++j)
            {
                auto& m = s.notes[t[j]];
                if (! gone[t[j]] && std::abs (m.pitch - n.pitch) < 0.5 && m.channel == n.channel && m.start >= n.start)
                {
                    n.length = std::max (n.end(), m.end()) - n.start;
                    gone[t[j]] = true;
                    break;
                }
            }
        }
        size_t w = 0;
        for (size_t i = 0; i < s.notes.size(); ++i)
            if (! gone[i])
                s.notes[w++] = s.notes[i];
        s.notes.resize (w);
    }

    void deleteTargets (Sequence& s)
    {
        if (! s.anySelected())
            return;   // nothing picked: nothing deleted (unlike the other edits)
        std::erase_if (s.notes, [] (const Note& n) { return n.selected; });
    }

    void selectAll (Sequence& s, bool selected)
    {
        for (auto& n : s.notes)
            n.selected = selected;
    }

    void nudge (Sequence& s, int64_t ticks)
    {
        for (auto i : s.targets())
            s.notes[i].start = std::max<int64_t> (0, s.notes[i].start + ticks);
        s.sort();
    }

    void restyle (Sequence& s, const PitchSystem& from, const PitchSystem& to, bool byDegree)
    {
        for (auto i : s.targets())
        {
            auto& n = s.notes[i];
            if (n.channel == 9)
                continue;
            if (! byDegree || from.degrees.empty() || to.degrees.empty())
                n.pitch = to.pitchOf (to.nearestStep (n.pitch));
            else
                n.pitch = to.pitchOf (from.nearestStep (n.pitch));
            n.pitch = std::clamp (n.pitch, 0.0, 12700.0);
        }
    }

    Key detectKey (const Sequence& s)
    {
        // Krumhansl and Kessler (1982), the probe-tone ratings for C major and C minor.
        static const double major[12] { 6.35, 2.23, 3.48, 2.33, 4.38, 4.09, 2.52, 5.19, 2.39, 3.66, 2.29, 2.88 };
        static const double minor[12] { 6.33, 2.68, 3.52, 5.38, 2.60, 3.53, 2.54, 4.75, 3.98, 2.69, 3.34, 3.17 };
        std::array<double, 12> weight {};
        for (const auto& n : s.notes)
            if (n.channel != 9)
                weight[(size_t) mod (n.key(), 12)] += (double) std::max<int64_t> (1, n.length);
        const auto correlate = [&] (const double* profile, int root)
        {
            double mx = 0, my = 0;
            for (int i = 0; i < 12; ++i)
            {
                mx += weight[(size_t) mod (i + root, 12)];
                my += profile[i];
            }
            mx /= 12;
            my /= 12;
            double sxy = 0, sxx = 0, syy = 0;
            for (int i = 0; i < 12; ++i)
            {
                const auto x = weight[(size_t) mod (i + root, 12)] - mx, y = profile[i] - my;
                sxy += x * y;
                sxx += x * x;
                syy += y * y;
            }
            return sxx > 0 && syy > 0 ? sxy / std::sqrt (sxx * syy) : -2.0;
        };
        Key best;
        double bestR = -3.0;
        for (int root = 0; root < 12; ++root)
            for (bool m : { false, true })
                if (const auto r = correlate (m ? minor : major, root); r > bestR + 1.0e-12)
                {
                    bestR = r;
                    best = { root, m };
                }
        return best;
    }

    //==========================================================================
    std::vector<size_t> soundingAt (const Sequence& s, int64_t tick)
    {
        std::vector<size_t> out;
        for (size_t i = 0; i < s.notes.size(); ++i)
            if (s.notes[i].start <= tick && s.notes[i].end() > tick)
                out.push_back (i);
        return out;
    }

    double roughness (const std::vector<double>& pitches, int partials)
    {
        // Sethares, "Local consonance and the relationship between timbre and scale" (JASA 94, 1993).
        constexpr double b1 = 3.51, b2 = 5.75, xStar = 0.24, s1 = 0.0207, s2 = 18.96;
        double total = 0.0;
        for (size_t i = 0; i < pitches.size(); ++i)
            for (size_t j = i + 1; j < pitches.size(); ++j)
            {
                const auto fi = 440.0 * std::pow (2.0, (pitches[i] - 6900.0) / 1200.0);
                const auto fj = 440.0 * std::pow (2.0, (pitches[j] - 6900.0) / 1200.0);
                for (int a = 1; a <= partials; ++a)
                    for (int b = 1; b <= partials; ++b)
                    {
                        const auto f1 = fi * a, f2 = fj * b;
                        const auto amp = std::min (std::pow (0.88, a - 1), std::pow (0.88, b - 1));
                        const auto sc = xStar / (s1 * std::min (f1, f2) + s2);
                        const auto d = std::abs (f2 - f1);
                        total += amp * (std::exp (-b1 * sc * d) - std::exp (-b2 * sc * d));
                    }
            }
        return total;
    }

    std::vector<double> degreeUsage (const Sequence& s, const PitchSystem& system)
    {
        std::vector<double> out ((size_t) std::max (1, system.size()), 0.0);
        for (const auto& n : s.notes)
            if (n.channel != 9)
                out[(size_t) system.degreeOf (system.nearestStep (n.pitch))] += (double) n.length;
        return out;
    }

    //==========================================================================
    void stepNote (Sequence& s, StepInput& in, double pitch, int channel)
    {
        if (in.insert && ! in.chord)
        {
            // Room made for the step: the notes from the cursor on move later by it.
            for (auto& n : s.notes)
                if (n.start >= in.position)
                    n.start += in.step;
            s.lastTick += in.step;
        }
        Note n;
        n.start = in.position;
        n.length = std::max<int64_t> (1, in.length);
        n.pitch = std::clamp (pitch, 0.0, 12700.0);
        n.velocity = std::clamp (in.velocity, 1, 127);
        n.channel = channel;
        s.notes.push_back (n);
        s.sort();
        if (! in.chord)
            in.position += in.step;
        s.lastTick = std::max (s.lastTick, s.end());
    }

    void stepRest (Sequence& s, StepInput& in)
    {
        if (in.insert)
        {
            for (auto& n : s.notes)
                if (n.start >= in.position)
                    n.start += in.step;
            s.lastTick += in.step;
        }
        in.position += in.step;
    }

    void stepBack (Sequence& s, StepInput& in)
    {
        in.position = std::max<int64_t> (0, in.position - in.step);
        std::erase_if (s.notes, [&] (const Note& n) { return n.start == in.position; });
    }

    //==========================================================================
    namespace
    {
        bool onLane (const midifile::Event& e, int lane, int channel)
        {
            if (e.channel() != channel)
                return false;
            if (lane == laneBend)
                return e.kind() == 0xE0;
            if (lane == lanePressure)
                return e.kind() == 0xD0;
            return e.kind() == 0xB0 && e.data1 == lane;
        }
    }

    std::vector<LanePoint> lanePoints (const Sequence& s, int lane, int channel)
    {
        std::vector<LanePoint> out;
        for (const auto& e : s.other)
            if (onLane (e, lane, channel))
                out.push_back ({ e.tick, lane == laneBend ? (e.data2 << 7 | e.data1) : lane == lanePressure ? e.data1 : e.data2 });
        return out;
    }

    void eraseLane (Sequence& s, int lane, int channel, int64_t from, int64_t to)
    {
        if (to < from)
            std::swap (from, to);
        std::erase_if (s.other, [&] (const midifile::Event& e) { return onLane (e, lane, channel) && e.tick >= from && e.tick <= to; });
    }

    void drawLane (Sequence& s, int lane, int channel, int64_t from, int64_t to, int valueFrom, int valueTo, int64_t every)
    {
        if (to < from)
        {
            std::swap (from, to);
            std::swap (valueFrom, valueTo);
        }
        eraseLane (s, lane, channel, from, to);
        every = std::max<int64_t> (1, every);
        const auto top = lane == laneBend ? 16383 : 127;
        int last = -1;
        for (int64_t t = from;; t = std::min (to, t + every))
        {
            const auto x = to > from ? (double) (t - from) / (double) (to - from) : 0.0;
            const auto v = std::clamp ((int) std::lround (valueFrom + (valueTo - valueFrom) * x), 0, top);
            if (v != last)
            {
                midifile::Event e;
                e.tick = t;
                if (lane == laneBend)
                {
                    e.status = (uint8_t) (0xE0 | channel);
                    e.data1 = (uint8_t) (v & 0x7F);
                    e.data2 = (uint8_t) (v >> 7);
                }
                else if (lane == lanePressure)
                {
                    e.status = (uint8_t) (0xD0 | channel);
                    e.data1 = (uint8_t) v;
                }
                else
                {
                    e.status = (uint8_t) (0xB0 | channel);
                    e.data1 = (uint8_t) lane;
                    e.data2 = (uint8_t) v;
                }
                s.other.push_back (e);
                last = v;
            }
            if (t >= to)
                break;
        }
        std::stable_sort (s.other.begin(), s.other.end(), [] (const midifile::Event& a, const midifile::Event& b) { return a.tick < b.tick; });
        s.lastTick = std::max (s.lastTick, to);
    }
}

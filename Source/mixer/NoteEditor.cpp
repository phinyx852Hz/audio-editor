// SPDX-License-Identifier: AGPL-3.0-only
#include "NoteEditor.h"

#include "Tunings.h"
#include "../core/Pitch.h"
#include "../core/Scala.h"

#include <algorithm>
#include <array>
#include <cmath>
#include <limits>
#include <map>
#include <memory>
#include <numeric>
#include <sstream>

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
        // What it repeats at: the octave, or a fourth, a tritave, a single step (Tunings.h).
        s.period = t.period > 0.0 ? t.period : 1200.0;
        // Sorted within the period, the names kept with their degrees.
        std::vector<std::pair<double, std::string>> at;
        for (size_t i = 0; i < t.cents.size(); ++i)
        {
            auto c = std::fmod (t.cents[i], s.period);
            if (c < 0)
                c += s.period;
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

    double Note::bendAt (int64_t t) const
    {
        if (curve.empty())
            return 0.0;
        if (t <= curve.front().first)
            return curve.front().second;
        if (t >= curve.back().first)
            return curve.back().second;
        const auto it = std::upper_bound (curve.begin(), curve.end(), t, [] (int64_t x, const auto& p) { return x < p.first; });
        const auto& b = *it;
        const auto& a = *(it - 1);
        return b.first == a.first ? b.second : a.second + (b.second - a.second) * (double) (t - a.first) / (double) (b.first - a.first);
    }

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
        for (const auto& m : song.markers)
        {
            PitchSystem ps;
            if (parseSystemMarker (m.text, ps))
                s.systems.push_back ({ m.tick, std::move (ps) });
            else
                s.markers.push_back (m);
        }
        s.lastTick = song.lastTick;

        std::array<ChannelState, 16> ch {};
        int lowerMembers = 0;   // an MPE zone set up: its members bend ±48 unless RPN 0 says otherwise
        std::vector<bool> drop (song.events.size(), false);
        std::array<size_t, 16> rpnMsbAt;     // each channel's last CC 101
        rpnMsbAt.fill (SIZE_MAX);
        std::map<int, std::vector<size_t>> open;   // (channel << 7 | key): notes not let go yet, oldest first
        std::array<std::vector<size_t>, 16> sounding;   // each channel's notes sounding now
        std::array<double, 16> baseBend {};             // cents: the bend the channel's note began with

        for (size_t i = 0; i < song.events.size(); ++i)
        {
            const auto& e = song.events[i];
            auto& c = ch[(size_t) e.channel()];
            switch (e.kind())
            {
                case 0xE0:
                {
                    c.bend = e.data2 << 7 | e.data1;
                    c.bendTick = e.tick;
                    c.bendIndex = i;
                    // During the only note on its channel, the bend is that note's: its curve.
                    auto& on = sounding[(size_t) e.channel()];
                    if (on.size() == 1 && e.tick > s.notes[on.front()].start)
                    {
                        auto& n = s.notes[on.front()];
                        const auto cents = (c.bend - 8192) / 8192.0 * c.bendRange * 100.0 - baseBend[(size_t) e.channel()];
                        if (n.curve.empty())
                            n.curve.push_back ({ 0, 0.0 });
                        // A bend steps: after a long gap the curve holds until it, not a ramp.
                        const auto at = e.tick - n.start;
                        if (at - n.curve.back().first > std::max (2, song.ticksPerQuarter / 24))
                            n.curve.push_back ({ at - 1, n.curve.back().second });
                        n.curve.push_back ({ at, cents });
                        drop[i] = true;
                    }
                    break;
                }
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
                            const auto b = (c.bend - 8192) / 8192.0 * c.bendRange * 100.0;
                            n.pitch += b;
                            drop[c.bendIndex] = true;
                            baseBend[(size_t) e.channel()] = b;
                        }
                        else
                            baseBend[(size_t) e.channel()] = 0.0;
                        open[e.channel() << 7 | e.data1].push_back (s.notes.size());
                        sounding[(size_t) e.channel()].push_back (s.notes.size());
                        s.notes.push_back (n);
                        break;
                    }
                    [[fallthrough]];
                case 0x80:
                {
                    auto& list = open[e.channel() << 7 | e.data1];
                    if (! list.empty())
                    {
                        std::erase (sounding[(size_t) e.channel()], list.front());
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

    //==========================================================================
    const PitchSystem& systemAt (const Sequence& s, const PitchSystem& before, int64_t tick)
    {
        const PitchSystem* in = &before;
        for (const auto& c : s.systems)
            if (c.tick <= tick)
                in = &c.system;
        return *in;
    }

    void changeSystem (Sequence& s, int64_t tick, const PitchSystem& system)
    {
        tick = std::max<int64_t> (0, tick);
        for (auto& c : s.systems)
            if (c.tick == tick)
            {
                c.system = system;
                return;
            }
        s.systems.push_back ({ tick, system });
        std::stable_sort (s.systems.begin(), s.systems.end(), [] (const auto& a, const auto& b) { return a.tick < b.tick; });
    }

    bool removeSystemChange (Sequence& s, int64_t tick)
    {
        for (auto i = (int) s.systems.size() - 1; i >= 0; --i)
            if (s.systems[(size_t) i].tick <= tick)
            {
                s.systems.erase (s.systems.begin() + i);
                return true;
            }
        return false;
    }

    void bySystem (Sequence& s, const PitchSystem& before, const std::function<void (Sequence&, const PitchSystem&)>& edit)
    {
        s.sort();
        const auto picked = s.anySelected();
        std::vector<bool> target (s.notes.size(), false);
        for (auto i : s.targets())
            target[i] = true;
        // The stretches: before the first change, then from each change to the next.
        std::vector<int64_t> from { std::numeric_limits<int64_t>::min() };
        for (const auto& c : s.systems)
            from.push_back (c.tick);
        for (size_t k = 0; k < from.size(); ++k)
        {
            const auto to = k + 1 < from.size() ? from[k + 1] : std::numeric_limits<int64_t>::max();
            if (k + 1 < from.size() && to <= from[k])
                continue;
            bool any = false;
            for (size_t i = 0; i < s.notes.size(); ++i)
            {
                const auto in = s.notes[i].start >= from[k] && s.notes[i].start < to;
                s.notes[i].selected = in && target[i];
                any |= s.notes[i].selected;
            }
            if (! any)
                continue;
            const auto& sys = k == 0 ? before : s.systems[k - 1].system;
            edit (s, sys);
            // Notes of one start may have changed places: their flags go with them.
            target.resize (s.notes.size(), false);
            for (size_t i = 0; i < s.notes.size(); ++i)
                if (s.notes[i].start >= from[k] && s.notes[i].start < to)
                    target[i] = s.notes[i].selected;
        }
        for (size_t i = 0; i < s.notes.size(); ++i)
            s.notes[i].selected = picked && target[i];
    }

    namespace
    {
        const std::string markerPrefix = "PHI system: ";
    }

    std::string systemMarker (const PitchSystem& p)
    {
        std::ostringstream out;
        out.imbue (std::locale::classic());
        out.precision (10);
        out << markerPrefix << p.name << " | " << p.family << " | tonic " << p.tonic << " | period " << p.period << " |";
        for (auto d : p.degrees)
            out << ' ' << d;
        out << " |";
        for (size_t i = 0; i < p.names.size(); ++i)
            out << (i == 0 ? " " : ",") << p.names[i];
        return out.str();
    }

    bool parseSystemMarker (const std::string& text, PitchSystem& p)
    {
        if (text.rfind (markerPrefix, 0) != 0)
            return false;
        std::vector<std::string> parts;
        size_t at = markerPrefix.size();
        for (;;)
        {
            const auto bar = text.find (" |", at);
            parts.push_back (text.substr (at, bar == std::string::npos ? std::string::npos : bar - at));
            if (bar == std::string::npos)
                break;
            at = bar + 2;
            if (at < text.size() && text[at] == ' ')
                ++at;
        }
        if (parts.size() < 5)
            return false;
        PitchSystem r;
        r.name = parts[0];
        r.family = parts[1];
        const auto number = [] (const std::string& t, const char* key, double& v)
        {
            std::istringstream in (t);
            in.imbue (std::locale::classic());
            std::string k;
            return (bool) (in >> k >> v) && k == key;
        };
        if (! number (parts[2], "tonic", r.tonic) || ! number (parts[3], "period", r.period) || r.period <= 0.0)
            return false;
        std::istringstream degrees (parts[4]);
        degrees.imbue (std::locale::classic());
        for (double d; degrees >> d;)
            r.degrees.push_back (d);
        if (r.degrees.empty() || ! std::is_sorted (r.degrees.begin(), r.degrees.end()))
            return false;
        if (parts.size() > 5 && ! parts[5].empty())
        {
            std::string name;
            std::istringstream names (parts[5]);
            while (std::getline (names, name, ','))
                r.names.push_back (name);
            if (r.names.size() != r.degrees.size())
                r.names.clear();
        }
        p = std::move (r);
        return true;
    }

    Encoding suggestedEncoding (const Sequence& s)
    {
        for (const auto& n : s.notes)
            if (std::abs (n.pitch - n.key() * 100.0) > 0.5 || (! n.curve.empty() && n.channel != 9))
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

        struct Chan { int64_t busyUntil = 0, curvedUntil = 0; int bend = -1; std::vector<std::pair<int64_t, int>> sounding; bool used = false, bentKeys = false; };
        std::array<Chan, 16> chans {};
        const auto toBend = [&] (double cents)
        { return std::clamp ((int) std::lround (8192.0 + cents / (range * 100.0) * 8192.0), 0, 16383); };
        const auto every = std::max<int64_t> (1, s.ticksPerQuarter / 48);
        // A curved note's bends while it sounds, after the one at its start.
        const auto curveBends = [&] (const Note& n, int channel, double offset, int first)
        {
            auto last = first;
            for (auto t = every; t < n.length; t += every)
            {
                const auto b = toBend (offset + n.bendAt (t));
                if (b != last)
                    add (n.start + t, (uint8_t) (0xE0 | channel), b & 0x7F, b >> 7, 3);
                last = b;
            }
            return last;
        };
        auto notes = s.notes;
        std::stable_sort (notes.begin(), notes.end(), [] (const Note& a, const Note& b) { return a.start < b.start; });
        for (const auto& n : notes)
        {
            if (n.length <= 0)
                continue;
            const auto drum = o.keepDrums && n.channel == 9;
            const auto curved = ! n.curve.empty() && ! drum;
            auto key = n.key();
            if (! bent || drum)
            {
                auto& c = chans[(size_t) n.channel];
                if (curved)
                {
                    // Keys: the curve on the note's own channel, as a file's pitch wheel bends it.
                    const auto first = toBend (n.bendAt (0));
                    add (n.start, (uint8_t) (0xE0 | n.channel), first & 0x7F, first >> 7, 3);
                    if (curveBends (n, n.channel, 0.0, first) != 8192 || first != 8192)
                        add (n.end(), (uint8_t) (0xE0 | n.channel), 0, 64, 3);
                    c.bentKeys = true;
                }
                add (n.start, (uint8_t) (0x90 | n.channel), key, std::clamp (n.velocity, 1, 127), 4);
                add (n.end(), (uint8_t) (0x80 | n.channel), key, n.release, 2);
                c.used = true;
                continue;
            }
            const auto offset = n.pitch - key * 100.0;
            const auto bend = toBend (offset + n.bendAt (0));
            for (auto& c : chans)
                std::erase_if (c.sounding, [&] (const auto& p) { return p.first <= n.start; });
            const auto free = [&] (int c) { return chans[(size_t) c].sounding.empty(); };
            const auto keyFree = [&] (int c) { for (auto& p : chans[(size_t) c].sounding) if (p.second == key) return false; return true; };
            // A note joins a channel only when no curved note is moving its bend, and a curved
            // note only a free one.
            const auto straight = [&] (int c) { return ! curved && chans[(size_t) c].curvedUntil <= n.start; };
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
                pick = earliest ([&] (int c) { return ! free (c) && straight (c) && chans[(size_t) c].bend == bend && keyFree (c); });
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
                    pick = earliest ([&] (int c) { return straight (c) && chans[(size_t) c].bend == bend && keyFree (c); });
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
            c.bend = curved ? curveBends (n, pick, offset, bend) : bend;
            if (curved)
                c.curvedUntil = std::max (c.curvedUntil, n.end());
            c.sounding.push_back ({ n.end(), key });
            c.busyUntil = std::max (c.busyUntil, n.end());
            c.used = true;
        }

        // The set-up at the start: the MPE zone, then the bend range of each channel bent.
        auto anyBentKeys = false;
        for (auto& c : chans)
            anyBentKeys |= c.bentKeys;
        if (bent || anyBentKeys)
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
            for (int c = 0; c < 16; ++c)
                if (chans[(size_t) c].bentKeys && std::find (pool.begin(), pool.end(), c) == pool.end())
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
        song->markers = s.markers;
        for (const auto& c : s.systems)
            song->markers.push_back ({ c.tick, systemMarker (c.system) });
        std::stable_sort (song->markers.begin(), song->markers.end(), [] (const auto& a, const auto& b) { return a.tick < b.tick; });
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
                const auto cut = tick - n.start;
                if (! n.curve.empty())
                {
                    // The curve split with it, each half holding the bend it had at the cut.
                    const auto at = n.bendAt (cut);
                    right.curve = { { 0, at } };
                    for (const auto& p : n.curve)
                        if (p.first > cut)
                            right.curve.push_back ({ p.first - cut, p.second });
                    std::erase_if (n.curve, [&] (const auto& p) { return p.first >= cut; });
                    n.curve.push_back ({ cut, at });
                }
                n.length = cut;
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
                    if (! n.curve.empty() || ! m.curve.empty())
                    {
                        // The curves joined: the first's to its end, then the second's from its start.
                        const auto shift = m.start - n.start;
                        if (n.curve.empty())
                            n.curve = { { 0, 0.0 } };
                        n.curve.push_back ({ std::min (n.length, shift), n.bendAt (n.length) });
                        if (m.curve.empty())
                            n.curve.push_back ({ shift, 0.0 });
                        for (const auto& p : m.curve)
                            n.curve.push_back ({ shift + p.first, p.second });
                        std::stable_sort (n.curve.begin(), n.curve.end(), [] (const auto& a, const auto& b) { return a.first < b.first; });
                    }
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
    namespace
    {
        // The cents from a note's pitch to the degree `steps` from its nearest one.
        double degreeAway (const PitchSystem& sys, const Note& n, int steps)
        {
            if (sys.degrees.empty())
                return steps * 100.0;
            return sys.pitchOf (sys.nearestStep (n.pitch) + steps) - n.pitch;
        }

        int64_t ticksPerSecond (const Sequence& s, double bpm)
        {
            return std::max<int64_t> (1, (int64_t) std::lround (s.ticksPerQuarter * (bpm > 0.0 ? bpm : 120.0) / 60.0));
        }

        // A curve from points, cut to the note and in time order.
        void setCurve (Note& n, std::vector<std::pair<int64_t, double>> points)
        {
            for (auto& p : points)
                p.first = std::clamp<int64_t> (p.first, 0, std::max<int64_t> (0, n.length));
            std::stable_sort (points.begin(), points.end(), [] (const auto& a, const auto& b) { return a.first < b.first; });
            n.curve = std::move (points);
        }

        // Each target that is not a drum.
        template <typename F> void eachTonal (Sequence& s, F&& f)
        {
            for (auto i : s.targets())
                if (s.notes[i].channel != 9 && s.notes[i].length > 0)
                    f (s.notes[i]);
        }
    }

    void glideToNext (Sequence& s, const PitchSystem& sys, double from, bool throughDegrees)
    {
        s.sort();
        const auto t = s.targets();
        for (auto i : t)
        {
            auto& n = s.notes[i];
            if (n.channel == 9 || n.length <= 1)
                continue;
            // The next note; of a chord there, the one nearest in pitch.
            const Note* next = nullptr;
            for (size_t j = i + 1; j < s.notes.size(); ++j)
            {
                const auto& m = s.notes[j];
                if (m.start <= n.start || m.channel == 9)
                    continue;
                if (next != nullptr && m.start > next->start)
                    break;
                if (next == nullptr || std::abs (m.pitch - n.pitch) < std::abs (next->pitch - n.pitch))
                    next = &m;
            }
            if (next == nullptr)
                continue;
            const auto target = next->pitch - n.pitch;
            const auto t0 = (int64_t) std::lround ((double) n.length * std::clamp (from, 0.0, 0.95));
            std::vector<std::pair<int64_t, double>> c { { 0, 0.0 }, { t0, 0.0 } };
            const auto a = sys.nearestStep (n.pitch), b = sys.nearestStep (next->pitch);
            if (throughDegrees && ! sys.degrees.empty() && std::abs (b - a) > 1)
            {
                // Mīṇḍ: each degree between touched and held a moment, as a pulled string does.
                const auto k = std::abs (b - a), dir = b > a ? 1 : -1;
                const auto d = (double) (n.length - t0) / k;
                for (int m = 1; m < k; ++m)
                {
                    const auto v = sys.pitchOf (a + dir * m) - n.pitch;
                    const auto at = (double) t0 + d * m;
                    c.push_back ({ (int64_t) std::lround (at - d * 0.3), v });
                    c.push_back ({ (int64_t) std::lround (at + d * 0.3), v });
                }
            }
            c.push_back ({ n.length, target });
            setCurve (n, std::move (c));
        }
    }

    void slideIn (Sequence& s, const PitchSystem& sys, int steps, int64_t ticks)
    {
        eachTonal (s, [&] (Note& n)
        {
            setCurve (n, { { 0, degreeAway (sys, n, steps) }, { std::min (ticks, n.length), 0.0 } });
        });
    }

    void swing (Sequence& s, double depth, double rateHz, double bpm, bool below)
    {
        const auto cycle = std::max (4.0, (double) ticksPerSecond (s, bpm) / std::max (0.1, rateHz));
        eachTonal (s, [&] (Note& n)
        {
            std::vector<std::pair<int64_t, double>> c;
            for (double t = 0; t <= (double) n.length; t += cycle / 8.0)
            {
                const auto phase = t / cycle * 2.0 * 3.14159265358979323846;
                c.push_back ({ (int64_t) std::lround (t), below ? -depth * (1.0 - std::cos (phase)) * 0.5 : depth * std::sin (phase) });
            }
            setCurve (n, std::move (c));
        });
    }

    void gamak (Sequence& s, const PitchSystem& sys, double rateHz, double bpm)
    {
        const auto cycle = std::max (2.0, (double) ticksPerSecond (s, bpm) / std::max (0.1, rateHz));
        eachTonal (s, [&] (Note& n)
        {
            const auto up = degreeAway (sys, n, 1);
            std::vector<std::pair<int64_t, double>> c;
            for (double t = 0; t < (double) n.length; t += cycle)
            {
                c.push_back ({ (int64_t) std::lround (t), 0.0 });
                c.push_back ({ (int64_t) std::lround (t + cycle * 0.5), up });
            }
            c.push_back ({ n.length, 0.0 });
            setCurve (n, std::move (c));
        });
    }

    void kan (Sequence& s, const PitchSystem& sys, int steps, int64_t ticks)
    {
        eachTonal (s, [&] (Note& n)
        {
            const auto v = degreeAway (sys, n, steps == 0 ? 1 : steps);
            const auto len = std::min (ticks, n.length / 2);
            setCurve (n, { { 0, v }, { len * 2 / 3, v }, { len, 0.0 } });
        });
    }

    void turn (Sequence& s, const PitchSystem& sys, const std::vector<int>& degrees, int64_t ticks)
    {
        if (degrees.empty() || ticks <= 0)
            return;
        eachTonal (s, [&] (Note& n)
        {
            // As short as the note needs: the turn fits in its first half.
            const auto each = std::max<int64_t> (1, std::min (ticks, n.length / (2 * (int64_t) degrees.size())));
            std::vector<std::pair<int64_t, double>> c;
            for (size_t k = 0; k < degrees.size(); ++k)
            {
                const auto v = degrees[k] == 0 ? 0.0 : degreeAway (sys, n, degrees[k]);
                const auto at = (int64_t) k * each;
                c.push_back ({ at, v });
                c.push_back ({ at + each * 3 / 4, v });
            }
            if (degrees.back() != 0)
                c.push_back ({ (int64_t) degrees.size() * each, 0.0 });
            setCurve (n, std::move (c));
        });
    }

    void clearCurves (Sequence& s)
    {
        for (auto i : s.targets())
            s.notes[i].curve.clear();
    }

    //==========================================================================
    std::vector<Note> notesFromPitch (const std::vector<Heard>& heard, const PitchSystem& sys, const HumOptions& o,
                                      int ticksPerQuarter, double bpm, int64_t at)
    {
        std::vector<Note> notes;
        if (heard.empty() || sys.degrees.empty())
            return notes;
        const auto perSecond = ticksPerQuarter * (bpm > 0.0 ? bpm : 120.0) / 60.0;
        const auto tickOf = [&] (double seconds) { return at + (int64_t) std::llround (seconds * perSecond); };
        const auto hop = heard.size() > 1 ? std::max (1.0e-4, (heard.back().seconds - heard.front().seconds) / (double) (heard.size() - 1)) : 0.01;
        const auto voiced = [&] (size_t i) { return heard[i].pitch >= 0.0; };

        struct Segment { size_t first, last; int step; size_t onStep; };   // onStep: points on its own degree
        std::vector<Segment> segments;
        bool open = false;
        Segment now {};
        int candidate = 0;
        size_t candidateFirst = 0;
        bool hasCandidate = false;
        const auto close = [&]
        {
            if (open && heard[now.last].seconds + hop - heard[now.first].seconds >= o.shortestSeconds - 1.0e-9)
                segments.push_back (now);
            open = false;
            hasCandidate = false;
        };
        for (size_t i = 0; i < heard.size(); ++i)
        {
            if (! voiced (i))
            {
                if (open && heard[i].seconds - heard[now.last].seconds > o.holdSeconds)
                    close();
                continue;
            }
            const auto step = sys.nearestStep (heard[i].pitch);
            if (! open)
            {
                now = { i, i, step, 1 };
                open = true;
                hasCandidate = false;
                continue;
            }
            now.last = i;
            if (step == now.step)
            {
                ++now.onStep;
                hasCandidate = false;
                continue;
            }
            if (! hasCandidate || step != candidate)
            {
                candidate = step;
                candidateFirst = i;
                hasCandidate = true;
            }
            if (heard[i].seconds - heard[candidateFirst].seconds + hop < o.holdSeconds - 1.0e-9)
                continue;   // a moment away (an ornament): still this note
            // A new degree, held: this note ends where it began. A note that was hardly on its own
            // degree is the slide into the new one, and starts it.
            Segment next { candidateFirst, i, candidate, i - candidateFirst + 1 };
            if ((double) now.onStep * hop < o.shortestSeconds - 1.0e-9)
                next.first = now.first;
            else
            {
                now.last = candidateFirst - 1;
                segments.push_back (now);
            }
            now = next;
            hasCandidate = false;
        }
        close();

        for (const auto& g : segments)
        {
            std::vector<double> sung;
            double loudest = 0.0;
            for (auto i = g.first; i <= g.last; ++i)
                if (voiced (i))
                {
                    sung.push_back (heard[i].pitch);
                    loudest = std::max (loudest, heard[i].level);
                }
            if (sung.empty())
                continue;
            Note n;
            if (o.snap)
                n.pitch = sys.pitchOf (g.step);
            else
            {
                auto middle = sung;
                std::nth_element (middle.begin(), middle.begin() + (std::ptrdiff_t) (middle.size() / 2), middle.end());
                n.pitch = std::round (middle[middle.size() / 2]);
            }
            const auto start = tickOf (heard[g.first].seconds);
            const auto end = tickOf (heard[g.last].seconds + hop);
            n.start = start;
            n.length = std::max<int64_t> (1, end - start);
            const auto db = 20.0 * std::log10 (std::max (loudest, 1.0e-6));
            n.velocity = std::clamp ((int) std::lround (127.0 + db * 107.0 / 50.0), 20, 127);
            if (o.curves)
            {
                std::vector<std::pair<int64_t, double>> c;
                double far = 0.0;
                for (auto i = g.first; i <= g.last; ++i)
                {
                    if (! voiced (i))
                        continue;
                    const auto off = heard[i].pitch - n.pitch;
                    far = std::max (far, std::abs (off));
                    if (c.empty() || std::abs (off - c.back().second) >= 3.0)
                        c.push_back ({ tickOf (heard[i].seconds) - start, off });
                }
                if (far > 20.0)
                    n.curve = std::move (c);
            }
            if (o.grid > 0)
            {
                const auto shift = (int64_t) std::llround ((double) n.start / (double) o.grid) * o.grid - n.start;
                n.start += shift;
                n.length = std::max (o.grid, (int64_t) std::llround ((double) n.length / (double) o.grid) * o.grid);
            }
            notes.push_back (std::move (n));
        }
        return notes;
    }

    std::vector<Heard> listen (const AudioData& audio)
    {
        std::vector<Heard> heard;
        pitch::Settings settings;
        settings.lowestHz = 55.0;
        settings.highestHz = 1600.0;
        settings.hop = std::max (64, (int) std::lround (audio.sampleRate() / 200.0));   // 5 ms
        const auto track = pitch::track (audio, settings);
        if (track == nullptr)
            return heard;
        // The loudness around each point: RMS over a hop each side, the channels mixed.
        const auto channels = audio.numChannels();
        const auto span = settings.hop * 2;
        std::vector<std::vector<float>> block ((size_t) channels, std::vector<float> ((size_t) span));
        std::vector<float*> to ((size_t) channels);
        for (int c = 0; c < channels; ++c)
            to[(size_t) c] = block[(size_t) c].data();
        for (const auto& p : track->points)
        {
            audio.read (p.at - settings.hop, span, to.data());
            double sum = 0.0;
            for (int i = 0; i < span; ++i)
            {
                double x = 0.0;
                for (int c = 0; c < channels; ++c)
                    x += block[(size_t) c][(size_t) i];
                x /= std::max (1, channels);
                sum += x * x;
            }
            Heard h;
            h.seconds = (double) p.at / audio.sampleRate();
            h.pitch = p.hz > 0.0f ? 6900.0 + 1200.0 * std::log2 ((double) p.hz / 440.0) : -1.0;
            h.level = std::sqrt (sum / span);
            heard.push_back (h);
        }
        return heard;
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

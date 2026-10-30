// SPDX-License-Identifier: AGPL-3.0-only
#include "ModalHarmony.h"

#include <algorithm>
#include <cmath>
#include <map>
#include <set>
#include <sstream>
#include <tuple>

namespace modalharmony
{
    namespace
    {
        constexpr double allowed = 45.0;   // cents from a just consonance a chord's worst interval may be

        int floorDiv (int a, int b) { return a >= 0 ? a / b : -((-a + b - 1) / b); }

        // A degree index counted on past the octave: its semitones and cents above the final.
        int semis (const std::vector<Degree>& d, int idx)
        {
            const int n = (int) d.size();
            const auto o = floorDiv (idx, n);
            return d[(size_t) (idx - o * n)].key + 12 * o;
        }

        double centsOf (const std::vector<Degree>& d, int idx)
        {
            const int n = (int) d.size();
            const auto o = floorDiv (idx, n);
            return d[(size_t) (idx - o * n)].cents + 1200.0 * o;
        }

        bool tritone (double cents)
        {
            const auto x = std::fmod (std::abs (cents), 1200.0);
            return x > 560.0 && x < 640.0;
        }

        // The degree above `from` (within an octave) nearest `target` cents above it, if within `allowed`.
        int nearestAbove (const std::vector<Degree>& d, int from, double target)
        {
            int best = -1;
            double miss = allowed;
            for (int j = from + 1; j <= from + (int) d.size(); ++j)
                if (const auto m = std::abs (centsOf (d, j) - centsOf (d, from) - target); m < miss)
                {
                    miss = m;
                    best = j;
                }
            return best;
        }

        int nearestBelow (const std::vector<Degree>& d, int from, double target)
        {
            int best = -1;
            double miss = allowed;
            for (int j = from - 1; j >= from - (int) d.size(); --j)
                if (const auto m = std::abs (centsOf (d, from) - centsOf (d, j) - target); m < miss)
                {
                    miss = m;
                    best = j;
                }
            return best;
        }

        Chord build (const std::vector<Degree>& d, Texture t, int root, int notes, const std::vector<int>& marked)
        {
            Chord c;
            if (t == Texture::quartal)
            {
                c.steps.push_back (root);
                for (int k = 1; k < notes; ++k)
                {
                    const auto next = nearestAbove (d, c.steps.back(), 498.04);
                    if (next < 0)
                        return {};
                    c.steps.push_back (next);
                }
            }
            else if (t == Texture::clusters)
            {
                // In the manner of a shō aitake: the root, its neighbour, its fourth and fifth, its octave.
                std::set<int> s { root, root + 1, root + (int) d.size() };
                for (double target : { 498.04, 701.96 })
                    if (const auto j = nearestAbove (d, root, target); j >= 0)
                        s.insert (j);
                c.steps.assign (s.begin(), s.end());
            }
            else
                for (int k = 0; k < notes; ++k)
                    c.steps.push_back (root + 2 * k);
            // A cluster is meant to be rough: only its root's fourth and fifth are judged.
            for (size_t a = 0; a < c.steps.size(); ++a)
                for (size_t b = a + 1; b < c.steps.size(); ++b)
                {
                    if ((t == Texture::clusters && a > 0) || (t == Texture::quartal && b != a + 1) || (t == Texture::chords && b >= 3))
                        continue;   // a stack of fourths is judged fourth by fourth, a seventh chord by its triad
                    const auto interval = centsOf (d, c.steps[b]) - centsOf (d, c.steps[a]);
                    if (t != Texture::clusters || b + 1 < c.steps.size())
                        c.roughness = std::max (c.roughness, roughness (interval));
                }
            if (t == Texture::clusters)
                c.roughness = std::min (c.roughness, allowed - 1.0);
            for (auto s : c.steps)
                c.characteristic = c.characteristic || std::find (marked.begin(), marked.end(), d[(size_t) (s - floorDiv (s, (int) d.size()) * (int) d.size())].key) != marked.end();
            return c;
        }

        bool hasTritone (const std::vector<Degree>& d, const Chord& c)
        {
            for (size_t a = 0; a < c.steps.size(); ++a)
                for (size_t b = a + 1; b < c.steps.size(); ++b)
                    if (tritone (centsOf (d, c.steps[b]) - centsOf (d, c.steps[a])))
                        return true;
            return false;
        }

        // A deterministic choice for the line, so a variation number gives the same line again.
        struct Random
        {
            unsigned state;
            unsigned next() { state = state * 1664525u + 1013904223u; return state >> 8; }
            double unit() { return (next() & 0xFFFF) / 65536.0; }
        };

        // Notes at times, written out as one voice with rests between, `total` beats long.
        struct Writer
        {
            std::vector<std::tuple<double, double, int, int>> notes;   // start, length, pitch, velocity
            void add (double at, double length, int pitch, int velocity) { if (length > 1.0e-6) notes.emplace_back (at, length, pitch, velocity); }

            harmony::Voice voice (std::string name, double total) const
            {
                auto sorted = notes;
                std::sort (sorted.begin(), sorted.end());
                harmony::Voice v { std::move (name), {} };
                double at = 0.0;
                for (size_t i = 0; i < sorted.size(); ++i)
                {
                    auto [start, length, pitch, velocity] = sorted[i];
                    if (start < at - 1.0e-6)
                        continue;   // under the note before
                    if (start > at + 1.0e-6)
                        v.notes.push_back ({ 0, start - at, true });
                    auto end = std::min (total, start + length);
                    if (i + 1 < sorted.size())
                        end = std::min (end, std::get<0> (sorted[i + 1]));
                    if (end <= start + 1.0e-6)
                        continue;
                    v.notes.push_back ({ pitch, end - start, false, velocity });
                    at = end;
                }
                if (total > at + 1.0e-6)
                    v.notes.push_back ({ 0, total - at, true });
                return v;
            }
        };
    }

    std::vector<Texture> textures()
    {
        return { Texture::chords, Texture::quartal, Texture::clusters, Texture::drone, Texture::parallel, Texture::heterophony,
                 Texture::layered, Texture::dyads };
    }

    std::string textureName (Texture t)
    {
        switch (t)
        {
            case Texture::chords:      return "chords (in thirds)";
            case Texture::quartal:     return "chords (in fourths)";
            case Texture::clusters:    return "clusters (as the shō)";
            case Texture::drone:       return "drone (as the tānpurā or ison)";
            case Texture::parallel:    return "parallel fourths (as organum)";
            case Texture::heterophony: return "heterophony (the line in variants, over a drone)";
            case Texture::layered:     return "layered (as a gamelan)";
            case Texture::dyads:       return "interlocking dyads (as the mbira)";
        }
        return {};
    }

    bool usesChords (Texture t) { return t == Texture::chords || t == Texture::quartal || t == Texture::clusters; }

    Texture traditional (const mixer::TuningSystem& t)
    {
        static const std::map<std::string, Texture> byFamily {
            { "jazz mode", Texture::quartal }, { "gagaku mode", Texture::clusters },
            { "raga", Texture::drone }, { "melakarta", Texture::drone }, { "murchana", Texture::drone }, { "grama", Texture::drone },
            { "byzantine echos", Texture::drone }, { "greek genus", Texture::drone }, { "greek octave species", Texture::drone },
            { "historical tuning", Texture::drone },
            { "maqam", Texture::heterophony }, { "makam", Texture::heterophony }, { "dastgah", Texture::heterophony },
            { "avaz", Texture::heterophony }, { "mugam", Texture::heterophony }, { "jins", Texture::heterophony },
            { "qenet", Texture::heterophony }, { "chinese mode", Texture::heterophony }, { "chinese regional mode", Texture::heterophony },
            { "korean mode", Texture::heterophony }, { "korean folk mode", Texture::heterophony }, { "japanese mode", Texture::heterophony },
            { "thai mode", Texture::heterophony }, { "vietnamese mode", Texture::heterophony },
            { "javanese gamelan", Texture::layered }, { "balinese gamelan", Texture::layered }, { "sundanese gamelan", Texture::layered },
            { "mbira dzavadzimu", Texture::dyads }, { "nyunga nyunga", Texture::dyads }, { "matepe", Texture::dyads } };
        auto family = t.family;
        if (const auto comma = family.find (','); comma != std::string::npos)
            family = family.substr (0, comma);   // "raga, equal tempered"
        const auto it = byFamily.find (family);
        return it != byFamily.end() ? it->second : Texture::chords;
    }

    std::vector<Degree> degrees (const mixer::TuningSystem& t)
    {
        const auto keys = mixer::modeKeys (t) | 1u;
        const auto cents = mixer::keyCents (t);
        std::vector<Degree> out;
        for (int k = 0; k < 12; ++k)
            if ((keys >> k) & 1u)
                out.push_back ({ k, k == 0 ? 0.0 : cents[(size_t) k] });
        return out;
    }

    std::vector<int> characteristic (const std::vector<Degree>& d)
    {
        if (d.size() != 7)
            return {};
        static const std::set<int> ionian { 0, 2, 4, 5, 7, 9, 11 }, aeolian { 0, 2, 3, 5, 7, 8, 10 };
        auto overlap = [&d] (const std::set<int>& s) { return std::count_if (d.begin(), d.end(), [&s] (const Degree& x) { return s.count (x.key) > 0; }); };
        const bool minor = std::any_of (d.begin(), d.end(), [] (const Degree& x) { return x.key == 3; });
        const auto oi = overlap (ionian), oa = overlap (aeolian);
        const auto& ref = oa > oi || (oa == oi && minor) ? aeolian : ionian;
        std::vector<int> out;
        for (const auto& x : d)
            if (ref.count (x.key) == 0)
                out.push_back (x.key);
        return out;
    }

    double roughness (double cents)
    {
        static const double just[] { 0.0, 315.64, 386.31, 498.04, 701.96, 813.69, 884.36, 1200.0 };
        const auto x = std::fmod (std::abs (cents), 1200.0);
        double best = 1200.0;
        for (auto j : just)
            best = std::min (best, std::abs (x - j));
        return best;
    }

    std::vector<Chord> progression (const std::vector<Degree>& d, Texture t, int changes, int notes)
    {
        if (d.size() < 2)
            return {};
        if (! usesChords (t))
            t = Texture::chords;
        const int n = (int) d.size();
        const auto marked = characteristic (d);
        const auto tonic = build (d, t, 0, notes, marked);
        if (tonic.steps.empty() || tonic.roughness > allowed)
            // No pure fourth on the final (lydian's is raised): its chords in thirds instead.
            return t == Texture::quartal ? progression (d, Texture::chords, changes, notes) : std::vector<Chord> {};
        // The others ranked: smooth, carrying the characteristic note, no tritone, near the final
        // by step, the dominant (a fifth above) kept away and the fourth above less so.
        std::vector<std::pair<double, Chord>> ranked;
        for (int root = 1; root < n; ++root)
        {
            auto c = build (d, t, root, notes, marked);
            if (c.steps.empty() || c.roughness > allowed)
                continue;
            const auto rootCents = d[(size_t) root].cents;
            double score = 2.0 * c.roughness + 6.0 * std::min (root, n - root);
            if (hasTritone (d, c) && t != Texture::clusters)
                score += 60.0;
            if (c.characteristic)
                score -= 40.0;
            if (std::abs (rootCents - 701.96) < 30.0)
                score += 40.0;
            else if (std::abs (rootCents - 498.04) < 30.0)
                score += 15.0;
            ranked.push_back ({ score, c });
        }
        std::stable_sort (ranked.begin(), ranked.end(), [] (const auto& a, const auto& b) { return a.first < b.first; });
        std::vector<Chord> out { tonic };
        for (int i = 1; i < std::max (1, changes); ++i)
            out.push_back (ranked.empty() ? tonic : ranked[(size_t) ((i - 1) % std::min<size_t> (2, ranked.size()))].second);
        return out;
    }

    std::string describe (const std::vector<Degree>& d, const std::vector<Chord>& chords)
    {
        std::ostringstream o;
        const int n = (int) d.size();
        for (size_t i = 0; i < chords.size(); ++i)
        {
            const auto& c = chords[i];
            if (c.steps.empty())
                continue;
            if (i > 0)
                o << " → ";
            const auto root = c.steps.front();
            o << (root - floorDiv (root, n) * n + 1) << " (";
            for (size_t k = 0; k < c.steps.size(); ++k)
                o << (k > 0 ? "·" : "") << (int) std::lround (centsOf (d, c.steps[k]) - centsOf (d, root));
            o << " ¢)";
        }
        return o.str();
    }

    Timing steady (double beatsPerChord, int chords)
    {
        Timing t;
        beatsPerChord = std::max (0.25, beatsPerChord);
        chords = std::max (1, chords);
        t.beats = beatsPerChord * chords;
        for (int i = 0; i < chords; ++i)
        {
            t.changes.push_back (i * beatsPerChord);
            t.strikes.push_back ({ i * beatsPerChord, i == 0 ? 100 : 84 });
        }
        return t;
    }

    Timing fromRhythm (const rhythm::Rhythm& r, bool onStrokes)
    {
        Timing t;
        t.beats = std::max (1, r.beats);
        double at = 0.0;
        for (auto s : r.sections)
        {
            if (at < t.beats)
                t.changes.push_back (at);
            at += s;
        }
        if (t.changes.empty())
            for (double b = 0.0; b < t.beats; b += t.beats >= 8 ? 4.0 : t.beats)
                t.changes.push_back (b);
        std::map<double, int> strikes;
        for (size_t i = 0; i < t.changes.size(); ++i)
            strikes[t.changes[i]] = i == 0 ? 100 : 84;
        if (onStrokes && ! r.lanes.empty())
        {
            // The first sound that is not a tāḷam's pulse: the ṭhekā's, the clave's, the kick's strokes.
            int lane = 0;
            while (lane + 1 < (int) r.lanes.size() && r.lanes[(size_t) lane].pulse)
                ++lane;
            const auto perBeat = std::max (1, r.lanes[(size_t) lane].perBeat);
            for (const auto& h : rhythm::hits (r))
                if (h.lane == lane)
                {
                    auto& v = strikes[(double) h.step / perBeat];
                    v = std::max (v, h.velocity);
                }
        }
        for (const auto& [beat, velocity] : strikes)
            if (beat < t.beats)
                t.strikes.push_back ({ beat, velocity });
        return t;
    }

    harmony::Harmony generate (const mixer::TuningSystem& system, const Timing& timing, const Settings& s)
    {
        harmony::Harmony h;
        h.name = "harmonized in " + system.name;
        h.plain = "harmonized in " + system.plain;
        h.texture = textureName (s.texture);
        const auto d = degrees (system);
        const int n = (int) d.size();
        const auto cycles = std::max (1, s.cycles);
        const auto beats = std::max (0.25, timing.beats);
        const auto total = beats * cycles;
        auto changes = timing.changes.empty() ? std::vector<double> { 0.0 } : timing.changes;
        auto strikes = timing.strikes.empty() ? std::vector<std::pair<double, int>> { { 0.0, 100 } } : timing.strikes;
        const int notes = system.family.rfind ("jazz mode", 0) == 0 ? 4 : 3;

        auto texture = s.texture;
        auto chords = progression (d, texture, (int) changes.size(), notes);
        if (usesChords (texture) && chords.empty())
            texture = Texture::drone;   // nothing smooth enough in this tuning: the mode over its final
        if (chords.empty())
            chords.assign (changes.size(), Chord { { 0 }, 0.0, false });
        h.about = describe (d, chords);
        auto chordAt = [&changes] (double beat)
        {
            size_t c = 0;
            for (size_t i = 0; i < changes.size(); ++i)
                if (changes[i] <= beat + 1.0e-6)
                    c = i;
            return c;
        };
        const int lineBase = 12;

        // The line: by step through the mode, a beat a note, landing on a tone of the chord at
        // each change, and on the final at the end (at every cycle's end for a gamelan's gong).
        Random random { s.variation * 2654435761u + 17u };
        std::vector<std::tuple<double, double, int>> line;   // start, length, degree index
        int cur = 0;
        for (int c = 0; c < cycles; ++c)
        {
            const auto steps = (int) std::ceil (beats - 1.0e-6);
            for (int b = 0; b < steps; ++b)
            {
                const double at = c * beats + b;
                const double length = std::min (1.0, beats - b);
                const auto chordIndex = chordAt (b);
                const bool change = std::any_of (changes.begin(), changes.end(), [b] (double x) { return std::abs (x - b) < 1.0e-6; });
                const bool lastOfCycle = b == steps - 1;
                if ((lastOfCycle && (c == cycles - 1 || texture == Texture::layered)))
                    cur = cur > n / 2 + 1 ? n : 0;
                else if (change)
                {
                    int best = cur, miss = 1 << 20;
                    for (auto step : chords[chordIndex].steps)
                        for (int o = -1; o <= 1; ++o)
                        {
                            const auto candidate = step + o * n;
                            if (candidate < -2 || candidate > n + 3)
                                continue;
                            if (const auto m = std::abs (candidate - cur); m < miss || (m == miss && random.unit() < 0.5))
                            {
                                miss = m;
                                best = candidate;
                            }
                        }
                    cur = best;
                }
                else if (random.unit() > 0.2)
                    cur = std::clamp (cur + (random.unit() < 0.5 ? -1 : 1), -2, n + 3);
                line.emplace_back (at, length, cur);
            }
        }

        Writer lineW, pedalW;
        if (s.line || texture == Texture::parallel || texture == Texture::heterophony || texture == Texture::layered || texture == Texture::dyads)
            for (const auto& [at, length, idx] : line)
                lineW.add (at, length, lineBase + semis (d, idx), 88);
        const bool wantsPedal = s.pedal && (usesChords (texture) || texture == Texture::parallel || texture == Texture::heterophony);
        if (wantsPedal)
            for (int c = 0; c < cycles; ++c)
                for (size_t i = 0; i < changes.size(); ++i)
                {
                    const auto from = c * beats + changes[i];
                    const auto to = c * beats + (i + 1 < changes.size() ? changes[i + 1] : beats);
                    pedalW.add (from, to - from, -12, i == 0 ? 76 : 64);
                }

        std::vector<harmony::Voice> voices;
        auto put = [&voices, total] (const Writer& w, std::string name) { if (! w.notes.empty()) voices.push_back (w.voice (std::move (name), total)); };

        if (usesChords (texture))
        {
            size_t size = 0;
            for (const auto& c : chords)
                size = std::max (size, c.steps.size());
            std::vector<Writer> parts (size);
            Writer bass;
            for (int c = 0; c < cycles; ++c)
                for (size_t i = 0; i < strikes.size(); ++i)
                {
                    const auto [beat, velocity] = strikes[i];
                    const auto next = i + 1 < strikes.size() ? strikes[i + 1].first : beats;
                    const auto& chord = chords[chordAt (beat)];
                    for (size_t v = 0; v < size; ++v)
                        parts[v].add (c * beats + beat, next - beat, semis (d, chord.steps[std::min (v, chord.steps.size() - 1)]), velocity * 4 / 5);
                    if (! wantsPedal)
                        bass.add (c * beats + beat, next - beat, semis (d, chord.steps.front()) - 12, velocity * 4 / 5);
                }
            put (pedalW, "pedal on the final");
            put (bass, "bass");
            for (size_t v = 0; v < size; ++v)
                put (parts[v], "chord " + std::to_string (v + 1));
        }
        else if (texture == Texture::drone)
        {
            Writer sa, pa;
            const auto fifth = nearestAbove (d, 0, 701.96);
            const auto other = fifth >= 0 ? fifth : nearestAbove (d, 0, 498.04);
            for (int c = 0; c < cycles; ++c)
            {
                sa.add (c * beats, beats, -12, 72);
                if (other >= 0)
                    pa.add (c * beats, beats, semis (d, other) - 12, 64);
            }
            put (sa, "drone (final)");
            put (pa, fifth >= 0 ? "drone (fifth)" : "drone (fourth)");
        }
        else if (texture == Texture::parallel)
        {
            Writer low;
            for (const auto& [at, length, idx] : line)
            {
                const auto j = nearestBelow (d, idx, 498.04);   // else the octave below
                low.add (at, length, lineBase + semis (d, j >= 0 ? j : idx - n), 80);
            }
            put (pedalW, "pedal on the final");
            put (low, "a fourth below");
        }
        else if (texture == Texture::heterophony)
        {
            Writer variant;
            for (const auto& [at, length, idx] : line)
                if (length >= 1.0 - 1.0e-6 && random.unit() < 0.6)
                {
                    variant.add (at, 0.5, lineBase + semis (d, idx), 80);
                    variant.add (at + 0.5, 0.25, lineBase + semis (d, idx + 1), 70);
                    variant.add (at + 0.75, length - 0.75, lineBase + semis (d, idx), 76);
                }
                else
                    variant.add (at, length, lineBase + semis (d, idx), 78);
            Writer drone;
            if (s.pedal)
                for (int c = 0; c < cycles; ++c)
                    drone.add (c * beats, beats, -12, 64);
            put (drone, "drone on the final");
            put (variant, "the line, varied");
        }
        else if (texture == Texture::layered)
        {
            Writer peking, kenong, gong;
            for (const auto& [at, length, idx] : line)
            {
                peking.add (at, length / 2, lineBase + 12 + semis (d, idx), 76);
                peking.add (at + length / 2, length / 2, lineBase + 12 + semis (d, idx), 70);
            }
            for (int c = 0; c < cycles; ++c)
            {
                // The kenong at each section's end on the line's tone there, the gong on the cycle's last beat.
                for (size_t i = 1; i <= changes.size(); ++i)
                {
                    const auto end = i < changes.size() ? changes[i] : beats;
                    const auto at = c * beats + std::max (0.0, end - 1.0);
                    for (const auto& [t, length, idx] : line)
                        if (std::abs (t - at) < 1.0e-6)
                            kenong.add (at, 1.0, semis (d, idx), 84);
                }
                gong.add (c * beats + std::max (0.0, beats - 1.0), 1.0, -24, 100);
            }
            put (peking, "peking (doubling, faster)");
            put (kenong, "kenong");
            put (gong, "gong");
        }
        else if (texture == Texture::dyads)
        {
            Writer low, between;
            for (size_t i = 0; i < line.size(); ++i)
            {
                const auto& [at, length, idx] = line[i];
                const auto j = nearestBelow (d, idx, 701.96);
                const auto k = j >= 0 ? j : nearestBelow (d, idx, 498.04);
                if (k >= 0)
                    low.add (at, std::min (0.5, length), lineBase + semis (d, k), 74);
                if (length > 0.5)
                    between.add (at + 0.5, length - 0.5, lineBase + semis (d, idx + 2), 70);
            }
            put (low, "kushaura (lower note)");
            put (between, "kutsinhira (between)");
        }
        put (lineW, "line");
        h.voices = std::move (voices);
        return h;
    }
}

// SPDX-License-Identifier: AGPL-3.0-only
#include "RhythmGenerator.h"

#include "Rhythms.h"
#include "Stochastic.h"

#include <algorithm>
#include <cmath>
#include <numeric>

namespace rhythmgen
{
    namespace
    {
        constexpr int tpq = 480;

        int stepTicks (const Lane& l) { return tpq / std::clamp (l.stepsPerBeat, 1, 48); }

        std::string compact (const std::string& hits)
        {
            std::string s;
            for (auto c : hits)
                if (c != ' ' && c != '|')
                    s += c == '-' ? '.' : c;
            return s;
        }

        struct Placed
        {
            int lane = 0;
            int64_t step = 0;   // in the lane's steps from the start of the clip
            int64_t tick = 0;   // with swing and humanized timing when asked for
            int velocity = 96;
        };

        // Every hit the settings play, lane by lane, drawn from the seed in a fixed order.
        bool place (const Settings& s, double bpm, bool timing, std::vector<Placed>& out, std::string& error)
        {
            const auto bars = lengthBars (s);
            const auto barTicks = (int64_t) s.beatsPerBar * tpq;
            const auto total = bars * barTicks;
            stochastic::Random random (s.seed);
            for (size_t i = 0; i < s.lanes.size(); ++i)
            {
                const auto& lane = s.lanes[i];
                if (lane.muted)
                    continue;
                stochastic::Sieve sieve;
                if (! lane.sieve.empty())
                {
                    std::string why;
                    sieve = stochastic::Sieve::parse (lane.sieve, why);
                    if (sieve.empty())
                    {
                        error = lane.name + ": " + why;
                        return false;
                    }
                }
                const auto pattern = cycle (lane);
                const auto n = (int64_t) pattern.size();
                if (n == 0)
                    continue;
                const auto step = (int64_t) stepTicks (lane);
                int64_t count = 0;
                for (int64_t k = 0; k * step < total; ++k)
                {
                    const auto at = k * step;
                    const auto bar = at / barTicks;
                    const auto turned = s.rotateEvery > 0 ? bar / s.rotateEvery : 0;
                    auto c = pattern[(size_t) (((k - turned) % n + n) % n)];
                    if (! sieve.empty() && ! sieve.contains (k))
                        c = '.';
                    const bool fill = s.fillEvery > 0 && bar % s.fillEvery == s.fillEvery - 1;
                    // One draw for each step, played or not, so a change to one setting
                    // leaves the rest of the groove where it was.
                    const auto roll = random.uniform();
                    const auto jitter = random.uniform();
                    const auto loud = random.uniform();
                    if (c == '.')
                    {
                        if (! fill || roll >= s.fillChance)
                            continue;
                        c = 'o';
                    }
                    else if (! fill)
                    {
                        const auto through = total > 0 ? (double) at / (double) total : 0.0;
                        const auto chance = lane.chance * (s.densityFrom + (s.densityTo - s.densityFrom) * through);
                        if (roll >= chance)
                            continue;
                    }
                    if (s.skipEvery > 0 && ++count % s.skipEvery == 0)
                        continue;
                    Placed p;
                    p.lane = (int) i;
                    p.step = k;
                    p.velocity = c == 'X' ? 120 : c == 'o' ? 60 : lane.velocity;
                    if (s.humanizeVelocity > 0.0)
                    {
                        stochastic::Random r ((uint64_t) (loud * 9007199254740992.0) + 1);
                        p.velocity = (int) std::lround (stochastic::draw (stochastic::Law::gaussian, p.velocity, s.humanizeVelocity, r));
                    }
                    p.velocity = std::clamp (p.velocity, 1, 127);
                    p.tick = at;
                    if (timing)
                    {
                        // Swing bends time inside each pair of swung steps: the first stretched,
                        // the second squeezed, so finer steps between them follow along.
                        const auto unit = s.swingPerBeat > 0 ? (int64_t) (tpq / std::clamp (s.swingPerBeat, 1, 48)) : step;
                        const auto mid = (double) unit * (1.0 + std::clamp (s.swing, 0.0, 0.5));
                        const auto pos = (double) (at % (2 * unit));
                        const auto bent = pos < (double) unit ? pos * mid / (double) unit
                                                              : mid + (pos - (double) unit) * (2.0 * (double) unit - mid) / (double) unit;
                        p.tick += (int64_t) std::lround (bent - pos);
                        p.tick += (int64_t) std::lround (lane.pushMs * bpm * tpq / 60000.0);
                        if (s.humanizeMs > 0.0)
                        {
                            stochastic::Random r ((uint64_t) (jitter * 9007199254740992.0) + 1);
                            const auto ticks = s.humanizeMs * bpm * tpq / 60000.0;
                            p.tick += (int64_t) std::lround (stochastic::draw (stochastic::Law::gaussian, 0.0, ticks, r));
                        }
                        p.tick = std::clamp<int64_t> (p.tick, 0, std::max<int64_t> (0, total - 1));
                    }
                    out.push_back (p);
                }
            }
            return true;
        }

        enum class Sound { kick, snare, cymbal, other };
        Sound soundOf (int note)
        {
            if (note == 35 || note == 36)
                return Sound::kick;
            if (note >= 37 && note <= 40)
                return Sound::snare;
            if (note == 42 || note == 44 || note == 46 || note == 51 || note == 53 || note == 59)
                return Sound::cymbal;
            return Sound::other;
        }

        Preset preset (std::string name, std::string about, int pulses, int steps, int stepsPerBeat, int rotation,
                       int beatsPerBar, const char* timeline = "bell", int note = 56)
        {
            Preset p;
            p.name = std::move (name);
            p.about = std::move (about);
            p.settings.beatsPerBar = beatsPerBar;
            Lane bell;
            bell.name = timeline;
            bell.note = note;
            bell.steps = steps;
            bell.stepsPerBeat = stepsPerBeat;
            bell.pulses = pulses;
            bell.rotation = rotation;
            Lane low;
            low.name = "low drum";
            low.note = 64;   // low conga
            low.steps = steps;
            low.stepsPerBeat = stepsPerBeat;
            low.pulses = 1;
            p.settings.lanes = { bell, low };
            return p;
        }
    }

    std::vector<bool> euclid (int pulses, int steps, int rotation)
    {
        steps = std::max (0, steps);
        pulses = std::clamp (pulses, 0, steps);
        std::vector<bool> out;
        if (steps == 0)
            return out;
        // Bjorklund: pair the groups of hits and rests, the remainder each time, until one
        // group (or none) is left over.
        std::vector<std::vector<bool>> a ((size_t) pulses, std::vector<bool> { true });
        std::vector<std::vector<bool>> b ((size_t) (steps - pulses), std::vector<bool> { false });
        while (b.size() > 1 && ! a.empty())
        {
            const auto m = std::min (a.size(), b.size());
            std::vector<std::vector<bool>> joined;
            for (size_t i = 0; i < m; ++i)
            {
                auto g = a[i];
                g.insert (g.end(), b[i].begin(), b[i].end());
                joined.push_back (std::move (g));
            }
            std::vector<std::vector<bool>> rest;
            if (a.size() > m)
                rest.assign (a.begin() + (std::ptrdiff_t) m, a.end());
            else
                rest.assign (b.begin() + (std::ptrdiff_t) m, b.end());
            a = std::move (joined);
            b = std::move (rest);
        }
        for (const auto& g : a)
            out.insert (out.end(), g.begin(), g.end());
        for (const auto& g : b)
            out.insert (out.end(), g.begin(), g.end());
        const auto r = ((rotation % steps) + steps) % steps;
        std::rotate (out.begin(), out.begin() + r, out.end());
        return out;
    }

    std::string cycle (const Lane& lane)
    {
        if (! lane.pattern.empty())
            return compact (lane.pattern);
        const auto e = euclid (lane.pulses, lane.steps, lane.rotation);
        std::string s;
        bool first = true;
        for (const auto on : e)
        {
            s += on ? (first && lane.accentFirst ? 'X' : 'x') : '.';
            first = first && ! on;
        }
        return s;
    }

    int lengthBars (const Settings& s)
    {
        if (s.bars > 0)
            return std::min (s.bars, 256);
        const auto barTicks = (int64_t) std::max (1, s.beatsPerBar) * tpq;
        int64_t together = barTicks;
        for (const auto& l : s.lanes)
            if (! l.muted)
            {
                const auto n = (int64_t) cycle (l).size();
                if (n > 0)
                    together = std::lcm (together, n * stepTicks (l));
                if (together / barTicks > 64)
                    return 64;
            }
        return (int) std::clamp<int64_t> (together / barTicks, 1, 64);
    }

    midifile::SongPtr generate (const Settings& s, double bpm, std::string& error)
    {
        auto song = std::make_shared<midifile::Song>();
        song->format = 0;
        song->ticksPerQuarter = tpq;
        song->tempos.push_back ({ 0, 60.0e6 / std::max (1.0, bpm) });
        std::vector<Placed> hits;
        if (! place (s, bpm, true, hits, error))
            return song;
        struct Timed { int64_t tick; int order; midifile::Event e; };
        std::vector<Timed> events;
        for (const auto& h : hits)
        {
            const auto note = (uint8_t) std::clamp (s.lanes[(size_t) h.lane].note, 0, 127);
            const auto length = (int64_t) std::min (stepTicks (s.lanes[(size_t) h.lane]), tpq / 4);
            events.push_back ({ h.tick, 1, { h.tick, 0x99, note, (uint8_t) h.velocity } });
            events.push_back ({ h.tick + length, 0, { h.tick + length, 0x89, note, 0 } });
        }
        std::stable_sort (events.begin(), events.end(), [] (const Timed& a, const Timed& b)
        {
            return a.tick != b.tick ? a.tick < b.tick : a.order < b.order;
        });
        for (const auto& t : events)
            song->events.push_back (t.e);
        if (! hits.empty())
            song->channelsUsed = 1u << 9;
        song->lastTick = (int64_t) lengthBars (s) * s.beatsPerBar * tpq;
        return song;
    }

    bool toRhythm (const Settings& s, const std::string& name, rhythm::Rhythm& r, std::string& error)
    {
        std::vector<Placed> hits;
        if (! place (s, 120.0, false, hits, error))
            return false;
        const auto bars = lengthBars (s);
        r = {};
        r.name = name;
        r.plain = name;
        r.group = "rhythm generator";
        r.beats = bars * s.beatsPerBar;
        r.sections.assign ((size_t) bars, s.beatsPerBar);
        r.bpm = 120.0;
        for (const auto& l : s.lanes)
        {
            const auto perBeat = std::clamp (l.stepsPerBeat, 1, 48);
            r.lanes.push_back ({ l.name, l.note, perBeat, std::string ((size_t) (r.beats * perBeat), '.'), 9 });
        }
        for (const auto& h : hits)
        {
            auto& lane = r.lanes[(size_t) h.lane].hits;
            if (h.step < (int64_t) lane.size())
                lane[(size_t) h.step] = h.velocity >= 110 ? 'X' : h.velocity >= 70 ? 'x' : 'o';
        }
        r.lanes.erase (std::remove_if (r.lanes.begin(), r.lanes.end(),
                                       [] (const rhythm::Lane& l) { return l.hits.find_first_not_of ('.') == std::string::npos; }),
                       r.lanes.end());
        if (r.lanes.empty())
        {
            error = "nothing plays";
            return false;
        }
        return true;
    }

    Settings fromRhythm (const rhythm::Rhythm& r)
    {
        Settings s;
        s.beatsPerBar = r.beats <= 32 ? std::max (1, r.beats) : 4;
        for (const auto& l : r.lanes)
        {
            if (l.channel != 9)
                continue;   // the generator plays drums
            Lane lane;
            lane.name = l.name;
            lane.note = l.note;
            lane.stepsPerBeat = l.perBeat;
            lane.pattern = compact (l.hits);
            lane.steps = (int) lane.pattern.size();
            lane.pulses = (int) std::count_if (lane.pattern.begin(), lane.pattern.end(), [] (char c) { return c != '.'; });
            s.lanes.push_back (std::move (lane));
        }
        return s;
    }

    const std::vector<Feel>& feels()
    {
        static const std::vector<Feel> all = [] {
            std::vector<Feel> f;
            auto add = [&f] (std::string name, std::string about, double percent, int perBeat, double kick, double snare,
                             double cymbal, double ms, double velocity) {
                f.push_back ({ std::move (name), std::move (about), percent, perBeat, kick, snare, cymbal, ms, velocity });
            };
            add ("straight", "On the grid.", 50, 4, 0, 0, 0, 0, 0);
            add ("16th swing 54%", "A touch of swing on the 16ths, as on an MPC at 54%.", 54, 4, 0, 0, 0, 0, 0);
            add ("16th swing 58%", "Swung 16ths, a little looser.", 58, 4, 0, 0, 0, 0, 0);
            add ("16th swing 62%", "Swung 16ths, between straight and the triplet.", 62, 4, 0, 0, 0, 0, 0);
            add ("triplet shuffle", "8ths at 66%: the shuffle of blues, jazz and the swung 12/8 feel.", 66.7, 2, 0, 0, 0, 0, 0);
            add ("laid back", "The snare behind the beat, the rest on it: the backbeat of soul, funk and hip-hop.", 50, 4, 0, 18, 0, 3, 4);
            add ("pushing", "The cymbals ahead of the beat: urgent.", 50, 4, 0, 0, -10, 3, 4);
            add ("loose (dilla-style)", "Swung 16ths, the kick a little early, the snare late, everything humanized: the unquantized, "
                "drunk feel J Dilla's beats are known for, approximated.", 57, 4, -8, 16, 0, 7, 8);
            add ("second line", "New Orleans: 16ths swung about halfway to the triplet, the snare a little behind.", 60, 4, 0, 8, 0, 4, 6);
            add ("human", "No swing, only the timing and loudness of hands.", 50, 4, 0, 0, 0, 6, 8);
            return f;
        }();
        return all;
    }

    void applyFeel (Settings& s, const Feel& f)
    {
        s.swing = std::clamp (f.swingPercent / 50.0 - 1.0, 0.0, 0.5);
        s.swingPerBeat = f.swingPerBeat;
        s.humanizeMs = f.humanizeMs;
        s.humanizeVelocity = f.humanizeVelocity;
        for (auto& l : s.lanes)
        {
            const auto sound = soundOf (l.note);
            l.pushMs = sound == Sound::kick ? f.kickMs : sound == Sound::snare ? f.snareMs : sound == Sound::cymbal ? f.cymbalMs : 0.0;
        }
    }

    const std::vector<Preset>& presets()
    {
        // Toussaint, "The Euclidean algorithm generates traditional musical rhythms" (2005):
        // each a rotation of E(pulses, steps), turned to start as the tradition counts it.
        static const std::vector<Preset> all {
            preset ("tresillo: e(3,8)", "Cuba and across the Americas: 3+3+2, twice to the bar.", 3, 8, 4, 0, 4),
            preset ("cinquillo: e(5,8)", "Cuba: the danzón's five strokes, twice to the bar.", 5, 8, 4, 0, 4),
            preset ("bembé bell: e(7,12)", "West Africa and Cuba: the standard 12/8 bell, x.x.xx.x.x.x.", 7, 12, 3, 3, 4),
            preset ("bossa nova: e(5,16)", "Brazil: x..x..x...x..x.., a turn of e(5,16).", 5, 16, 4, 6, 4, "rim", 37),
            preset ("samba: e(7,16)", "Brazil: x..x.x.x..x.x.x.", 7, 16, 4, 0, 4, "tamborim", 76),
            preset ("central african: e(9,16)", "Central Africa: x.xx.x.x.xx.x.x.", 9, 16, 4, 0, 4),
            preset ("aka: e(13,24)", "The Aka of the Central African Republic: thirteen strokes in twenty-four.", 13, 24, 6, 0, 4),
            preset ("khafif-e-ramal: e(2,5)", "Persia, 13th century: x.x.., in eighths.", 2, 5, 1, 0, 5, "frame drum", 64),
            preset ("ruchenitza: e(3,7)", "Bulgaria: 7/8 as 2+2+3, x.x.x.., in eighths.", 3, 7, 1, 0, 7, "tapan", 64),
            preset ("aksak: e(4,9)", "Turkey: 9/8 as 2+2+2+3, x.x.x.x.., in eighths.", 4, 9, 1, 0, 9, "darbuka", 63),
        };
        return all;
    }
}

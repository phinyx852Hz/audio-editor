// SPDX-License-Identifier: AGPL-3.0-only
#include "MixAssist.h"

#include "../core/Dsp.h"

#include <algorithm>
#include <cctype>
#include <cmath>
#include <numeric>

namespace mixassist
{
    namespace
    {
        constexpr double lowestDb = -40.0, highestDb = 6.0;   // the faders the assistant may set
        constexpr double pi = 3.14159265358979323846;

        double toNormal (double db) { return (std::clamp (db, lowestDb, highestDb) - lowestDb) / (highestDb - lowestDb); }
        double toDb (double normal) { return lowestDb + normal * (highestDb - lowestDb); }
        double gainOf (double db) { return std::pow (10.0, db / 20.0); }
        double powerDb (double p) { return p > 1.0e-20 ? 10.0 * std::log10 (p) : -200.0; }

        std::vector<std::string> words (const std::string& name)
        {
            std::vector<std::string> out;
            std::string w;
            for (const auto c : name)
            {
                if (std::isalnum ((unsigned char) c))
                    w += (char) std::tolower ((unsigned char) c);
                else if (! w.empty())
                {
                    out.push_back (w);
                    w.clear();
                }
            }
            if (! w.empty())
                out.push_back (w);
            return out;
        }

        bool has (const std::vector<std::string>& ws, std::initializer_list<const char*> exact, std::initializer_list<const char*> starts = {})
        {
            for (const auto& w : ws)
            {
                for (const auto* e : exact)
                    if (w == e)
                        return true;
                for (const auto* s : starts)
                    if (w.rfind (s, 0) == 0)
                        return true;
            }
            return false;
        }

        // Energy of each side of a stem, for how a pan changes its loudness.
        void sideEnergies (const AudioData& stem, double& left, double& right)
        {
            left = right = 0.0;
            const auto channels = stem.numChannels();
            constexpr int chunk = 1 << 15;
            std::vector<std::vector<float>> block ((size_t) channels, std::vector<float> (chunk));
            std::vector<float*> ptrs;
            for (auto& c : block)
                ptrs.push_back (c.data());
            for (Frame at = 0; at < stem.length(); at += chunk)
            {
                const auto n = (int) std::min<Frame> (chunk, stem.length() - at);
                stem.read (at, n, ptrs.data());
                for (int i = 0; i < n; ++i)
                {
                    left += (double) block[0][(size_t) i] * block[0][(size_t) i];
                    const auto r = block[channels > 1 ? 1 : 0][(size_t) i];
                    right += (double) r * r;
                }
            }
        }
    }

    const char* roleName (Role r)
    {
        switch (r)
        {
            case Role::leadVocal:      return "Lead vocal";
            case Role::vocal:          return "Backing vocal";
            case Role::kick:           return "Kick";
            case Role::snare:          return "Snare";
            case Role::drums:          return "Drums";
            case Role::bass:           return "Bass";
            case Role::leadInstrument: return "Lead instrument";
            case Role::guitar:         return "Guitar";
            case Role::keys:           return "Keys";
            case Role::pad:            return "Pad";
            case Role::fx:             return "Effects";
            case Role::other:          break;
        }
        return "Other";
    }

    Role guessRole (const std::string& name)
    {
        const auto ws = words (name);
        const bool vocal = has (ws, { "vox", "voc", "vocal", "vocals", "voice", "vo", "lv", "sing", "singer" }, { "vocal" });
        if (vocal && has (ws, { "lead", "main", "lv" }))
            return Role::leadVocal;
        if (has (ws, { "bv", "bvs", "bgv", "backing", "backings", "harmony", "harmonies", "choir", "adlib", "adlibs", "dbl", "double" }, { "harm", "backing" }))
            return Role::vocal;
        if (vocal)
            return Role::leadVocal;
        if (has (ws, { "kick", "kik", "bd", "kd" }, { "kick" }))
            return Role::kick;
        if (has (ws, { "snare", "snr", "sd", "sn", "clap", "rim" }, { "snare" }))
            return Role::snare;
        if (has (ws, { "bass", "ba", "bs", "sub", "808" }, { "bass" }))
            return Role::bass;
        if (has (ws, { "drum", "drums", "dr", "hat", "hats", "hh", "hihat", "perc", "percussion", "tom", "toms", "oh", "overhead",
                       "overheads", "room", "cymbal", "cymbals", "ride", "crash", "shaker", "tamb", "beat", "loop", "kit" },
                 { "drum", "perc", "hat", "overhead", "cymbal" }))
            return Role::drums;
        if (has (ws, { "lead", "ld", "solo", "melody", "mel", "hook" }))
            return Role::leadInstrument;
        if (has (ws, { "gtr", "gt", "guitar", "guitars", "egtr", "agtr", "acoustic" }, { "guitar" }))
            return Role::guitar;
        if (has (ws, { "keys", "key", "kb", "piano", "pno", "organ", "rhodes", "ep", "wurli", "synth", "syn", "pluck", "arp", "chords" },
                 { "piano", "synth", "organ" }))
            return Role::keys;
        if (has (ws, { "pad", "pads", "strings", "str", "ambient", "amb", "atmos", "drone", "choirpad" }, { "string", "atmo" }))
            return Role::pad;
        if (has (ws, { "fx", "sfx", "riser", "risers", "noise", "sweep", "impact", "verb", "reverb", "delay" }, { "riser", "sweep" }))
            return Role::fx;
        return Role::other;
    }

    double roleOffsetDb (Role r)
    {
        // Rules of thumb, as engineers put a pop or rock mix together; the starting
        // point of the cross-adaptive mixers is all at equal loudness (offset 0).
        switch (r)
        {
            case Role::leadVocal:      return 3.0;
            case Role::leadInstrument: return 2.0;
            case Role::kick:           return 1.0;
            case Role::bass:           return 0.5;
            case Role::snare:          return 0.0;
            case Role::vocal:          return -3.0;
            case Role::drums:          return -2.0;
            case Role::guitar:         return -2.0;
            case Role::keys:           return -2.5;
            case Role::pad:            return -5.0;
            case Role::fx:             return -8.0;
            case Role::other:          break;
        }
        return -2.0;
    }

    bool staysCentred (Role r)
    {
        return r == Role::leadVocal || r == Role::kick || r == Role::snare || r == Role::bass;
    }

    int rolePriority (Role r)
    {
        switch (r)
        {
            case Role::leadVocal:      return 10;
            case Role::leadInstrument: return 9;
            case Role::kick:           return 8;
            case Role::bass:           return 7;
            case Role::snare:          return 6;
            case Role::vocal:          return 5;
            case Role::guitar:         return 4;
            case Role::keys:           return 4;
            case Role::drums:          return 3;
            case Role::other:          return 3;
            case Role::pad:            return 2;
            case Role::fx:             return 1;
        }
        return 3;
    }

    void panGains (const Track& t, double pan, double& left, double& right)
    {
        pan = std::clamp (pan, -1.0, 1.0);
        if (t.monoAudio)
        {
            const auto angle = (pan + 1.0) * 0.25 * pi;
            left = std::cos (angle);
            right = std::sin (angle);
        }
        else
        {
            left = std::min (1.0, 1.0 - pan);
            right = std::min (1.0, 1.0 + pan);
        }
    }

    AudioDataPtr mix (const std::vector<Track>& tracks, const std::vector<double>& faderDb, const std::vector<double>& pan)
    {
        Frame length = 0;
        double rate = 48000.0;
        for (const auto& t : tracks)
            if (t.stem != nullptr)
            {
                length = std::max (length, t.stem->length());
                rate = t.stem->sampleRate();
            }

        std::vector<double> gl (tracks.size()), gr (tracks.size());
        for (size_t i = 0; i < tracks.size(); ++i)
        {
            panGains (tracks[i], pan[i], gl[i], gr[i]);
            const auto g = gainOf (faderDb[i]);
            gl[i] *= g;
            gr[i] *= g;
        }

        AudioData::Builder out (2, rate);
        constexpr int chunk = 1 << 15;
        std::vector<float> l (chunk), r (chunk), a (chunk), b (chunk);
        for (Frame at = 0; at < length; at += chunk)
        {
            const auto n = (int) std::min<Frame> (chunk, length - at);
            std::fill (l.begin(), l.end(), 0.0f);
            std::fill (r.begin(), r.end(), 0.0f);
            for (size_t i = 0; i < tracks.size(); ++i)
            {
                const auto& stem = tracks[i].stem;
                if (stem == nullptr || at >= stem->length())
                    continue;
                float* ptrs[] { a.data(), b.data() };
                if (stem->numChannels() >= 2)
                    stem->read (at, n, ptrs);
                else
                {
                    stem->read (at, n, ptrs);
                    std::copy (a.begin(), a.begin() + n, b.begin());
                }
                // Mono audio is one signal (the bounce has it on both sides), panned with equal power.
                const auto& right = tracks[i].monoAudio ? a : b;
                for (int k = 0; k < n; ++k)
                {
                    l[(size_t) k] += (float) gl[i] * a[(size_t) k];
                    r[(size_t) k] += (float) gr[i] * right[(size_t) k];
                }
            }
            const float* ptrs[] { l.data(), r.data() };
            out.append (ptrs, n);
        }
        return out.finish();
    }

    std::vector<double> rulePans (const std::vector<Track>& tracks, const std::vector<assist::Profile>& stems, bool keepPans)
    {
        std::vector<double> pans (tracks.size());
        std::vector<std::pair<double, size_t>> spread;   // brightness, track
        for (size_t i = 0; i < tracks.size(); ++i)
        {
            const auto& t = tracks[i];
            pans[i] = t.pan;
            if (! t.movePan || (keepPans && std::abs (t.pan) > 0.01) || ! stems[i].valid)
                continue;
            // The low end and the lead stay in the middle; so does a stem already wide in itself.
            if (staysCentred (t.role) || (! t.monoAudio && stems[i].sideToMidDb > -12.0))
            {
                pans[i] = 0.0;
                continue;
            }
            // Brightness: the level-weighted mean octave.
            double sum = 0.0, weight = 0.0;
            for (int k = 0; k < assist::Profile::numBands; ++k)
            {
                const auto w = std::pow (10.0, stems[i].bandsDb[(size_t) k] / 10.0);
                sum += w * k;
                weight += w;
            }
            spread.push_back ({ weight > 0.0 ? sum / weight : 0.0, i });
        }

        // The brightest go widest, sides taking turns, so the two halves stay balanced.
        std::sort (spread.begin(), spread.end(), [] (auto& x, auto& y) { return x.first > y.first; });
        for (size_t k = 0; k < spread.size(); ++k)
        {
            const auto rank = (double) (k / 2) / std::max<double> (1.0, (double) ((spread.size() + 1) / 2));
            const auto amount = 0.7 - 0.45 * rank;
            pans[spread[k].second] = (k % 2 == 0 ? 1.0 : -1.0) * amount;
        }
        // An odd one out goes nearer the middle, so one side does not carry more.
        if (spread.size() % 2 == 1)
            pans[spread.back().second] *= 0.5;
        return pans;
    }

    Suggestion suggest (const std::vector<Track>& tracks, const Settings& settings, const std::atomic<bool>* cancel,
                        const std::function<void (double)>& progress)
    {
        Suggestion out;
        const auto n = tracks.size();
        out.faderDb.resize (n);
        out.pan.resize (n);
        for (size_t i = 0; i < n; ++i)
        {
            out.faderDb[i] = tracks[i].faderDb;
            out.pan[i] = tracks[i].pan;
        }
        if (n == 0)
            return out;

        // The same stretches of every track: the loudest of the mix as it is, and a typical one.
        std::vector<double> nowDb (n), nowPan (n);
        for (size_t i = 0; i < n; ++i)
        {
            nowDb[i] = tracks[i].faderDb;
            nowPan[i] = tracks[i].pan;
        }
        const auto whole = mix (tracks, nowDb, nowPan);
        const auto windows = assist::excerptWindows (*whole, 0, whole->length());
        auto clips = tracks;
        std::vector<assist::Profile> stems (n);
        std::vector<double> stemLufs (n, -200.0), energyL (n, 0.0), energyR (n, 0.0);
        for (size_t i = 0; i < n; ++i)
        {
            if (tracks[i].stem == nullptr)
                continue;
            clips[i].stem = assist::cut (*tracks[i].stem, windows);
            stems[i] = assist::measure (*clips[i].stem, 0, clips[i].stem->length());
            stemLufs[i] = stems[i].lufs;
            sideEnergies (*clips[i].stem, energyL[i], energyR[i]);
        }

        // Each track's loudness in the mix: its own, moved by its fader and pan.
        auto levelOf = [&] (size_t i, double db, double pan)
        {
            double l = 0.0, r = 0.0;
            panGains (tracks[i], pan, l, r);
            // A bounce has mono audio on both sides at full level; heard, it is the one signal panned.
            const auto el = energyL[i], er = tracks[i].monoAudio ? energyL[i] : energyR[i];
            const auto total = energyL[i] + energyR[i];
            const auto panDb = total > 0.0 ? powerDb ((l * l * el + r * r * er) / total) : 0.0;
            return stemLufs[i] + db + panDb;
        };
        std::vector<size_t> heard;
        for (size_t i = 0; i < n; ++i)
            if (stems[i].valid && stemLufs[i] > -70.0)
                heard.push_back (i);
        double offsetMean = 0.0;
        for (const auto i : heard)
            offsetMean += roleOffsetDb (tracks[i].role);
        offsetMean /= std::max<size_t> (1, heard.size());
        auto relativeLevels = [&] (const std::vector<double>& db, const std::vector<double>& pan)
        {
            std::vector<double> rel (n, 0.0);
            double mean = 0.0;
            for (const auto i : heard)
                mean += levelOf (i, db[i], pan[i]);
            mean /= std::max<size_t> (1, heard.size());
            for (const auto i : heard)
                rel[i] = levelOf (i, db[i], pan[i]) - mean;
            return rel;
        };

        // Pans first, by rule; the faders are then fitted with them in place.
        out.pan = settings.panning ? rulePans (tracks, stems, settings.keepPans) : nowPan;

        const auto now = mix (clips, nowDb, nowPan);
        out.before = assist::measure (*now, 0, now->length());
        out.levelBefore = relativeLevels (nowDb, nowPan);
        out.levelAim.assign (n, 0.0);
        for (const auto i : heard)
            out.levelAim[i] = roleOffsetDb (tracks[i].role) - offsetMean;

        std::vector<size_t> moving;
        std::vector<double> start;
        for (size_t i = 0; i < n; ++i)
            if (tracks[i].moveFader && tracks[i].stem != nullptr)
            {
                moving.push_back (i);
                start.push_back (toNormal (tracks[i].faderDb));
            }

        auto fadersFor = [&] (const std::vector<double>& x)
        {
            auto db = nowDb;
            for (size_t k = 0; k < moving.size(); ++k)
                db[moving[k]] = toDb (x[k]);
            return db;
        };
        const assist::Evaluate evaluate = [&] (const std::vector<double>& x, std::vector<double>& r)
        {
            if (cancel != nullptr && cancel->load())
                return false;
            const auto db = fadersFor (x);
            const auto m = mix (clips, db, out.pan);
            const auto p = assist::measure (*m, 0, m->length());
            r.clear();
            if (settings.balance)
            {
                const auto rel = relativeLevels (db, out.pan);
                for (const auto i : heard)
                    r.push_back (rel[i] - out.levelAim[i]);
            }
            r.push_back (p.valid ? 0.3 * (p.lufs - settings.mixLufs) : 50.0);   // softer than the balance: a mix may sit a little under
            r.push_back (p.valid ? 2.0 * std::max (0.0, p.truePeakDb - settings.ceilingDb) : 0.0);
            if (settings.reference != nullptr && settings.reference->valid && p.valid)
                for (int k = 0; k < assist::Profile::numBands; ++k)
                    r.push_back (0.3 * (p.bandsDb[(size_t) k] - settings.reference->bandsDb[(size_t) k]));
            return true;
        };

        assist::Settings search;
        search.maxEvaluations = settings.maxEvaluations;
        search.freedom = 1.0;      // faders are level knobs: as far as they need
        search.stay = 0.02;
        search.step = 0.01;        // about half a dB
        const auto result = assist::optimise (start, evaluate, search, progress);
        out.evaluations = result.evaluations;
        out.stopped = result.stopped;
        out.faderDb = fadersFor (result.values);
        for (auto& db : out.faderDb)
            db = std::round (db * 10.0) / 10.0;

        const auto after = mix (clips, out.faderDb, out.pan);
        out.after = assist::measure (*after, 0, after->length());
        out.levelAfter = relativeLevels (out.faderDb, out.pan);

        // Overlaps: in each octave from 63 Hz to 8 kHz, a less important track as loud as
        // a more important one, where that one has its strongest octaves, hides it.
        const auto numBands = assist::Profile::numBands;
        std::vector<std::array<double, assist::Profile::numBands>> bandLevel (n);
        for (const auto i : heard)
        {
            double l = 0.0, r = 0.0;
            panGains (tracks[i], out.pan[i], l, r);
            const auto g = 20.0 * std::log10 (std::max (1.0e-6, gainOf (out.faderDb[i]) * std::sqrt (0.5 * (l * l + r * r))));
            for (int k = 0; k < numBands; ++k)
                bandLevel[i][(size_t) k] = stems[i].bandsDb[(size_t) k] + stems[i].bandsMeanDb + g;
        }
        std::vector<std::pair<double, Overlap>> found;
        for (const auto a : heard)
        {
            std::vector<int> order (numBands);
            std::iota (order.begin(), order.end(), 0);
            std::sort (order.begin(), order.end(), [&] (int x, int y) { return stems[a].bandsDb[(size_t) x] > stems[a].bandsDb[(size_t) y]; });
            for (const auto b : heard)
            {
                if (a == b || rolePriority (tracks[b].role) >= rolePriority (tracks[a].role))
                    continue;
                double best = -1.0e9;
                Overlap o;
                for (int top = 0; top < 3; ++top)
                {
                    const auto k = order[(size_t) top];
                    // Only where `a` really lives: within 10 dB of its strongest octave.
                    if (k < 1 || k > 8 || stems[a].bandsDb[(size_t) k] < stems[a].bandsDb[(size_t) order[0]] - 10.0)
                        continue;
                    const auto la = bandLevel[a][(size_t) k], lb = bandLevel[b][(size_t) k];
                    const auto closeness = lb - la;   // above -6 dB: b is near or over a there
                    if (closeness > -6.0 && closeness > best)
                    {
                        best = closeness;
                        o = { (int) a, (int) b, k, std::clamp (3.0 + closeness, 2.0, 6.0) };
                    }
                }
                if (best > -1.0e8)
                    found.push_back ({ best + 0.1 * rolePriority (tracks[a].role), o });
            }
        }
        std::sort (found.begin(), found.end(), [] (auto& x, auto& y) { return x.first > y.first; });
        for (size_t k = 0; k < found.size() && k < 6; ++k)
            out.overlaps.push_back (found[k].second);
        return out;
    }
}

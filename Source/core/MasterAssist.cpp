// SPDX-License-Identifier: AGPL-3.0-only
#include "MasterAssist.h"

#include "Dsp.h"
#include "Fft.h"

#include <algorithm>
#include <cctype>
#include <cmath>
#include <numeric>

namespace assist
{
    namespace
    {
        constexpr double silentDb = -200.0;

        double toDb (double power) { return power > 1.0e-20 ? 10.0 * std::log10 (power) : silentDb; }

        bool contains (const std::string& text, const char* word) { return text.find (word) != std::string::npos; }

        // Solves a x = b in place (a is n by n, row major) with partial pivoting; false when singular.
        bool solve (std::vector<double> a, std::vector<double>& b)
        {
            const auto n = b.size();
            for (size_t col = 0; col < n; ++col)
            {
                size_t pivot = col;
                for (size_t r = col + 1; r < n; ++r)
                    if (std::abs (a[r * n + col]) > std::abs (a[pivot * n + col]))
                        pivot = r;
                if (std::abs (a[pivot * n + col]) < 1.0e-12)
                    return false;
                if (pivot != col)
                {
                    for (size_t c = 0; c < n; ++c)
                        std::swap (a[col * n + c], a[pivot * n + c]);
                    std::swap (b[col], b[pivot]);
                }
                for (size_t r = col + 1; r < n; ++r)
                {
                    const auto f = a[r * n + col] / a[col * n + col];
                    for (size_t c = col; c < n; ++c)
                        a[r * n + c] -= f * a[col * n + c];
                    b[r] -= f * b[col];
                }
            }
            for (size_t i = n; i-- > 0;)
            {
                double sum = b[i];
                for (size_t c = i + 1; c < n; ++c)
                    sum -= a[i * n + c] * b[c];
                b[i] = sum / a[i * n + i];
            }
            return true;
        }

        double sumOfSquares (const std::vector<double>& r)
        {
            return std::inner_product (r.begin(), r.end(), r.begin(), 0.0);
        }
    }

    const std::array<double, Profile::numBands>& Profile::bandCentres()
    {
        static const std::array<double, numBands> centres { 31.5, 63.0, 125.0, 250.0, 500.0, 1000.0, 2000.0, 4000.0, 8000.0, 16000.0 };
        return centres;
    }

    Profile measure (const AudioData& data, Frame start, Frame end)
    {
        Profile p;
        start = std::clamp<Frame> (start, 0, data.length());
        end = std::clamp<Frame> (end, start, data.length());
        const auto channels = data.numChannels();
        if (end - start < 1 || channels < 1)
            return p;

        const auto loudness = dsp::measureLoudness (data, start, end);
        p.lufs = loudness.integratedLufs;
        p.truePeakDb = loudness.truePeakDb;
        p.lra = loudness.loudnessRange;
        if (p.lufs < -100.0)
            return p;
        p.plr = p.truePeakDb - p.lufs;

        // The average spectrum of the mid, and the side and mid energies, in 8192-sample Hann frames half overlapped.
        constexpr int order = 13, size = 1 << order, hop = size / 2;
        SpectrumAnalyzer analyzer (order);
        std::vector<double> power ((size_t) analyzer.numBins(), 0.0);
        std::vector<std::vector<float>> block ((size_t) channels, std::vector<float> (size));
        std::vector<float*> ptrs;
        for (auto& c : block)
            ptrs.push_back (c.data());
        std::vector<float> mid (size), db ((size_t) analyzer.numBins());
        double midEnergy = 0.0, sideEnergy = 0.0;
        int frames = 0;
        for (Frame at = start; at < end; at += hop)
        {
            data.read (at, size, ptrs.data());
            const auto valid = (int) std::min<Frame> (size, end - at);
            for (int i = 0; i < size; ++i)
            {
                const auto l = i < valid ? block[0][(size_t) i] : 0.0f;
                const auto r = i < valid ? block[channels > 1 ? 1 : 0][(size_t) i] : 0.0f;
                mid[(size_t) i] = 0.5f * (l + r);
                midEnergy += 0.25 * (double) (l + r) * (l + r);
                sideEnergy += 0.25 * (double) (l - r) * (l - r);
            }
            analyzer.magnitudesDb (mid.data(), db.data());
            for (size_t b = 0; b < power.size(); ++b)
                power[b] += std::pow (10.0, db[b] / 10.0);
            ++frames;
            if (valid < size)
                break;
        }

        const auto binHz = data.sampleRate() / size;
        std::array<double, Profile::numBands> bandPower {};
        for (size_t b = 1; b < power.size(); ++b)
        {
            const auto hz = (double) b * binHz;
            for (int k = 0; k < Profile::numBands; ++k)
            {
                const auto centre = Profile::bandCentres()[(size_t) k];
                if (hz >= centre / std::sqrt (2.0) && hz < centre * std::sqrt (2.0))
                    bandPower[(size_t) k] += power[b];
            }
        }
        double mean = 0.0;
        for (int k = 0; k < Profile::numBands; ++k)
        {
            // An octave above Nyquist has nothing in it; give it a floor so it does not dominate.
            p.bandsDb[(size_t) k] = std::max (toDb (bandPower[(size_t) k] / std::max (frames, 1)), -150.0);
            mean += p.bandsDb[(size_t) k];
        }
        mean /= Profile::numBands;
        for (auto& b : p.bandsDb)
            b -= mean;

        // Hop-overlapped sums count every sample about twice in both, so the ratio is right.
        if (channels > 1 && midEnergy > 0.0)
            p.sideToMidDb = std::max (toDb (sideEnergy / midEnergy), silentDb);
        p.valid = true;
        return p;
    }

    std::array<double, Profile::numBands> toneCurve (Tone tone)
    {
        // A gentle curve near the long-term average of commercial pop and rock masters: full
        // lows, a steady fall above 1 kHz. Approximate; a reference track says more.
        std::array<double, Profile::numBands> curve { -1.0, 3.0, 3.5, 2.0, 0.0, -1.5, -3.0, -4.5, -7.0, -12.0 };
        const auto tilt = tone == Tone::warm ? -0.75 : tone == Tone::bright ? 0.75 : 0.0;   // dB per octave about 1 kHz
        double mean = 0.0;
        for (int k = 0; k < Profile::numBands; ++k)
        {
            curve[(size_t) k] += tilt * (k - 5);
            mean += curve[(size_t) k];
        }
        mean /= Profile::numBands;
        for (auto& c : curve)
            c -= mean;
        return curve;
    }

    Target Target::forExcerpt (const Profile& whole, const Profile& part) const
    {
        auto t = *this;
        if (! whole.valid || ! part.valid)
            return t;
        t.lufs -= whole.lufs - part.lufs;
        t.plr -= whole.plr - part.plr;
        for (int k = 0; k < Profile::numBands; ++k)
            t.bandsDb[(size_t) k] -= whole.bandsDb[(size_t) k] - part.bandsDb[(size_t) k];
        if (whole.sideToMidDb > -100.0 && part.sideToMidDb > -100.0 && t.sideToMidDb > -100.0)
            t.sideToMidDb -= whole.sideToMidDb - part.sideToMidDb;
        return t;
    }

    Target fromReference (const Profile& reference, double ceilingDb)
    {
        Target t;
        t.lufs = reference.lufs;
        t.truePeakDb = ceilingDb;
        t.plr = reference.plr;
        t.dynamicsWeight = 0.5;
        t.bandsDb = reference.bandsDb;
        t.toneWeight = 0.5;
        t.sideToMidDb = reference.sideToMidDb;
        t.widthWeight = reference.sideToMidDb > -100.0 ? 0.3 : 0.0;
        return t;
    }

    std::vector<double> residuals (const Profile& got, const Target& t)
    {
        std::vector<double> r (4 + Profile::numBands, 0.0);
        if (! got.valid)
        {
            // Silence: as far from any target as can be, so the search turns back.
            std::fill (r.begin(), r.end(), 0.0);
            r[0] = 100.0 * t.loudnessWeight;
            return r;
        }
        r[0] = t.loudnessWeight * (got.lufs - t.lufs);
        r[1] = t.peakWeight * std::max (0.0, got.truePeakDb - t.truePeakDb);
        r[2] = t.dynamicsWeight * (got.plr - t.plr);
        if (got.sideToMidDb > -100.0 && t.sideToMidDb > -100.0)
            r[3] = t.widthWeight * (got.sideToMidDb - t.sideToMidDb);
        for (int k = 0; k < Profile::numBands; ++k)
            r[(size_t) (4 + k)] = t.toneWeight * (got.bandsDb[(size_t) k] - t.bandsDb[(size_t) k]);
        return r;
    }

    double distance (const Profile& got, const Target& t) { return sumOfSquares (residuals (got, t)); }

    AudioDataPtr excerpt (const AudioData& data, Frame start, Frame end, double seconds)
    {
        start = std::clamp<Frame> (start, 0, data.length());
        end = std::clamp<Frame> (end, start, data.length());
        const auto rate = data.sampleRate();
        const auto channels = data.numChannels();
        constexpr int windowSeconds = 6;
        const auto blockLength = (Frame) std::llround (rate);
        if ((double) (end - start) <= seconds * rate || blockLength < 1)
            return data.slice (start, end);

        // The energy of every second, then of every six seconds in a row.
        const auto numBlocks = (size_t) ((end - start) / blockLength);
        std::vector<double> energy (numBlocks, 0.0);
        std::vector<std::vector<float>> block ((size_t) channels, std::vector<float> ((size_t) blockLength));
        std::vector<float*> ptrs;
        for (auto& c : block)
            ptrs.push_back (c.data());
        for (size_t i = 0; i < numBlocks; ++i)
        {
            data.read (start + (Frame) i * blockLength, (int) blockLength, ptrs.data());
            for (const auto& c : block)
                for (const auto s : c)
                    energy[i] += (double) s * s;
        }
        if (numBlocks < (size_t) windowSeconds)
            return data.slice (start, end);
        std::vector<std::pair<double, size_t>> windows;
        for (size_t i = 0; i + windowSeconds <= numBlocks; ++i)
            windows.push_back ({ std::accumulate (energy.begin() + (long) i, energy.begin() + (long) (i + windowSeconds), 0.0), i });

        const auto wanted = std::max<size_t> (1, (size_t) (seconds / windowSeconds));
        std::vector<size_t> chosen;
        auto free = [&] (size_t at)
        {
            return std::none_of (chosen.begin(), chosen.end(), [&] (size_t c) { return at + windowSeconds > c && c + windowSeconds > at; });
        };
        auto byEnergy = windows;
        std::sort (byEnergy.begin(), byEnergy.end(), [] (auto& a, auto& b) { return a.first > b.first; });
        for (const auto& [e, at] : byEnergy)
            if (chosen.size() + 1 < wanted && free (at))
                chosen.push_back (at);
        // And one typical stretch: the free window nearest the median.
        for (size_t i = byEnergy.size() / 2, step = 0; step < byEnergy.size(); ++step)
        {
            const auto j = step % 2 == 0 ? i + step / 2 : i - (step + 1) / 2;
            if (j < byEnergy.size() && free (byEnergy[j].second))
            {
                chosen.push_back (byEnergy[j].second);
                break;
            }
        }
        std::sort (chosen.begin(), chosen.end());

        AudioData::Builder out (channels, rate);
        const auto length = (Frame) windowSeconds * blockLength;
        const auto fade = (int) std::max (1.0, rate * 0.015);
        std::vector<std::vector<float>> piece ((size_t) channels, std::vector<float> ((size_t) length));
        std::vector<float*> piecePtrs;
        for (auto& c : piece)
            piecePtrs.push_back (c.data());
        for (const auto at : chosen)
        {
            data.read (start + (Frame) at * blockLength, (int) length, piecePtrs.data());
            for (auto& c : piece)
                for (int i = 0; i < fade; ++i)
                {
                    const auto g = (float) i / (float) fade;
                    c[(size_t) i] *= g;
                    c[(size_t) (length - 1 - i)] *= g;
                }
            std::vector<const float*> in (piecePtrs.begin(), piecePtrs.end());
            out.append (in.data(), (int) length);
        }
        return out.finish();
    }

    Role guessRole (const std::string& text)
    {
        std::string name;
        for (const auto c : text)
            name += (char) std::tolower ((unsigned char) c);

        if (contains (name, "ceiling"))
            return Role::ceiling;
        if (contains (name, "threshold") || contains (name, "ratio") || contains (name, "knee") || contains (name, "range"))
            return Role::dynamics;
        if (contains (name, "attack") || contains (name, "release") || contains (name, "hold") || contains (name, "lookahead")
            || contains (name, "look-ahead") || contains (name, "delay") || contains (name, "time"))
            return Role::time;
        if (contains (name, "width") || contains (name, "stereo") || contains (name, "side"))
            return Role::width;
        if (contains (name, "freq") || contains (name, " q") || name.rfind ("q", 0) == 0 || contains (name, "slope")
            || contains (name, "cut"))
            return Role::other;
        // Band gains carry a band number ("Gain 3", "Band 2 Gain"); a shelf or air band is EQ too.
        if ((contains (name, "gain") && std::any_of (name.begin(), name.end(), [] (char c) { return std::isdigit ((unsigned char) c); }))
            || contains (name, "band") || contains (name, "shelf") || contains (name, "air") || contains (name, "treble"))
            return Role::eq;
        if (contains (name, "gain") || contains (name, "makeup") || contains (name, "make-up") || contains (name, "drive")
            || contains (name, "input") || contains (name, "volume") || contains (name, "level") || contains (name, "trim"))
            return Role::level;
        return Role::other;
    }

    bool movedByDefault (const std::string& text, int numSteps, bool isBoolean)
    {
        if (isBoolean || numSteps > 0)
            return false;
        std::string name;
        for (const auto c : text)
            name += (char) std::tolower ((unsigned char) c);
        for (const auto* never : { "bypass", "oversampl", "quality", "mode", "type", "phase", "scale", "mix", "wet", "dry",
                                   "listen", "solo", "mute", "rotation", "output" })
            if (contains (name, never))
                return false;
        switch (guessRole (text))
        {
            case Role::level: case Role::eq: case Role::dynamics: case Role::width: return true;
            case Role::time: case Role::ceiling: case Role::other: break;
        }
        return false;
    }

    Policy policy (Role role, const Target& t, double freedom)
    {
        Policy p;
        p.freedom = freedom;
        switch (role)
        {
            case Role::level:
                p.moves = t.loudnessWeight > 0.0;
                p.freedom = 1.0;
                p.stay = 0.05;
                break;
            case Role::eq:       p.moves = t.toneWeight > 0.0; break;
            case Role::width:    p.moves = t.widthWeight > 0.0; break;
            case Role::dynamics: p.moves = t.loudnessWeight > 0.0 || t.dynamicsWeight > 0.0; break;
            case Role::time: case Role::ceiling: case Role::other: break;   // only when you tick it, so always
        }
        return p;
    }

    Result optimise (const std::vector<double>& start, const Evaluate& evaluate, const Settings& settings,
                     const std::function<void (double)>& progress)
    {
        Result result;
        result.values = start;
        const auto n = start.size();
        std::vector<double> lo (n), hi (n);
        for (size_t i = 0; i < n; ++i)
        {
            const auto freedom = i < settings.freedomEach.size() ? settings.freedomEach[i] : settings.freedom;
            lo[i] = std::max (0.0, start[i] - freedom);
            hi[i] = std::min (1.0, start[i] + freedom);
        }

        // The pull towards your own settings joins the residuals as one more row per parameter.
        std::vector<double> stayWeight (n);
        for (size_t i = 0; i < n; ++i)
            stayWeight[i] = (i < settings.stayEach.size() ? settings.stayEach[i] : settings.stay) / 0.1;
        auto withStay = [&] (const std::vector<double>& x, std::vector<double> r)
        {
            for (size_t i = 0; i < n; ++i)
                r.push_back (stayWeight[i] * (x[i] - start[i]));
            return r;
        };

        auto report = [&] { if (progress) progress (std::min (1.0, (double) result.evaluations / settings.maxEvaluations)); };
        auto run = [&] (const std::vector<double>& x, std::vector<double>& r)
        {
            ++result.evaluations;
            const auto ok = evaluate (x, r);
            report();
            return ok;
        };

        std::vector<double> x = start, r;
        if (! run (x, r))
        {
            result.stopped = true;
            return result;
        }
        result.startError = result.error = sumOfSquares (withStay (x, r));
        if (n == 0)
            return result;

        const auto m = r.size();
        std::vector<double> jac (m * n, 0.0);   // row major, residual by parameter
        std::vector<bool> inert (n, false);
        auto fullJacobian = [&]() -> bool
        {
            for (size_t j = 0; j < n; ++j)
            {
                if (inert[j])
                    continue;
                if (result.evaluations >= settings.maxEvaluations)
                    return true;
                auto xs = x;
                auto h = x[j] + settings.step <= hi[j] ? settings.step : -settings.step;
                if (x[j] + h < lo[j])
                    h = hi[j] - x[j] >= x[j] - lo[j] ? hi[j] - x[j] : lo[j] - x[j];
                if (std::abs (h) < 1.0e-9)
                    continue;
                xs[j] += h;
                std::vector<double> rs;
                if (! run (xs, rs))
                    return false;
                double column = 0.0;
                for (size_t i = 0; i < m; ++i)
                {
                    jac[i * n + j] = (rs[i] - r[i]) / h;
                    column += std::abs (jac[i * n + j]);
                }
                // A parameter that changes nothing (an EQ band that is switched off) is not asked again.
                inert[j] = column < 1.0e-6;
            }
            return true;
        };

        if (! fullJacobian())
        {
            result.stopped = true;
            return result;
        }

        double lambda = 1.0e-2;
        int acceptedSinceFull = 0;
        while (result.evaluations < settings.maxEvaluations)
        {
            // (J^T J + lambda diag(J^T J) + stay^2 I) d = -(J^T r + stay^2 (x - start)), over the moving parameters.
            std::vector<size_t> live;
            for (size_t j = 0; j < n; ++j)
                if (! inert[j])
                    live.push_back (j);
            if (live.empty())
                break;
            const auto k = live.size();
            std::vector<double> a (k * k, 0.0), g (k, 0.0);
            for (size_t p = 0; p < k; ++p)
            {
                for (size_t q = 0; q < k; ++q)
                {
                    double sum = 0.0;
                    for (size_t i = 0; i < m; ++i)
                        sum += jac[i * n + live[p]] * jac[i * n + live[q]];
                    a[p * k + q] = sum;
                }
                double sum = 0.0;
                for (size_t i = 0; i < m; ++i)
                    sum += jac[i * n + live[p]] * r[i];
                g[p] = -(sum + stayWeight[live[p]] * stayWeight[live[p]] * (x[live[p]] - start[live[p]]));
            }
            for (size_t p = 0; p < k; ++p)
                a[p * k + p] += lambda * std::max (a[p * k + p], 1.0e-6) + stayWeight[live[p]] * stayWeight[live[p]];
            if (! solve (a, g))
            {
                lambda *= 10.0;
                continue;
            }

            auto trial = x;
            double moved = 0.0;
            for (size_t p = 0; p < k; ++p)
            {
                const auto j = live[p];
                trial[j] = std::clamp (x[j] + g[p], lo[j], hi[j]);
                moved = std::max (moved, std::abs (trial[j] - x[j]));
            }
            if (moved < 1.0e-4)
                break;

            std::vector<double> rt;
            if (! run (trial, rt))
            {
                result.stopped = true;
                break;
            }
            const auto trialError = sumOfSquares (withStay (trial, rt));
            if (trialError < result.error)
            {
                // Broyden: correct the Jacobian along the step taken, instead of measuring it again.
                std::vector<double> dx (n, 0.0);
                double dxx = 0.0;
                for (size_t j = 0; j < n; ++j)
                {
                    dx[j] = trial[j] - x[j];
                    dxx += dx[j] * dx[j];
                }
                for (size_t i = 0; i < m; ++i)
                {
                    double predicted = 0.0;
                    for (size_t j = 0; j < n; ++j)
                        predicted += jac[i * n + j] * dx[j];
                    const auto miss = (rt[i] - r[i]) - predicted;
                    for (size_t j = 0; j < n; ++j)
                        if (! inert[j])
                            jac[i * n + j] += miss * dx[j] / dxx;
                }
                const auto gain = result.error - trialError;
                x = trial;
                r = rt;
                result.values = x;
                result.error = trialError;
                lambda = std::max (lambda / 3.0, 1.0e-6);
                if (gain < 1.0e-4 * std::max (1.0, result.startError))
                    break;
                if (++acceptedSinceFull >= 3)
                {
                    acceptedSinceFull = 0;
                    if (! fullJacobian())
                    {
                        result.stopped = true;
                        break;
                    }
                }
            }
            else
            {
                lambda *= 4.0;
                if (lambda > 1.0e6)
                    break;
            }
        }
        report();
        return result;
    }
}

// SPDX-License-Identifier: AGPL-3.0-only
#include "PhiForensics.h"
#include "Fft.h"

#include <algorithm>
#include <cmath>
#include <complex>
#include <cstdint>
#include <cstdio>
#include <map>

namespace forensic
{
namespace
{
    constexpr double phi = 1.6180339887498949;
    constexpr int numPowers = 28;               // φ^(n/2), n = 1..28: √φ up to φ¹⁴ (843)
    constexpr double maxRatio = 1000.0;          // ratios are looked for between 1 and 1000: 20 Hz to 20 kHz
    const double logRange = std::log (maxRatio);

    std::string format (const char* pattern, double value)
    {
        char text[64];
        std::snprintf (text, sizeof (text), pattern, value);
        return text;
    }

    std::string powerName (int halves)
    {
        static const char* names[] = { "", "√φ", "φ", "φ^1.5", "φ²", "φ^2.5", "φ³", "φ^3.5", "φ⁴", "φ^4.5", "φ⁵", "φ^5.5", "φ⁶",
                                       "φ^6.5", "φ⁷", "φ^7.5", "φ⁸", "φ^8.5", "φ⁹", "φ^9.5", "φ¹⁰", "φ^10.5", "φ¹¹", "φ^11.5", "φ¹²",
                                       "φ^12.5", "φ¹³", "φ^13.5", "φ¹⁴" };
        return halves >= 1 && halves <= numPowers ? names[halves] : "φ^" + format ("%.1f", halves / 2.0);
    }

    // Simple fractions: whole numbers to 20 and fractions of small numbers (up to 12/5), such as
    // the 8/5 of a minor sixth or the 10/3 of 300 Hz to 1 kHz. A ratio near one says nothing about φ.
    const std::vector<double>& fractionLogs()
    {
        static const std::vector<double> logs = []
        {
            std::vector<double> v;
            for (int q = 1; q <= 5; ++q)
                for (int p = q + 1; p <= (q == 1 ? 20 : 12); ++p)
                    v.push_back (std::log ((double) p / q));
            std::sort (v.begin(), v.end());
            v.erase (std::unique (v.begin(), v.end(), [] (double a, double b) { return std::abs (a - b) < 1.0e-12; }), v.end());
            return v;
        }();
        return logs;
    }

    bool nearFraction (double logValue, double tolerance)
    {
        const auto& logs = fractionLogs();
        const auto it = std::lower_bound (logs.begin(), logs.end(), logValue - tolerance);
        return it != logs.end() && *it <= logValue + tolerance;
    }

    // One comparison: a ratio (log) known to within `tolerance` (log), or a numeral (a frequency or time) to look at.
    struct Candidate
    {
        std::string what, group;
        double logValue = 0.0, tolerance = 0.0;
        bool numeral = false;
    };

    // Golden numerals: 1.618, 2.618, 3.82 (10/φ²), 4.236 (φ³) and 6.18 (10/φ) in any decade, or a Fibonacci number from 13 up.
    struct Numeral { double value; std::string name; };
    const std::vector<Numeral>& numerals()
    {
        static const std::vector<Numeral> list
        {
            { phi, "φ" }, { phi * phi, "φ²" }, { 10.0 / (phi * phi), "10/φ²" }, { phi * phi * phi, "φ³" }, { 10.0 / phi, "10/φ" }
        };
        return list;
    }

    struct Hit
    {
        std::string target;          // empty: no hit
        double errorPercent = 0.0;
        double chance = 0.0;         // the chance of a hit for this candidate by luck
    };

    // Looks for a hit at `factor` times the candidate's tolerance. For ratios the targets are
    // b^(n/2) (b is φ, or a control base), leaving out any that is a simple fraction too.
    Hit evaluate (const Candidate& c, double logBase, double factor)
    {
        Hit hit;
        const auto tolerance = std::max (0.0005, c.tolerance * factor);
        double bestError = 1.0e9;
        if (! c.numeral)
        {
            int usable = 0;
            for (int n = 1; n <= numPowers; ++n)
            {
                const auto t = 0.5 * n * logBase;
                if (t > logRange + tolerance)
                    break;
                if (nearFraction (t, tolerance))
                    continue;
                ++usable;
                const auto e = std::abs (c.logValue - t);
                if (e < tolerance && e < bestError)
                {
                    bestError = e;
                    hit.target = powerName (n);
                }
            }
            hit.chance = std::min (1.0, usable * 2.0 * tolerance / logRange);
        }
        else
        {
            const auto value = std::exp (c.logValue);
            const auto decade = std::pow (10.0, std::floor (std::log10 (value)));
            const auto mantissa = value / decade;
            for (const auto& numeral : numerals())
                for (auto m : { mantissa, mantissa * 10.0, mantissa / 10.0 })
                {
                    const auto e = std::abs (std::log (m / numeral.value));
                    if (e < tolerance && e < bestError && m >= 1.0 && m < 10.0)
                    {
                        bestError = e;
                        hit.target = numeral.name + " × " + format ("%g", decade);
                    }
                }
            hit.chance = std::min (1.0, (double) numerals().size() * 2.0 * tolerance / std::log (10.0));
            if (value >= 12.5)
            {
                // Fibonacci numbers lie a factor φ apart.
                hit.chance = std::min (1.0, hit.chance + 2.0 * tolerance / std::log (phi));
                for (std::uint64_t a = 13, b = 21; a < 1000000; std::swap (a, b), b += a)
                {
                    const auto e = std::abs (std::log (value / (double) a));
                    if (e < tolerance && e < bestError)
                    {
                        bestError = e;
                        hit.target = "Fibonacci " + std::to_string (a);
                    }
                }
            }
        }
        if (! hit.target.empty())
            hit.errorPercent = (std::exp (bestError) - 1.0) * 100.0;
        return hit;
    }

    std::string groupName (const std::string& group)
    {
        if (group == "frequency") return "peaks and notches of the response";
        if (group == "phase")     return "phase turns";
        if (group == "edge")      return "edges of the response";
        if (group == "echo")      return "echo times";
        if (group == "gap")       return "gaps between echoes";
        if (group == "level")     return "echo levels";
        if (group == "tone")      return "tones it adds";
        if (group == "dynamics")  return "compression ratio";
        if (group == "time")      return "attack and release";
        if (group == "rate")      return "LFO rates";
        if (group == "rhythm")    return "rhythm";
        if (group == "numbers")   return "golden numbers";
        return group;
    }

    double poissonTail (double lambda, int k)   // P(X >= k)
    {
        if (k <= 0)
            return 1.0;
        double term = std::exp (-lambda), below = term;
        for (int i = 1; i < k; ++i)
        {
            term *= lambda / i;
            below += term;
        }
        return std::clamp (1.0 - below, 0.0, 1.0);
    }

    //==========================================================================
    // Peaks of a curve with parabolic refinement (index units), each with its prominence.
    struct Peak { double at = 0.0, value = 0.0, prominence = 0.0; };
    std::vector<Peak> peaksOf (const std::vector<double>& v, double minProminence)
    {
        std::vector<Peak> peaks;
        for (std::size_t i = 1; i + 1 < v.size(); ++i)
        {
            if (! (v[i] > v[i - 1] && v[i] >= v[i + 1]))
                continue;
            double leftMin = v[i], rightMin = v[i];
            for (std::size_t j = i; j-- > 0;)
            {
                if (v[j] > v[i])
                    break;
                leftMin = std::min (leftMin, v[j]);
            }
            for (std::size_t j = i + 1; j < v.size(); ++j)
            {
                if (v[j] > v[i])
                    break;
                rightMin = std::min (rightMin, v[j]);
            }
            const auto prominence = v[i] - std::max (leftMin, rightMin);
            if (prominence < minProminence)
                continue;
            const auto a = v[i - 1], b = v[i], c = v[i + 1];
            const auto d = a - 2 * b + c;
            const auto shift = std::abs (d) > 1.0e-12 ? std::clamp (0.5 * (a - c) / d, -0.5, 0.5) : 0.0;
            peaks.push_back ({ (double) i + shift, b, prominence });
        }
        return peaks;
    }

    // A fine per-bin curve read at log-spaced points, so a peak's place is found to a fixed share of its frequency.
    struct LogCurve
    {
        std::vector<double> hz, value;
        double step = 0.0;   // log step between points
    };

    LogCurve resample (const std::vector<double>& fine, double binHz, double from, double to, int count)
    {
        LogCurve c;
        c.step = std::log (to / from) / (count - 1);
        for (int i = 0; i < count; ++i)
        {
            const auto hz = from * std::exp (i * c.step);
            const auto pos = hz / binHz;
            const auto k = std::clamp ((std::size_t) pos, (std::size_t) 0, fine.size() - 2);
            const auto frac = std::clamp (pos - (double) k, 0.0, 1.0);
            c.hz.push_back (hz);
            c.value.push_back (fine[k] * (1.0 - frac) + fine[k + 1] * frac);
        }
        return c;
    }

    double hzAt (const LogCurve& c, double index)
    {
        return c.hz.front() * std::exp (index * c.step);
    }

    // Keeps the `count` most prominent peaks.
    void keepStrongest (std::vector<Peak>& peaks, std::size_t count)
    {
        std::sort (peaks.begin(), peaks.end(), [] (const Peak& a, const Peak& b) { return a.prominence > b.prominence; });
        if (peaks.size() > count)
            peaks.resize (count);
    }

    bool channelsDiffer (const std::vector<double>& a, const std::vector<double>& b, double by)
    {
        if (a.size() != b.size())
            return ! b.empty();
        double sum = 0.0;
        for (std::size_t i = 0; i < a.size(); ++i)
            sum += std::abs (std::max (a[i], -150.0) - std::max (b[i], -150.0));
        return a.empty() ? false : sum / (double) a.size() > by;
    }
}

//==============================================================================
std::vector<Feature> features (const measure::Results& r)
{
    std::vector<Feature> list;
    const auto rate = r.sampleRate;

    if (r.hasLinear && ! r.silent && r.binHz > 0.0)
    {
        // Arrivals: peaks of the impulse response's envelope (both channels' energy), at
        // least 2 ms apart. Those after the first millisecond are echoes.
        const auto& left = r.impulse[0];
        const auto& right = r.impulse[1];
        const auto n = std::min (left.size(), right.size());
        const auto pre = (double) r.impulsePre;
        std::vector<Peak> arrivals;
        if (n > 0)
        {
            std::vector<double> energy (n);
            for (std::size_t i = 0; i < n; ++i)
                energy[i] = (double) left[i] * left[i] + (double) right[i] * right[i];
            // A short moving maximum, so a peak spread over a few samples counts once.
            const auto width = std::max<std::size_t> (1, (std::size_t) (0.0005 * rate));
            std::vector<double> env (n, 0.0);
            for (std::size_t i = 0; i < n; ++i)
            {
                double m = 0.0;
                for (auto j = i >= width ? i - width : 0; j <= std::min (n - 1, i + width); ++j)
                    m = std::max (m, energy[j]);
                env[i] = 10.0 * std::log10 (std::max (m, 1.0e-20));
            }
            const auto top = *std::max_element (env.begin(), env.end());
            std::vector<Peak> found;
            for (auto& p : peaksOf (env, 6.0))
                if (p.value > top - 40.0)
                    found.push_back (p);
            // A plateau (the moving maximum's) has its peak at its start: put it at its middle instead.
            for (auto& p : found)
            {
                auto end = (std::size_t) p.at;
                while (end + 1 < n && env[end + 1] >= env[(std::size_t) p.at] - 1.0e-9)
                    ++end;
                p.value = env[(std::size_t) p.at];   // the plateau's own level, not a parabola's guess
                p.at = 0.5 * ((double) (std::size_t) p.at + (double) end);
            }
            // An arrival is sharp: its energy sits within half a millisecond, well above the
            // few milliseconds around it. Resonances beating against each other swell and fade
            // slowly instead; they are no echoes.
            auto sharp = [&] (const Peak& p)
            {
                const auto at = (long long) p.at, inner = (long long) (0.0005 * rate), outer = (long long) (0.005 * rate);
                double centre = 0.0, around = 0.0;
                long long counted = 0;
                for (auto i = std::max (0LL, at - outer); i < std::min ((long long) n, at + outer); ++i)
                {
                    if (std::abs (i - at) <= inner)
                        centre = std::max (centre, energy[(std::size_t) i]);
                    else if (std::abs (i - at) > 2 * inner)
                    {
                        around += energy[(std::size_t) i];
                        ++counted;
                    }
                }
                return counted == 0 || centre > 10.0 * (around / (double) counted);
            };
            found.erase (std::remove_if (found.begin(), found.end(), [&] (const Peak& p) { return ! sharp (p); }), found.end());
            std::sort (found.begin(), found.end(), [] (const Peak& a, const Peak& b) { return a.value > b.value; });
            for (const auto& p : found)
                if (std::none_of (arrivals.begin(), arrivals.end(), [&] (const Peak& e) { return std::abs (e.at - p.at) < 0.002 * rate; })
                    && arrivals.size() < 13)
                    arrivals.push_back (p);
            std::sort (arrivals.begin(), arrivals.end(), [] (const Peak& a, const Peak& b) { return a.at < b.at; });
        }

        std::vector<Peak> echoes;
        for (const auto& a : arrivals)
            if (a.at - pre > 0.001 * rate)
                echoes.push_back (a);
        for (std::size_t i = 0; i < echoes.size(); ++i)
        {
            const auto samples = echoes[i].at - pre;
            const auto ms = samples / rate * 1000.0;
            list.push_back ({ "echo", "Echo " + std::to_string (i + 1) + " at " + format ("%.2f", ms) + " ms", ms,
                              std::max (0.001, 2.0 / samples), "ms" });
        }
        // The gaps between one echo and the next: a pre-delay shifts every echo time but no gap,
        // so delay lines in golden proportion still show behind one.
        for (std::size_t i = 0; i + 1 < echoes.size(); ++i)
        {
            const auto samples = echoes[i + 1].at - echoes[i].at;
            const auto ms = samples / rate * 1000.0;
            list.push_back ({ "gap", "Gap from echo " + std::to_string (i + 1) + " to " + std::to_string (i + 2) + ", " + format ("%.2f", ms) + " ms",
                              ms, std::max (0.001, 3.0 / samples), "" });
        }
        // How much quieter each echo is than the one before: the energy within half a millisecond
        // either side, which does not depend on where between two samples the echo falls.
        auto amplitude = [&] (const Peak& p)
        {
            const auto half = 0.0005 * rate;
            double sum = 0.0;
            for (auto i = (std::size_t) std::max (0.0, p.at - half); i < n && (double) i <= p.at + half; ++i)
                sum += (double) left[i] * left[i] + (double) right[i] * right[i];
            return std::sqrt (sum);
        };
        for (std::size_t i = 0; i + 1 < echoes.size() && i < 8; ++i)
        {
            const auto a = amplitude (echoes[i]), b = amplitude (echoes[i + 1]);
            if (a <= 0.0 || b <= 0.0)
                continue;
            const auto ratio = std::max (a, b) / std::min (a, b);
            if (ratio > 1.01)
                list.push_back ({ "level", "Level of echo " + std::to_string (i + 1) + " against echo " + std::to_string (i + 2),
                                  ratio, 0.003, "" });
        }

        // The response. With separate arrivals the whole response is a comb of them, whose
        // teeth say nothing a designer chose: read only the first arrival's own sound, up to the next.
        std::array<std::vector<double>, 2> magnitude = r.fineMagnitudeDb, groupDelay = r.fineGroupDelayMs;
        auto binHz = r.binHz;
        std::size_t windowLength = 0;
        if (arrivals.size() >= 2 && arrivals[1].at - arrivals[0].at > 0.005 * rate)
        {
            const auto from = (std::size_t) std::max (0.0, arrivals[0].at - 0.001 * rate);
            const auto to = (std::size_t) (arrivals[1].at - 0.001 * rate);
            const auto length = to - from;
            windowLength = length;
            int order = 16;
            while ((std::size_t (1) << order) < length)
                ++order;
            const Fft fft (order);
            const auto size = (std::size_t) fft.size();
            binHz = rate / (double) size;
            for (int c = 0; c < 2; ++c)
            {
                std::vector<std::complex<float>> x (size);
                const auto& ir = r.impulse[(std::size_t) c];
                for (std::size_t i = 0; i < length; ++i)
                {
                    const auto pos = (double) i / (double) length;   // fade the last tenth out
                    x[i] = ir[from + i] * (float) (pos > 0.9 ? 0.5 + 0.5 * std::cos (3.14159265358979 * (pos - 0.9) / 0.1) : 1.0);
                }
                fft.forward (x);
                auto& mag = magnitude[(std::size_t) c];
                auto& gd = groupDelay[(std::size_t) c];
                mag.assign (size / 2 + 1, -200.0);
                gd.assign (size / 2 + 1, 0.0);
                for (std::size_t k = 0; k <= size / 2; ++k)
                    mag[k] = 20.0 * std::log10 (std::max (1.0e-10, (double) std::abs (x[k])));
                for (std::size_t k = 1; k < size / 2; ++k)
                    gd[k] = -std::arg (std::complex<double> (x[k + 1]) * std::conj (std::complex<double> (x[k - 1])))
                          / (4.0 * 3.14159265358979 * binHz) * 1000.0;
            }
        }

        const auto stereo = channelsDiffer (magnitude[0], magnitude[1], 0.05);
        const auto top = std::min (0.9 * rate / 2, 20000.0);
        for (int c = 0; c < (stereo ? 2 : 1); ++c)
        {
            const std::string side = stereo ? (c == 0 ? " (L)" : " (R)") : "";
            auto mag = resample (magnitude[(std::size_t) c], binHz, 20.0, top, 3000);
            const auto sigma = std::max (0.003, mag.step);
            const auto [lo, hi] = std::minmax_element (mag.value.begin(), mag.value.end());
            if (*hi - *lo > 1.0)
            {
                // Peaks and notches: where a designer puts a band.
                auto peaks = peaksOf (mag.value, 1.0);
                keepStrongest (peaks, 16);
                for (const auto& p : peaks)
                    list.push_back ({ "frequency", "Peak at " + format ("%.1f", hzAt (mag, p.at)) + " Hz" + side, hzAt (mag, p.at), sigma, "Hz" });
                auto inverted = mag.value;
                for (auto& v : inverted)
                    v = -v;
                auto notches = peaksOf (inverted, 1.0);
                keepStrongest (notches, 16);
                for (const auto& p : notches)
                    list.push_back ({ "frequency", "Notch at " + format ("%.1f", hzAt (mag, p.at)) + " Hz" + side, hzAt (mag, p.at), sigma, "Hz" });
            }

            // Edges: where the response climbs or falls fastest (shelves, crossovers, cut filters),
            // read as the steepest points of its slope per octave.
            if (*hi - *lo > 1.0)
            {
                const auto k = std::max<std::size_t> (1, (std::size_t) std::lround (std::log (2.0) / 24.0 / mag.step));
                std::vector<double> steep (mag.value.size(), 0.0);
                for (std::size_t i = k; i + k < mag.value.size(); ++i)
                    steep[i] = std::abs (mag.value[i + k] - mag.value[i - k]) / (2.0 * (double) k * mag.step / std::log (2.0));
                auto edges = peaksOf (steep, 2.0);
                edges.erase (std::remove_if (edges.begin(), edges.end(), [] (const Peak& p) { return p.value < 3.0; }), edges.end());
                keepStrongest (edges, 12);
                for (const auto& p : edges)
                    list.push_back ({ "edge", "Edge at " + format ("%.1f", hzAt (mag, p.at)) + " Hz (" + format ("%.0f", p.value) + " dB/octave)" + side,
                                      hzAt (mag, p.at), std::max (0.006, 2.0 * mag.step), "Hz" });
            }

            // Phase turns: peaks of the group delay (allpasses, crossovers), away from deep notches.
            auto gd = resample (groupDelay[(std::size_t) c], binHz, 20.0, top, 3000);
            for (std::size_t i = 0; i < gd.value.size(); ++i)
                if (mag.value[i] < *hi - 30.0)
                    gd.value[i] = 0.0;
            // In cycles (delay × frequency), so a phase turn high up counts as much as one low down;
            // smoothed over a few points, as the group delay is a derivative and picks up noise.
            auto smooth = gd.value;
            for (std::size_t i = 2; i + 2 < smooth.size(); ++i)
            {
                const auto ms = (gd.value[i - 2] + gd.value[i - 1] + gd.value[i] + gd.value[i + 1] + gd.value[i + 2]) / 5.0;
                smooth[i] = ms > 0.01 ? ms * gd.hz[i] / 1000.0 : 0.0;   // less than half a sample at 48 kHz is noise
            }
            // Below a few cycles of the part read, the group delay is the window's, not the effect's.
            const auto lowest = windowLength > 0 ? 10.0 * rate / (double) windowLength : 4.0 * binHz;
            for (std::size_t i = 0; i < smooth.size(); ++i)
                if (gd.hz[i] < lowest)
                    smooth[i] = 0.0;
            const auto [glo, ghi] = std::minmax_element (smooth.begin(), smooth.end());
            if (*ghi - *glo > 0.02)
            {
                auto turns = peaksOf (smooth, std::max (0.02, 0.05 * (*ghi - *glo)));
                keepStrongest (turns, 12);
                for (const auto& p : turns)
                    list.push_back ({ "phase", "Phase turn at " + format ("%.1f", hzAt (gd, p.at)) + " Hz" + side, hzAt (gd, p.at),
                                      std::max (0.01, gd.step * 2), "Hz" });
            }
        }
    }

    // Tones that are no harmonic: a pitch shifter's copies, a ring modulator.
    if (r.hasHarmonics && r.sineSpectrum.toneHz > 0.0)
    {
        const auto tone = r.sineSpectrum.toneHz;
        const auto binHz = rate / 65536.0;
        for (std::size_t i = 0; i < std::min<std::size_t> (r.otherLines.size(), 12); ++i)
        {
            const auto& l = r.otherLines[i];
            if (l.db < -60.0)
                continue;
            auto ratio = l.hz / tone;
            if (ratio < 1.0)
                ratio = 1.0 / ratio;
            list.push_back ({ "tone", "Tone at " + format ("%.1f", l.hz) + " Hz from a " + format ("%.0f", tone) + " Hz input",
                              ratio, std::max (0.001, 2.0 * binHz / l.hz), "" });
        }
    }

    if (r.hasDynamics)
    {
        if (r.ratio > 1.05 && r.ratio < 99.0)
            list.push_back ({ "dynamics", "Compression ratio " + format ("%.2f", r.ratio) + ":1", r.ratio, 0.015, "" });
        if (r.attackMs > 0.5)
            list.push_back ({ "time", "Attack " + format ("%.1f", r.attackMs) + " ms", r.attackMs, std::max (0.08, 2.5 / r.attackMs), "ms" });
        if (r.releaseMs > 0.5)
            list.push_back ({ "time", "Release " + format ("%.1f", r.releaseMs) + " ms", r.releaseMs, std::max (0.08, 2.5 / r.releaseMs), "ms" });
    }

    if (r.hasModulation)
    {
        for (auto hz : r.modulationHz)
            list.push_back ({ "rate", "LFO at " + format ("%.3f", hz) + " Hz", hz, std::max (0.002, 0.1 * r.modulationResolutionHz / hz), "Hz" });
        if (r.gated && r.gateOpenSteps >= 3 && r.gateClosedSteps >= 3)
        {
            auto ratio = (double) r.gateOpenSteps / r.gateClosedSteps;
            if (ratio < 1.0)
                ratio = 1.0 / ratio;
            list.push_back ({ "rhythm", "Gate: " + std::to_string (r.gateOpenSteps) + " open steps to " + std::to_string (r.gateClosedSteps) + " closed",
                              ratio, 1.0 / std::min (r.gateOpenSteps, r.gateClosedSteps), "" });
            // The counts themselves: whole numbers, so a tolerance of half a step keeps the chance of a Fibonacci number honest.
            for (const auto& [count, what] : { std::pair { r.gateOpenSteps, "open" }, std::pair { r.gateClosedSteps, "closed" } })
                list.push_back ({ "rhythm", "Gate: " + std::to_string (count) + " " + what + " steps", (double) count, 0.25 / count, "steps" });
        }
    }

    if (r.reportedLatency > 0)
    {
        const auto ms = r.reportedLatency / rate * 1000.0;
        list.push_back ({ "latency", "Latency " + format ("%.3f", ms) + " ms", ms, std::max (0.001, 0.5 / r.reportedLatency), "ms" });
    }
    return list;
}

//==============================================================================
Report analyse (const std::vector<Feature>& list)
{
    Report report;
    report.features = list;
    const auto logPhi = std::log (phi);

    // The candidates: features that are ratios already, ratios between features of a
    // kind, and exactly measured frequencies and times (for golden numerals).
    std::vector<Candidate> candidates;
    std::map<std::string, std::vector<const Feature*>> groups;
    for (const auto& f : list)
    {
        if (f.unit == "steps")
            candidates.push_back ({ f.label, f.group, std::log (f.value), 2.0 * f.sigma, true });
        else if (f.group == "level" || f.group == "tone" || f.group == "dynamics" || f.group == "rhythm")
        {
            if (f.value > 1.0 && f.value <= 20.0)
                candidates.push_back ({ f.label, f.group, std::log (f.value), std::max (0.0025, 2.0 * f.sigma) });
        }
        else if (f.group != "latency")
            groups[f.group].push_back (&f);

        if ((f.unit == "Hz" || f.unit == "ms") && f.sigma <= 0.01 && f.value > 0.0)
            candidates.push_back ({ f.label, "numbers", std::log (f.value), std::max (0.0025, 2.0 * f.sigma), true });
    }
    for (auto& [name, members] : groups)
    {
        std::sort (members.begin(), members.end(), [] (const Feature* a, const Feature* b) { return a->value < b->value; });
        for (std::size_t i = 0; i < members.size(); ++i)
            for (std::size_t j = i + 1; j < members.size(); ++j)
            {
                const auto ratio = members[j]->value / members[i]->value;
                const auto tolerance = std::max (0.0025, 2.0 * std::hypot (members[i]->sigma, members[j]->sigma));
                if (ratio < 1.0 + 2 * tolerance || ratio > maxRatio)
                    continue;
                candidates.push_back ({ members[j]->label + " / " + members[i]->label, name, std::log (ratio), tolerance });
            }
    }
    report.comparisons = (int) candidates.size();

    // Every hit within the full tolerance, for the list.
    for (const auto& c : candidates)
    {
        const auto hit = evaluate (c, logPhi, 1.0);
        if (! hit.target.empty())
            (c.numeral ? report.numerals : report.matches).push_back ({ c.what, c.numeral ? std::exp (c.logValue) : std::exp (c.logValue),
                                                                       hit.target, hit.errorPercent, hit.chance, hit.errorPercent < 0.1 });
    }
    report.hits = (int) (report.matches.size() + report.numerals.size());

    // Each kind of evidence weighed on its own, at the full tolerance and at a fifth of it
    // (a designer's numbers come out exact; luck seldom does). The chance of each is multiplied
    // by how many were tried (Bonferroni), so trying many cannot fake a finding.
    std::map<std::string, std::vector<const Candidate*>> byGroup;
    for (const auto& c : candidates)
        byGroup[c.group].push_back (&c);
    for (const auto& [name, members] : byGroup)
        for (const auto factor : { 1.0, 0.2 })
        {
            GroupScore score;
            score.name = name;
            score.fine = factor < 1.0;
            for (const auto* c : members)
            {
                const auto hit = evaluate (*c, logPhi, factor);
                score.expected += hit.chance;
                score.hits += hit.target.empty() ? 0 : 1;
                ++score.comparisons;
            }
            score.pValue = poissonTail (score.expected, score.hits);
            if (! score.fine)
                report.expected += score.expected;
            if (! score.fine || score.hits > 0)
                report.groups.push_back (score);
        }
    const auto tries = (double) (2 * byGroup.size());

    // The control scan: the same search for 1000 other ratios. φ must beat nearly all of them.
    auto controlScan = [&] (const GroupScore& g, std::vector<ScanPoint>& scan, int& phiHits)
    {
        const auto factor = g.fine ? 0.2 : 1.0;
        const auto& members = byGroup[g.name];
        auto count = [&] (double logBase)
        {
            int hits = 0;
            for (const auto* c : members)
                // Golden numerals and Fibonacci counts belong to φ alone: other ratios get no such credit.
                if ((! c->numeral || std::abs (logBase - logPhi) < 1.0e-9) && ! evaluate (*c, logBase, factor).target.empty())
                    ++hits;
            return hits;
        };
        phiHits = count (logPhi);
        scan.clear();
        int asGood = 0, others = 0;
        for (int i = 0; i <= 1000; ++i)
        {
            const auto base = 1.40 + 0.0005 * i;
            const auto hits = count (std::log (base));
            scan.push_back ({ base, hits });
            if (std::abs (std::log (base / phi)) < 0.004)
                continue;   // φ's own neighbourhood shares its hits
            ++others;
            if (hits >= phiHits)
                ++asGood;
        }
        return others > 0 ? (double) asGood / others : 1.0;
    };

    // The most telling kind decides: the strongest verdict any kind earns, then the smallest chance.
    std::vector<const GroupScore*> order;
    for (const auto& g : report.groups)
        if (g.hits >= 2)
            order.push_back (&g);
    std::sort (order.begin(), order.end(), [] (const GroupScore* a, const GroupScore* b) { return a->pValue < b->pValue; });
    int decisiveHits = 0;
    report.verdict = report.hits == 0 ? Verdict::nothing : Verdict::chance;
    report.pValue = std::min (1.0, poissonTail (report.expected, report.hits));
    for (std::size_t i = 0; i < order.size(); ++i)
    {
        const auto& g = *order[i];
        const auto p = std::min (1.0, g.pValue * tries);
        std::vector<ScanPoint> scan;
        int phiHits = 0;
        const auto percentile = g.name == "numbers" ? 0.0 : controlScan (g, scan, phiHits);
        const auto verdict = p < 0.001 && g.hits >= 3 && percentile <= 0.01 ? Verdict::strong
                           : p < 0.05 && percentile <= 0.05                  ? Verdict::suggestive
                                                                             : Verdict::chance;
        if (report.decisive.empty() || verdict > report.verdict || (verdict == report.verdict && p < report.pValue))
        {
            report.verdict = std::max (verdict, Verdict::chance);
            report.decisive = g.name;
            report.pValue = p;
            report.percentile = percentile;
            report.scan = scan;
            report.phiHits = phiHits;
            decisiveHits = g.hits;
        }
        if (report.verdict == Verdict::strong)
            break;
    }

    std::sort (report.matches.begin(), report.matches.end(), [] (const Match& a, const Match& b) { return a.errorPercent < b.errorPercent; });
    std::sort (report.numerals.begin(), report.numerals.end(), [] (const Match& a, const Match& b) { return a.errorPercent < b.errorPercent; });
    const auto bestGroup = report.decisive;

    const auto chancePercent = report.pValue * 100.0;
    const auto chanceText = chancePercent < 1.0e-10 ? std::string ("less than 1e-10") : format (chancePercent < 0.1 ? "%.2g" : "%.1f", chancePercent);
    const auto what = groupName (bestGroup);
    switch (report.verdict)
    {
        case Verdict::nothing:
            report.summary = "No golden ratios found among " + std::to_string (report.comparisons) + " comparisons.";
            break;
        case Verdict::chance:
            report.summary = std::to_string (report.hits) + " golden match" + (report.hits == 1 ? "" : "es") + " among "
                           + std::to_string (report.comparisons) + " comparisons, about what luck gives: nothing here points to φ.";
            break;
        case Verdict::suggestive:
            report.summary = "Suggestive: " + std::to_string (decisiveHits) + " golden matches in the " + what + " (a "
                           + chanceText + " % chance of so many by luck).";
            break;
        case Verdict::strong:
            report.summary = "Strong evidence of φ in the " + what + ": " + std::to_string (decisiveHits) + " golden matches (a "
                           + chanceText + " % chance by luck)" + (bestGroup == "numbers" ? std::string (".")
                           : ", and φ beats " + format ("%.0f", (1.0 - report.percentile) * 100.0) + " % of the other ratios tried.");
            break;
    }
    return report;
}

std::string verdictName (Verdict v)
{
    switch (v)
    {
        case Verdict::nothing:    return "No φ found";
        case Verdict::chance:     return "Consistent with chance";
        case Verdict::suggestive: return "Suggestive";
        case Verdict::strong:     return "Strong evidence";
    }
    return {};
}

std::string text (const Report& r)
{
    std::string out = "Forensic Φ: " + verdictName (r.verdict) + "\n" + r.summary + "\n";
    out += "\n" + std::to_string (r.comparisons) + " comparisons, " + std::to_string (r.hits) + " hits, " + format ("%.2f", r.expected)
         + " expected by chance. Chance of the deciding evidence by luck (corrected for the kinds tried): " + format ("%.3g", r.pValue * 100.0) + " %.\n";
    for (const auto& g : r.groups)
        out += "  " + groupName (g.name) + (g.fine ? " (exact, within a fifth of the tolerance)" : "") + ": " + std::to_string (g.hits)
             + " of " + std::to_string (g.comparisons) + ", " + format ("%.2f", g.expected) + " expected, chance " + format ("%.3g", g.pValue * 100.0)
             + " %" + (g.name == r.decisive ? " <" : "") + "\n";
    if (! r.scan.empty())
        out += "Control scan (" + groupName (r.decisive) + "): φ scores " + std::to_string (r.phiHits) + "; " + format ("%.1f", r.percentile * 100.0)
             + " % of the other ratios from 1.40 to 1.90 score as well.\n";

    if (! r.matches.empty())
    {
        out += "\nGolden ratios\n";
        for (const auto& m : r.matches)
            out += "  " + m.what + " = " + format ("%.4f", m.value) + " ~ " + m.target + " (off by " + format ("%.2f", m.errorPercent) + " %"
                 + (m.exact ? ", exact" : "") + ")\n";
    }
    if (! r.numerals.empty())
    {
        out += "\nGolden numbers\n";
        for (const auto& m : r.numerals)
            out += "  " + m.what + " ~ " + m.target + " (off by " + format ("%.2f", m.errorPercent) + " %" + (m.exact ? ", exact" : "") + ")\n";
    }
    out += "\nWhat was read (" + std::to_string (r.features.size()) + ")\n";
    for (const auto& f : r.features)
        out += "  [" + f.group + "] " + f.label + " (±" + format ("%.2g", f.sigma * 100.0) + " %)\n";
    out += "\nCaveats: a match says the numbers fit φ, not that the maker meant it. Ratios near simple fractions (8/5, 10/3)"
           " are not counted. The effect is measured as it is set: bands at 0 dB, a dry mix or a compressor below its"
           " threshold show nothing, so set it to work. Pairs of ratios from one set of numbers are not independent, so"
           " the chance figures are a guide, not a proof.\n";
    return out;
}
}

// SPDX-License-Identifier: AGPL-3.0-only
#include "PluginMeasure.h"

#include <algorithm>
#include <cmath>
#include <complex>
#include <cstdio>
#include <numeric>

namespace measure
{
namespace
{
    constexpr double pi = 3.14159265358979323846;
    using Complex = std::complex<double>;

    double toDb (double x) { return 20.0 * std::log10 (std::max (x, 1.0e-10)); }
    double fromDb (double db) { return std::pow (10.0, db / 20.0); }

    int nextPowerOfTwo (std::size_t n)
    {
        int order = 0;
        while ((std::size_t (1) << order) < n)
            ++order;
        return order;
    }

    // An FFT in double precision: the deconvolution divides by the sweep's spectrum,
    // and the harmonics it finds lie 80 dB and more below the fundamental.
    void fft (std::vector<Complex>& a, bool inverse)
    {
        const auto n = a.size();
        for (std::size_t i = 1, j = 0; i < n; ++i)
        {
            auto bit = n >> 1;
            for (; j & bit; bit >>= 1)
                j ^= bit;
            j ^= bit;
            if (i < j)
                std::swap (a[i], a[j]);
        }
        for (std::size_t len = 2; len <= n; len <<= 1)
        {
            const auto angle = 2.0 * pi / (double) len * (inverse ? 1.0 : -1.0);
            const Complex step (std::cos (angle), std::sin (angle));
            for (std::size_t start = 0; start < n; start += len)
            {
                Complex w (1.0, 0.0);
                for (std::size_t k = 0; k < len / 2; ++k)
                {
                    const auto u = a[start + k], t = w * a[start + k + len / 2];
                    a[start + k] = u + t;
                    a[start + k + len / 2] = u - t;
                    w *= step;
                }
            }
        }
        if (inverse)
            for (auto& v : a)
                v /= (double) n;
    }

    // Log-spaced display points from `from` to `to`.
    std::vector<double> logPoints (double from, double to, int count)
    {
        std::vector<double> points ((std::size_t) count);
        for (int i = 0; i < count; ++i)
            points[(std::size_t) i] = from * std::pow (to / from, i / (double) (count - 1));
        return points;
    }

    // Plays signals through the device in blocks; the device keeps its state between calls.
    struct Player
    {
        Device& device;
        const Settings& settings;
        bool broken = false;   // it gave back something that is not a number

        // Before each test: reset it, then let it fall silent too, as many effects keep their
        // delay lines through a reset (and a test tone's echoes would land in the next test).
        void clear()
        {
            device.reset();
            const auto n = (std::size_t) (std::max (0.05, settings.tailSeconds) * settings.sampleRate) + (std::size_t) std::max (0, device.reportedLatency());
            std::vector<float> l ((std::size_t) settings.block), r ((std::size_t) settings.block);
            for (std::size_t at = 0; at < n; at += (std::size_t) settings.block)
            {
                std::fill (l.begin(), l.end(), 0.0f);
                std::fill (r.begin(), r.end(), 0.0f);
                device.process (l.data(), r.data(), (int) std::min<std::size_t> ((std::size_t) settings.block, n - at));
            }
        }

        void play (const std::vector<float>& in, std::array<std::vector<float>, 2>& out)
        {
            const auto n = in.size();
            for (auto& o : out)
                o.assign (n, 0.0f);
            if (settings.input != Input::right)
                std::copy (in.begin(), in.end(), out[0].begin());
            if (settings.input != Input::left)
                std::copy (in.begin(), in.end(), out[1].begin());

            for (std::size_t at = 0; at < n; at += (std::size_t) settings.block)
            {
                const auto count = (int) std::min<std::size_t> ((std::size_t) settings.block, n - at);
                device.process (out[0].data() + at, out[1].data() + at, count);
            }
            for (auto& o : out)
                for (auto& v : o)
                    if (! std::isfinite (v))
                    {
                        v = 0.0f;
                        broken = true;
                    }
        }
    };

    // A short raised-cosine fade at both ends, so a test signal starts and stops without a click.
    void fade (std::vector<float>& signal, std::size_t inLength, std::size_t outLength, std::size_t end)
    {
        for (std::size_t i = 0; i < inLength && i < end; ++i)
            signal[i] *= (float) (0.5 - 0.5 * std::cos (pi * (double) i / (double) inLength));
        for (std::size_t i = 0; i < outLength && i < end; ++i)
            signal[end - 1 - i] *= (float) (0.5 - 0.5 * std::cos (pi * (double) i / (double) outLength));
    }

    // Blackman-Harris (4 term): its sidelobes are 92 dB down, so harmonics far below
    // the fundamental still show; the main lobe is 4 bins either side.
    std::vector<double> blackmanHarris (int n)
    {
        std::vector<double> w ((std::size_t) n);
        for (int i = 0; i < n; ++i)
        {
            const auto x = 2.0 * pi * i / n;
            w[(std::size_t) i] = 0.35875 - 0.48829 * std::cos (x) + 0.14128 * std::cos (2 * x) - 0.01168 * std::cos (3 * x);
        }
        return w;
    }

    // The spectrum of a steady tone's last `n` samples, scaled so that a bin's power
    // summed over a main lobe gives the amplitude of the sine there.
    struct ToneSpectrum
    {
        std::vector<double> power;   // per bin, |X|² / (n Σw²) × 4: a lobe sums to amplitude²
        std::vector<double> peak;    // per bin, the amplitude a sine exactly on that bin would show
        int n = 0;

        ToneSpectrum (const float* samples, int size)
            : n (size)
        {
            const auto w = blackmanHarris (n);
            double sumW = 0.0, sumW2 = 0.0;
            for (auto v : w)
            {
                sumW += v;
                sumW2 += v * v;
            }
            std::vector<Complex> x ((std::size_t) n);
            for (int i = 0; i < n; ++i)
                x[(std::size_t) i] = samples[i] * w[(std::size_t) i];
            fft (x, false);
            power.resize ((std::size_t) n / 2 + 1);
            peak.resize ((std::size_t) n / 2 + 1);
            for (int k = 0; k <= n / 2; ++k)
            {
                const auto m = std::norm (x[(std::size_t) k]);
                power[(std::size_t) k] = 4.0 * m / ((double) n * sumW2);
                peak[(std::size_t) k] = 2.0 * std::sqrt (m) / sumW;
            }
        }

        double amplitude (int bin) const   // the sine's amplitude at `bin`, from its main lobe
        {
            double sum = 0.0;
            for (int k = std::max (1, bin - 4); k <= std::min (n / 2, bin + 4); ++k)
                sum += power[(std::size_t) k];
            return std::sqrt (sum);
        }
    };

    // Fills one channel of a display spectrum: the loudest bin around each point.
    void fillDisplay (Spectrum& s, int channel, const ToneSpectrum& t, double sampleRate, double reference)
    {
        const auto binHz = sampleRate / t.n;
        const auto ratio = std::sqrt (s.hz[1] / s.hz[0]);
        auto& db = s.db[(std::size_t) channel];
        db.assign (s.hz.size(), -200.0);
        for (std::size_t i = 0; i < s.hz.size(); ++i)
        {
            auto from = (int) std::floor (s.hz[i] / ratio / binHz), to = (int) std::ceil (s.hz[i] * ratio / binHz);
            from = std::clamp (from, 1, t.n / 2);
            to = std::clamp (to, from, t.n / 2);
            double best = 0.0;
            for (int k = from; k <= to; ++k)
                best = std::max (best, t.peak[(std::size_t) k]);
            db[i] = toDb (best / reference);
        }
    }

    // Where harmonic k of a tone on bin b lands after folding at Nyquist.
    int foldedBin (long long k, long long b, long long n)
    {
        auto x = (k * b) % n;
        if (x > n / 2)
            x = n - x;
        return (int) x;
    }

    // A steady sine of amplitude `a` on `hz`, `settle` samples then `n` more; the last n of the output come back.
    std::array<std::vector<float>, 2> playTone (Player& player, double hz, double a, std::size_t settle, std::size_t n, double rate)
    {
        std::vector<float> in (settle + n);
        for (std::size_t i = 0; i < in.size(); ++i)
            in[i] = (float) (a * std::sin (2.0 * pi * hz * (double) i / rate));
        fade (in, (std::size_t) (0.02 * rate), 0, in.size());
        std::array<std::vector<float>, 2> out;
        player.play (in, out);
        for (auto& o : out)
            o.erase (o.begin(), o.begin() + (std::ptrdiff_t) settle);
        return out;
    }

    // Peaks of a sampled curve: local maxima above `floor` and at least `minProminence` above
    // the lowest point between them and any higher peak, with parabolic refinement. Index units.
    struct Peak { double at = 0.0, value = 0.0; };
    std::vector<Peak> findPeaks (const std::vector<double>& v, std::size_t from, std::size_t to, double floor, double minProminence)
    {
        std::vector<Peak> peaks;
        to = std::min (to, v.size() > 0 ? v.size() - 1 : 0);
        for (std::size_t i = std::max<std::size_t> (from, 1); i + 1 <= to && i + 1 < v.size(); ++i)
        {
            if (! (v[i] > v[i - 1] && v[i] >= v[i + 1]) || v[i] < floor)
                continue;
            // Prominence: walk out each way until something higher, keeping the lowest point.
            double leftMin = v[i], rightMin = v[i];
            for (std::size_t j = i; j-- > from;)
            {
                if (v[j] > v[i])
                    break;
                leftMin = std::min (leftMin, v[j]);
            }
            for (std::size_t j = i + 1; j <= to && j < v.size(); ++j)
            {
                if (v[j] > v[i])
                    break;
                rightMin = std::min (rightMin, v[j]);
            }
            if (v[i] - std::max (leftMin, rightMin) < minProminence)
                continue;
            const auto a = v[i - 1], b = v[i], c = v[i + 1];
            const auto d = a - 2 * b + c;
            const auto shift = std::abs (d) > 1.0e-12 ? std::clamp (0.5 * (a - c) / d, -0.5, 0.5) : 0.0;
            peaks.push_back ({ (double) i + shift, b - 0.25 * (a - c) * shift });
        }
        return peaks;
    }

    //==========================================================================
    void measureLinear (Player& player, const Settings& s, Results& r)
    {
        const auto rate = s.sampleRate;
        const auto f1 = 10.0, f2 = 0.95 * rate / 2;
        const auto sweepLength = (std::size_t) (std::max (0.25, s.sweepSeconds) * rate);
        const auto L = (double) sweepLength / rate / std::log (f2 / f1);
        const auto a = fromDb (s.levelDb);

        std::vector<float> sweep (sweepLength);
        for (std::size_t i = 0; i < sweepLength; ++i)
        {
            const auto t = (double) i / rate;
            sweep[i] = (float) (a * std::sin (2.0 * pi * f1 * L * (std::exp (t / L) - 1.0)));
        }
        fade (sweep, (std::size_t) (0.01 * rate), (std::size_t) (0.002 * rate), sweepLength);

        const auto latency = (std::size_t) std::max (0, r.reportedLatency);
        const auto tail = (std::size_t) (std::max (0.05, s.tailSeconds) * rate) + latency;
        const auto harmonicSpan = (std::size_t) (L * std::log (6.0) * rate);
        const auto total = sweepLength + tail;
        const auto order = nextPowerOfTwo (std::max (total, tail + harmonicSpan + (std::size_t) (0.05 * rate)) + 16);
        const auto n = std::size_t (1) << order;

        std::vector<float> in (total, 0.0f);
        std::copy (sweep.begin(), sweep.end(), in.begin());
        std::array<std::vector<float>, 2> out;
        player.play (in, out);

        std::vector<Complex> x (n);
        for (std::size_t i = 0; i < sweepLength; ++i)
            x[i] = sweep[i];
        fft (x, false);

        // The inverse of the sweep, regularised outside its band so nothing blows up there.
        double maxPower = 0.0;
        for (const auto& v : x)
            maxPower = std::max (maxPower, std::norm (v));
        std::vector<Complex> inverse (n);
        for (std::size_t k = 0; k < n; ++k)
        {
            const auto bin = k <= n / 2 ? k : n - k;
            const auto hz = (double) bin * rate / (double) n;
            // Eased in over the octave above f1 (and the last 3% below f2), in decibels: a sharp
            // edge would ring for longer than the impulse window keeps, and bend the bass.
            const auto ease = [] (double t) { t = std::clamp (t, 0.0, 1.0); return 0.5 - 0.5 * std::cos (pi * t); };
            const auto edge = std::min (hz <= 0.0 ? 0.0 : ease (std::log2 (hz / f1)), ease ((f2 - hz) / (0.03 * f2)));
            const auto epsilon = maxPower * std::pow (10.0, -2.0 - 8.0 * edge);
            inverse[k] = std::conj (x[k]) / (std::norm (x[k]) + epsilon);
        }

        std::array<std::vector<Complex>, 2> h;
        double outEnergy = 0.0;
        for (int c = 0; c < 2; ++c)
        {
            auto& y = h[(std::size_t) c];
            y.assign (n, Complex());
            for (std::size_t i = 0; i < total; ++i)
            {
                y[i] = out[(std::size_t) c][i];
                outEnergy += (double) out[(std::size_t) c][i] * out[(std::size_t) c][i];
            }
            fft (y, false);
            for (std::size_t k = 0; k < n; ++k)
                y[k] *= inverse[k];
            fft (y, true);
        }

        if (outEnergy < 1.0e-14)
        {
            r.silent = true;
            return;
        }

        // The impulse response: from a little before no delay (a band-limited impulse rings
        // both ways, and linear-phase effects ring ahead) to the tail's end. Its peak is the latency.
        const auto pre = (std::size_t) std::min (0.005 * rate, 0.25 * L * std::log (2.0) * rate);
        r.impulsePre = (int) pre;
        double peak = 0.0;
        std::size_t peakAt = 0;
        for (int c = 0; c < 2; ++c)
        {
            auto& ir = r.impulse[(std::size_t) c];
            ir.resize (pre + tail);
            for (std::size_t i = 0; i < pre + tail; ++i)
            {
                ir[i] = (float) h[(std::size_t) c][(i + n - pre) % n].real();
                if (std::abs (ir[i]) > peak)
                {
                    peak = std::abs (ir[i]);
                    peakAt = i;
                }
            }
        }
        {
            // Refine on whichever channel holds it.
            double best = 0.0;
            int channel = 0;
            for (int c = 0; c < 2; ++c)
                if (std::abs (r.impulse[(std::size_t) c][peakAt]) > best)
                {
                    best = std::abs (r.impulse[(std::size_t) c][peakAt]);
                    channel = c;
                }
            const auto& ir = r.impulse[(std::size_t) channel];
            double shift = 0.0;
            if (peakAt > 0 && peakAt + 1 < ir.size())
            {
                const auto p = std::abs (ir[peakAt - 1]), q = std::abs (ir[peakAt]), u = std::abs (ir[peakAt + 1]);
                const auto d = p - 2 * q + u;
                if (std::abs (d) > 1.0e-12)
                    shift = std::clamp (0.5 * (p - u) / d, -0.5, 0.5);
            }
            r.measuredLatency = (double) peakAt + shift - (double) pre;
        }

        // The response: the impulse response's spectrum, with the reported latency taken
        // out of the phase (as a host compensates it), on bins fine enough for narrow peaks.
        const auto fineOrder = std::max (16, nextPowerOfTwo (pre + tail));
        const auto fineN = std::size_t (1) << fineOrder;
        r.binHz = rate / (double) fineN;
        r.hz = logPoints (f1, rate / 2, 512);
        const auto delay = (double) r.reportedLatency;

        // What the window does to a perfect wire: the deconvolution's band edges ring both
        // ways, and cutting that ringing off bends the bass. Dividing by it calibrates it out.
        std::vector<Complex> calibration (n);
        for (std::size_t k = 0; k < n; ++k)
            calibration[k] = x[k] * inverse[k];
        fft (calibration, true);
        {
            std::vector<Complex> windowed (fineN);
            for (std::size_t i = 0; i < pre + tail && i < fineN; ++i)
                windowed[i] = calibration[(i + n - pre) % n].real();
            fft (windowed, false);
            calibration.assign (fineN / 2 + 1, Complex (1.0));
            for (std::size_t k = 0; k <= fineN / 2; ++k)
            {
                const auto c = windowed[k] * std::polar (1.0, 2.0 * pi * (double) k * (double) pre / (double) fineN);
                if (std::abs (c) > 0.5)
                    calibration[k] = c;
            }
        }

        std::array<std::vector<Complex>, 2> spectra;
        for (int c = 0; c < 2; ++c)
        {
            auto& spectrum = spectra[(std::size_t) c];
            spectrum.assign (fineN, Complex());
            for (std::size_t i = 0; i < pre + tail && i < fineN; ++i)
                spectrum[i] = r.impulse[(std::size_t) c][i];
            fft (spectrum, false);
            spectrum.resize (fineN / 2 + 1);
            for (std::size_t k = 0; k <= fineN / 2; ++k)
                spectrum[k] *= std::polar (1.0, 2.0 * pi * (double) k * (delay + (double) pre) / (double) fineN) / calibration[k];

            auto& fine = r.fineMagnitudeDb[(std::size_t) c];
            auto& fineGd = r.fineGroupDelayMs[(std::size_t) c];
            fine.resize (fineN / 2 + 1);
            fineGd.assign (fineN / 2 + 1, 0.0);
            for (std::size_t k = 0; k <= fineN / 2; ++k)
                fine[k] = toDb (std::abs (spectrum[k]));
            const auto dOmega = 2.0 * 2.0 * pi * r.binHz;   // across two bins
            for (std::size_t k = 1; k < fineN / 2; ++k)
                fineGd[k] = -std::arg (spectrum[k + 1] * std::conj (spectrum[k - 1])) / dOmega * 1000.0;

            // Display points: the power over each point's band (or between two bins, where the band is narrower).
            const auto ratio = std::sqrt (r.hz[1] / r.hz[0]);
            auto& mag = r.magnitudeDb[(std::size_t) c];
            auto& phase = r.phaseDeg[(std::size_t) c];
            auto& gd = r.groupDelayMs[(std::size_t) c];
            mag.resize (r.hz.size());
            phase.resize (r.hz.size());
            gd.resize (r.hz.size());
            for (std::size_t i = 0; i < r.hz.size(); ++i)
            {
                const auto centre = r.hz[i] / r.binHz;
                const auto from = std::max (1, (int) std::ceil (r.hz[i] / ratio / r.binHz));
                const auto to = std::min ((int) (fineN / 2), (int) std::floor (r.hz[i] * ratio / r.binHz));
                if (to - from >= 1)
                {
                    double power = 0.0, delaySum = 0.0;
                    Complex sum;
                    for (int k = from; k <= to; ++k)
                    {
                        power += std::norm (spectrum[(std::size_t) k]);
                        sum += spectrum[(std::size_t) k];
                        delaySum += fineGd[(std::size_t) k];
                    }
                    mag[i] = toDb (std::sqrt (power / (to - from + 1)));
                    phase[i] = std::arg (sum) * 180.0 / pi;
                    gd[i] = delaySum / (to - from + 1);
                }
                else
                {
                    const auto k = std::clamp ((int) centre, 1, (int) (fineN / 2) - 1);
                    const auto frac = std::clamp (centre - k, 0.0, 1.0);
                    const auto v = spectrum[(std::size_t) k] * (1.0 - frac) + spectrum[(std::size_t) k + 1] * frac;
                    mag[i] = toDb (std::abs (v));
                    phase[i] = std::arg (v) * 180.0 / pi;
                    gd[i] = fineGd[(std::size_t) k] * (1.0 - frac) + fineGd[(std::size_t) k + 1] * frac;
                }
            }
        }

        // The harmonics, each its own impulse response L ln k ahead of the linear one
        // (wrapped round to the end of the deconvolution). Hk at k·f is harmonic k of a tone at f.
        for (int c = 0; c < 2; ++c)
        {
            auto& harmonics = r.sweepHarmonicDb[(std::size_t) c];
            harmonics.assign (4, std::vector<double> (r.hz.size(), -200.0));
            for (int k = 2; k <= 5; ++k)
            {
                const auto at = (double) n - L * std::log ((double) k) * rate + (double) latency;
                const auto before = 0.5 * L * (std::log (k + 1.0) - std::log ((double) k)) * rate;
                const auto after = 0.5 * L * (std::log ((double) k) - std::log (k - 1.0)) * rate;
                const auto length = (std::size_t) (before + after);
                const auto order2 = nextPowerOfTwo (std::max<std::size_t> (length, 4096));
                const auto m = std::size_t (1) << order2;
                std::vector<Complex> seg (m);
                for (std::size_t i = 0; i < length; ++i)
                {
                    const auto index = (std::size_t) ((long long) (at - before) + (long long) i) % n;
                    // Tukey taper: flat in the middle, a quarter faded at each end.
                    const auto pos = (double) i / (double) length;
                    const auto taper = pos < 0.1 ? 0.5 - 0.5 * std::cos (pi * pos / 0.1)
                                     : pos > 0.9 ? 0.5 - 0.5 * std::cos (pi * (1.0 - pos) / 0.1) : 1.0;
                    seg[i] = h[(std::size_t) c][index] * taper;
                }
                fft (seg, false);
                for (std::size_t i = 0; i < r.hz.size(); ++i)
                {
                    const auto hz = r.hz[i] * k;
                    if (hz >= f2 || r.hz[i] < 20.0)
                        continue;
                    const auto bin = std::clamp ((std::size_t) std::lround (hz * (double) m / rate), (std::size_t) 1, m / 2);
                    harmonics[(std::size_t) k - 2][i] = toDb (std::abs (seg[bin])) - r.magnitudeDb[(std::size_t) c][i];
                }
            }
        }
        r.hasLinear = true;
    }

    //==========================================================================
    void measureHarmonics (Player& player, const Settings& s, Results& r)
    {
        const auto rate = s.sampleRate;
        const auto n = 1 << s.fftOrder;
        const auto hz = coherentHz (s.sineHz, rate, s.fftOrder);
        const auto bin = (int) std::lround (hz * n / rate);
        const auto a = fromDb (s.levelDb);
        const auto settle = (std::size_t) (std::max (0.5, s.tailSeconds) * rate) + (std::size_t) std::max (0, r.reportedLatency);
        auto out = playTone (player, hz, a, settle, (std::size_t) n, rate);

        r.sineSpectrum.toneHz = hz;
        r.sineSpectrum.hz = logPoints (10.0, rate / 2, 1024);
        r.otherLines.clear();
        double strongest = 0.0;
        for (int c = 0; c < 2; ++c)
        {
            const ToneSpectrum t (out[(std::size_t) c].data(), n);
            fillDisplay (r.sineSpectrum, c, t, rate, a);
            const auto h1 = t.amplitude (bin);
            auto& list = r.harmonicDb[(std::size_t) c];
            list.clear();
            list.push_back (toDb (h1 / a));
            r.gainDb[(std::size_t) c] = toDb (h1 / a);
            double harmonicPower = 0.0;
            for (int k = 2; (long long) k * bin < n / 2 - 4; ++k)
            {
                const auto hk = t.amplitude (k * bin);
                harmonicPower += hk * hk;
                if (k <= 10)
                    list.push_back (toDb (hk / std::max (h1, 1.0e-12)));
            }
            double total = 0.0;
            for (int k = 5; k <= n / 2; ++k)
                total += t.power[(std::size_t) k];
            const auto fundamental = h1 * h1;
            r.thdPercent[(std::size_t) c] = h1 > 1.0e-9 ? 100.0 * std::sqrt (harmonicPower) / h1 : 0.0;
            r.thdnPercent[(std::size_t) c] = h1 > 1.0e-9 ? 100.0 * std::sqrt (std::max (0.0, total - fundamental)) / h1 : 0.0;

            // What is neither a harmonic nor a folded one: a pitch shifter's copies, a ring modulator, noise tones.
            if (h1 <= strongest * 0.1)
                continue;
            std::vector<double> db (t.peak.size());
            for (std::size_t k = 0; k < db.size(); ++k)
                db[k] = toDb (t.peak[k] / std::max (h1, 1.0e-12));
            std::vector<int> explained;
            for (long long k = 1; k <= 200; ++k)
                explained.push_back (foldedBin (k, bin, n));
            for (const auto& p : findPeaks (db, 8, (std::size_t) n / 2 - 2, -90.0, 10.0))
            {
                const auto at = (int) std::lround (p.at);
                const auto near = std::any_of (explained.begin(), explained.end(), [at] (int e) { return std::abs (e - at) <= 6; });
                if (near)
                    continue;
                const auto lineHz = p.at * rate / n;
                const auto same = std::any_of (r.otherLines.begin(), r.otherLines.end(), [&] (const Line& l) { return std::abs (l.hz - lineHz) < 3.0 * rate / n; });
                if (! same && r.otherLines.size() < 48)
                    r.otherLines.push_back ({ lineHz, p.value, 0, false });
            }
            strongest = std::max (strongest, h1);
        }
        std::sort (r.otherLines.begin(), r.otherLines.end(), [] (const Line& x, const Line& y) { return x.db > y.db; });
        r.hasHarmonics = true;
    }

    //==========================================================================
    void measureAliasing (Player& player, const Settings& s, Results& r, const std::function<bool (double)>& step)
    {
        const auto rate = s.sampleRate;
        const auto order = std::min (s.fftOrder, 15);
        const auto n = 1 << order;
        const auto a = fromDb (s.levelDb);
        const auto settle = (std::size_t) (std::max (0.25, s.tailSeconds) * rate) + (std::size_t) std::max (0, r.reportedLatency);

        std::vector<double> tones;
        for (auto t : s.aliasTones)
            if (t > 20.0 && t < 0.98 * rate / 2)
                tones.push_back (t);
        double keep = tones.empty() ? 0.0 : tones.front();
        for (auto t : tones)
            if (std::abs (t - s.aliasSpectrumHz) < std::abs (keep - s.aliasSpectrumHz))
                keep = t;

        r.alias.clear();
        for (std::size_t i = 0; i < tones.size(); ++i)
        {
            if (step && ! step ((double) i / (double) tones.size()))
                return;
            player.clear();
            const auto hz = coherentHz (tones[i], rate, order);
            const auto bin = (int) std::lround (hz * n / rate);
            auto out = playTone (player, hz, a, settle, (std::size_t) n, rate);

            AliasPoint point;
            point.toneHz = hz;
            double worstAll = 0.0;
            const auto kept = tones[i] == keep;
            if (kept)
            {
                r.aliasSpectrum.toneHz = hz;
                r.aliasSpectrum.hz = logPoints (10.0, rate / 2, 1024);
                r.aliasLines.clear();
            }
            for (int c = 0; c < 2; ++c)
            {
                const ToneSpectrum t (out[(std::size_t) c].data(), n);
                const auto h1 = t.amplitude (bin);
                if (kept)
                    fillDisplay (r.aliasSpectrum, c, t, rate, a);
                if (h1 < 1.0e-9)
                    continue;

                std::vector<int> harmonicBins;
                for (long long k = 1; k * bin < n / 2; ++k)
                    harmonicBins.push_back ((int) (k * bin));
                double harmonicPower = 0.0;
                for (std::size_t k = 1; k < harmonicBins.size(); ++k)
                {
                    const auto v = t.amplitude (harmonicBins[k]);
                    harmonicPower += v * v;
                    if (kept && c == 0 && k < 12)
                        r.aliasLines.push_back ({ harmonicBins[k] * rate / n, toDb (v / h1), (int) k + 1, false });
                }

                std::vector<int> seen;
                double aliasPower = 0.0;
                for (long long k = (long long) harmonicBins.size() + 1; k <= 200; ++k)
                {
                    const auto f = foldedBin (k, bin, n);
                    if (f < 5 || std::find (seen.begin(), seen.end(), f) != seen.end())
                        continue;
                    if (std::any_of (harmonicBins.begin(), harmonicBins.end(), [f] (int b) { return std::abs (b - f) <= 8; }))
                        continue;
                    seen.push_back (f);
                    const auto v = t.amplitude (f);
                    aliasPower += v * v;
                    if (v > worstAll)
                    {
                        worstAll = v;
                        point.worstHz = f * rate / n;
                    }
                    point.worstDb[(std::size_t) c] = std::max (point.worstDb[(std::size_t) c], toDb (v / h1));
                    if (kept && c == 0 && toDb (v / h1) > -130.0 && r.aliasLines.size() < 64)
                        r.aliasLines.push_back ({ f * rate / n, toDb (v / h1), (int) k, true });
                }
                point.aliasDb[(std::size_t) c] = toDb (std::sqrt (aliasPower) / h1);
                point.harmonicDb[(std::size_t) c] = toDb (std::sqrt (harmonicPower) / h1);
            }
            r.alias.push_back (point);
        }
        r.hasAliasing = ! r.alias.empty();
    }

    //==========================================================================
    // A tone on a whole number of samples per cycle, so windows of whole cycles read it exactly.
    struct WholeTone
    {
        int period;   // samples per cycle
        double hz;
        explicit WholeTone (double rate) : period (std::max (8, (int) std::lround (rate / 1000.0))), hz (rate / period) {}
    };

    // The amplitude and phase of the tone over a window of whole cycles (one DFT bin).
    Complex readTone (const float* x, int length, int period)
    {
        Complex sum;
        for (int i = 0; i < length; ++i)
            sum += (double) x[i] * std::polar (1.0, -2.0 * pi * (i % period) / period);
        return sum * (2.0 / length);
    }

    void measureDynamics (Player& player, const Settings& s, Results& r)
    {
        const auto rate = s.sampleRate;
        const WholeTone tone (rate);
        const auto settle = (std::size_t) (s.dynamicsSettleMs / 1000.0 * rate) / (std::size_t) tone.period * (std::size_t) tone.period;
        const auto window = (std::size_t) tone.period * (std::size_t) std::max (8, (int) (0.04 * rate / tone.period));
        const auto latency = (std::size_t) std::max (0, r.reportedLatency);

        // The level climbs step by step, without stopping the tone, as a compressor would hear it.
        std::vector<double> levels;
        for (auto db = s.dynamicsFromDb; db <= s.dynamicsToDb + 1.0e-9; db += std::max (0.25, s.dynamicsStepDb))
            levels.push_back (db);
        const auto each = settle + window;
        // A second at the first level before anything is read, so detectors that start from silence have settled.
        const auto lead = (std::size_t) rate / (std::size_t) tone.period * (std::size_t) tone.period;
        std::vector<float> in (lead + each * levels.size() + latency + window);
        const auto ramp = (std::size_t) (0.005 * rate);
        for (std::size_t i = 0; i < lead; ++i)
            in[i] = (float) (fromDb (levels.front()) * std::sin (2.0 * pi * (double) (i % (std::size_t) tone.period) / tone.period));
        double previous = fromDb (levels.front());
        for (std::size_t l = 0; l < levels.size(); ++l)
        {
            const auto target = fromDb (levels[l]);
            for (std::size_t i = 0; i < each; ++i)
            {
                const auto at = lead + l * each + i;
                const auto g = i < ramp && l > 0 ? previous + (target - previous) * (double) i / (double) ramp : target;
                in[at] = (float) (g * std::sin (2.0 * pi * (double) (at % (std::size_t) tone.period) / tone.period));
            }
            previous = target;
        }
        fade (in, (std::size_t) (0.01 * rate), 0, in.size());
        std::array<std::vector<float>, 2> out;
        player.play (in, out);

        r.levelInDb = levels;
        for (int c = 0; c < 2; ++c)
        {
            auto& list = r.levelOutDb[(std::size_t) c];
            list.clear();
            for (std::size_t l = 0; l < levels.size(); ++l)
            {
                const auto start = lead + l * each + settle + latency;
                list.push_back (toDb (std::abs (readTone (out[(std::size_t) c].data() + start, (int) window, tone.period))));
            }
        }

        // The curve: averaged over the channels that carry sound.
        std::vector<double> curve (levels.size(), 0.0);
        for (std::size_t l = 0; l < levels.size(); ++l)
        {
            int used = 0;
            for (int c = 0; c < 2; ++c)
                if (r.levelOutDb[(std::size_t) c][l] > -150.0)
                {
                    curve[l] += r.levelOutDb[(std::size_t) c][l];
                    ++used;
                }
            curve[l] = used > 0 ? curve[l] / used : -200.0;
        }
        r.ratio = 1.0;
        r.thresholdDb = 0.0;
        if (levels.size() >= 4 && curve.front() > -150.0)
        {
            // The gain at low levels: the median of the first few, so one odd reading does not count.
            std::vector<double> lows;
            for (std::size_t l = 0; l < std::min<std::size_t> (3, levels.size()); ++l)
                lows.push_back (curve[l] - levels[l]);
            std::sort (lows.begin(), lows.end());
            const auto lowGain = lows[lows.size() / 2];
            std::size_t bend = levels.size();
            for (std::size_t l = 0; l < levels.size(); ++l)
                if (std::abs ((curve[l] - levels[l]) - lowGain) > 1.0)
                {
                    bend = l;
                    break;
                }
            if (bend < levels.size() - 1)
            {
                r.thresholdDb = levels[bend > 0 ? bend - 1 : 0];
                // A least-squares line through the upper half of the curve above the bend (past any knee).
                auto from = std::min (std::max (bend + 2, (bend + levels.size()) / 2), levels.size() - 3);
                double sx = 0, sy = 0, sxx = 0, sxy = 0;
                int count = 0;
                for (auto l = from; l < levels.size(); ++l)
                {
                    sx += levels[l];
                    sy += curve[l];
                    sxx += levels[l] * levels[l];
                    sxy += levels[l] * curve[l];
                    ++count;
                }
                const auto denominator = count * sxx - sx * sx;
                if (count >= 2 && std::abs (denominator) > 1.0e-9)
                {
                    const auto slope = (count * sxy - sx * sy) / denominator;
                    r.ratio = slope > 0.01 ? 1.0 / slope : 100.0;
                }
            }
        }

        // The level step: low, high, low again; the gain the effect gives, read every two cycles.
        player.clear();
        const auto low = fromDb (s.stepLowDb), high = fromDb (s.stepHighDb);
        const auto before = (std::size_t) (1.0 * rate), on = (std::size_t) (1.5 * rate), after = (std::size_t) (2.0 * rate);
        const auto align = [&] (std::size_t v) { return v / (std::size_t) tone.period * (std::size_t) tone.period; };
        const auto upAt = align (before), downAt = align (before + on), end = align (before + on + after);
        std::vector<float> stepIn (end + latency + (std::size_t) tone.period * 4);
        std::vector<double> inAmplitude (stepIn.size());
        for (std::size_t i = 0; i < stepIn.size(); ++i)
        {
            const auto g = i >= upAt && i < downAt ? high : low;
            inAmplitude[i] = g;
            stepIn[i] = (float) (g * std::sin (2.0 * pi * (double) (i % (std::size_t) tone.period) / tone.period));
        }
        fade (stepIn, (std::size_t) (0.01 * rate), 0, stepIn.size());
        player.play (stepIn, out);

        const auto hop = (std::size_t) tone.period * 2;
        r.stepMs.clear();
        for (auto& g : r.stepGainDb)
            g.clear();
        for (std::size_t at = upAt - align ((std::size_t) (0.1 * rate)); at + hop <= end; at += hop)
        {
            r.stepMs.push_back (((double) at - (double) upAt) / rate * 1000.0);
            for (int c = 0; c < 2; ++c)
            {
                const auto v = std::abs (readTone (out[(std::size_t) c].data() + at + latency, (int) hop, tone.period));
                r.stepGainDb[(std::size_t) c].push_back (toDb (v / inAmplitude[at]));
            }
        }

        // Attack and release: to 63 % of the change in gain, on the louder channel.
        const auto channel = r.levelOutDb[1].back() > r.levelOutDb[0].back() ? 1 : 0;
        const auto& g = r.stepGainDb[(std::size_t) channel];
        auto mean = [&] (double fromMs, double toMs)
        {
            double sum = 0.0;
            int count = 0;
            for (std::size_t i = 0; i < g.size(); ++i)
                if (r.stepMs[i] >= fromMs && r.stepMs[i] < toMs)
                {
                    sum += g[i];
                    ++count;
                }
            return count > 0 ? sum / count : 0.0;
        };
        const auto onMs = (double) (downAt - upAt) / rate * 1000.0;
        const auto g0 = mean (-100.0, -5.0), g1 = mean (onMs - 200.0, onMs - 5.0), g2 = mean (onMs + 1700.0, onMs + 2000.0);
        auto timeTo = [&] (double fromMs, double start, double target)
        {
            for (std::size_t i = 0; i < g.size(); ++i)
                if (r.stepMs[i] >= fromMs && ((target < start && g[i] <= target) || (target > start && g[i] >= target)))
                    return std::max (0.0, r.stepMs[i] - fromMs);
            return 0.0;
        };
        r.attackMs = std::abs (g1 - g0) > 0.5 ? timeTo (0.0, g0, g0 + 0.632 * (g1 - g0)) : 0.0;
        r.releaseMs = std::abs (g2 - g1) > 0.5 ? timeTo (onMs, g1, g1 + 0.632 * (g2 - g1)) : 0.0;
        r.hasDynamics = true;
    }

    //==========================================================================
    void measureModulation (Player& player, const Settings& s, Results& r)
    {
        const auto rate = s.sampleRate;
        const WholeTone tone (rate);
        const auto a = fromDb (s.levelDb);
        const auto length = (std::size_t) (std::max (2.0, s.modulationSeconds) * rate);
        std::vector<float> in (length + (std::size_t) std::max (0, r.reportedLatency));
        for (std::size_t i = 0; i < in.size(); ++i)
            in[i] = (float) (a * std::sin (2.0 * pi * (double) (i % (std::size_t) tone.period) / tone.period));
        fade (in, (std::size_t) (0.01 * rate), 0, in.size());
        std::array<std::vector<float>, 2> out;
        player.play (in, out);

        const auto hop = tone.period * 2;
        const auto envRate = rate / hop;
        const auto skip = (std::size_t) (0.5 * rate) + (std::size_t) std::max (0, r.reportedLatency);
        const auto count = (length - (std::size_t) (0.5 * rate)) / (std::size_t) hop;
        r.envelopeMs.clear();
        std::array<std::vector<double>, 2> amplitude, cents;
        for (std::size_t i = 0; i < count; ++i)
            r.envelopeMs.push_back ((double) i * hop / rate * 1000.0);
        for (int c = 0; c < 2; ++c)
        {
            auto& env = r.envelopeDb[(std::size_t) c];
            env.clear();
            double lastPhase = 0.0;
            for (std::size_t i = 0; i < count; ++i)
            {
                const auto v = readTone (out[(std::size_t) c].data() + skip + i * (std::size_t) hop, hop, tone.period);
                amplitude[(std::size_t) c].push_back (std::abs (v));
                env.push_back (toDb (std::abs (v) / a));
                const auto phase = std::arg (v);
                if (i > 0)
                {
                    // The phase moves when the pitch does: cents from how far it turned in one hop.
                    auto d = phase - lastPhase;
                    while (d > pi) d -= 2 * pi;
                    while (d < -pi) d += 2 * pi;
                    const auto hz = tone.hz + d / (2 * pi) * envRate;
                    cents[(std::size_t) c].push_back (std::abs (v) > a * 1.0e-3 ? 1200.0 * std::log2 (std::max (1.0, hz) / tone.hz) : 0.0);
                }
                lastPhase = phase;
            }
        }

        // How much level and pitch move, on the channel that moves most.
        r.modulationDepthDb = 0.0;
        r.pitchDepthCents = 0.0;
        int channel = 0;
        for (int c = 0; c < 2; ++c)
        {
            const auto& env = r.envelopeDb[(std::size_t) c];
            if (env.empty())
                continue;
            const auto [lo, hi] = std::minmax_element (env.begin(), env.end());
            const auto depth = std::min (*hi, 20.0) - std::max (*lo, -100.0);
            if (depth > r.modulationDepthDb)
            {
                r.modulationDepthDb = depth;
                channel = c;
            }
            // Pitch: the spread between the 1st and 99th percentiles, so a single glitch does not count.
            auto sorted = cents[(std::size_t) c];
            if (sorted.size() > 10)
            {
                std::sort (sorted.begin(), sorted.end());
                const auto spread = sorted[sorted.size() * 99 / 100] - sorted[sorted.size() / 100];
                r.pitchDepthCents = std::max (r.pitchDepthCents, spread);
            }
        }

        // LFO rates: peaks in the spectrum of the level (and of the pitch, when it moves).
        const auto order = nextPowerOfTwo (count * 8);
        const auto m = std::size_t (1) << order;
        r.modulationResolutionHz = envRate / (double) count;
        std::vector<Peak> found;
        auto rates = [&] (const std::vector<double>& trace)
        {
            if (trace.size() < 16)
                return;
            const auto mean = std::accumulate (trace.begin(), trace.end(), 0.0) / (double) trace.size();
            std::vector<Complex> x (m);
            for (std::size_t i = 0; i < trace.size(); ++i)
                x[i] = (trace[i] - mean) * (0.5 - 0.5 * std::cos (2.0 * pi * (double) i / (double) trace.size()));
            fft (x, false);
            std::vector<double> mag (m / 2);
            for (std::size_t k = 0; k < m / 2; ++k)
                mag[k] = std::abs (x[k]);
            const auto from = (std::size_t) std::ceil (0.05 * (double) m / envRate), to = (std::size_t) (30.0 * (double) m / envRate);
            double top = 0.0;
            for (auto k = from; k < std::min (to, mag.size()); ++k)
                top = std::max (top, mag[k]);
            for (const auto& p : findPeaks (mag, from, to, top * 0.15, top * 0.05))
                found.push_back ({ p.at * envRate / (double) m, p.value / std::max (top, 1.0e-12) });
        };
        r.modulationHz.clear();
        if (r.modulationDepthDb > 0.5)
            rates (amplitude[(std::size_t) channel]);
        if (r.pitchDepthCents > 3.0)
            for (int c = 0; c < 2; ++c)
                rates (cents[(std::size_t) c]);
        std::sort (found.begin(), found.end(), [] (const Peak& x, const Peak& y) { return x.value > y.value; });
        for (const auto& p : found)
        {
            const auto same = std::any_of (r.modulationHz.begin(), r.modulationHz.end(),
                                           [&] (double h) { return std::abs (h - p.at) < r.modulationResolutionHz * 1.5; });
            if (! same && r.modulationHz.size() < 8)
                r.modulationHz.push_back (p.at);
        }

        // A gate: the level goes fully on and off. Count its open and closed steps.
        r.gated = false;
        const auto& level = amplitude[(std::size_t) channel];
        if (r.modulationDepthDb > 20.0 && level.size() > 32)
        {
            // Open where the tone is above half its loudest (fades and their tails don't stretch the runs).
            const auto loudest = *std::max_element (level.begin(), level.end());
            std::vector<std::pair<bool, int>> runs;
            for (auto v : level)
            {
                const auto open = v > 0.5 * loudest;
                if (runs.empty() || runs.back().first != open)
                    runs.push_back ({ open, 0 });
                ++runs.back().second;
            }
            if (runs.size() > 6)
            {
                runs.erase (runs.begin());   // partial at both ends
                runs.pop_back();
                // From one opening to the next is a whole number of steps, whatever the fades do.
                std::vector<double> periods;
                for (std::size_t i = 0; i + 1 < runs.size(); ++i)
                    if (runs[i].first)
                        periods.push_back ((double) (runs[i].second + runs[i + 1].second));
                if (periods.size() >= 3)
                {
                    // The step: the longest that fits every period a whole number of times.
                    const auto shortest = *std::min_element (periods.begin(), periods.end());
                    double step = 0.0;
                    for (auto s = shortest; s >= shortest / 4.0 && step == 0.0; s *= 0.998)
                    {
                        double error = 0.0;
                        for (auto p : periods)
                            error += std::pow (p / s - std::round (p / s), 2.0);
                        if (std::sqrt (error / (double) periods.size()) < 0.06)
                            step = s;
                    }
                    if (step > 0.0)
                    {
                        // Counted over whole periods only (an opening to the next).
                        double total = 0.0;
                        int open = 0;
                        for (std::size_t i = 0; i + 1 < runs.size(); ++i)
                            if (runs[i].first)
                            {
                                total += std::round ((runs[i].second + runs[i + 1].second) / step);
                                open += (int) std::max (1.0, std::round (runs[i].second / step));
                            }
                        r.gated = true;
                        r.gateStepMs = step * hop / rate * 1000.0;
                        r.gateOpenSteps = open;
                        r.gateClosedSteps = std::max (0, (int) total - open);
                    }
                }
            }
        }
        r.hasModulation = true;
    }

    std::string format (const char* pattern, double value)
    {
        char text[64];
        std::snprintf (text, sizeof (text), pattern, value);
        return text;
    }
}

double coherentHz (double hz, double sampleRate, int fftOrder)
{
    const auto n = 1 << fftOrder;
    auto bin = std::max (3, (int) std::lround (hz * n / sampleRate));
    if (bin % 2 == 0)
        ++bin;
    return bin * sampleRate / n;
}

Results run (Device& device, const Settings& settings, const std::function<bool (double)>& progress)
{
    Results r;
    r.sampleRate = settings.sampleRate;
    r.reportedLatency = device.reportedLatency();
    Player player { device, settings };

    // Rough shares of the time each test takes, for the progress.
    struct Step { bool on; double share; };
    const std::array<Step, 5> steps { { { settings.linear, 3.0 }, { settings.harmonics, 2.0 },
                                        { settings.aliasing, (double) settings.aliasTones.size() * 1.1 },
                                        { settings.dynamics, 5.0 }, { settings.modulation, settings.modulationSeconds / 2.0 } } };
    double total = 0.0, done = 0.0;
    for (const auto& st : steps)
        total += st.on ? st.share : 0.0;
    auto report = [&] (double within, std::size_t index)
    {
        return ! progress || progress ((done + within * steps[index].share) / std::max (total, 1.0e-9));
    };
    auto next = [&] (std::size_t index) { done += steps[index].on ? steps[index].share : 0.0; };

    if (settings.linear && report (0.0, 0))
    {
        player.clear();
        measureLinear (player, settings, r);
    }
    next (0);
    if (settings.harmonics && report (0.0, 1))
    {
        player.clear();
        measureHarmonics (player, settings, r);
    }
    next (1);
    if (settings.aliasing && report (0.0, 2))
        measureAliasing (player, settings, r, [&] (double within) { return report (within, 2); });
    next (2);
    if (settings.dynamics && report (0.0, 3))
    {
        player.clear();
        measureDynamics (player, settings, r);
    }
    next (3);
    if (settings.modulation && report (0.0, 4))
    {
        player.clear();
        measureModulation (player, settings, r);
    }
    next (4);
    if (progress)
        progress (1.0);

    // What the reader should know.
    if (player.broken)
        r.notes.push_back ("The effect gave back values that are not numbers (NaN or infinity); they were read as silence.");
    if (r.silent)
        r.notes.push_back ("Nothing came out of the effect during the sweep: it may need a key, a side chain or MIDI to make sound.");
    if (r.hasLinear && ! r.silent && std::abs (r.measuredLatency - r.reportedLatency) > 1.5 && r.measuredLatency < 0.1 * r.sampleRate)
        r.notes.push_back ("It reports " + std::to_string (r.reportedLatency) + " samples of latency, but its impulse peaks at "
                           + format ("%.1f", r.measuredLatency) + ": hosts will line it up wrongly by the difference"
                           " (unless that is the sound itself, such as an echo with no dry signal).");
    if (r.hasModulation && (r.modulationDepthDb > 1.0 || r.pitchDepthCents > 5.0))
        r.notes.push_back ("It changes on its own over time (an LFO, a gate or a rhythm): the frequency response is an average of the sweep's two seconds.");
    if (r.hasHarmonics && std::max (r.thdPercent[0], r.thdPercent[1]) > 1.0)
        r.notes.push_back ("It distorts at this level (THD " + format ("%.1f", std::max (r.thdPercent[0], r.thdPercent[1]))
                           + " %): its frequency response depends on how loud the sound is; try another level.");
    if (r.hasDynamics && r.ratio > 1.2)
        r.notes.push_back ("It compresses above about " + format ("%.0f", r.thresholdDb) + " dBFS: tests at louder levels hear it working.");
    return r;
}

std::string report (const Results& r, const std::string& title)
{
    std::string text = title + "\n" + std::string (title.size(), '=') + "\n\n";
    auto line = [&] (const std::string& s) { text += s + "\n"; };
    line ("Sample rate " + format ("%.0f", r.sampleRate) + " Hz. Latency reported " + std::to_string (r.reportedLatency)
          + " samples" + (r.hasLinear ? ", impulse peaks at " + format ("%.1f", r.measuredLatency) + " samples ("
                                        + format ("%.2f", r.measuredLatency / r.sampleRate * 1000.0) + " ms)" : std::string()) + ".");
    for (const auto& n : r.notes)
        line ("Note: " + n);

    if (r.hasLinear)
    {
        line ("\nFrequency response (left / right)");
        for (double hz : { 20.0, 50.0, 100.0, 200.0, 500.0, 1000.0, 2000.0, 5000.0, 10000.0, 15000.0, 20000.0 })
        {
            if (hz >= r.sampleRate / 2)
                break;
            const auto it = std::lower_bound (r.hz.begin(), r.hz.end(), hz);
            const auto i = (std::size_t) std::min<std::ptrdiff_t> (it - r.hz.begin(), (std::ptrdiff_t) r.hz.size() - 1);
            char row[160];
            std::snprintf (row, sizeof (row), "  %7.0f Hz  %+7.2f / %+7.2f dB   phase %+7.1f / %+7.1f deg   group delay %8.3f / %8.3f ms",
                           hz, r.magnitudeDb[0][i], r.magnitudeDb[1][i], r.phaseDeg[0][i], r.phaseDeg[1][i], r.groupDelayMs[0][i], r.groupDelayMs[1][i]);
            line (row);
        }
    }
    if (r.hasHarmonics)
    {
        line ("\nHarmonics of a " + format ("%.1f", r.sineSpectrum.toneHz) + " Hz tone");
        for (int c = 0; c < 2; ++c)
        {
            std::string row = c == 0 ? "  Left:  " : "  Right: ";
            row += "gain " + format ("%+.2f", r.gainDb[(std::size_t) c]) + " dB, THD " + format ("%.4f", r.thdPercent[(std::size_t) c])
                 + " %, THD+N " + format ("%.4f", r.thdnPercent[(std::size_t) c]) + " %; H2..H10:";
            for (std::size_t k = 1; k < r.harmonicDb[(std::size_t) c].size(); ++k)
                row += " " + format ("%.0f", r.harmonicDb[(std::size_t) c][k]);
            line (row);
        }
        if (! r.otherLines.empty())
        {
            std::string row = "  Other tones (not harmonics):";
            for (std::size_t i = 0; i < std::min<std::size_t> (r.otherLines.size(), 12); ++i)
                row += " " + format ("%.1f", r.otherLines[i].hz) + " Hz (" + format ("%.0f", r.otherLines[i].db) + " dB)";
            line (row);
        }
    }
    if (r.hasAliasing)
    {
        line ("\nAliasing (folded harmonics against the fundamental, left / right)");
        for (const auto& p : r.alias)
        {
            char row[200];
            std::snprintf (row, sizeof (row), "  %7.0f Hz tone: aliasing %7.1f / %7.1f dB (loudest %6.1f dB at %.0f Hz), harmonics %7.1f / %7.1f dB",
                           p.toneHz, p.aliasDb[0], p.aliasDb[1], std::max (p.worstDb[0], p.worstDb[1]), p.worstHz, p.harmonicDb[0], p.harmonicDb[1]);
            line (row);
        }
    }
    if (r.hasDynamics)
    {
        line ("\nDynamics");
        std::string row = "  In -> out (dBFS):";
        for (std::size_t i = 0; i < r.levelInDb.size(); i += 5)
            row += " " + format ("%.0f", r.levelInDb[i]) + "->" + format ("%.1f", 0.5 * (r.levelOutDb[0][i] + r.levelOutDb[1][i]));
        line (row);
        if (r.ratio > 1.05)
            line ("  Ratio " + (r.ratio >= 99.0 ? std::string ("limiting") : format ("%.2f", r.ratio) + ":1") + " above about " + format ("%.0f", r.thresholdDb) + " dBFS");
        else
            line ("  No compression over this range");
        line ("  Attack " + format ("%.1f", r.attackMs) + " ms, release " + format ("%.1f", r.releaseMs) + " ms (to 63 %)");
    }
    if (r.hasModulation)
    {
        line ("\nModulation");
        line ("  Level moves " + format ("%.2f", r.modulationDepthDb) + " dB, pitch " + format ("%.1f", r.pitchDepthCents) + " cents");
        if (! r.modulationHz.empty())
        {
            std::string row = "  Rates:";
            for (auto hz : r.modulationHz)
                row += " " + format ("%.3f", hz) + " Hz";
            line (row + " (resolution " + format ("%.3f", r.modulationResolutionHz) + " Hz)");
        }
        if (r.gated)
            line ("  Gated: steps of " + format ("%.1f", r.gateStepMs) + " ms, " + std::to_string (r.gateOpenSteps) + " open and "
                  + std::to_string (r.gateClosedSteps) + " closed");
    }
    return text;
}
}

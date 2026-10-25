// SPDX-License-Identifier: AGPL-3.0-only
#include "Paulstretch.h"

#include "Fft.h"
#include "TimeStretch.h"

#include <algorithm>
#include <cmath>
#include <complex>
#include <cstdint>
#include <vector>

namespace
{
    constexpr double pi = 3.14159265358979323846;

    // Mean of v over bins [a, b] (clamped), from its prefix sums.
    double meanOver (const std::vector<double>& prefix, double a, double b)
    {
        const auto last = (double) prefix.size() - 2;
        const auto lo = (size_t) std::clamp (std::floor (a), 0.0, last), hi = (size_t) std::clamp (std::ceil (b), (double) lo, last);
        return (prefix[hi + 1] - prefix[lo]) / (double) (hi - lo + 1);
    }

    // Adds `gain` times m with its partials moved by the ratio, then by `shift` bins, into
    // out. Moving down (or only shifting), several bins land on one, so their power is shared between the two
    // nearest; moving up, each bin reads between two and the spacing grows, so the
    // magnitudes are scaled to keep the power.
    void addMoved (const std::vector<float>& m, std::vector<float>& out, double ratio, double shift, float gain,
                   std::vector<float>& power)
    {
        const auto bins = m.size();
        if (ratio <= 1.0)
        {
            power.assign (bins, 0.0f);
            for (size_t i = 1; i < bins; ++i)
            {
                const auto to = (double) i * ratio + shift;
                if (to < 1.0 || to >= (double) bins - 1.0)
                    continue;
                const auto k = (size_t) to;
                const auto f = (float) (to - (double) k);
                const auto p = m[i] * m[i];
                power[k] += (1.0f - f) * p;
                power[k + 1] += f * p;
            }
            for (size_t k = 1; k < bins; ++k)
                out[k] += gain * std::sqrt (power[k]);
            return;
        }
        const auto keep = (float) std::sqrt (1.0 / ratio);
        for (size_t k = 1; k < bins; ++k)
        {
            const auto from = ((double) k - shift) / ratio;
            if (from < 1.0 || from > (double) bins - 1.0)
                continue;
            const auto i = (size_t) from;
            const auto f = (float) (from - (double) i);
            out[k] += gain * keep * (i + 1 < bins ? m[i] + f * (m[i + 1] - m[i]) : m[i]);
        }
    }

    // The magnitudes of one window (bins 0 to N/2) through the spectral chain.
    void shape (std::vector<float>& m, const paulstretch::Settings& s, double binHz, std::vector<double>& prefix, std::vector<float>& scratch,
                std::vector<float>& power)
    {
        const auto bins = m.size();
        auto sums = [&] (bool power)
        {
            prefix.assign (bins + 1, 0.0);
            for (size_t k = 0; k < bins; ++k)
                prefix[k + 1] = prefix[k] + (power ? (double) m[k] * m[k] : (double) m[k]);
        };
        auto use = [&] { m.swap (scratch); };

        if (s.harmonics > 0)
        {
            // Only the bands around the first n harmonics of the fundamental pass.
            const auto f0 = std::max (10.0, s.harmonicsHz);
            const auto width = std::exp2 (std::max (1.0, s.harmonicsCents) / 1200.0) - 1.0;
            for (size_t k = 1; k < bins; ++k)
            {
                const auto f = (double) k * binHz;
                const auto h = std::clamp (std::round (f / f0), 1.0, (double) s.harmonics);
                const auto sigma = std::max (0.5 * binHz, 0.5 * width * h * f0);
                const auto x = (f - h * f0) / sigma;
                const auto g = std::exp (-x * x);
                m[k] *= (float) (s.harmonicsSoft ? g : g > 0.37 ? 1.0 : 0.0);
            }
        }
        if (std::abs (s.tonalNoise) > 1e-6)
        {
            // Tones are what stands above the average of a third of an octave around it.
            sums (false);
            const auto t = std::clamp (s.tonalNoise, -1.0, 1.0);
            constexpr double sixth = 1.122462048309373;   // 2^(1/6)
            scratch.assign (bins, 0.0f);
            for (size_t k = 1; k < bins; ++k)
            {
                const auto avg = (float) meanOver (prefix, std::min ((double) k / sixth, (double) k - 2.0), std::max ((double) k * sixth, (double) k + 2.0));
                const auto tonal = std::max (0.0f, m[k] - avg), noise = std::min (m[k], avg);
                scratch[k] = t > 0.0 ? (float) ((1.0 - t) * m[k] + t * tonal) : (float) ((1.0 + t) * m[k] - t * noise);
            }
            use();
        }
        const auto shift = s.frequencyShiftHz / binHz;
        if (std::abs (shift) > 1e-9)
        {
            scratch.assign (bins, 0.0f);
            addMoved (m, scratch, 1.0, shift, 1.0f, power);
            use();
        }
        const auto pitch = std::exp2 (std::clamp (s.pitchSemitones, -48.0, 48.0) / 12.0);
        if (std::abs (pitch - 1.0) > 1e-9)
        {
            scratch.assign (bins, 0.0f);
            addMoved (m, scratch, pitch, 0.0, 1.0f, power);
            use();
        }
        const paulstretch::Settings plain;
        if (s.ratioLevels != plain.ratioLevels)
        {
            // The partials at each ratio, by its level; the copies do not overlap, so their power adds.
            double total = 0.0;
            for (const auto l : s.ratioLevels)
                total += l * l;
            scratch.assign (bins, 0.0f);
            if (total > 1e-9)
                for (size_t r = 0; r < paulstretch::mixRatios.size(); ++r)
                    if (s.ratioLevels[r] > 1e-6)
                    {
                        addMoved (m, scratch, paulstretch::mixRatios[r], 0.0, (float) (s.ratioLevels[r] / std::sqrt (total)), power);
                    }
            use();
        }
        if (s.spreadOctaves > 1e-6)
        {
            // Each bin becomes the power average of the bins within half the spread either side.
            sums (true);
            const auto half = std::exp2 (std::min (s.spreadOctaves, 4.0) * 0.5);
            scratch.assign (bins, 0.0f);
            for (size_t k = 1; k < bins; ++k)
                scratch[k] = (float) std::sqrt (meanOver (prefix, (double) k / half, (double) k * half));
            use();
        }
        if (s.highHz > 0.0 || s.highDamp > 1e-6)
        {
            const auto lo = std::min (s.lowHz, s.highHz), hi = std::max (s.lowHz, s.highHz);
            for (size_t k = 1; k < bins; ++k)
            {
                const auto f = (double) k * binHz;
                double g = 1.0;
                if (s.highHz > 0.0)
                    g = ((f >= lo && f < hi) != s.bandStop) ? 1.0 : 0.0;
                if (f > 1000.0)
                    g *= std::pow (f / 1000.0, -std::clamp (s.highDamp, 0.0, 1.0));
                m[k] *= (float) g;
            }
        }
        m[0] = 0.0f;   // no DC
    }

    // Where each result frame reads from when hits pass at near normal speed: each hit's
    // window (a quarter before it to a half after) at ratio^(1 - amount), the rest
    // stretched more so the total stays. Linear when nothing is found or it cannot fit.
    std::vector<std::pair<double, double>> onsetMap (const AudioData& audio, Frame start, Frame length, Frame resultLength,
                                                     int window, double amount)
    {
        const auto ratio = (double) resultLength / (double) length;
        std::vector<std::pair<double, double>> regions;   // source [from, to)
        for (const auto h : timestretch::transients (audio, 0.5, 1000.0 * window / audio.sampleRate() / 2.0))
        {
            const auto from = (double) std::clamp<Frame> (h - start - window / 4, 0, length);
            const auto to = (double) std::clamp<Frame> (h - start + window / 2, 0, length);
            if (h < start || h >= start + length || to <= from)
                continue;
            if (! regions.empty() && from <= regions.back().second)
                regions.back().second = std::max (regions.back().second, to);
            else
                regions.push_back ({ from, to });
        }
        if (regions.empty() || ratio <= 1.0)
            return {};
        const auto fast = std::pow (ratio, 1.0 - std::clamp (amount, 0.0, 1.0));
        double inHits = 0.0;
        for (const auto& [a, b] : regions)
            inHits += b - a;
        const auto rest = (double) length - inHits;
        const auto restRatio = ((double) resultLength - inHits * fast) / std::max (1.0, rest);
        if (rest < 1.0 || restRatio < fast)
            return {};
        // (target, source) points along the way.
        std::vector<std::pair<double, double>> map { { 0.0, 0.0 } };
        double source = 0.0, target = 0.0;
        for (const auto& [a, b] : regions)
        {
            target += (a - source) * restRatio;
            map.push_back ({ target, a });
            target += (b - a) * fast;
            map.push_back ({ target, b });
            source = b;
        }
        map.push_back ({ (double) resultLength, (double) length });
        return map;
    }
}

int paulstretch::windowFrames (double windowSeconds, double sampleRate)
{
    const auto wanted = std::max (1.0, windowSeconds * sampleRate);
    return 1 << std::clamp ((int) std::lround (std::log2 (wanted)), 8, 18);
}

Frame paulstretch::maxResultFrames (double sampleRate)
{
    return (Frame) std::llround (3600.0 * sampleRate);
}

AudioDataPtr paulstretch::render (const AudioData& audio, Frame start, Frame end, Frame resultLength, const Settings& s,
                                  std::string& error, std::atomic<float>* progress, const std::atomic<bool>* cancel,
                                  const std::function<double (double)>& sourceAt)
{
    start = std::clamp<Frame> (start, 0, audio.length());
    end = std::clamp<Frame> (end, start, audio.length());
    const auto length = end - start;
    const auto rate = audio.sampleRate();
    if (length < 2 || resultLength < 2)
    {
        error = "The range is too short.";
        return nullptr;
    }
    if (resultLength > maxResultFrames (rate))
    {
        error = "The result would be longer than an hour.";
        return nullptr;
    }
    const auto channels = audio.numChannels();
    const auto n = windowFrames (s.windowSeconds, rate), hop = n / 2, bins = n / 2 + 1;
    const Fft fft ((int) std::lround (std::log2 ((double) n)));

    // Sine windows in and out: two half-overlapped frames of unrelated phases add to a
    // steady level (sin² + cos² = 1), and the analysis window halves the power, hence √2.
    std::vector<float> window ((size_t) n);
    for (int i = 0; i < n; ++i)
        window[(size_t) i] = (float) std::sin (pi * ((double) i + 0.5) / (double) n);
    const auto gain = (float) std::sqrt (2.0);

    std::vector<std::vector<float>> in ((size_t) channels, std::vector<float> ((size_t) n));
    std::vector<std::vector<float>> acc ((size_t) channels, std::vector<float> ((size_t) n, 0.0f));
    std::vector<float*> inPtrs;
    for (auto& c : in)
        inPtrs.push_back (c.data());
    std::vector<std::complex<float>> z ((size_t) n);
    std::vector<std::complex<float>> phase ((size_t) bins);
    std::vector<float> magX ((size_t) bins), magY ((size_t) bins), scratch, power;
    std::vector<double> prefix;
    std::vector<const float*> outPtrs ((size_t) channels);
    AudioData::Builder out (channels, rate);
    const auto binHz = rate / (double) n;
    auto random = (std::uint64_t) s.seed * 0x9E3779B97F4A7C15ull + 1;
    const auto hitMap = ! sourceAt && s.onsets > 1e-3 ? onsetMap (audio, start, length, resultLength, n, s.onsets)
                                                      : std::vector<std::pair<double, double>> {};
    auto readAt = [&] (double t)
    {
        if (sourceAt)
            return sourceAt (t);
        if (hitMap.empty())
            return t * (double) length / (double) resultLength;
        const auto next = std::upper_bound (hitMap.begin(), hitMap.end(), std::pair { t, 1e300 });
        if (next == hitMap.begin() || next == hitMap.end())
            return next == hitMap.begin() ? 0.0 : (double) length;
        const auto& [t0, s0] = *(next - 1);
        const auto& [t1, s1] = *next;
        return s0 + (t - t0) * (s1 - s0) / std::max (1e-9, t1 - t0);
    };

    for (Frame j = 0;; ++j)
    {
        const auto centre = j * hop;
        if (centre - hop >= resultLength)
            break;
        // The window of the source this frame reads, zero outside the range.
        const auto at = readAt ((double) centre);
        const auto from = (Frame) std::llround (at) - hop;
        for (auto& c : in)
            std::fill (c.begin(), c.end(), 0.0f);
        const auto a = std::max<Frame> (0, from), b = std::min<Frame> (length, from + n);
        if (b > a)
        {
            std::vector<float*> dest;
            for (auto* p : inPtrs)
                dest.push_back (p + (a - from));
            audio.read (start + a, (int) (b - a), dest.data());
        }

        for (int k = 1; k < bins; ++k)
        {
            random ^= random << 13; random ^= random >> 7; random ^= random << 17;
            phase[(size_t) k] = std::polar (1.0f, (float) (2.0 * pi * (double) (random >> 11) / 9007199254740992.0));
        }
        phase[(size_t) (bins - 1)] = 1.0f;   // Nyquist stays real

        // The compressor: one gain for all channels, pulling the window's level towards -20 dBFS.
        auto level = 1.0f;
        if (s.compress > 1e-3)
        {
            double sum = 0.0;
            for (const auto& c : in)
                for (int i = 0; i < n; ++i)
                    sum += (double) c[(size_t) i] * c[(size_t) i] * window[(size_t) i] * window[(size_t) i];
            const auto rms = std::sqrt (2.0 * sum / ((double) n * channels));
            level = (float) std::min (31.6, std::pow (0.1 / std::max (rms, 1e-6), std::clamp (s.compress, 0.0, 1.0)));
        }

        // Two channels per FFT: x + iy in, the two spectra separated by symmetry.
        for (int c = 0; c < channels; c += 2)
        {
            const bool pair = c + 1 < channels;
            const auto& x = in[(size_t) c];
            for (int i = 0; i < n; ++i)
                z[(size_t) i] = { x[(size_t) i] * window[(size_t) i], pair ? in[(size_t) c + 1][(size_t) i] * window[(size_t) i] : 0.0f };
            fft.forward (z);
            for (int k = 0; k < bins; ++k)
            {
                const auto p = z[(size_t) k], q = std::conj (z[(size_t) ((n - k) % n)]);
                magX[(size_t) k] = std::abs (p + q) * 0.5f;
                magY[(size_t) k] = std::abs (p - q) * 0.5f;
            }
            shape (magX, s, binHz, prefix, scratch, power);
            if (pair)
                shape (magY, s, binHz, prefix, scratch, power);
            if (level != 1.0f)
                for (int k = 0; k < bins; ++k)
                {
                    magX[(size_t) k] *= level;
                    magY[(size_t) k] *= level;
                }
            // Both with the same new phases, back as one Hermitian pair: x' + iy'.
            for (int k = 0; k < bins; ++k)
            {
                const auto xp = magX[(size_t) k] * phase[(size_t) k];
                const auto yp = pair ? magY[(size_t) k] * phase[(size_t) k] : std::complex<float> {};
                const std::complex<float> i1 { 0.0f, 1.0f };
                z[(size_t) k] = xp + i1 * yp;
                if (k > 0 && k < n - k)
                    z[(size_t) (n - k)] = std::conj (xp) + i1 * std::conj (yp);
            }
            fft.inverse (z);
            for (int i = 0; i < n; ++i)
            {
                const auto w = window[(size_t) i] * gain;
                acc[(size_t) c][(size_t) i] += z[(size_t) i].real() * w;
                if (pair)
                    acc[(size_t) c + 1][(size_t) i] += z[(size_t) i].imag() * w;
            }
        }

        // [centre - hop, centre) is complete: the next frame starts at centre.
        const auto first = std::max<Frame> (0, centre - hop), last = std::min<Frame> (resultLength, centre);
        if (last > first)
        {
            for (int c = 0; c < channels; ++c)
                outPtrs[(size_t) c] = acc[(size_t) c].data() + (first - (centre - hop));
            out.append (outPtrs.data(), (int) (last - first));
        }
        for (auto& c : acc)
        {
            std::copy (c.begin() + hop, c.end(), c.begin());
            std::fill (c.begin() + hop, c.end(), 0.0f);
        }

        if (j % 16 == 0)
        {
            if (progress != nullptr)
                progress->store ((float) centre / (float) resultLength);
            if (cancel != nullptr && cancel->load())
            {
                error = "Cancelled.";
                return nullptr;
            }
        }
    }
    if (progress != nullptr)
        progress->store (1.0f);
    return out.finish();
}

AudioDataPtr paulstretch::stretch (const AudioData& audio, Frame start, Frame end, double ratio, const Settings& s, std::string& error,
                                   std::atomic<float>* progress, const std::atomic<bool>* cancel)
{
    const auto length = std::max<Frame> (0, std::min (end, audio.length()) - std::max<Frame> (0, start));
    return render (audio, start, end, (Frame) std::llround ((double) length * ratio), s, error, progress, cancel);
}

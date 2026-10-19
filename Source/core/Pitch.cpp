// SPDX-License-Identifier: AGPL-3.0-only
#include "Pitch.h"
#include "Fft.h"

#include <algorithm>
#include <cmath>

namespace pitch
{
    float Track::at (Frame frame) const
    {
        if (points.empty() || hop <= 0)
            return 0.0f;
        const auto first = points.front().at;
        const auto index = (long long) std::llround ((double) (frame - first) / hop);
        if (index < 0 || index >= (long long) points.size())
            return 0.0f;
        return points[(size_t) index].hz;
    }

    namespace
    {
        int powerOfTwoAtLeast (int n)
        {
            int p = 1;
            while (p < n)
                p *= 2;
            return p;
        }

        int orderOf (int size)
        {
            int order = 0;
            while ((1 << order) < size)
                ++order;
            return order;
        }

        float median (std::vector<float> v)
        {
            std::nth_element (v.begin(), v.begin() + (long) v.size() / 2, v.end());
            return v[v.size() / 2];
        }
    }

    std::shared_ptr<Track> track (const AudioData& data, const Settings& s, std::atomic<float>* progress, const std::atomic<bool>* cancel)
    {
        auto result = std::make_shared<Track>();
        const auto rate = data.sampleRate();
        result->sampleRate = rate;
        result->hop = std::max (16, s.hop);
        if (data.length() == 0 || data.numChannels() == 0 || rate <= 0.0)
            return result;

        const auto tauMin = std::max (2, (int) std::floor (rate / s.highestHz));
        const auto tauMax = std::max (tauMin + 2, (int) std::ceil (rate / s.lowestHz));
        const auto window = tauMax;                     // the integration window: one period of the lowest pitch
        const auto span = window + tauMax + 1;          // samples each frame reads
        const auto size = powerOfTwoAtLeast (span);
        Fft fft (orderOf (size));
        const auto silence = std::pow (10.0, s.silenceDb / 20.0);

        const auto channels = data.numChannels();
        std::vector<std::vector<float>> input ((size_t) channels, std::vector<float> ((size_t) span));
        std::vector<float*> ptrs;
        for (auto& c : input)
            ptrs.push_back (c.data());
        std::vector<double> mono ((size_t) span), energy ((size_t) span + 1), d ((size_t) tauMax + 1), cmnd ((size_t) tauMax + 1);
        std::vector<std::complex<float>> a ((size_t) size), b ((size_t) size);

        const auto hop = result->hop;
        const auto count = (size_t) (data.length() / hop + 1);
        result->points.resize (count);

        for (size_t i = 0; i < count; ++i)
        {
            if (cancel != nullptr && cancel->load (std::memory_order_relaxed))
                return result;
            if (progress != nullptr && (i & 255) == 0)
                progress->store ((float) i / (float) count);

            const auto centre = (Frame) i * hop;
            auto& point = result->points[i];
            point.at = centre;

            data.read (centre - span / 2, span, ptrs.data());
            double rms = 0.0;
            for (int j = 0; j < span; ++j)
            {
                double sum = 0.0;
                for (int ch = 0; ch < channels; ++ch)
                    sum += input[(size_t) ch][(size_t) j];
                mono[(size_t) j] = sum / channels;
                rms += mono[(size_t) j] * mono[(size_t) j];
            }
            if (std::sqrt (rms / span) < silence)
                continue;

            // r(tau) = sum over the window of x[j] x[j + tau], by FFT.
            std::fill (a.begin(), a.end(), std::complex<float>());
            std::fill (b.begin(), b.end(), std::complex<float>());
            for (int j = 0; j < window; ++j)
                a[(size_t) j] = { (float) mono[(size_t) j], 0.0f };
            for (int j = 0; j < span; ++j)
                b[(size_t) j] = { (float) mono[(size_t) j], 0.0f };
            fft.forward (a);
            fft.forward (b);
            for (int k = 0; k < size; ++k)
                b[(size_t) k] = std::conj (a[(size_t) k]) * b[(size_t) k];
            fft.inverse (b);

            energy[0] = 0.0;
            for (int j = 0; j < span; ++j)
                energy[(size_t) j + 1] = energy[(size_t) j] + mono[(size_t) j] * mono[(size_t) j];
            const auto e0 = energy[(size_t) window];

            // The difference function and its cumulative mean normalised form.
            double running = 0.0;
            cmnd[0] = 1.0;
            for (int tau = 1; tau <= tauMax; ++tau)
            {
                const auto shifted = energy[(size_t) (tau + window)] - energy[(size_t) tau];
                d[(size_t) tau] = std::max (0.0, e0 + shifted - 2.0 * (double) b[(size_t) tau].real());
                running += d[(size_t) tau];
                cmnd[(size_t) tau] = running > 0.0 ? d[(size_t) tau] * tau / running : 1.0;
            }

            // The first dip under the threshold (then down to its bottom), else the deepest dip.
            int best = -1;
            for (int tau = tauMin; tau <= tauMax; ++tau)
                if (cmnd[(size_t) tau] < s.threshold)
                {
                    while (tau + 1 <= tauMax && cmnd[(size_t) tau + 1] < cmnd[(size_t) tau])
                        ++tau;
                    best = tau;
                    break;
                }
            if (best < 0)
            {
                best = tauMin;
                for (int tau = tauMin; tau <= tauMax; ++tau)
                    if (cmnd[(size_t) tau] < cmnd[(size_t) best])
                        best = tau;
                if (cmnd[(size_t) best] > 0.35)
                    continue;   // nothing periodic enough
            }

            // Between samples: a parabola through the dip.
            double refined = best;
            if (best > tauMin && best < tauMax)
            {
                const auto l = cmnd[(size_t) best - 1], c = cmnd[(size_t) best], r = cmnd[(size_t) best + 1];
                const auto denominator = l - 2.0 * c + r;
                if (std::abs (denominator) > 1.0e-12)
                    refined = best + 0.5 * (l - r) / denominator;
            }
            point.hz = (float) (rate / refined);
            point.confidence = (float) std::clamp (1.0 - cmnd[(size_t) best], 0.0, 1.0);
        }

        // Smoothing. An octave slip (about double or half its neighbours) is folded back;
        // then voiced runs shorter than three points are dropped as strays.
        auto& p = result->points;
        for (size_t i = 0; i < p.size(); ++i)
        {
            if (p[i].hz <= 0.0f)
                continue;
            std::vector<float> around;
            for (size_t j = i >= 3 ? i - 3 : 0; j < std::min (p.size(), i + 4); ++j)
                if (j != i && p[j].hz > 0.0f)
                    around.push_back (p[j].hz);
            if (around.size() < 3)
                continue;
            const auto m = median (around);
            const auto ratio = p[i].hz / m;
            if (std::abs (ratio - 2.0f) < 0.12f)
                p[i].hz *= 0.5f;
            else if (std::abs (ratio - 0.5f) < 0.03f)
                p[i].hz *= 2.0f;
        }
        for (size_t i = 0; i < p.size();)
        {
            if (p[i].hz <= 0.0f)
            {
                ++i;
                continue;
            }
            auto end = i;
            while (end < p.size() && p[end].hz > 0.0f)
                ++end;
            if (end - i < 3)
                for (auto j = i; j < end; ++j)
                    p[j].hz = 0.0f;
            i = end;
        }

        if (progress != nullptr)
            progress->store (1.0f);
        return result;
    }
}

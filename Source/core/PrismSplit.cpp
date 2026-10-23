// SPDX-License-Identifier: AGPL-3.0-only
#include "PrismSplit.h"

#include <algorithm>
#include <cmath>

namespace prism
{
    namespace
    {
        enum class Shape { low, high, allpass };

        // RBJ biquads at Q = 1/sqrt 2: two Butterworths in a row make a Linkwitz-Riley
        // crossover, and its lowpass plus highpass is exactly this allpass.
        template <typename Biquad>
        void design (Biquad& f, Shape shape, double hz, double rate)
        {
            const auto w0 = 2.0 * M_PI * std::clamp (hz, 10.0, rate * 0.45) / rate;
            const auto alpha = std::sin (w0) / (2.0 * M_SQRT1_2);
            const auto c = std::cos (w0), a0 = 1.0 + alpha;
            switch (shape)
            {
                case Shape::low:
                    f.b0 = f.b2 = (1.0 - c) / 2.0 / a0;
                    f.b1 = (1.0 - c) / a0;
                    break;
                case Shape::high:
                    f.b0 = f.b2 = (1.0 + c) / 2.0 / a0;
                    f.b1 = -(1.0 + c) / a0;
                    break;
                case Shape::allpass:
                    f.b0 = (1.0 - alpha) / a0;
                    f.b1 = -2.0 * c / a0;
                    f.b2 = 1.0;
                    break;
            }
            f.a1 = -2.0 * c / a0;
            f.a2 = (1.0 - alpha) / a0;
        }
    }

    const char* splitName (Split s)
    {
        switch (s)
        {
            case Split::parallel:  return "Parallel";
            case Split::bands:     return "Bands";
            case Split::midSide:   return "Mid/Side";
            case Split::leftRight: return "Left/Right";
        }
        return "";
    }

    int raysHeard (Split s, int numRays)
    {
        switch (s)
        {
            case Split::parallel:  return std::min (numRays, maxRays);
            case Split::bands:     return std::min (numRays, maxBands);
            case Split::midSide:
            case Split::leftRight: return std::min (numRays, 2);
        }
        return 0;
    }

    //==========================================================================
    void Crossover::prepare (double sampleRate, int numChannels)
    {
        rate = sampleRate;
        channels = std::clamp (numChannels, 1, maxChannels);
        stages.assign ((size_t) channels, {});
        designed = { 0.0, 0.0, 0.0 };
        design();
    }

    void Crossover::set (int count, const double* crossoverHz)
    {
        numBands = std::clamp (count, 1, maxBands);
        std::array<double, maxBands - 1> wanted = hz;
        for (int i = 0; i < numBands - 1; ++i)
            wanted[(size_t) i] = std::clamp (crossoverHz[i], 20.0, 20000.0);
        std::sort (wanted.begin(), wanted.begin() + (numBands - 1));
        hz = wanted;
        if (hz != designed)
            design();
    }

    void Crossover::design()
    {
        designed = hz;
        for (auto& s : stages)
            for (size_t k = 0; k < hz.size(); ++k)
            {
                for (auto& f : s.low[k])
                    prism::design (f, Shape::low, hz[k], rate);
                for (auto& f : s.high[k])
                    prism::design (f, Shape::high, hz[k], rate);
                for (auto& band : s.allpass)
                    prism::design (band[k], Shape::allpass, hz[k], rate);
            }
    }

    void Crossover::reset()
    {
        stages.assign (stages.size(), {});
        designed = { 0.0, 0.0, 0.0 };
        design();
    }

    void Crossover::process (const float* const* in, int numChannels, int numFrames, float* const* const* out)
    {
        numChannels = std::min (numChannels, (int) stages.size());
        const auto last = numBands - 1;
        for (int ch = 0; ch < numChannels; ++ch)
        {
            auto& s = stages[(size_t) ch];
            for (int i = 0; i < numFrames; ++i)
            {
                double rest = in[ch][i];
                for (int k = 0; k < last; ++k)
                {
                    auto low = s.low[(size_t) k][1].run (s.low[(size_t) k][0].run (rest));
                    rest = s.high[(size_t) k][1].run (s.high[(size_t) k][0].run (rest));
                    // Band k has not been through the crossovers above it: give it their phase.
                    for (int j = k + 1; j < last; ++j)
                        low = s.allpass[(size_t) k][(size_t) j].run (low);
                    out[k][ch][i] = (float) low;
                }
                out[last][ch][i] = (float) rest;
            }
        }
    }

    //==========================================================================
    void Splitter::prepare (double sampleRate, int numChannels)
    {
        crossover.prepare (sampleRate, numChannels);
    }

    void Splitter::split (Split mode, const float* const* in, int numChannels, int numFrames, float* const* const* rays, int numRays)
    {
        const auto heard = raysHeard (mode, numRays);
        for (int r = heard; r < numRays; ++r)
            for (int ch = 0; ch < numChannels; ++ch)
                std::fill (rays[r][ch], rays[r][ch] + numFrames, 0.0f);

        auto copy = [&] (int r, int ch) { std::copy (in[ch], in[ch] + numFrames, rays[r][ch]); };
        // Mid/side and left/right work on the front pair; any other channels go through ray 1.
        const auto paired = numChannels >= 2 && (mode == Split::midSide || mode == Split::leftRight);
        if (! paired && mode != Split::bands)
        {
            for (int r = 0; r < heard; ++r)
                for (int ch = 0; ch < numChannels; ++ch)
                    copy (r, ch);
            return;
        }

        if (mode == Split::bands)
        {
            if (heard <= 1)
            {
                for (int ch = 0; ch < numChannels; ++ch)
                    copy (0, ch);
                return;
            }
            if (crossover.bands() != heard)   // keeps its frequencies; only the count follows the rays
                crossover.set (heard, crossover.frequencies());
            crossover.process (in, numChannels, numFrames, rays);
            return;
        }

        for (int ch = 2; ch < numChannels; ++ch)
        {
            copy (0, ch);
            if (heard > 1)
                std::fill (rays[1][ch], rays[1][ch] + numFrames, 0.0f);
        }

        for (int i = 0; i < numFrames; ++i)
        {
            const auto l = in[0][i], r = in[1][i];
            if (mode == Split::midSide)
            {
                const auto mid = 0.5f * (l + r), side = 0.5f * (l - r);
                rays[0][0][i] = rays[0][1][i] = mid;
                if (heard > 1)
                {
                    rays[1][0][i] = side;
                    rays[1][1][i] = -side;
                }
            }
            else
            {
                rays[0][0][i] = rays[0][1][i] = l;
                if (heard > 1)
                    rays[1][0][i] = rays[1][1][i] = r;
            }
        }
    }

    void Splitter::join (Split mode, const float* const* const* rays, const float* gains, int numRays,
                         float* const* out, int numChannels, int numFrames)
    {
        const auto heard = raysHeard (mode, numRays);
        for (int ch = 0; ch < numChannels; ++ch)
            std::fill (out[ch], out[ch] + numFrames, 0.0f);

        const auto paired = numChannels >= 2 && (mode == Split::midSide || mode == Split::leftRight);
        if (! paired)
        {
            for (int r = 0; r < heard; ++r)
                if (std::abs (gains[r]) > 0.0f)
                    for (int ch = 0; ch < numChannels; ++ch)
                        for (int i = 0; i < numFrames; ++i)
                            out[ch][i] += gains[r] * rays[r][ch][i];
            return;
        }

        const auto g0 = heard > 0 ? gains[0] : 0.0f, g1 = heard > 1 ? gains[1] : 0.0f;
        for (int ch = 2; ch < numChannels; ++ch)
            for (int i = 0; i < numFrames; ++i)
                out[ch][i] = g0 * rays[0][ch][i];

        for (int i = 0; i < numFrames; ++i)
        {
            // A ray's plugins may have made its two sides differ; each ray carries one signal, so take their average.
            const auto first = heard > 0 ? g0 * 0.5f * (rays[0][0][i] + rays[0][1][i]) : 0.0f;
            if (mode == Split::midSide)
            {
                const auto side = heard > 1 ? g1 * 0.5f * (rays[1][0][i] - rays[1][1][i]) : 0.0f;
                out[0][i] = first + side;
                out[1][i] = first - side;
            }
            else
            {
                out[0][i] = first;
                out[1][i] = heard > 1 ? g1 * 0.5f * (rays[1][0][i] + rays[1][1][i]) : 0.0f;
            }
        }
    }

    //==========================================================================
    void Delay::prepare (int numChannels, int maxSamples)
    {
        length = std::max (1, maxSamples);
        ring.assign ((size_t) std::max (1, numChannels), std::vector<float> ((size_t) length, 0.0f));
        write = 0;
    }

    void Delay::reset()
    {
        for (auto& r : ring)
            std::fill (r.begin(), r.end(), 0.0f);
        write = 0;
    }

    void Delay::process (float* const* io, int numChannels, int numFrames, int delaySamples)
    {
        numChannels = std::min (numChannels, (int) ring.size());
        delaySamples = std::clamp (delaySamples, 0, length - 1);
        for (int ch = 0; ch < numChannels; ++ch)
        {
            auto& r = ring[(size_t) ch];
            auto w = write;
            for (int i = 0; i < numFrames; ++i)
            {
                r[(size_t) w] = io[ch][i];
                io[ch][i] = r[(size_t) ((w - delaySamples + length) % length)];
                w = (w + 1) % length;
            }
        }
        write = (write + numFrames) % length;
    }
}

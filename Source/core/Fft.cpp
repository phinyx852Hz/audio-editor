// SPDX-License-Identifier: AGPL-3.0-only
#include "Fft.h"

#include <algorithm>
#include <cmath>

namespace
{
    constexpr double pi = 3.14159265358979323846;
}

Fft::Fft (int order) : n (1 << order), bitReverse ((size_t) n), twiddles ((size_t) n / 2)
{
    for (int i = 0; i < n; ++i)
    {
        int r = 0;
        for (int b = 0; b < order; ++b)
            if (i & (1 << b))
                r |= 1 << (order - 1 - b);
        bitReverse[(size_t) i] = r;
    }

    for (int i = 0; i < n / 2; ++i)
        twiddles[(size_t) i] = std::polar (1.0f, (float) (-2.0 * pi * i / n));
}

void Fft::forward (std::vector<std::complex<float>>& data) const
{
    for (int i = 0; i < n; ++i)
        if (i < bitReverse[(size_t) i])
            std::swap (data[(size_t) i], data[(size_t) bitReverse[(size_t) i]]);

    for (int len = 2; len <= n; len <<= 1)
    {
        const int half = len / 2, step = n / len;
        for (int start = 0; start < n; start += len)
            for (int k = 0; k < half; ++k)
            {
                const auto t = twiddles[(size_t) (k * step)] * data[(size_t) (start + k + half)];
                const auto u = data[(size_t) (start + k)];
                data[(size_t) (start + k)] = u + t;
                data[(size_t) (start + k + half)] = u - t;
            }
    }
}

SpectrumAnalyzer::SpectrumAnalyzer (int order) : fft (order), window ((size_t) fft.size()), buffer ((size_t) fft.size())
{
    double sum = 0.0;
    for (int i = 0; i < fft.size(); ++i)
    {
        window[(size_t) i] = (float) (0.5 - 0.5 * std::cos (2.0 * pi * i / fft.size()));
        sum += window[(size_t) i];
    }

    // A sine of amplitude A peaks at A * sum(window) / 2 in its bin.
    scale = (float) (2.0 / sum);
}

void SpectrumAnalyzer::magnitudesDb (const float* input, float* outDb)
{
    for (int i = 0; i < fft.size(); ++i)
        buffer[(size_t) i] = { input[i] * window[(size_t) i], 0.0f };

    fft.forward (buffer);

    for (int i = 0; i < numBins(); ++i)
    {
        const auto mag = std::abs (buffer[(size_t) i]) * scale;
        outDb[i] = 20.0f * std::log10 (std::max (mag, 1.0e-10f));
    }
}

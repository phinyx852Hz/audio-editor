// SPDX-License-Identifier: AGPL-3.0-only
#pragma once

#include "Fft.h"

#include <cmath>
#include <complex>
#include <vector>

namespace repair
{
// Short-time Fourier transform of one stretch of audio: periodic Hann windows
// at 75 % overlap. addChange() overlap-adds a change to some frames back onto
// the samples, divided by the summed squared windows (exact reconstruction).
class Stft
{
public:
    using Bins = std::vector<std::complex<float>>;

    explicit Stft (int order) : fft (order), n (1 << order), hop (n / 4), window ((size_t) n)
    {
        for (int i = 0; i < n; ++i)
            window[(size_t) i] = (float) (0.5 - 0.5 * std::cos (2.0 * 3.14159265358979323846 * i / n));
    }

    int size() const     { return n; }
    int step() const     { return hop; }
    int numBins() const  { return n / 2 + 1; }
    float windowAt (int i) const { return window[(size_t) i]; }

    int numFrames (int length) const { return length < n ? 0 : (length - n) / hop + 1; }

    void analyse (const std::vector<float>& x, int frame, Bins& out) const
    {
        out.resize ((size_t) n);
        const auto p = frame * hop;
        for (int i = 0; i < n; ++i)
            out[(size_t) i] = { x[(size_t) (p + i)] * window[(size_t) i], 0.0f };
        fft.forward (out);
    }

    // `change` holds bins 0..n/2; the mirror half is filled in here.
    void addChange (Bins change, int frame, std::vector<float>& sum) const
    {
        change.resize ((size_t) n);
        for (int k = 1; k < n / 2; ++k)
            change[(size_t) (n - k)] = std::conj (change[(size_t) k]);
        change[0] = { change[0].real(), 0.0f };
        change[(size_t) (n / 2)] = { change[(size_t) (n / 2)].real(), 0.0f };
        fft.inverse (change);

        const auto p = frame * hop;
        for (int i = 0; i < n; ++i)
            sum[(size_t) (p + i)] += change[(size_t) i].real() * window[(size_t) i];
    }

    // Summed squared windows at each sample of a stretch `length` long.
    std::vector<float> normalisation (int length) const
    {
        std::vector<float> norm ((size_t) length, 0.0f);
        for (int t = 0; t < numFrames (length); ++t)
            for (int i = 0; i < n; ++i)
                norm[(size_t) (t * hop + i)] += window[(size_t) i] * window[(size_t) i];
        return norm;
    }

private:
    Fft fft;
    int n, hop;
    std::vector<float> window;
};
}

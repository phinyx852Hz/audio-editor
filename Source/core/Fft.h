// SPDX-License-Identifier: AGPL-3.0-only
#pragma once

#include <complex>
#include <vector>

// Radix-2 FFT and a windowed magnitude analyzer, used by the spectrogram and
// the analysis tools. Plain C++.
class Fft
{
public:
    explicit Fft (int order);  // size = 2^order

    int size() const { return n; }
    void forward (std::vector<std::complex<float>>& data) const;  // in place, data.size() == size()

private:
    int n;
    std::vector<int> bitReverse;
    std::vector<std::complex<float>> twiddles;
};

class SpectrumAnalyzer
{
public:
    explicit SpectrumAnalyzer (int order);

    int size() const     { return fft.size(); }
    int numBins() const  { return fft.size() / 2 + 1; }

    // Hann-windowed magnitudes in dBFS (a full-scale sine reads about 0 dB).
    // `input` has size() samples; `outDb` receives numBins() values.
    void magnitudesDb (const float* input, float* outDb);

private:
    Fft fft;
    std::vector<float> window;
    std::vector<std::complex<float>> buffer;
    float scale = 1.0f;
};

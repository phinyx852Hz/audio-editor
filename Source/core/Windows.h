// SPDX-License-Identifier: AGPL-3.0-only
#pragma once

#include <vector>

// FFT window functions for spectrum analysis. Plain C++.
//
// Every window is periodic (DFT-even): n samples, symmetric about n / 2, so
// w[i] == w[n - i], peaking at 1. Kinds 0 to 4 keep the Analysis Meters'
// original order, so a saved choice still means the same window.
namespace windows
{
    enum Kind
    {
        hann, blackmanHarris, flatTop, hamming, rectangular,
        triangular, blackman, welch, kaiser140, kaiser180,
        nuttall, blackmanNuttall, gaussian, tukey, chebyshev,
        bartlettHann, bohman, parzen, lanczos, exponential,
        hft95, hft90d,
        numKinds
    };

    std::vector<double> make (int kind, int n);

    // The first `count` discrete prolate spheroidal sequences (Slepian tapers) of length
    // n and time-bandwidth product nw: unit energy, the most concentrated first, each even
    // one summing positive and each odd one rising first. For multitaper spectra.
    std::vector<std::vector<double>> dpss (int n, double nw, int count);

    // Highest sidelobe in dB for Dolph-Chebyshev, the Gaussian's sigma (of half the
    // length), Tukey's tapered fraction and the Exponential's decay (e^-3, 26 dB, at the
    // edges: harris's Poisson window with alpha 3).
    constexpr double chebyshevSidelobeDb = 120.0;
    constexpr double gaussianSigma = 0.4;
    constexpr double tukeyTaper = 0.5;
    constexpr double exponentialDecay = 3.0;
}

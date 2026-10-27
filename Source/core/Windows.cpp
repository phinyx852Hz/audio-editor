// SPDX-License-Identifier: AGPL-3.0-only
#include "Windows.h"

#include <algorithm>
#include <cmath>
#include <complex>

namespace windows
{
namespace
{
    constexpr double pi = 3.14159265358979323846;

    // Modified Bessel function of the first kind, order 0, by its power series.
    double besselI0 (double x)
    {
        double sum = 1.0, term = 1.0;
        for (int k = 1; k < 500 && term > sum * 1.0e-17; ++k)
        {
            term *= (x / (2.0 * k)) * (x / (2.0 * k));
            sum += term;
        }
        return sum;
    }

    // Chebyshev polynomial of the first kind, T_m (y), for any real y.
    double chebyshevT (int m, double y)
    {
        if (std::abs (y) <= 1.0)
            return std::cos (m * std::acos (y));
        const auto v = std::cosh (m * std::acosh (std::abs (y)));
        return y < 0.0 && (m & 1) ? -v : v;
    }

    // In place inverse DFT in double precision (unscaled): radix 2 when n is a power of two.
    void inverseDft (std::vector<std::complex<double>>& x)
    {
        const auto n = x.size();
        if (n == 0 || (n & (n - 1)) != 0)
        {
            std::vector<std::complex<double>> out (n);
            for (size_t t = 0; t < n; ++t)
                for (size_t k = 0; k < n; ++k)
                    out[t] += x[k] * std::polar (1.0, 2.0 * pi * (double) ((k * t) % n) / (double) n);
            x = std::move (out);
            return;
        }
        for (size_t i = 1, j = 0; i < n; ++i)
        {
            auto bit = n >> 1;
            for (; j & bit; bit >>= 1)
                j ^= bit;
            j ^= bit;
            if (i < j)
                std::swap (x[i], x[j]);
        }
        for (size_t len = 2; len <= n; len <<= 1)
        {
            const auto step = std::polar (1.0, 2.0 * pi / (double) len);
            for (size_t i = 0; i < n; i += len)
            {
                std::complex<double> w = 1.0;
                for (size_t k = 0; k < len / 2; ++k, w *= step)
                {
                    const auto a = x[i + k], b = x[i + k + len / 2] * w;
                    x[i + k] = a + b;
                    x[i + k + len / 2] = a - b;
                }
            }
        }
    }

    // Dolph-Chebyshev: every sidelobe at the same height. Built in the frequency domain
    // as the symmetric window of n - 1 points (order n - 2), so the DFT of n points
    // samples its response without aliasing; w[0] is the one zero.
    std::vector<double> dolphChebyshev (int n)
    {
        std::vector<double> w ((size_t) n, 1.0);
        if (n < 4)
            return w;
        const int order = n - 2;
        const auto ratio = std::pow (10.0, chebyshevSidelobeDb / 20.0);
        const auto x0 = std::cosh (std::acosh (ratio) / order);
        std::vector<std::complex<double>> spectrum ((size_t) n);
        for (int k = 0; k < n; ++k)
            spectrum[(size_t) k] = chebyshevT (order, x0 * std::cos (pi * k / n));
        inverseDft (spectrum);   // zero phase, centred on sample 0
        for (int i = 0; i < n; ++i)
            w[(size_t) i] = spectrum[(size_t) ((i + n / 2) % n)].real();
        w[0] = 0.0;
        const auto peak = *std::max_element (w.begin(), w.end());
        for (auto& v : w)
            v /= peak;
        return w;
    }

    // Beta for a highest sidelobe attenuationDb down (Kuo and Kaiser's fit for spectral analysis).
    double kaiser (double u, double attenuationDb)
    {
        const auto beta = 0.12438 * (attenuationDb + 6.3);
        return besselI0 (beta * std::sqrt (std::max (0.0, 1.0 - u * u))) / besselI0 (beta);
    }
}

std::vector<double> make (int kind, int n)
{
    n = std::max (1, n);
    if (kind == chebyshev)
        return dolphChebyshev (n);

    std::vector<double> w ((size_t) n, 1.0);
    for (int i = 0; i < n; ++i)
    {
        const double t = (double) i / n;   // 0 to 1, the middle at 0.5
        const double u = 2.0 * t - 1.0;    // -1 to 1, the middle at 0
        const double a = std::abs (u);
        const double x = 2.0 * pi * t;
        double v = 1.0;
        switch (kind)
        {
            case hann:            v = 0.5 - 0.5 * std::cos (x); break;
            case blackmanHarris:  v = 0.35875 - 0.48829 * std::cos (x) + 0.14128 * std::cos (2 * x) - 0.01168 * std::cos (3 * x); break;
            case flatTop:         v = 0.21557895 - 0.41663158 * std::cos (x) + 0.277263158 * std::cos (2 * x)
                                      - 0.083578947 * std::cos (3 * x) + 0.006947368 * std::cos (4 * x); break;
            case hamming:         v = 0.54 - 0.46 * std::cos (x); break;
            case triangular:      v = 1.0 - a; break;
            case blackman:        v = 0.42 - 0.5 * std::cos (x) + 0.08 * std::cos (2 * x); break;
            case welch:           v = 1.0 - u * u; break;
            case kaiser140:       v = kaiser (u, 140.0); break;
            case kaiser180:       v = kaiser (u, 180.0); break;
            case nuttall:         v = 0.355768 - 0.487396 * std::cos (x) + 0.144232 * std::cos (2 * x) - 0.012604 * std::cos (3 * x); break;
            case blackmanNuttall: v = 0.3635819 - 0.4891775 * std::cos (x) + 0.1365995 * std::cos (2 * x) - 0.0106411 * std::cos (3 * x); break;
            case gaussian:        v = std::exp (-0.5 * (u / gaussianSigma) * (u / gaussianSigma)); break;
            case tukey:           v = a <= 1.0 - tukeyTaper ? 1.0 : 0.5 + 0.5 * std::cos (pi * (a - (1.0 - tukeyTaper)) / tukeyTaper); break;
            case bartlettHann:    v = 0.62 - 0.48 * std::abs (t - 0.5) - 0.38 * std::cos (x); break;
            case bohman:          v = (1.0 - a) * std::cos (pi * a) + std::sin (pi * a) / pi; break;
            case parzen:          v = a <= 0.5 ? 1.0 - 6.0 * a * a + 6.0 * a * a * a : 2.0 * (1.0 - a) * (1.0 - a) * (1.0 - a); break;
            case lanczos:         v = a < 1.0e-12 ? 1.0 : std::sin (pi * u) / (pi * u); break;
            case exponential:     v = std::exp (-exponentialDecay * a); break;
            default:              break;   // rectangular
        }
        w[(size_t) i] = v;
    }
    return w;
}
}

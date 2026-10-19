// SPDX-License-Identifier: AGPL-3.0-only
#include "Generate.h"

#include <algorithm>
#include <cmath>
#include <random>

namespace gen
{
    namespace
    {
        constexpr double pi = 3.14159265358979323846;

        // PolyBLEP: subtracts the aliasing part of a jump at phase 0 (t: phase 0..1, dt: phase step).
        double polyBlep (double t, double dt)
        {
            if (t < dt)
            {
                t /= dt;
                return t + t - t * t - 1.0;
            }
            if (t > 1.0 - dt)
            {
                t = (t - 1.0) / dt;
                return t * t + t + t + 1.0;
            }
            return 0.0;
        }

        void scaleToPeak (std::vector<float>& x, double peakDb)
        {
            float peak = 0.0f;
            for (auto v : x)
                peak = std::max (peak, std::abs (v));
            if (peak <= 0.0f)
                return;
            const auto g = (float) (std::pow (10.0, peakDb / 20.0) / peak);
            for (auto& v : x)
                v *= g;
        }
    }

    std::vector<float> tone (const Tone& t, double rate, std::int64_t length)
    {
        std::vector<float> out ((size_t) std::max<std::int64_t> (0, length));
        if (out.empty() || rate <= 0.0)
            return out;

        const auto nyquist = rate / 2.0;
        const auto f0 = std::clamp (t.startHz, 0.01, nyquist), f1 = std::clamp (t.endHz, 0.01, nyquist);
        const auto width = std::clamp (t.pulseWidth, 0.01, 0.99);
        double phase = 0.0, integrator = -1.0;   // the triangle starts at its lowest point, rising

        for (size_t i = 0; i < out.size(); ++i)
        {
            // An even glide in pitch: the frequency moves exponentially from start to end.
            const auto progress = out.size() > 1 ? (double) i / (double) (out.size() - 1) : 0.0;
            const auto hz = f0 * std::pow (f1 / f0, progress);
            const auto dt = hz / rate;
            double v = 0.0;

            switch (t.wave)
            {
                case Wave::sine:
                    v = std::sin (2.0 * pi * phase);
                    break;
                case Wave::sawtooth:
                    v = 2.0 * phase - 1.0;
                    if (t.bandLimited)
                        v -= polyBlep (phase, dt);
                    break;
                case Wave::square:
                case Wave::triangle:
                {
                    const auto pw = t.wave == Wave::triangle ? 0.5 : width;
                    v = phase < pw ? 1.0 : -1.0;
                    if (t.bandLimited)
                    {
                        v += polyBlep (phase, dt);
                        v -= polyBlep (std::fmod (phase + 1.0 - pw, 1.0), dt);
                    }
                    if (t.wave == Wave::triangle)
                    {
                        if (t.bandLimited)
                        {
                            // The band-limited square, integrated (leaking a little so it cannot drift).
                            integrator = 0.9995 * integrator + 4.0 * dt * v;
                            v = integrator;
                        }
                        else
                            v = phase < 0.5 ? 4.0 * phase - 1.0 : 3.0 - 4.0 * phase;
                    }
                    break;
                }
            }

            out[i] = (float) v;
            phase += dt;
            if (phase >= 1.0)
                phase -= std::floor (phase);
        }

        scaleToPeak (out, t.peakDb);
        return out;
    }

    std::vector<float> noise (Noise colour, double peakDb, double rate, std::int64_t length, std::uint32_t seed)
    {
        std::vector<float> out ((size_t) std::max<std::int64_t> (0, length));
        if (out.empty())
            return out;

        std::mt19937 rng (seed);
        std::normal_distribution<double> white (0.0, 1.0);

        // Pink from white with Paul Kellet's filter (accurate to about 0.05 dB above 9 Hz at 44.1 kHz).
        double b0 = 0, b1 = 0, b2 = 0, b3 = 0, b4 = 0, b5 = 0, b6 = 0;
        auto pink = [&] (double w)
        {
            b0 = 0.99886 * b0 + w * 0.0555179;
            b1 = 0.99332 * b1 + w * 0.0750759;
            b2 = 0.96900 * b2 + w * 0.1538520;
            b3 = 0.86650 * b3 + w * 0.3104856;
            b4 = 0.55000 * b4 + w * 0.5329522;
            b5 = -0.7616 * b5 - w * 0.0168980;
            const auto p = b0 + b1 + b2 + b3 + b4 + b5 + b6 + w * 0.5362;
            b6 = w * 0.115926;
            return p;
        };
        // Brown: white integrated, with a leak below about 5 Hz so it does not wander off.
        const auto leak = 1.0 - 2.0 * pi * 5.0 / rate;
        double brown = 0.0, previous = 0.0;

        for (auto& v : out)
        {
            const auto w = white (rng);
            double s = w;
            switch (colour)
            {
                case Noise::white:  s = w; break;
                case Noise::pink:   s = pink (w); break;
                case Noise::brown:  brown = leak * brown + w * 0.02; s = brown; break;
                case Noise::blue:   { const auto p = pink (w); s = p - previous; previous = p; break; }   // pink, differentiated: +3 dB/octave
                case Noise::violet: s = w - previous; previous = w; break;                                // white, differentiated: +6 dB/octave
            }
            v = (float) s;
        }

        // Brown noise's lowest frequencies wander: centre it.
        if (colour == Noise::brown)
        {
            double mean = 0.0;
            for (auto v : out)
                mean += v;
            mean /= (double) out.size();
            for (auto& v : out)
                v -= (float) mean;
        }

        scaleToPeak (out, peakDb);
        return out;
    }
}

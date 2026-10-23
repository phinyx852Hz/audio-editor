// SPDX-License-Identifier: AGPL-3.0-only
#include "Phi.h"

#include <algorithm>
#include <cmath>
#include <complex>

namespace phi
{
    namespace
    {
        constexpr double twoPi = 6.283185307179586;
        // The golden angle as a share of a turn: 1 - 1/φ (137.5°).
        constexpr double goldenTurn = 1.0 - inverse;

        double clamp01 (double v) { return std::clamp (v, 0.0, 1.0); }

        float smoothingCoefficient (double sampleRate, double ms)
        {
            return (float) std::exp (-1.0 / std::max (1.0, ms * 0.001 * sampleRate));
        }
    }

    double power (int n)
    {
        return std::pow (ratio, (double) n);
    }

    std::uint64_t fibonacci (int n)
    {
        std::uint64_t a = 0, b = 1;
        for (int i = 0; i < std::clamp (n, 0, 92); ++i)
        {
            const auto next = a + b;
            a = b;
            b = next;
        }
        return a;
    }

    double golden (std::int64_t n, double seed)
    {
        const auto v = seed + (double) n * inverse;
        return v - std::floor (v);
    }

    bool hit (std::int64_t step, double densityValue, double shift)
    {
        const auto n = (double) step;
        return std::ceil ((n + 1.0) * densityValue - shift) - std::ceil (n * densityValue - shift) >= 1.0;
    }

    double density (int index)
    {
        return power (-std::clamp (index, 0, numDensities - 1) - 1);
    }

    //==========================================================================
    double Eq::bandHz (int band)
    {
        return baseHz * power (std::clamp (band, 0, numBands - 1));
    }

    void Eq::prepare (double sampleRate, int numChannels)
    {
        rate = sampleRate;
        state.assign ((size_t) std::max (1, numChannels), {});
        for (int b = 0; b < numBands; ++b)
            design (b);
    }

    void Eq::set (const std::array<double, numBands>& gainsDb)
    {
        for (int b = 0; b < numBands; ++b)
            if (std::abs (gainsDb[(size_t) b] - gains[(size_t) b]) > 1.0e-6)
            {
                gains[(size_t) b] = gainsDb[(size_t) b];
                design (b);
            }
    }

    void Eq::reset()
    {
        for (auto& channel : state)
            for (auto& z : channel)
                z = { 0.0, 0.0 };
    }

    // The bells and shelves of the Audio EQ Cookbook (R. Bristow-Johnson).
    void Eq::design (int band)
    {
        auto& c = coefs[(size_t) band];
        const auto db = gains[(size_t) band];
        if (std::abs (db) < 1.0e-6)
        {
            c = {};
            return;
        }

        const auto hz = std::min (bandHz (band), 0.45 * rate);
        const auto w = twoPi * hz / rate;
        const auto cw = std::cos (w), sw = std::sin (w);
        const auto A = std::pow (10.0, db / 40.0);
        double b0, b1, b2, a0, a1, a2;

        if (band == 0 || band == numBands - 1)
        {
            const auto alpha = sw / 2.0 * std::sqrt (2.0);   // shelf slope 1
            const auto sqA = 2.0 * std::sqrt (A) * alpha;
            if (band == 0)
            {
                b0 = A * ((A + 1) - (A - 1) * cw + sqA);
                b1 = 2 * A * ((A - 1) - (A + 1) * cw);
                b2 = A * ((A + 1) - (A - 1) * cw - sqA);
                a0 = (A + 1) + (A - 1) * cw + sqA;
                a1 = -2 * ((A - 1) + (A + 1) * cw);
                a2 = (A + 1) + (A - 1) * cw - sqA;
            }
            else
            {
                b0 = A * ((A + 1) + (A - 1) * cw + sqA);
                b1 = -2 * A * ((A - 1) + (A + 1) * cw);
                b2 = A * ((A + 1) + (A - 1) * cw - sqA);
                a0 = (A + 1) - (A - 1) * cw + sqA;
                a1 = 2 * ((A - 1) - (A + 1) * cw);
                a2 = (A + 1) - (A - 1) * cw - sqA;
            }
        }
        else
        {
            const auto q = std::pow (ratio, 1.5);   // a bandwidth of one golden step
            const auto alpha = sw / (2.0 * q);
            b0 = 1 + alpha * A;
            b1 = -2 * cw;
            b2 = 1 - alpha * A;
            a0 = 1 + alpha / A;
            a1 = -2 * cw;
            a2 = 1 - alpha / A;
        }

        c = { b0 / a0, b1 / a0, b2 / a0, a1 / a0, a2 / a0 };
    }

    void Eq::process (float* const* channels, int numChannels, int numFrames)
    {
        for (int ch = 0; ch < std::min (numChannels, (int) state.size()); ++ch)
        {
            auto* x = channels[ch];
            auto& z = state[(size_t) ch];
            for (int b = 0; b < numBands; ++b)
            {
                if (std::abs (gains[(size_t) b]) < 1.0e-6)
                    continue;
                const auto& c = coefs[(size_t) b];
                auto z1 = z[(size_t) b][0], z2 = z[(size_t) b][1];
                for (int i = 0; i < numFrames; ++i)
                {
                    const double in = x[i];
                    const auto y = c.b0 * in + z1;
                    z1 = c.b1 * in - c.a1 * y + z2;
                    z2 = c.b2 * in - c.a2 * y;
                    x[i] = (float) y;
                }
                z[(size_t) b] = { z1, z2 };
            }
        }
    }

    double Eq::responseDb (double hz) const
    {
        const auto w = twoPi * hz / rate;
        const std::complex<double> z1 = std::polar (1.0, -w), z2 = z1 * z1;
        std::complex<double> h = 1.0;
        for (int b = 0; b < numBands; ++b)
            if (std::abs (gains[(size_t) b]) >= 1.0e-6)
            {
                const auto& c = coefs[(size_t) b];
                h *= (c.b0 + c.b1 * z1 + c.b2 * z2) / (1.0 + c.a1 * z1 + c.a2 * z2);
            }
        return 20.0 * std::log10 (std::abs (h));
    }

    //==========================================================================
    double Echo::tapSeconds (const Settings& s, int tap)
    {
        return std::clamp (s.timeMs, 1.0, maxTimeSeconds * 1000.0) * 0.001 * power (tap);
    }

    void Echo::prepare (double sampleRate, int numChannels)
    {
        rate = sampleRate;
        channels = std::max (1, numChannels);
        ring.assign ((size_t) (maxSpanSeconds * sampleRate) + 2, 0.0f);
        pos = 0;
        smoothedMix = (float) settings.mix;
        set (settings);
    }

    void Echo::set (const Settings& s)
    {
        settings = s;
        settings.taps = std::clamp (s.taps, 1, maxTaps);
        settings.feedback = std::clamp (s.feedback, 0.0, 0.9);
        const auto size = std::max (ring.size(), (size_t) 2);
        auto level = 1.0;
        for (int t = 0; t < maxTaps; ++t)
        {
            delays[(size_t) t] = std::clamp ((size_t) std::lround (tapSeconds (settings, t) * rate), (size_t) 1, size - 1);
            // Tap t sits t golden angles round the circle; its sine is where it lands from left (-1) to right (+1).
            const auto pan = clamp01 (settings.spread) * std::sin (twoPi * goldenTurn * t);
            const auto angle = (pan + 1.0) * 0.25 * twoPi / 2.0;   // 0 (left) .. a quarter turn (right)
            const auto on = t < settings.taps ? level : 0.0;
            gainL[(size_t) t] = (float) (on * std::cos (angle) * std::sqrt (2.0));
            gainR[(size_t) t] = (float) (on * std::sin (angle) * std::sqrt (2.0));
            level *= clamp01 (settings.decay);
        }
    }

    void Echo::reset()
    {
        std::fill (ring.begin(), ring.end(), 0.0f);
        pos = 0;
    }

    void Echo::process (float* const* io, int numChannels, int numFrames)
    {
        if (ring.empty() || numChannels < 1)
            return;
        const auto channelsUsed = std::min (numChannels, std::min (channels, 2));
        const auto size = ring.size();
        const auto mixTarget = (float) clamp01 (settings.mix);
        const auto last = (size_t) settings.taps - 1;
        const auto fb = (float) settings.feedback;

        for (int i = 0; i < numFrames; ++i)
        {
            smoothedMix += 0.001f * (mixTarget - smoothedMix);
            float wetL = 0.0f, wetR = 0.0f;
            // The echoes come from the mid of the input, panned tap by tap.
            float mid = 0.0f;
            for (int ch = 0; ch < channelsUsed; ++ch)
                mid += io[ch][i];
            mid /= (float) channelsUsed;

            auto read = [&] (size_t delay) { return ring[(pos + size - delay) % size]; };
            for (size_t t = 0; t <= last; ++t)
            {
                const auto v = read (delays[t]);
                wetL += v * gainL[t];
                wetR += v * gainR[t];
            }
            ring[pos] = mid + read (delays[last]) * fb;

            if (channelsUsed == 1)
                io[0][i] = io[0][i] * (1.0f - smoothedMix) + 0.5f * (wetL + wetR) * smoothedMix;
            else
            {
                io[0][i] = io[0][i] * (1.0f - smoothedMix) + wetL * smoothedMix;
                io[1][i] = io[1][i] * (1.0f - smoothedMix) + wetR * smoothedMix;
            }
            pos = (pos + 1) % size;
        }
    }

    //==========================================================================
    void Space::prepare (double sampleRate)
    {
        rate = sampleRate;
        // The longest line: 34 ms × √φ⁷.
        const auto longest = (size_t) (0.034 * std::pow (ratio, 3.5) * sampleRate) + 2;
        for (auto& l : lines)
            l.assign (longest, 0.0f);
        pre.assign ((size_t) (0.2 * sampleRate) * 2 + 4, 0.0f);
        smoothedMix = (float) settings.mix;
        set (settings);
        reset();
    }

    void Space::set (const Settings& s)
    {
        settings = s;
        const auto shortest = (0.007 + 0.027 * clamp01 (s.size)) * rate;
        const auto rt60 = std::clamp (s.decaySeconds, 0.1, 30.0);
        for (int k = 0; k < numLines; ++k)
        {
            const auto length = std::clamp ((int) std::lround (shortest * std::pow (ratio, 0.5 * k)), 1, (int) lines[(size_t) k].size() - 1);
            lengths[(size_t) k] = length;
            positions[(size_t) k] %= length;
            // The level each pass round this line leaves, for a 60 dB fall in rt60.
            feedback[(size_t) k] = std::pow (10.0, -3.0 * length / (rt60 * rate));
        }
        dampCoef = 0.7 * clamp01 (s.damping);
        preLength = std::clamp ((int) std::lround (std::clamp (s.preDelayMs, 0.0, 200.0) * 0.001 * rate), 1, (int) pre.size() / 2 - 1);
    }

    void Space::reset()
    {
        for (auto& l : lines)
            std::fill (l.begin(), l.end(), 0.0f);
        std::fill (pre.begin(), pre.end(), 0.0f);
        positions = {};
        lowpass = {};
        prePos = 0;
    }

    void Space::process (float* const* channels, int numChannels, int numFrames)
    {
        if (numChannels < 1 || pre.empty())
            return;
        const auto mixTarget = (float) clamp01 (settings.mix);
        const auto preSize = (int) pre.size() / 2;
        const auto stereo = numChannels >= 2;

        for (int i = 0; i < numFrames; ++i)
        {
            smoothedMix += 0.001f * (mixTarget - smoothedMix);
            const auto dryL = channels[0][i];
            const auto dryR = stereo ? channels[1][i] : dryL;

            pre[(size_t) prePos * 2] = dryL;
            pre[(size_t) prePos * 2 + 1] = dryR;
            const auto readPos = (prePos + preSize - preLength) % preSize;
            const auto inL = pre[(size_t) readPos * 2], inR = pre[(size_t) readPos * 2 + 1];
            prePos = (prePos + 1) % preSize;

            std::array<double, numLines> v {};
            double wetL = 0.0, wetR = 0.0;
            for (int k = 0; k < numLines; ++k)
            {
                const double out = lines[(size_t) k][(size_t) positions[(size_t) k]];
                lowpass[(size_t) k] = out + dampCoef * (lowpass[(size_t) k] - out);
                (k % 2 == 0 ? wetL : wetR) += lowpass[(size_t) k];
                v[(size_t) k] = lowpass[(size_t) k] * feedback[(size_t) k];
            }

            // An 8-point Hadamard mix (orthonormal, so the loop only loses what feedback takes).
            for (int len = 1; len < numLines; len *= 2)
                for (int a = 0; a < numLines; a += 2 * len)
                    for (int b = a; b < a + len; ++b)
                    {
                        const auto x = v[(size_t) b], y = v[(size_t) b + (size_t) len];
                        v[(size_t) b] = x + y;
                        v[(size_t) b + (size_t) len] = x - y;
                    }
            const auto norm = 1.0 / std::sqrt ((double) numLines);

            for (int k = 0; k < numLines; ++k)
            {
                // Left feeds the even lines and right the odd ones, with alternating signs.
                const double in = (k % 2 == 0 ? inL : inR) * ((k / 2) % 2 == 0 ? 0.5 : -0.5);
                lines[(size_t) k][(size_t) positions[(size_t) k]] = (float) (v[(size_t) k] * norm + in);
                positions[(size_t) k] = (positions[(size_t) k] + 1) % lengths[(size_t) k];
            }

            const auto scale = 0.5f;
            if (stereo)
            {
                channels[0][i] = dryL * (1.0f - smoothedMix) + (float) wetL * scale * smoothedMix;
                channels[1][i] = dryR * (1.0f - smoothedMix) + (float) wetR * scale * smoothedMix;
            }
            else
                channels[0][i] = dryL * (1.0f - smoothedMix) + (float) (wetL + wetR) * 0.5f * scale * smoothedMix;
        }
    }

    //==========================================================================
    double Drift::lfo (double seconds, double rateHz, double phase)
    {
        // Three sines at rate × φ⁰, φ¹, φ², each 1/φ the level of the one before.
        double sum = 0.0, weight = 1.0, total = 0.0, speed = 1.0;
        for (int k = 0; k < 3; ++k)
        {
            const auto turns = rateHz * speed * seconds + phase * speed;
            sum += weight * std::sin (twoPi * (turns - std::floor (turns)));
            total += weight;
            weight *= inverse;
            speed *= ratio;
        }
        return sum / total;
    }

    void Drift::prepare (double sampleRate, int numChannels)
    {
        rate = sampleRate;
        ring.assign ((size_t) std::max (1, numChannels), std::vector<float> ((size_t) (maxDelayMs * 0.001 * sampleRate) + 4, 0.0f));
        pos = 0;
        time = 0.0;
    }

    void Drift::set (const Settings& s)
    {
        settings = s;
        settings.rateHz = std::clamp (s.rateHz, 0.01, 20.0);
        settings.depth = clamp01 (s.depth);
        settings.mix = clamp01 (s.mix);
    }

    void Drift::reset()
    {
        for (auto& r : ring)
            std::fill (r.begin(), r.end(), 0.0f);
        pos = 0;
        time = 0.0;
    }

    void Drift::process (float* const* channels, int numChannels, int numFrames)
    {
        if (ring.empty())
            return;
        const auto channelsUsed = std::min (numChannels, (int) ring.size());
        const auto size = ring[0].size();
        const auto step = 1.0 / rate;
        const auto depth = settings.depth;

        for (int i = 0; i < numFrames; ++i)
        {
            switch (settings.mode)
            {
                case Mode::chorus:
                    for (int ch = 0; ch < channelsUsed; ++ch)
                    {
                        auto& r = ring[(size_t) ch];
                        const auto dry = channels[ch][i];
                        r[pos] = dry;
                        // 12 ms ± 8 ms, each channel a golden angle further round.
                        const auto ms = 12.0 + 8.0 * depth * lfo (time, settings.rateHz, goldenTurn * ch);
                        const auto delay = std::clamp (ms * 0.001 * rate, 1.0, (double) size - 3.0);
                        const auto whole = (size_t) delay;
                        const auto frac = (float) (delay - (double) whole);
                        const auto a = r[(pos + size - whole) % size], b = r[(pos + size - whole - 1) % size];
                        const auto wet = a + frac * (b - a);
                        channels[ch][i] = dry * (1.0f - (float) settings.mix * 0.5f) + wet * (float) settings.mix * 0.5f;
                    }
                    break;

                case Mode::tremolo:
                {
                    const auto g = (float) (1.0 - depth * 0.5 * (1.0 + lfo (time, settings.rateHz, 0.0)));
                    for (int ch = 0; ch < channelsUsed; ++ch)
                        channels[ch][i] *= g;
                    break;
                }

                case Mode::pan:
                {
                    const auto p = depth * lfo (time, settings.rateHz, 0.0);
                    if (channelsUsed >= 2)
                    {
                        channels[0][i] *= (float) std::min (1.0, 1.0 - p);
                        channels[1][i] *= (float) std::min (1.0, 1.0 + p);
                    }
                    else
                        channels[0][i] *= (float) (1.0 - 0.5 * std::abs (p));
                    break;
                }
            }
            pos = (pos + 1) % size;
            time += step;
        }
    }

    //==========================================================================
    void Rhythm::prepare (double sampleRate)
    {
        rate = sampleRate;
        set (settings);
        reset();
    }

    void Rhythm::set (const Settings& s)
    {
        settings = s;
        settings.bpm = std::clamp (s.bpm, 20.0, 400.0);
        settings.stepsPerBeat = std::clamp (s.stepsPerBeat, 1, 16);
        settings.depth = clamp01 (s.depth);
        smoothCoef = smoothingCoefficient (rate, std::clamp (s.smoothMs, 0.1, 50.0));
    }

    void Rhythm::reset()
    {
        beats = 0.0;
        gain = 1.0f;
    }

    void Rhythm::setPosition (double b)
    {
        beats = b;
    }

    std::int64_t Rhythm::currentStep() const
    {
        return (std::int64_t) std::floor (beats * settings.stepsPerBeat);
    }

    void Rhythm::process (float* const* channels, int numChannels, int numFrames)
    {
        const auto beatsPerSample = settings.bpm / 60.0 / rate;
        const auto d = density (settings.densityIndex);
        const auto closed = (float) (1.0 - settings.depth);
        for (int i = 0; i < numFrames; ++i)
        {
            const auto target = hit (currentStep(), d, settings.shift) ? 1.0f : closed;
            gain = target + smoothCoef * (gain - target);
            for (int ch = 0; ch < numChannels; ++ch)
                channels[ch][i] *= gain;
            beats += beatsPerSample;
        }
    }
}

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

    int deal (std::int64_t n, int colour, int count, double seed)
    {
        if (count < 1)
            return 0;
        static const double steps[] { inverse, std::sqrt (7.0), std::sqrt (13.0), std::sqrt (19.0),
                                      std::sqrt (29.0), std::sqrt (41.0), std::sqrt (89.0), std::sqrt (73.0) };
        const auto c = ((colour % 8) + 8) % 8;
        const auto step = steps[c] - std::floor (steps[c]);
        // n α mod 1 worked out in parts so it stays exact for large n.
        const auto turns = std::fmod ((double) (n % 1000000) * step, 1.0) + std::fmod ((double) (n / 1000000) * std::fmod (1000000.0 * step, 1.0), 1.0);
        const auto v = golden (colour, seed) + turns;
        return std::clamp ((int) std::floor ((v - std::floor (v)) * count), 0, count - 1);
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

    double Eq::stepHz (int step)
    {
        return baseHz * std::pow (ratio, std::clamp (step, minStep, maxStep) / 4.0);
    }

    int Eq::nearestStep (double hz)
    {
        if (! (hz > 0.0))
            return minStep;
        return std::clamp ((int) std::lround (4.0 * std::log (hz / baseHz) / std::log (ratio)), minStep, maxStep);
    }

    double Eq::stepQ (int qStep, bool shelf)
    {
        return (shelf ? shelfQ : bellQ) * std::pow (ratio, std::clamp (qStep, minQStep, maxQStep) / 2.0);
    }

    Eq::Band Eq::defaultBand (int band)
    {
        Band b;
        b.hz = bandHz (band);
        b.type = band == 0 ? Type::lowShelf : band == numBands - 1 ? Type::highShelf : Type::bell;
        b.q = b.type == Type::bell ? bellQ : shelfQ;
        return b;
    }

    std::array<Eq::Band, Eq::numBands> Eq::defaultBands()
    {
        std::array<Band, numBands> bands {};
        for (int b = 0; b < numBands; ++b)
            bands[(size_t) b] = defaultBand (b);
        return bands;
    }

    void Eq::prepare (double sampleRate, int numChannels)
    {
        rate = sampleRate;
        state.assign ((size_t) std::max (1, numChannels), {});
        design();
    }

    void Eq::set (const Settings& s)
    {
        if (s == settings)
            return;
        settings = s;
        design();
    }

    void Eq::set (const std::array<double, numBands>& gainsDb)
    {
        auto s = settings;
        for (int b = 0; b < numBands; ++b)
            s.bands[(size_t) b].gainDb = gainsDb[(size_t) b];
        set (s);
    }

    void Eq::reset()
    {
        for (auto& channel : state)
            for (auto& z : channel)
                z = { 0.0, 0.0 };
    }

    // The filters of the Audio EQ Cookbook (R. Bristow-Johnson), and Butterworth cuts
    // as cascades of its high- and low-passes. Each filter writes its own stages.
    void Eq::design()
    {
        used.fill (false);
        auto put = [&] (int stage, double b0, double b1, double b2, double a0, double a1, double a2)
        {
            if (! used[(size_t) stage])
                clear (stage);
            coefs[(size_t) stage] = { b0 / a0, b1 / a0, b2 / a0, a1 / a0, a2 / a0 };
            used[(size_t) stage] = true;
        };
        auto filter = [&] (int stage, Type type, double hz, double q, double db)
        {
            hz = std::clamp (hz, 10.0, 0.45 * rate);
            q = std::clamp (q, 0.05, 50.0);
            const auto w = twoPi * hz / rate;
            const auto cw = std::cos (w), sw = std::sin (w);
            const auto alpha = sw / (2.0 * q);
            const auto A = std::pow (10.0, db / 40.0);
            const auto sqA = 2.0 * std::sqrt (A) * alpha;
            switch (type)
            {
                case Type::bell:
                    if (std::abs (db) >= 1.0e-6)
                        put (stage, 1 + alpha * A, -2 * cw, 1 - alpha * A, 1 + alpha / A, -2 * cw, 1 - alpha / A);
                    break;
                case Type::lowShelf:
                    if (std::abs (db) >= 1.0e-6)
                        put (stage, A * ((A + 1) - (A - 1) * cw + sqA), 2 * A * ((A - 1) - (A + 1) * cw), A * ((A + 1) - (A - 1) * cw - sqA),
                             (A + 1) + (A - 1) * cw + sqA, -2 * ((A - 1) + (A + 1) * cw), (A + 1) + (A - 1) * cw - sqA);
                    break;
                case Type::highShelf:
                    if (std::abs (db) >= 1.0e-6)
                        put (stage, A * ((A + 1) + (A - 1) * cw + sqA), -2 * A * ((A - 1) + (A + 1) * cw), A * ((A + 1) + (A - 1) * cw - sqA),
                             (A + 1) - (A - 1) * cw + sqA, 2 * ((A - 1) - (A + 1) * cw), (A + 1) - (A - 1) * cw - sqA);
                    break;
                case Type::notch:
                    put (stage, 1, -2 * cw, 1, 1 + alpha, -2 * cw, 1 - alpha);
                    break;
                case Type::bandPass:   // 0 dB at the centre
                    put (stage, alpha, 0, -alpha, 1 + alpha, -2 * cw, 1 - alpha);
                    break;
                case Type::off:
                    break;
            }
        };
        auto cut = [&] (int stage, bool low, double hz, int slope)
        {
            static constexpr double q12[] { 0.70710678 }, q24[] { 0.54119610, 1.30656296 },
                                    q48[] { 0.50979558, 0.60134489, 0.89997622, 2.56291545 };
            const auto* qs = slope >= 48 ? q48 : slope >= 24 ? q24 : q12;
            const auto n = slope >= 48 ? 4 : slope >= 24 ? 2 : 1;
            hz = std::clamp (hz, 10.0, 0.45 * rate);
            const auto w = twoPi * hz / rate;
            const auto cw = std::cos (w), sw = std::sin (w);
            for (int i = 0; i < n; ++i)
            {
                const auto alpha = sw / (2.0 * qs[i]);
                if (low)
                    put (stage + i, (1 + cw) / 2, -(1 + cw), (1 + cw) / 2, 1 + alpha, -2 * cw, 1 - alpha);
                else
                    put (stage + i, (1 - cw) / 2, 1 - cw, (1 - cw) / 2, 1 + alpha, -2 * cw, 1 - alpha);
            }
        };

        for (int b = 0; b < numBands; ++b)
        {
            const auto& band = settings.bands[(size_t) b];
            filter (b, band.type, band.hz, band.q, band.gainDb);
        }
        if (settings.lowCutHz > 0.0)
            cut (lowCutStage, true, settings.lowCutHz, settings.lowCutSlope);
        if (settings.highCutHz > 0.0)
            cut (highCutStage, false, settings.highCutHz, settings.highCutSlope);
        if (std::abs (settings.tiltDb) >= 1.0e-6)
        {
            filter (tiltStage, Type::lowShelf, 1000.0, 0.5, -settings.tiltDb / 2.0);
            filter (tiltStage + 1, Type::highShelf, 1000.0, 0.5, settings.tiltDb / 2.0);
        }
        wasUsed = used;
    }

    void Eq::clear (int stage)
    {
        // A stage coming on starts from silence rather than from where it last stopped.
        if (! wasUsed[(size_t) stage])
            for (auto& channel : state)
                channel[(size_t) stage] = { 0.0, 0.0 };
    }

    void Eq::process (float* const* channels, int numChannels, int numFrames)
    {
        for (int ch = 0; ch < std::min (numChannels, (int) state.size()); ++ch)
        {
            auto* x = channels[ch];
            auto& z = state[(size_t) ch];
            for (int s = 0; s < maxStages; ++s)
            {
                if (! used[(size_t) s])
                    continue;
                const auto& c = coefs[(size_t) s];
                auto z1 = z[(size_t) s][0], z2 = z[(size_t) s][1];
                for (int i = 0; i < numFrames; ++i)
                {
                    const double in = x[i];
                    const auto y = c.b0 * in + z1;
                    z1 = c.b1 * in - c.a1 * y + z2;
                    z2 = c.b2 * in - c.a2 * y;
                    x[i] = (float) y;
                }
                z[(size_t) s] = { z1, z2 };
            }
        }
    }

    double Eq::responseDb (double hz) const
    {
        const auto w = twoPi * hz / rate;
        const std::complex<double> z1 = std::polar (1.0, -w), z2 = z1 * z1;
        std::complex<double> h = 1.0;
        for (int s = 0; s < maxStages; ++s)
        {
            if (! used[(size_t) s])
                continue;
            const auto& c = coefs[(size_t) s];
            h *= (c.b0 + c.b1 * z1 + c.b2 * z2) / (1.0 + c.a1 * z1 + c.a2 * z2);
        }
        return 20.0 * std::log10 (std::abs (h) + 1.0e-12);
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

    //==========================================================================
    namespace
    {
        Biquad normalised (double b0, double b1, double b2, double a0, double a1, double a2)
        {
            return { b0 / a0, b1 / a0, b2 / a0, a1 / a0, a2 / a0 };
        }

        // Constant-power gains for a pan from -1 (left) to 1 (right), 1 each in the centre.
        void panGains (double pan, float& left, float& right)
        {
            const auto angle = (std::clamp (pan, -1.0, 1.0) + 1.0) * twoPi / 8.0;
            left = (float) (std::cos (angle) * std::sqrt (2.0));
            right = (float) (std::sin (angle) * std::sqrt (2.0));
        }

        // Band or copy n of a golden fan: 0 in the centre, then left, right, left...
        double fanPan (int n, double width)
        {
            return n == 0 ? 0.0 : (n % 2 == 1 ? -1.0 : 1.0) * std::clamp (width, 0.0, 1.0);
        }
    }

    Biquad Biquad::bandPass (double hz, double q, double sampleRate)
    {
        const auto w = twoPi * std::min (hz, 0.45 * sampleRate) / sampleRate;
        const auto alpha = std::sin (w) / (2.0 * q);
        return normalised (alpha, 0.0, -alpha, 1.0 + alpha, -2.0 * std::cos (w), 1.0 - alpha);
    }

    Biquad Biquad::bell (double hz, double q, double gainDb, double sampleRate)
    {
        const auto w = twoPi * std::min (hz, 0.45 * sampleRate) / sampleRate;
        const auto alpha = std::sin (w) / (2.0 * q), A = std::pow (10.0, gainDb / 40.0), c = std::cos (w);
        return normalised (1.0 + alpha * A, -2.0 * c, 1.0 - alpha * A, 1.0 + alpha / A, -2.0 * c, 1.0 - alpha / A);
    }

    Biquad Biquad::lowPass (double hz, double q, double sampleRate)
    {
        const auto w = twoPi * std::min (hz, 0.45 * sampleRate) / sampleRate;
        const auto alpha = std::sin (w) / (2.0 * q), c = std::cos (w);
        return normalised ((1.0 - c) / 2.0, 1.0 - c, (1.0 - c) / 2.0, 1.0 + alpha, -2.0 * c, 1.0 - alpha);
    }

    //==========================================================================
    double Resonator::bandHz (const Settings& s, int band)
    {
        return std::clamp (s.rootHz, 20.0, 2000.0) * power (band);
    }

    void Resonator::prepare (double sampleRate, int)
    {
        rate = sampleRate;
        set (settings);
        reset();
    }

    void Resonator::set (const Settings& s)
    {
        settings = s;
        settings.bands = std::clamp (s.bands, 1, maxBands);
        settings.q = std::clamp (s.q, 2.0, 100.0);
        used = 0;
        for (int b = 0; b < settings.bands && bandHz (settings, b) < 0.45 * rate; ++b)
        {
            filters[(size_t) b] = Biquad::bandPass (bandHz (settings, b), settings.q, rate);
            panGains (fanPan (b, settings.width), gainL[(size_t) b], gainR[(size_t) b]);
            used = b + 1;
        }
        // A band this narrow passes little of a broad sound; bring it back up.
        makeup = (float) std::sqrt (settings.q);
    }

    void Resonator::reset()
    {
        state = {};
    }

    void Resonator::process (float* const* channels, int numChannels, int numFrames)
    {
        if (numChannels < 1)
            return;
        const auto stereo = numChannels >= 2;
        const auto mix = (float) clamp01 (settings.mix), noise = (float) clamp01 (settings.noise);
        for (int i = 0; i < numFrames; ++i)
        {
            noiseState = noiseState * 1664525u + 1013904223u;
            const auto white = ((float) (noiseState >> 8) / 8388608.0f - 1.0f) * 0.5f;
            const auto in = (stereo ? 0.5f * (channels[0][i] + channels[1][i]) : channels[0][i]) + noise * white;
            float wetL = 0.0f, wetR = 0.0f;
            for (int b = 0; b < used; ++b)
            {
                const auto y = state[(size_t) b].run (filters[(size_t) b], in);
                wetL += y * gainL[(size_t) b];
                wetR += y * gainR[(size_t) b];
            }
            wetL *= makeup;
            wetR *= makeup;
            if (stereo)
            {
                channels[0][i] = channels[0][i] * (1.0f - mix) + wetL * mix;
                channels[1][i] = channels[1][i] * (1.0f - mix) + wetR * mix;
            }
            else
                channels[0][i] = channels[0][i] * (1.0f - mix) + 0.5f * (wetL + wetR) * mix;
        }
    }

    //==========================================================================
    void Stack::prepare (double sampleRate, int)
    {
        rate = sampleRate;
        grain = std::max<size_t> (16, (size_t) (grainMs * 0.001 * sampleRate));
        ring.assign (grain + 4, 0.0f);
        smoothedMix = (float) settings.mix;
        set (settings);
        reset();
    }

    void Stack::set (const Settings& s)
    {
        settings = s;
        settings.copies = std::clamp (s.copies, 1, maxCopies);
        auto level = 1.0;
        for (int c = 0; c < maxCopies; ++c)
        {
            level *= clamp01 (s.decay);
            float l, r;
            panGains (fanPan (c + 1, s.width), l, r);
            const auto on = c < settings.copies ? level : 0.0;
            gainL[(size_t) c] = (float) on * l;
            gainR[(size_t) c] = (float) on * r;
        }
        tone = Biquad::lowPass (std::clamp (s.toneHz, 200.0, 20000.0), 0.7071, rate);
    }

    void Stack::reset()
    {
        std::fill (ring.begin(), ring.end(), 0.0f);
        pos = 0;
        for (int c = 0; c < maxCopies; ++c)
            phase[(size_t) c] = golden (c);   // the copies' grains start at different places
        toneState = {};
    }

    void Stack::process (float* const* channels, int numChannels, int numFrames)
    {
        if (numChannels < 1 || ring.empty())
            return;
        const auto stereo = numChannels >= 2;
        const auto size = ring.size();
        const auto g = (double) grain;
        const auto mixTarget = (float) clamp01 (settings.mix);

        auto read = [&] (double delay)
        {
            const auto whole = (size_t) delay;
            const auto frac = (float) (delay - (double) whole);
            const auto a = ring[(pos + size - whole) % size], b = ring[(pos + size - whole - 1) % size];
            return a + frac * (b - a);
        };

        for (int i = 0; i < numFrames; ++i)
        {
            smoothedMix += 0.001f * (mixTarget - smoothedMix);
            ring[pos] = stereo ? 0.5f * (channels[0][i] + channels[1][i]) : channels[0][i];

            float wetL = 0.0f, wetR = 0.0f;
            auto shift = ratio;
            for (int c = 0; c < settings.copies; ++c)
            {
                // The delay shrinks by (shift - 1) samples a sample, so the copy plays shift times faster;
                // two taps half a grain apart, each faded in and out (Hann), cover the jumps back.
                auto& p = phase[(size_t) c];
                p -= (shift - 1.0) / g;
                p -= std::floor (p);
                const auto q = p + 0.5 - std::floor (p + 0.5);
                const auto wp = (float) (0.5 - 0.5 * std::cos (twoPi * p)), wq = 1.0f - wp;
                const auto v = wp * read (1.0 + p * (g - 2.0)) + wq * read (1.0 + q * (g - 2.0));
                wetL += v * gainL[(size_t) c];
                wetR += v * gainR[(size_t) c];
                shift *= ratio;
            }
            wetL = toneState[0].run (tone, wetL);
            wetR = toneState[1].run (tone, wetR);

            if (stereo)
            {
                channels[0][i] += wetL * smoothedMix;
                channels[1][i] += wetR * smoothedMix;
            }
            else
                channels[0][i] += 0.5f * (wetL + wetR) * smoothedMix;
            pos = (pos + 1) % size;
        }
    }

    //==========================================================================
    void Tame::prepare (double sampleRate, int numChannels)
    {
        rate = sampleRate;
        analyzer = std::make_unique<SpectrumAnalyzer> (order);
        ring.assign ((size_t) analyzer->size(), 0.0f);
        block.assign (ring.size(), 0.0f);
        magnitudes.assign ((size_t) analyzer->numBins(), -200.0f);
        state.assign ((size_t) std::max (1, numChannels), {});
        designedHz = 0.0;
        set (settings);
        reset();
    }

    void Tame::set (const Settings& s)
    {
        settings = s;
        settings.fundamentalHz = std::clamp (s.fundamentalHz, lowestHz, highestHz);
        settings.depthDb = std::clamp (s.depthDb, -18.0, 0.0);
        settings.q = std::clamp (s.q, 0.3, 10.0);
        if (! settings.automatic)
            found = settings.fundamentalHz;
        design();
    }

    void Tame::reset()
    {
        std::fill (ring.begin(), ring.end(), 0.0f);
        for (auto& st : state)
            st = {};
        pos = 0;
        sinceAnalysis = 0;
    }

    void Tame::design()
    {
        const auto hz = cutHz (found);
        if (designedHz > 0.0 && std::abs (hz / designedHz - 1.0) < 0.002 && std::abs (settings.depthDb - designedDepth) < 1.0e-6)
            return;
        designedHz = hz;
        designedDepth = settings.depthDb;
        cut = Biquad::bell (hz, settings.q, settings.depthDb, rate);
    }

    void Tame::analyse()
    {
        if (analyzer == nullptr)
            return;
        const auto n = ring.size();
        for (size_t i = 0; i < n; ++i)
            block[i] = ring[(pos + i) % n];
        analyzer->magnitudesDb (block.data(), magnitudes.data());

        const auto binHz = rate / (double) n;
        const auto from = std::max (1, (int) std::ceil (lowestHz / binHz)), to = std::min ((int) magnitudes.size() - 2, (int) (highestHz / binHz));
        int best = -1;
        for (int b = from; b <= to; ++b)
            if (best < 0 || magnitudes[(size_t) b] > magnitudes[(size_t) best])
                best = b;
        if (best < 0 || magnitudes[(size_t) best] < -70.0f)
            return;   // nothing there: keep what was found

        // Parabolic interpolation between the bins on either side.
        const double l = magnitudes[(size_t) best - 1], c = magnitudes[(size_t) best], r = magnitudes[(size_t) best + 1];
        const auto denominator = l - 2.0 * c + r;
        const auto offset = std::abs (denominator) > 1.0e-9 ? std::clamp (0.5 * (l - r) / denominator, -0.5, 0.5) : 0.0;
        const auto hz = std::clamp ((best + offset) * binHz, lowestHz, highestHz);
        found *= std::pow (hz / found, 0.3);   // glide there, in pitch
    }

    void Tame::process (float* const* channels, int numChannels, int numFrames)
    {
        if (ring.empty())
            return;
        const auto channelsUsed = std::min (numChannels, (int) state.size());
        for (int i = 0; i < numFrames; ++i)
        {
            float mid = 0.0f;
            for (int ch = 0; ch < channelsUsed; ++ch)
                mid += channels[ch][i];
            ring[pos] = mid / (float) std::max (1, channelsUsed);
            pos = (pos + 1) % ring.size();

            if (settings.automatic && ++sinceAnalysis >= 2048)
            {
                sinceAnalysis = 0;
                analyse();
                design();
            }
            for (int ch = 0; ch < channelsUsed; ++ch)
                channels[ch][i] = state[(size_t) ch].run (cut, channels[ch][i]);
        }
    }

    //==========================================================================
    void Deresonator::prepare (double sampleRate, int numChannels)
    {
        rate = sampleRate;
        const auto order = 11 + (sampleRate > 60000.0 ? 1 : 0) + (sampleRate > 120000.0 ? 1 : 0);
        fft = std::make_unique<Fft> (order);
        size = fft->size();
        hop = size / 4;
        bins = size / 2 + 1;
        window.resize ((size_t) size);
        for (int i = 0; i < size; ++i)
            window[(size_t) i] = (float) (0.5 - 0.5 * std::cos (twoPi * i / size));
        const auto channels = (size_t) std::max (1, numChannels);
        input.assign (channels, std::vector<float> ((size_t) size, 0.0f));
        output.assign (channels, std::vector<float> ((size_t) size, 0.0f));
        ready.assign (channels, std::vector<float> ((size_t) hop, 0.0f));
        spectra.assign (channels, std::vector<std::complex<float>> ((size_t) size));
        power.assign ((size_t) bins, 0.0);
        levelDb.assign ((size_t) bins, 0.0);
        prefix.assign ((size_t) bins + 1, 0.0);
        envelopeDb.assign ((size_t) bins, 0.0f);
        gainDb.assign ((size_t) bins, 0.0f);
        spanLow.assign ((size_t) bins, 0);
        spanHigh.assign ((size_t) bins, 0);
        set (settings);
        reset();
    }

    void Deresonator::set (const Settings& s)
    {
        const auto sharpnessChanged = std::abs (s.sharpness - settings.sharpness) > 1.0e-9;
        settings = s;
        settings.depth = clamp01 (s.depth);
        settings.sharpness = clamp01 (s.sharpness);
        settings.speed = clamp01 (s.speed);
        settings.mix = clamp01 (s.mix);
        const auto hopSeconds = hop / rate;
        const auto attackSeconds = 0.05 * std::pow (0.1, settings.speed);   // 50 ms .. 5 ms
        attack = std::exp (-hopSeconds / attackSeconds);
        release = std::exp (-hopSeconds / (attackSeconds * ratio));
        if (sharpnessChanged || spanHigh.empty() || spanHigh.back() == 0)
            layout();
    }

    // Each bin's curve is the average level over a span of bins around it, one
    // golden step wide at 50 % Sharpness (φ times wider at 0 %, φ times narrower at 100 %).
    void Deresonator::layout()
    {
        if (spanLow.empty())
            return;
        const auto octaves = std::log2 (ratio) * std::pow (ratio, 1.0 - 2.0 * settings.sharpness);
        const auto half = std::pow (2.0, octaves / 2.0);
        for (int k = 0; k < bins; ++k)
        {
            spanLow[(size_t) k] = std::clamp ((int) std::floor (k / half), 0, bins - 1);
            spanHigh[(size_t) k] = std::clamp ((int) std::ceil (k * half), k + 1, bins);
            // Leave out the bin itself and its neighbours, so a peak does not lift its own curve.
            if (spanHigh[(size_t) k] - spanLow[(size_t) k] < 7)
            {
                spanLow[(size_t) k] = std::max (0, k - 3);
                spanHigh[(size_t) k] = std::min (bins, k + 4);
            }
        }
    }

    void Deresonator::reset()
    {
        for (auto* group : { &input, &output, &ready })
            for (auto& v : *group)
                std::fill (v.begin(), v.end(), 0.0f);
        std::fill (gainDb.begin(), gainDb.end(), 0.0f);
        fill = 0;
        deepest = 0.0f;
        for (int p = 0; p < pictureSize; ++p)
        {
            shownLevel[(size_t) p].store (-200.0f, std::memory_order_relaxed);
            shownCut[(size_t) p].store (0.0f, std::memory_order_relaxed);
        }
    }

    void Deresonator::frame()
    {
        const auto channels = (int) input.size();
        std::fill (power.begin(), power.end(), 0.0);
        for (int ch = 0; ch < channels; ++ch)
        {
            auto& x = spectra[(size_t) ch];
            for (int i = 0; i < size; ++i)
                x[(size_t) i] = { input[(size_t) ch][(size_t) i] * window[(size_t) i], 0.0f };
            fft->forward (x);
            for (int k = 0; k < bins; ++k)
                power[(size_t) k] += std::norm (x[(size_t) k]);
        }

        prefix[0] = 0.0;
        for (int k = 0; k < bins; ++k)
        {
            levelDb[(size_t) k] = 10.0 * std::log10 (power[(size_t) k] + 1.0e-20);
            prefix[(size_t) k + 1] = prefix[(size_t) k] + levelDb[(size_t) k];
        }

        const auto binHz = rate / size;
        const auto threshold = 3.0;   // dB above the curve before anything happens
        const auto depth = settings.depth;
        deepest = 0.0f;
        for (int k = 0; k < bins; ++k)
        {
            const auto lo = spanLow[(size_t) k], hi = spanHigh[(size_t) k];
            // The span's average without the bin and its two neighbours.
            const auto innerLo = std::max (lo, k - 1), innerHi = std::min (hi, k + 2);
            const auto count = (hi - lo) - (innerHi - innerLo);
            const auto sum = (prefix[(size_t) hi] - prefix[(size_t) lo]) - (prefix[(size_t) innerHi] - prefix[(size_t) innerLo]);
            const auto curve = count > 0 ? sum / count : levelDb[(size_t) k];
            envelopeDb[(size_t) k] = (float) curve;

            const auto hz = k * binHz;
            auto target = 0.0;
            if (hz >= settings.lowHz && hz <= settings.highHz && levelDb[(size_t) k] > -100.0)
                target = -std::min (24.0, std::max (0.0, levelDb[(size_t) k] - curve - threshold) * depth);

            auto& g = gainDb[(size_t) k];
            const auto coef = target < g ? attack : release;
            g = (float) (target + coef * (g - target));
            deepest = std::min (deepest, g);
        }

        // The picture for the window: a Hann frame of a full-scale sine peaks at size / 4 per channel.
        const auto fullScaleDb = 20.0 * std::log10 (size / 4.0 * std::max (1, channels));
        for (int p = 0; p < pictureSize; ++p)
        {
            const auto from = std::clamp ((int) std::floor (pictureHz (p) / binHz), 0, bins - 1);
            const auto to = std::clamp ((int) std::floor ((p + 1 < pictureSize ? pictureHz (p + 1) : rate / 2.0) / binHz), from + 1, bins);
            auto level = -200.0;
            auto cut = 0.0f;
            for (int k = from; k < to; ++k)
            {
                level = std::max (level, levelDb[(size_t) k]);
                cut = std::min (cut, gainDb[(size_t) k]);
            }
            shownLevel[(size_t) p].store ((float) (level - fullScaleDb), std::memory_order_relaxed);
            shownCut[(size_t) p].store (cut, std::memory_order_relaxed);
        }

        // Hann in and out at 75 % overlap adds up to 1.5.
        constexpr float overlapScale = 1.0f / 1.5f;
        const auto mix = (float) settings.mix;
        for (int ch = 0; ch < channels; ++ch)
        {
            auto& x = spectra[(size_t) ch];
            for (int k = 0; k < bins; ++k)
            {
                const auto gain = std::pow (10.0f, gainDb[(size_t) k] / 20.0f);
                const auto wet = settings.delta ? 1.0f - gain : gain;
                const auto applied = settings.delta ? mix * wet : 1.0f - mix * (1.0f - wet);
                x[(size_t) k] *= applied;
                if (k > 0 && k < size / 2)
                    x[(size_t) (size - k)] = std::conj (x[(size_t) k]);
            }
            fft->inverse (x);
            auto& out = output[(size_t) ch];
            for (int i = 0; i < size; ++i)
                out[(size_t) i] += x[(size_t) i].real() * window[(size_t) i] * overlapScale;

            std::copy (out.begin(), out.begin() + hop, ready[(size_t) ch].begin());
            std::copy (out.begin() + hop, out.end(), out.begin());
            std::fill (out.end() - hop, out.end(), 0.0f);
            auto& in = input[(size_t) ch];
            std::copy (in.begin() + hop, in.end(), in.begin());
        }
    }

    double Deresonator::pictureHz (int point)
    {
        return 20.0 * std::pow (1000.0, (double) point / (pictureSize - 1));
    }

    void Deresonator::picture (float* levelDb, float* cutDb) const
    {
        for (int p = 0; p < pictureSize; ++p)
        {
            levelDb[p] = shownLevel[(size_t) p].load (std::memory_order_relaxed);
            cutDb[p] = shownCut[(size_t) p].load (std::memory_order_relaxed);
        }
    }

    void Deresonator::process (float* const* channels, int numChannels, int numFrames)
    {
        if (fft == nullptr)
            return;
        const auto used = std::min (numChannels, (int) input.size());
        for (int i = 0; i < numFrames; ++i)
        {
            for (int ch = 0; ch < used; ++ch)
            {
                input[(size_t) ch][(size_t) (size - hop + fill)] = channels[ch][i];
                channels[ch][i] = ready[(size_t) ch][(size_t) fill];
            }
            if (++fill == hop)
            {
                fill = 0;
                frame();
            }
        }
    }
}


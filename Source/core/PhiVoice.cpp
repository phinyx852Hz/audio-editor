// SPDX-License-Identifier: AGPL-3.0-only
#include "PhiVoice.h"
#include "Phi.h"

#include <algorithm>
#include <cmath>

namespace phi
{
    namespace
    {
        constexpr double twoPi = 6.283185307179586;
        constexpr double halfPi = 1.5707963267948966;

        double frac (double v) { return v - std::floor (v); }

        // From 0..1 to lo..hi on a log scale (times, rates).
        double logScale (double v, double lo, double hi) { return lo * std::pow (hi / lo, v); }

        template <typename T, size_t N>
        const T& pick (const std::array<T, N>& from, double v)
        {
            return from[std::min (N - 1, (size_t) (v * (double) N))];
        }

        // The generalised golden ratio for d dimensions: the root above 1 of x^(d+1) = x + 1.
        double generalisedRatio (int d)
        {
            double x = 1.5;
            for (int i = 0; i < 40; ++i)
            {
                const auto f = std::pow (x, d + 1) - x - 1.0;
                const auto slope = (d + 1) * std::pow (x, d) - 1.0;
                x -= f / slope;
            }
            return x;
        }
    }

    PdPatch PdPatch::at (std::int64_t n, double seed)
    {
        static const double g = generalisedRatio (dimensions);
        PdPatch p;
        double alpha = 1.0;
        for (int i = 0; i < dimensions; ++i)
        {
            alpha /= g;
            p.values[(size_t) i] = frac (seed + 0.5 + (double) n * alpha);
        }
        return p;
    }

    double pdWave (PdWave wave, double p, double d, double minWidth)
    {
        d = std::clamp (d, 0.0, 1.0);
        double q = p;
        switch (wave)
        {
            case PdWave::saw:
            {
                // Through the first half-cycle of the cosine fast, the second slowly.
                const auto k = std::max (minWidth, 0.5 * (1.0 - d));
                q = p < k ? 0.5 * p / k : 0.5 + 0.5 * (p - k) / (1.0 - k);
                break;
            }
            case PdWave::square:
            {
                // Each half-cycle done quickly and then held.
                const auto w = std::max (minWidth, 0.5 * (1.0 - d));
                q = p < 0.5 ? 0.5 * std::min (1.0, p / w) : 0.5 + 0.5 * std::min (1.0, (p - 0.5) / w);
                break;
            }
            case PdWave::pulse:
            {
                const auto w = std::max (minWidth, 1.0 - d);
                q = std::min (1.0, p / w);
                break;
            }
            case PdWave::resonantSaw:
            case PdWave::resonantTriangle:
            {
                // A cosine at a higher, sweeping frequency, restarted each cycle under a
                // window that is 0 where it restarts, so there is no jump (the CZ's resonance).
                const auto window = wave == PdWave::resonantSaw ? 1.0 - p : 1.0 - std::abs (2.0 * p - 1.0);
                const auto r = 1.0 + d * 15.0;
                const auto reso = 1.0 - window * (1.0 - std::cos (twoPi * r * p));
                return (1.0 - d) * std::cos (twoPi * p) + d * reso;   // reso is within ±1, as the window is within 0..1
            }
            case PdWave::count: break;
        }
        return std::cos (twoPi * q);
    }

    //==========================================================================
    void GranularReverb::prepare (double sampleRate)
    {
        rate = sampleRate > 0.0 ? sampleRate : 48000.0;
        size = (int) (rate * 4.0);
        bufferLeft.assign ((size_t) size, 0.0f);
        bufferRight.assign ((size_t) size, 0.0f);
        window.resize (2049);
        for (size_t i = 0; i < window.size(); ++i)
            window[i] = (float) (0.5 - 0.5 * std::cos (twoPi * (double) i / 2048.0));
        spacing = std::max (1, (int) (rate * 0.008));
        damping = (float) std::exp (-twoPi * 5000.0 / rate);
        setTail (tail);
        reset();
    }

    void GranularReverb::reset()
    {
        std::fill (bufferLeft.begin(), bufferLeft.end(), 0.0f);
        std::fill (bufferRight.begin(), bufferRight.end(), 0.0f);
        grains = {};
        write = 0;
        untilNext = 0;
        lowLeft = lowRight = 0.0f;
    }

    void GranularReverb::setTail (double seconds)
    {
        tail = std::clamp (seconds, 0.5, 8.0);
        // Each pass through the grains comes back about `loop` seconds later, a little
        // quieter than it went in; enough passes to fall 60 dB in `tail` seconds.
        constexpr double loop = 0.25, cloudGain = 0.62;
        feedback = (float) std::min (0.9, std::pow (10.0, -3.0 * loop / tail) / cloudGain);
    }

    void GranularReverb::setMix (double w) { wet = std::clamp (w, 0.0, 1.0); }

    float GranularReverb::random()
    {
        seed = seed * 1664525u + 1013904223u;
        return (float) (seed >> 8) * (1.0f / 16777216.0f);
    }

    void GranularReverb::spawn()
    {
        for (auto& g : grains)
        {
            if (g.active)
                continue;
            g.active = true;
            g.length = (int) (rate * (0.07 + 0.12 * random()));
            g.age = 0;
            g.step = std::pow (2.0, (random() - 0.5) * 24.0 / 1200.0);   // ±12 cents
            // From the past only: far enough back that a grain read a little fast stays behind the writing.
            const auto back = g.length * (g.step + 0.1) + random() * 0.5 * rate;
            g.position = (double) write - back;
            if (g.position < 0.0)
                g.position += size;
            const auto angle = halfPi * (0.5 + (random() - 0.5) * 0.9);
            g.gainLeft = (float) std::cos (angle);
            g.gainRight = (float) std::sin (angle);
            return;
        }
    }

    void GranularReverb::process (float* left, float* right, int frames)
    {
        if (size == 0)
            prepare (rate);
        // About length / spacing grains overlap; their sum, uncorrelated, rises by its square root.
        const auto norm = (float) std::sqrt ((double) spacing / (rate * 0.13));
        for (int i = 0; i < frames; ++i)
        {
            if (--untilNext <= 0)
            {
                spawn();
                untilNext = (int) (spacing * (0.6 + 0.8 * random()));
            }
            float outLeft = 0.0f, outRight = 0.0f;
            for (auto& g : grains)
            {
                if (! g.active)
                    continue;
                const auto w = window[(size_t) ((int64_t) g.age * 2048 / g.length)];
                const auto at = (int) g.position;
                const auto t = (float) (g.position - at);
                const auto next = at + 1 < size ? at + 1 : 0;
                const auto s = 0.5f * ((bufferLeft[(size_t) at] + bufferRight[(size_t) at]) * (1.0f - t)
                                       + (bufferLeft[(size_t) next] + bufferRight[(size_t) next]) * t);
                outLeft += w * g.gainLeft * s;
                outRight += w * g.gainRight * s;
                g.position += g.step;
                if (g.position >= size)
                    g.position -= size;
                if (++g.age >= g.length)
                    g.active = false;
            }
            outLeft *= norm;
            outRight *= norm;

            // Back into the buffer, each side with a little of the other and the highs softened.
            lowLeft = (1.0f - damping) * (0.75f * outLeft + 0.25f * outRight) + damping * lowLeft;
            lowRight = (1.0f - damping) * (0.75f * outRight + 0.25f * outLeft) + damping * lowRight;
            const auto fbLeft = feedback * lowLeft, fbRight = feedback * lowRight;
            const auto dryLeft = left[i], dryRight = right[i];
            bufferLeft[(size_t) write] = std::tanh (dryLeft + fbLeft);
            bufferRight[(size_t) write] = std::tanh (dryRight + fbRight);
            if (++write >= size)
                write = 0;

            left[i] = dryLeft + (float) wet * outLeft;
            right[i] = dryRight + (float) wet * outRight;
        }
    }

    //==========================================================================
    double StartupSound::Envelope::next (double r, bool held)
    {
        if (! held && stage < 3)
            stage = 3;
        switch (stage)
        {
            case 0:
                level += 1.0 / std::max (1.0, attack * r);
                if (level >= 1.0)
                {
                    level = 1.0;
                    stage = 1;
                }
                break;
            case 1:
            case 2:
                level += (sustain - level) * (1.0 - std::exp (-4.0 / std::max (1.0, decay * r)));
                break;
            case 3:
                level *= std::exp (-6.9 / std::max (1.0, release * r));   // -60 dB in `release`
                if (level < 1.0e-4)
                {
                    level = 0.0;
                    stage = 4;
                }
                break;
            default: level = 0.0; break;
        }
        return level;
    }

    StartupSound::StartupSound (std::int64_t launch, double seed) : settings (PdPatch::at (launch, seed))
    {
        const auto& v = settings.values;
        constexpr int numWaves = (int) PdWave::count;
        line[0].wave = (PdWave) std::min (numWaves - 1, (int) (v[0] * numWaves));
        line[1].wave = (PdWave) std::min (numWaves - 1, (int) (v[1] * numWaves));
        line[0].ratio = 1.0;
        line[1].ratio = pick (std::array<double, 6> { 1.0, 2.0, ratio, 1.5, ratio * ratio, 3.0 }, v[2]);
        line[0].level = 1.0;
        line[1].level = 0.2 + 0.7 * v[3];
        line[0].depth = 0.3 + 0.7 * v[4];
        line[1].depth = 0.3 + 0.7 * v[5];
        ring = v[14] > 0.75;

        Envelope dcw;
        dcw.attack = logScale (v[6], 0.005, 0.4);
        dcw.decay = logScale (v[7], 0.15, 1.6);
        dcw.sustain = 0.7 * v[8];
        dcw.release = 0.6;
        Envelope amp;
        amp.attack = logScale (v[9], 0.002, 0.3);
        amp.decay = logScale (v[10], 0.3, 2.0);
        amp.sustain = 0.35 + 0.5 * frac (v[10] * ratio * 7.0);
        amp.release = logScale (frac (v[10] * 13.0), 0.4, 1.2);

        // The root on a pentatonic scale from C2 to C4, and a chord over it: a fifth, an
        // octave, or golden ratios (φ and φ², between the notes of the scale).
        constexpr std::array<int, 11> scale { 36, 38, 40, 43, 45, 48, 50, 52, 55, 57, 60 };
        const auto root = 440.0 * std::pow (2.0, (pick (scale, v[11]) - 69) / 12.0);
        const auto chord = pick (std::array<std::array<double, 3>, 6> {{
            { 1.0, 0.0, 0.0 }, { 1.0, 1.5, 0.0 }, { 1.0, 2.0, 0.0 }, { 1.0, ratio, 0.0 },
            { 1.0, 1.5, 2.0 }, { 1.0, ratio, ratio * ratio } }}, v[12]);
        notes = 0;
        for (size_t i = 0; i < chord.size(); ++i)
        {
            if (chord[i] <= 0.0)
                break;
            auto& vo = voice[(size_t) notes++];
            vo.hz = root * chord[i];
            vo.pan = notes == 1 ? 0.0 : (i % 2 == 1 ? -0.45 : 0.45);
            vo.amp = amp;
            vo.dcw = dcw;
            vo.phase = { golden ((std::int64_t) i), golden ((std::int64_t) i, 0.5) };
        }

        vibratoDepth = 15.0 * v[13] * v[13];   // cents
        vibratoHz = 4.0 + 2.5 * frac (v[13] * ratio * 5.0);
        holdSeconds = 1.0 + (ratio - 1.0) * v[15];
        pitchDrop = (frac (v[15] * 11.0) - 0.5) * 2.0;   // semitones, gone within ~0.1 s
        releaseSeconds = amp.release;
        gain = 0.3 / std::sqrt ((double) notes);
    }

    void StartupSound::prepare (double sampleRate)
    {
        rate = sampleRate > 0.0 ? sampleRate : 48000.0;
        reverb.prepare (rate);
        reverb.setTail (2.0);
        reverb.setMix (0.55);
        scratchLeft.assign (512, 0.0f);
        scratchRight.assign (512, 0.0f);
        total = (std::int64_t) (lengthSeconds() * rate);
        fade = std::max<std::int64_t> (1, (std::int64_t) (0.3 * rate));
        dcCoefficient = 1.0 - std::exp (-twoPi * 10.0 / rate);
        dcLeft = dcRight = 0.0;
        frame = 0;
    }

    bool StartupSound::render (float* left, float* right, int frames)
    {
        if (scratchLeft.empty())
            prepare (rate);
        if (frame >= total)
            return false;
        const auto holdFrames = (std::int64_t) (holdSeconds * rate);
        int done = 0;
        while (done < frames && frame < total)
        {
            const auto n = std::min<int> (frames - done, (int) scratchLeft.size());
            for (int i = 0; i < n; ++i)
            {
                const auto t = (double) (frame + i) / rate;
                const bool held = frame + i < holdFrames;
                vibratoPhase = frac (vibratoPhase + vibratoHz / rate);
                const auto bend = std::pow (2.0, (pitchDrop * std::exp (-t / 0.03)
                                                  + vibratoDepth * 0.01 * std::min (1.0, t / 0.6) * std::sin (twoPi * vibratoPhase)) / 12.0);
                double outLeft = 0.0, outRight = 0.0;
                for (int k = 0; k < notes; ++k)
                {
                    auto& vo = voice[(size_t) k];
                    const auto a = vo.amp.next (rate, held);
                    const auto d = vo.dcw.next (rate, held);
                    std::array<double, 2> s {};
                    for (size_t l = 0; l < 2; ++l)
                    {
                        const auto step = vo.hz * line[l].ratio * bend / rate;
                        vo.phase[l] = frac (vo.phase[l] + step);
                        s[l] = pdWave (line[l].wave, vo.phase[l], d * line[l].depth, std::min (0.5, 2.0 * step));
                    }
                    const auto mixed = ring ? (s[0] + line[1].level * s[0] * s[1]) / (1.0 + line[1].level)
                                            : (s[0] + line[1].level * s[1]) / (1.0 + line[1].level);
                    const auto out = mixed * a * gain;
                    outLeft += out * std::cos (halfPi * 0.5 * (1.0 + vo.pan));
                    outRight += out * std::sin (halfPi * 0.5 * (1.0 + vo.pan));
                }
                // Bent cosines carry some DC; a high-pass at about 10 Hz takes it off.
                dcLeft += (outLeft - dcLeft) * dcCoefficient;
                dcRight += (outRight - dcRight) * dcCoefficient;
                scratchLeft[(size_t) i] = (float) (outLeft - dcLeft);
                scratchRight[(size_t) i] = (float) (outRight - dcRight);
            }
            reverb.process (scratchLeft.data(), scratchRight.data(), n);
            for (int i = 0; i < n; ++i)
            {
                const auto left0 = total - (frame + i);
                const auto f = left0 < fade ? (float) left0 / (float) fade : 1.0f;
                left[done + i] += f * scratchLeft[(size_t) i];
                right[done + i] += f * scratchRight[(size_t) i];
            }
            done += n;
            frame += n;
        }
        return frame < total;
    }
}

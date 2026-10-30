// SPDX-License-Identifier: AGPL-3.0-only
#include "Modulators.h"
#include "PhiModulation.h"

#include <algorithm>
#include <cmath>
#include <cstdio>

namespace modrack
{
    namespace
    {
        constexpr double pi = 3.141592653589793;
        constexpr double twoPi = 6.283185307179586;

        double frac (double v) { return v - std::floor (v); }
        float clamp01 (float v) { return std::clamp (v, 0.0f, 1.0f); }

        // The same "random" value every time for event n of a stream and seed: 0 <= value < 1.
        double hash (std::int64_t n, int stream, float seed)
        {
            auto x = (std::uint64_t) n * 0x9e3779b97f4a7c15ull ^ ((std::uint64_t) (stream + 1) << 52)
                   ^ (std::uint64_t) std::llround (clamp01 (seed) * 1.0e9);
            x ^= x >> 30; x *= 0xbf58476d1ce4e5b9ull;
            x ^= x >> 27; x *= 0x94d049bb133111ebull;
            x ^= x >> 31;
            return (double) (x >> 11) / 9007199254740992.0;
        }

        float ease (double t) { return (float) (0.5 - 0.5 * std::cos (pi * std::clamp (t, 0.0, 1.0))); }

        std::vector<const char*> counting (int from, int to)
        {
            static const char* numbers[] { "0", "1", "2", "3", "4", "5", "6", "7", "8", "9", "10", "11", "12", "13", "14", "15", "16" };
            std::vector<const char*> v;
            for (int i = from; i <= to; ++i)
                v.push_back (numbers[i]);
            return v;
        }

        Control knob (const char* name, float low, float high, float initial, const char* unit = "", bool logarithmic = false)
        {
            Control c;
            c.name = name;
            c.low = low;
            c.high = high;
            c.initial = initial;
            c.unit = unit;
            c.logarithmic = logarithmic;
            return c;
        }

        Control pick (const char* name, std::vector<const char*> choices, int initial)
        {
            auto c = knob (name, 0.0f, (float) choices.size() - 1.0f, (float) initial);
            c.choices = std::move (choices);
            return c;
        }

        Control rateControl (float initial, float high = 20.0f) { return knob ("Rate", 0.01f, high, initial, "Hz", true); }

        Control sourceControl (const char* name)
        {
            auto c = knob (name, 0.0f, (float) maxModulators, 0.0f);
            c.source = true;
            return c;
        }

        // Shaper's steps: smooth, then Fibonacci-ish counts.
        constexpr int shaperSteps[] { 0, 2, 3, 4, 5, 8, 13 };

        std::vector<Control> stepControls()
        {
            std::vector<Control> c { rateControl (4.0f, 40.0f), pick ("Steps", counting (1, 16), 7), knob ("Glide", 0, 1, 0) };
            // A pattern to start: the golden sequence over the steps.
            for (int s = 0; s < maxSteps; ++s)
            {
                static char names[maxSteps][8] {};
                std::snprintf (names[s], sizeof names[s], "Step %d", s + 1);
                c.push_back (knob (names[s], 0.0f, 1.0f, (float) phi::golden (s)));
            }
            return c;
        }
    }

    const char* kindName (Kind k)
    {
        static const char* names[numKinds] { "LFO", "Envelope Follower", "Step Modulator", "Random", "Sample & Hold",
                                             "Shaper", "Attack-Decay", "Macro Knob", "Crossfader", "Φ Flow" };
        return names[std::clamp ((int) k, 0, numKinds - 1)];
    }

    const std::vector<Control>& controls (Kind k)
    {
        static const std::vector<Control> table[numKinds] {
            // LFO: Morph blends towards the next shape, Fold folds the wave back on itself.
            { rateControl (1.0f), pick ("Shape", std::vector<const char*> { "Sine", "Triangle", "Saw Up", "Saw Down", "Square", "Φ Drift" }, 0),
              knob ("Morph", 0, 1, 0), knob ("Fold", 0, 1, 0), knob ("Phase", 0, 1, 0) },
            // Envelope Follower: the chain's input level, Floor to 0 dB.
            { knob ("Attack", 0.1f, 500.0f, 10.0f, "ms", true), knob ("Release", 1.0f, 5000.0f, 233.0f, "ms", true),
              knob ("Gain", -24.0f, 24.0f, 0.0f, "dB"), knob ("Floor", -90.0f, -6.0f, -60.0f, "dB") },
            stepControls(),
            // Random: Smooth glides between values; Déjà vu (after Marbles) replays a loop of Length.
            { rateControl (2.0f, 40.0f), knob ("Smooth", 0, 1, 0.5f), knob ("Déjà vu", 0, 1, 0),
              pick ("Length", counting (1, 16), 7), knob ("Seed", 0, 1, 0) },
            // Sample & Hold: another modulator (or noise) caught Rate times a second.
            { sourceControl ("Source"), rateControl (4.0f, 40.0f), knob ("Seed", 0, 1, 0) },
            // Shaper: another modulator bent, stepped and turned over.
            { sourceControl ("Source"), knob ("Bend", -1, 1, 0), pick ("Steps", std::vector<const char*> { "Smooth", "2", "3", "4", "5", "8", "13" }, 0),
              pick ("Invert", std::vector<const char*> { "Off", "On" }, 0) },
            // Attack-Decay: fired when the input rises past Threshold, or Retrigger times a second.
            { knob ("Attack", 0.1f, 2000.0f, 5.0f, "ms", true), knob ("Decay", 1.0f, 5000.0f, 377.0f, "ms", true),
              knob ("Threshold", -60.0f, 0.0f, -24.0f, "dB"), knob ("Retrigger", 0.0f, 20.0f, 0.0f, "Hz"), knob ("Curve", -1, 1, 0) },
            { knob ("Value", 0, 1, 0.5f) },
            { sourceControl ("Source A"), sourceControl ("Source B"), knob ("Position", 0, 1, 0.5f),
              pick ("Curve", std::vector<const char*> { "Linear", "Equal Power" }, 1) },
            // Φ Flow: the sequence of Φ Modulation's Flow page, one of its four outputs.
            { rateControl (0.1f), knob ("Ratio", 1.05f, 4.0f, (float) phi::ratio, "", true),
              pick ("Mode", std::vector<const char*> { "Periodic", "Irrational", "Fibonacci", "Hybrid" }, 1),
              pick ("Shape", std::vector<const char*> { "Smooth", "Stepped", "Wave" }, 0), pick ("Output", counting (1, 4), 0),
              knob ("Jitter", 0, 1, 0.25f), knob ("Seed", 0, 1, 0) },
        };
        return table[std::clamp ((int) k, 0, numKinds - 1)];
    }

    Modulator make (Kind k)
    {
        Modulator m;
        m.kind = k;
        m.name = kindName (k);
        const auto& c = controls (k);
        for (size_t i = 0; i < c.size() && i < (size_t) maxControls; ++i)
            m.values[i] = c[i].initial;
        return m;
    }

    int stepControl (int s) { return 3 + std::clamp (s, 0, maxSteps - 1); }

    //==========================================================================
    void Rack::prepare (double sampleRate)
    {
        rate = std::max (1.0, sampleRate);
        modulators.reserve (maxModulators);
        states.reserve (maxModulators);
        reset();
    }

    void Rack::set (const std::vector<Modulator>& m)
    {
        const auto n = std::min ((int) m.size(), maxModulators);
        modulators.assign (m.begin(), m.begin() + n);
        states.resize ((size_t) n);
        for (int i = 0; i < n; ++i)
            if (states[(size_t) i].kind != modulators[(size_t) i].kind)
            {
                states[(size_t) i] = {};
                states[(size_t) i].kind = modulators[(size_t) i].kind;
            }
    }

    void Rack::reset()
    {
        for (size_t i = 0; i < states.size(); ++i)
        {
            states[i] = {};
            states[i].kind = modulators[i].kind;
        }
    }

    float Rack::value (int m) const
    {
        return m >= 0 && m < (int) states.size() ? states[(size_t) m].value : 0.0f;
    }

    float Rack::source (int control, const Modulator& m) const
    {
        const auto chosen = (int) std::lround (m.values[(size_t) control]) - 1;   // 0 is none
        return chosen >= 0 && chosen < (int) states.size() ? states[(size_t) chosen].value : 0.0f;
    }

    float Rack::lfoAt (const Modulator& m, double phase) const
    {
        auto shapeAt = [phase] (int shape)
        {
            const auto p = frac (phase);
            switch (shape)
            {
                case 0:  return 0.5 - 0.5 * std::cos (twoPi * p);
                case 1:  return 1.0 - std::abs (2.0 * p - 1.0);
                case 2:  return p;
                case 3:  return 1.0 - p;
                case 4:  return p < 0.5 ? 1.0 : 0.0;
                default:
                {
                    // Φ Drift: three sines at the rate × 1, φ and φ², a shape that never comes back.
                    double sum = 0.0;
                    for (int k = 0; k < 3; ++k)
                        sum += 0.5 - 0.5 * std::cos (twoPi * phase * phi::power (k));
                    return sum / 3.0;
                }
            }
        };
        const auto shape = std::clamp ((int) std::lround (m.values[1]), 0, 5);
        const auto morph = (double) clamp01 (m.values[2]);
        auto v = shapeAt (shape) * (1.0 - morph) + shapeAt ((shape + 1) % 6) * morph;
        // Fold: the wave driven past the top folds back down, a triangle fold.
        const auto x = v * (1.0 + 4.0 * clamp01 (m.values[3]));
        v = 1.0 - std::abs (1.0 - std::fmod (x, 2.0));
        return (float) std::clamp (v, 0.0, 1.0);
    }

    void Rack::process (const float* const* input, int numChannels, int frames)
    {
        if (frames <= 0)
            return;
        const auto dt = frames / rate;
        constexpr double wrap = 1099511627776.0;   // 2^40: keeps the fractions fine

        // The input's loudest sample this block, and the follower run over it sample by sample.
        float peak = 0.0f;
        for (int ch = 0; ch < numChannels && input != nullptr; ++ch)
            for (int i = 0; i < frames; ++i)
                peak = std::max (peak, std::abs (input[ch][i]));
        const auto peakDb = 20.0 * std::log10 (peak + 1.0e-9);

        for (size_t index = 0; index < modulators.size(); ++index)
        {
            const auto& m = modulators[index];
            auto& s = states[index];
            const auto* v = m.values.data();
            switch (m.kind)
            {
                case Kind::lfo:
                    s.phase = std::fmod (s.phase + std::max (0.0f, v[0]) * dt, wrap);
                    s.value = lfoAt (m, s.phase + v[4]);
                    break;

                case Kind::follower:
                {
                    const auto attack = std::exp (-1.0 / std::max (1.0, v[0] * 0.001 * rate));
                    const auto release = std::exp (-1.0 / std::max (1.0, v[1] * 0.001 * rate));
                    for (int i = 0; i < frames; ++i)
                    {
                        float x = 0.0f;
                        for (int ch = 0; ch < numChannels && input != nullptr; ++ch)
                            x = std::max (x, std::abs (input[ch][i]));
                        const auto c = x > s.envelope ? attack : release;
                        s.envelope = x + (s.envelope - x) * c;
                    }
                    const auto db = 20.0 * std::log10 (s.envelope + 1.0e-9) + v[2];
                    s.value = clamp01 ((float) ((db - v[3]) / -std::min (-1.0f, v[3])));
                    break;
                }

                case Kind::steps:
                {
                    const auto count = std::clamp ((int) std::lround (v[1]) + 1, 1, maxSteps);
                    s.phase = std::fmod (s.phase + std::max (0.0f, v[0]) * dt, wrap);
                    const auto n = (std::int64_t) std::floor (s.phase);
                    const auto f = s.phase - (double) n;
                    const auto now = clamp01 (v[stepControl ((int) (n % count))]);
                    const auto before = clamp01 (v[stepControl ((int) ((n + count - 1) % count))]);
                    const auto glide = (double) clamp01 (v[2]);
                    s.value = glide > 0.0 && f < glide ? before + (now - before) * ease (f / glide) : now;
                    break;
                }

                case Kind::random:
                {
                    s.phase = std::fmod (s.phase + std::max (0.0f, v[0]) * dt, wrap);
                    const auto n = (std::int64_t) std::floor (s.phase);
                    if (n != s.event)
                    {
                        // Déjà vu, after Marbles: up to the middle, a growing chance of playing the
                        // loop again in order (always at the middle); past it, of playing it in
                        // another order. The loop takes whatever is played.
                        s.event = n;
                        const auto length = std::clamp ((int) std::lround (v[3]) + 1, 1, maxSteps);
                        const auto dejaVu = clamp01 (v[2]);
                        const auto replay = std::min (1.0f, 2.0f * dejaVu), shuffle = std::max (0.0f, 2.0f * dejaVu - 1.0f);
                        const auto slot = (int) (n % length);
                        auto next = (float) hash (n, 0, v[4]);
                        if (n >= length && hash (n, 1, v[4]) < replay)   // the first round fills the loop
                            next = s.loop[(size_t) (hash (n, 2, v[4]) < shuffle ? (int) (hash (n, 3, v[4]) * length) % length : slot)];
                        s.loop[(size_t) slot] = next;
                        s.from = s.value;
                        s.to = next;
                    }
                    const auto smooth = (double) clamp01 (v[1]);
                    const auto f = s.phase - (double) n;
                    s.value = smooth > 0.0 && f < smooth ? s.from + (s.to - s.from) * ease (f / smooth) : s.to;
                    break;
                }

                case Kind::sampleHold:
                {
                    s.phase = std::fmod (s.phase + std::max (0.0f, v[1]) * dt, wrap);
                    const auto n = (std::int64_t) std::floor (s.phase);
                    if (n != s.event)
                    {
                        s.event = n;
                        s.held = std::lround (v[0]) > 0 ? source (0, m) : (float) hash (n, 4, v[2]);
                    }
                    s.value = s.held;
                    break;
                }

                case Kind::shaper:
                {
                    auto x = source (0, m);
                    if (v[3] > 0.5f)
                        x = 1.0f - x;
                    x = (float) std::pow ((double) clamp01 (x), std::pow (4.0, -(double) std::clamp (v[1], -1.0f, 1.0f)));
                    const auto steps = shaperSteps[std::clamp ((int) std::lround (v[2]), 0, 6)];
                    if (steps > 1)
                        x = std::round (x * (steps - 1)) / (float) (steps - 1);
                    s.value = clamp01 (x);
                    break;
                }

                case Kind::attackDecay:
                {
                    // Fired on the way up past the threshold; ready again 6 dB below it.
                    if (peakDb > v[2] && s.armed)
                    {
                        s.stage = 1;
                        s.armed = false;
                    }
                    else if (peakDb < v[2] - 6.0f)
                        s.armed = true;
                    if (v[3] > 0.0f)
                    {
                        s.phase += v[3] * dt;
                        if (s.phase >= 1.0)
                        {
                            s.phase = frac (s.phase);
                            s.stage = 1;
                        }
                    }
                    if (s.stage == 1)
                    {
                        s.envelope += dt / std::max (1.0e-4, v[0] * 0.001);
                        if (s.envelope >= 1.0)
                        {
                            s.envelope = 1.0;
                            s.stage = 2;
                        }
                    }
                    else if (s.stage == 2)
                    {
                        s.envelope -= dt / std::max (1.0e-3, v[1] * 0.001);
                        if (s.envelope <= 0.0)
                        {
                            s.envelope = 0.0;
                            s.stage = 0;
                        }
                    }
                    s.value = (float) std::pow (std::clamp (s.envelope, 0.0, 1.0), std::pow (4.0, -(double) std::clamp (v[4], -1.0f, 1.0f)));
                    break;
                }

                case Kind::macro:
                    s.value = clamp01 (v[0]);
                    break;

                case Kind::crossfader:
                {
                    const auto a = source (0, m), b = source (1, m), p = clamp01 (v[2]);
                    s.value = clamp01 (v[3] > 0.5f ? a * (float) std::cos (p * pi / 2.0) + b * (float) std::sin (p * pi / 2.0)
                                                   : a + (b - a) * p);
                    break;
                }

                case Kind::phiFlow:
                {
                    phi::Flow::Settings f;
                    f.rateHz = std::max (0.0f, v[0]);
                    f.ratio = v[1];
                    f.mode = (phi::Flow::Mode) std::clamp ((int) std::lround (v[2]), 0, phi::Flow::numModes - 1);
                    f.shape = (phi::Flow::Shape) std::clamp ((int) std::lround (v[3]), 0, phi::Flow::numShapes - 1);
                    f.jitter = v[5];
                    f.seed = v[6];
                    const auto output = std::clamp ((int) std::lround (v[4]), 0, phi::Flow::numOutputs - 1);
                    s.phase = std::fmod (s.phase + phi::Flow::outputRate (f, output) * dt, wrap);
                    s.value = (float) phi::Flow::at (f, output, s.phase);
                    break;
                }
            }
        }
    }
}

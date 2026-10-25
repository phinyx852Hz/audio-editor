// SPDX-License-Identifier: AGPL-3.0-only
#include "PhiSitar.h"

#include <algorithm>
#include <cmath>

namespace phi
{
    namespace
    {
        constexpr double lowestHz = 20.0;

        float cubic (const std::vector<float>& line, double position)
        {
            const auto size = (int) line.size();
            const auto base = (int) std::floor (position);
            const auto t = (float) (position - base);
            auto at = [&] (int i) { return line[(size_t) (((base + i) % size + size) % size)]; };
            const float y0 = at (-1), y1 = at (0), y2 = at (1), y3 = at (2);
            const float c1 = 0.5f * (y2 - y0), c2 = y0 - 2.5f * y1 + 2.0f * y2 - 0.5f * y3, c3 = 0.5f * (y3 - y0) + 1.5f * (y1 - y2);
            return ((c3 * t + c2) * t + c1) * t + y1;
        }
    }

    void SitarString::prepare (double sampleRate)
    {
        rate = sampleRate;
        line.assign ((size_t) (rate / lowestHz) + 8, 0.0f);
        write = 0;
        glide = 1.0 - std::exp (-1.0 / (0.004 * rate));
        dcPole = (float) (1.0 - 6.283185307179586 * 15.0 / rate);
        silence();
    }

    void SitarString::silence()
    {
        std::fill (line.begin(), line.end(), 0.0f);
        burst.clear();
        low = drive = energy = dcIn = dcOut = 0.0f;
    }

    void SitarString::setDecay (double seconds)
    {
        decaySeconds = std::max (0.02, seconds);
    }

    void SitarString::pluck (double hz, double velocity, double bright)
    {
        current = target = std::clamp (hz, lowestHz * 1.5, rate / 4.0);
        velocity = std::clamp (velocity, 0.0, 1.0);
        // The loop's damping (high harmonics die first) and the pluck's own tone, harder plucks brighter.
        brightness = (float) std::clamp (0.8 + 0.17 * bright * (0.5 + 0.5 * velocity), 0.6, 0.98);
        const auto pluckTone = (float) std::clamp (0.3 + 0.6 * bright * (0.5 + 0.5 * velocity), 0.1, 0.95);
        // One period of noise, plucked an eighth of the way along (the mizrāb's place): the comb
        // takes out the harmonics with a node there.
        const auto period = std::max (2, (int) std::lround (rate / current));
        std::vector<float> noise ((size_t) period);
        float smooth = 0.0f;
        for (auto& n : noise)
        {
            seed = seed * 1664525u + 1013904223u;
            const auto white = (float) ((seed >> 9) * (1.0 / 8388608.0)) * 2.0f - 1.0f;
            smooth += pluckTone * (white - smooth);
            n = smooth;
        }
        const auto at = std::max (1, period / 8);
        burst.assign ((size_t) period, 0.0f);
        for (int i = 0; i < period; ++i)
            burst[(size_t) i] = (float) velocity * 0.5f * (noise[(size_t) i] - (i >= at ? noise[(size_t) (i - at)] : 0.0f));
        burstAt = 0;
        energy = std::max (energy, 1.0e-3f);
    }

    float SitarString::tick()
    {
        current += (target - current) * glide;
        const auto size = (int) line.size();
        // The loop's own delay (the one-pole's) comes off the line's, so the pitch is true.
        const double filterDelay = (1.0 - brightness) / brightness;
        const double length = std::clamp (rate / current - filterDelay, 2.0, (double) size - 4.0);
        auto y = cubic (line, write - length);
        // Jawārī: pressed against the bridge, the string is shorter; its pitch flickers with its
        // own displacement, and that throws energy into the high harmonics (the buzz).
        if (jawari > 0.0 && y > 0.0f)
            y = cubic (line, write - length * (1.0 - jawari * 0.005 * std::min (1.0f, y * 4.0f)));
        const auto loss = (float) std::pow (10.0, -3.0 / (decaySeconds * current));
        low += brightness * (y - low);
        float in = low * loss + drive;
        drive = 0.0f;
        if (burstAt < (int) burst.size())
            in += burst[(size_t) burstAt++];
        dcOut = in - dcIn + dcPole * dcOut;
        dcIn = in;
        line[(size_t) write] = dcOut;
        write = (write + 1) % size;
        energy = 0.9995f * energy + 0.0005f * y * y;
        return y;
    }

    //==========================================================================
    void Sitar::Resonance::set (double hz, double q, double rate)
    {
        // A band-pass (RBJ, constant 0 dB peak gain).
        const auto w = 6.283185307179586 * hz / rate, alpha = std::sin (w) / (2.0 * q), a0 = 1.0 + alpha;
        b0 = (float) (alpha / a0);
        a1 = (float) (-2.0 * std::cos (w) / a0);
        a2 = (float) ((1.0 - alpha) / a0);
        x1 = x2 = y1 = y2 = 0.0f;
    }

    float Sitar::Resonance::tick (float x)
    {
        const auto y = b0 * x - b0 * x2 - a1 * y1 - a2 * y2;
        x2 = x1;
        x1 = x;
        y2 = y1;
        y1 = y;
        return y;
    }

    void Sitar::prepare (double sampleRate)
    {
        rate = sampleRate;
        for (auto& p : played)
        {
            p.string.prepare (rate);
            p.string.setDecay (5.0);
            p.held = false;
        }
        for (auto& t : tarab)
        {
            t.prepare (rate);
            t.setDecay (7.0);
            t.setJawari (0.4);
        }
        for (auto& c : chikari)
        {
            c.prepare (rate);
            c.setDecay (2.5);
        }
        body[0].set (190.0, 2.0, rate);   // the tumbā
        body[1].set (520.0, 3.0, rate);   // the top plate
        setChikari (261.6255653005986);
    }

    void Sitar::setJawari (double amount)
    {
        for (auto& p : played)
            p.string.setJawari (amount);
        for (auto& c : chikari)
            c.setJawari (amount);
    }

    int Sitar::pluck (double hz, double velocity)
    {
        // A free string, else the one plucked longest ago.
        int pick = 0;
        for (int i = 0; i < maxPlayed; ++i)
        {
            if (! played[(size_t) i].held && ! played[(size_t) i].string.ringing())
            {
                pick = i;
                break;
            }
            if (played[(size_t) i].age < played[(size_t) pick].age)
                pick = i;
        }
        auto& p = played[(size_t) pick];
        p.string.setDecay (8.0);
        p.string.pluck (hz, velocity);
        p.held = true;
        p.age = ++clock;
        return pick;
    }

    void Sitar::setHz (int string, double hz)
    {
        if (string >= 0 && string < maxPlayed)
            played[(size_t) string].string.setHz (hz);
    }

    void Sitar::release (int string)
    {
        if (string < 0 || string >= maxPlayed)
            return;
        played[(size_t) string].held = false;
        played[(size_t) string].string.setDecay (0.35);
    }

    void Sitar::setTarab (const std::vector<double>& hz)
    {
        tarabCount = std::min ((int) hz.size(), maxTarab);
        for (int i = 0; i < tarabCount; ++i)
        {
            tarab[(size_t) i].silence();
            tarab[(size_t) i].pluck (hz[(size_t) i], 0.0);   // tuned, at rest
        }
    }

    void Sitar::setChikari (double saHz)
    {
        chikari[0].tune (saHz * 2.0);
        chikari[1].tune (saHz * 4.0);
    }

    void Sitar::strikeChikari (double velocity)
    {
        for (auto& c : chikari)
            c.pluck (c.hz(), velocity * 0.6, 0.9);
    }

    void Sitar::reset()
    {
        for (auto& p : played)
        {
            p.string.silence();
            p.held = false;
        }
        for (int i = 0; i < tarabCount; ++i)
            tarab[(size_t) i].silence();
        for (auto& c : chikari)
            c.silence();
    }

    void Sitar::process (float* left, float* right, int frames)
    {
        const auto anyPlayed = std::any_of (played.begin(), played.end(), [] (const Played& p) { return p.string.ringing(); });
        const auto anyChikari = chikari[0].ringing() || chikari[1].ringing();
        bool anyTarab = false;
        for (int i = 0; i < tarabCount; ++i)
            anyTarab = anyTarab || tarab[(size_t) i].ringing();
        if (! anyPlayed && ! anyChikari && ! anyTarab)
            return;   // silent strings cost nothing
        for (int n = 0; n < frames; ++n)
        {
            float main = 0.0f, drone = 0.0f;
            for (auto& p : played)
                if (p.held || p.string.ringing())
                    main += p.string.tick();
            for (auto& c : chikari)
                if (c.ringing())
                    drone += c.tick();
            // The tarab hear the bridge: a little of what is played goes into each, and only the
            // ones tuned to it build up.
            float tarabLeft = 0.0f, tarabRight = 0.0f;
            const auto feed = (main + drone) * 0.004f;
            for (int i = 0; i < tarabCount; ++i)
            {
                auto& t = tarab[(size_t) i];
                t.excite (feed);
                const auto y = t.tick();
                (i % 2 == 0 ? tarabLeft : tarabRight) += y;
            }
            const auto dry = main * 0.8f + drone * 0.5f;
            const auto wood = dry + 0.6f * body[0].tick (dry) + 0.3f * body[1].tick (dry);
            left[n] += 0.5f * (wood + tarabLevel * (0.7f * tarabLeft + 0.3f * tarabRight));
            right[n] += 0.5f * (wood + tarabLevel * (0.3f * tarabLeft + 0.7f * tarabRight));
        }
    }
}

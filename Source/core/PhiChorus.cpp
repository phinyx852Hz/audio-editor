// SPDX-License-Identifier: AGPL-3.0-only
#include "PhiChorus.h"

#include <algorithm>
#include <cmath>

namespace phi
{
    namespace
    {
        constexpr double goldenRatio = 1.6180339887498949;
        constexpr double twoPi = 6.283185307179586;
        constexpr double goldenTurn = 0.3819660112501051;   // the golden angle, 137.5°, as a share of a turn (1 - 1/φ)

        int voicesOf (const Chorus::Settings& s) { return std::clamp (s.voices, 1, Chorus::maxVoices); }
        // Voice v's place among n, centred: -(n-1)/2 .. (n-1)/2.
        double centred (const Chorus::Settings& s, int voice) { return voice - (voicesOf (s) - 1) / 2.0; }
    }

    double Chorus::voiceRate (const Settings& s, int voice)
    {
        return s.rateHz * std::pow (goldenRatio, centred (s, voice) / 2.0);
    }

    double Chorus::voiceDelayMs (const Settings& s, int voice)
    {
        return s.delayMs * std::pow (goldenRatio, centred (s, voice) / voicesOf (s));
    }

    double Chorus::voicePan (const Settings& s, int voice)
    {
        if (voicesOf (s) == 1)
            return 0.0;
        // Around a circle by the golden angle from the centre, unrolled left to right: each
        // voice lands in the widest gap the others left (0, 0.76, -0.47, 0.29, -0.94...).
        return std::clamp (s.width, 0.0, 1.0) * (2.0 * std::fmod (0.5 + goldenTurn * voice, 1.0) - 1.0);
    }

    void Chorus::prepare (double sampleRate, int)
    {
        rate = sampleRate;
        line.assign ((size_t) std::ceil (maxDelayMs * sampleRate / 1000.0) + 4, 0.0f);
        reset();
    }

    void Chorus::set (const Settings& s)
    {
        settings = s;
        settings.delayMs = std::clamp (s.delayMs, 1.0, 34.0);
        settings.depthMs = std::clamp (s.depthMs, 0.0, 10.0);
    }

    void Chorus::reset()
    {
        std::fill (line.begin(), line.end(), 0.0f);
        writePos = 0;
        for (int v = 0; v < maxVoices; ++v)
        {
            phases[(size_t) v] = std::fmod (goldenTurn * v, 1.0);   // a golden angle apart
            shownDelay[(size_t) v].store (0.0);
        }
    }

    void Chorus::process (float* const* channels, int numChannels, int numFrames)
    {
        if (line.empty() || numChannels < 1)
            return;
        const auto n = voicesOf (settings);
        const auto size = (int) line.size();
        const auto mix = std::clamp (settings.mix, 0.0, 1.0);
        const auto voiceGain = 1.0 / std::sqrt ((double) n);
        std::array<double, maxVoices> step {}, base {}, left {}, right {};
        for (int v = 0; v < n; ++v)
        {
            step[(size_t) v] = voiceRate (settings, v) / rate;
            base[(size_t) v] = voiceDelayMs (settings, v) * rate / 1000.0;
            const auto pan = voicePan (settings, v);   // equal power
            left[(size_t) v] = std::cos ((pan + 1.0) * twoPi / 8.0) * voiceGain * std::sqrt (2.0);
            right[(size_t) v] = std::sin ((pan + 1.0) * twoPi / 8.0) * voiceGain * std::sqrt (2.0);
        }
        const auto depth = settings.depthMs * rate / 1000.0 / 2.0;
        std::array<double, maxVoices> now {};

        for (int i = 0; i < numFrames; ++i)
        {
            double in = 0.0;
            for (int ch = 0; ch < numChannels; ++ch)
                in += channels[ch][i];
            line[(size_t) writePos] = (float) (in / numChannels);

            double wetLeft = 0.0, wetRight = 0.0;
            for (int v = 0; v < n; ++v)
            {
                auto& phase = phases[(size_t) v];
                const auto delay = base[(size_t) v] + depth * (1.0 + std::sin (twoPi * phase));
                phase += step[(size_t) v];
                if (phase >= 1.0)
                    phase -= 1.0;
                now[(size_t) v] = delay;

                // Linear interpolation between the two samples around the delay.
                auto read = (double) writePos - delay;
                while (read < 0.0)
                    read += size;
                const auto a = (int) read;
                const auto frac = read - a;
                const auto y = line[(size_t) a] * (1.0 - frac) + line[(size_t) ((a + 1) % size)] * frac;
                wetLeft += y * left[(size_t) v];
                wetRight += y * right[(size_t) v];
            }
            writePos = (writePos + 1) % size;

            if (numChannels == 1)
                channels[0][i] = (float) (channels[0][i] * (1.0 - mix) + (wetLeft + wetRight) / std::sqrt (2.0) * mix);
            else
                for (int ch = 0; ch < numChannels; ++ch)
                    channels[ch][i] = (float) (channels[ch][i] * (1.0 - mix) + (ch % 2 == 0 ? wetLeft : wetRight) * mix);
        }
        for (int v = 0; v < maxVoices; ++v)
            shownDelay[(size_t) v].store (v < n ? now[(size_t) v] * 1000.0 / rate : 0.0);
    }
}

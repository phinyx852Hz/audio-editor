// SPDX-License-Identifier: AGPL-3.0-only
#include "PhiPhaser.h"
#include "Phi.h"

#include <algorithm>
#include <cmath>
#include <complex>

namespace phi
{
    namespace
    {
        constexpr double pi = 3.141592653589793;
        constexpr double twoPi = 2.0 * pi;
        // The golden angle as a share of a turn: 1 - 1/φ (137.5°).
        constexpr double goldenTurn = 1.0 - inverse;
        constexpr int updateEvery = 32;   // samples between new allpass coefficients

        int nearestStages (int n)
        {
            int best = Phaser::stageChoices.front();
            for (const auto c : Phaser::stageChoices)
                if (std::abs (c - n) < std::abs (best - n))
                    best = c;
            return best;
        }

        double coefficient (double hz, double rate)
        {
            const auto t = std::tan (pi * std::clamp (hz, 10.0, 0.45 * rate) / rate);
            return (t - 1.0) / (t + 1.0);
        }
    }

    double Phaser::stageHz (const Settings& s, int k, double lfoValue)
    {
        const auto middle = 0.5 * (s.stages - 1);
        return s.centreHz * std::pow (ratio, s.spread * (k - middle))
                          * std::pow (2.0, s.depth * sweepOctaves * lfoValue);
    }

    double Phaser::lfo (double seconds, double rateHz, double phase)
    {
        return lfoAt (rateHz * seconds, rateHz * ratio * seconds, phase);
    }

    double Phaser::lfoAt (double slowTurns, double fastTurns, double phase)
    {
        const auto slow = std::sin (twoPi * (slowTurns + phase));
        const auto fast = std::sin (twoPi * (fastTurns + phase));
        return (slow + inverse * fast) / (1.0 + inverse);
    }

    double Phaser::responseDb (const Settings& s, double hz, double sampleRate)
    {
        // The chain A(z) at rest, fed back a sample later as in process(): W = A / (1 - fb z⁻¹ A).
        const auto z1 = std::polar (1.0, -twoPi * hz / sampleRate);
        std::complex<double> chain (1.0, 0.0);
        for (int k = 0; k < s.stages; ++k)
        {
            const auto a = coefficient (stageHz (s, k, 0.0), sampleRate);
            chain *= (a + z1) / (1.0 + a * z1);
        }
        const auto wet = chain / (1.0 - s.feedback * z1 * chain);
        const auto h = (1.0 - s.mix) + s.mix * wet;
        return 20.0 * std::log10 (std::abs (h) + 1.0e-12);
    }

    void Phaser::prepare (double sampleRate, int numChannels)
    {
        rate = sampleRate > 0.0 ? sampleRate : 48000.0;
        state.assign ((size_t) std::max (1, numChannels), Channel {});
        mixStep = (float) (1.0 / (0.02 * rate));   // a mix change takes 20 ms
        reset();
    }

    void Phaser::set (const Settings& s)
    {
        settings = s;
        settings.stages = nearestStages (s.stages);
        settings.rateHz = std::clamp (s.rateHz, 0.001, 20.0);
        settings.depth = std::clamp (s.depth, 0.0, 1.0);
        settings.centreHz = std::clamp (s.centreHz, 20.0, 20000.0);
        settings.spread = std::clamp (s.spread, 0.0, 1.0);
        settings.feedback = std::clamp (s.feedback, -0.9, 0.9);
        settings.stereo = std::clamp (s.stereo, 0.0, 1.0);
        settings.mix = std::clamp (s.mix, 0.0, 1.0);
    }

    void Phaser::reset()
    {
        for (auto& c : state)
            c = Channel {};
        slowTurns = fastTurns = 0.0;
        sinceUpdate = 0;
        smoothedMix = (float) settings.mix;
        for (int ch = 0; ch < (int) state.size(); ++ch)
            update (ch, lfoAt (0.0, 0.0, ch % 2 == 1 ? -settings.stereo * goldenTurn : 0.0));
    }

    void Phaser::update (int ch, double lfoValue)
    {
        auto& c = state[(size_t) ch];
        for (int k = 0; k < settings.stages; ++k)
            c.a[(size_t) k] = (float) coefficient (stageHz (settings, k, lfoValue), rate);
    }

    void Phaser::process (float* const* channels, int numChannels, int numFrames)
    {
        if (state.empty())
            prepare (rate, numChannels);
        const auto n = std::min (numChannels, (int) state.size());
        const auto stages = settings.stages;
        const auto feedback = (float) settings.feedback;
        const auto target = (float) settings.mix;
        int done = 0;
        while (done < numFrames)
        {
            if (sinceUpdate <= 0)
            {
                for (int ch = 0; ch < n; ++ch)
                    update (ch, lfoAt (slowTurns, fastTurns, ch % 2 == 1 ? -settings.stereo * goldenTurn : 0.0));
                sinceUpdate = updateEvery;
            }
            const auto block = std::min (sinceUpdate, numFrames - done);
            const auto mixAtStart = smoothedMix;
            for (int ch = 0; ch < n; ++ch)
            {
                auto& c = state[(size_t) ch];
                auto* data = channels[ch] + done;
                auto mix = mixAtStart;
                for (int i = 0; i < block; ++i)
                {
                    mix += std::clamp (target - mix, -mixStep, mixStep);
                    const auto dry = data[i];
                    auto v = dry + feedback * c.last;
                    for (int k = 0; k < stages; ++k)
                    {
                        const auto a = c.a[(size_t) k];
                        const auto y = a * v + c.x1[(size_t) k] - a * c.y1[(size_t) k];
                        c.x1[(size_t) k] = v;
                        c.y1[(size_t) k] = std::abs (y) < 1.0e-20f ? 0.0f : y;   // no denormals as it dies away
                        v = y;
                    }
                    c.last = std::clamp (v, -4.0f, 4.0f);
                    data[i] = (1.0f - mix) * dry + mix * v;
                }
                if (ch == n - 1)
                    smoothedMix = mix;
            }
            done += block;
            sinceUpdate -= block;
            // Each LFO keeps its own phase, so a change of rate never jumps.
            slowTurns += settings.rateHz * block / rate;
            fastTurns += settings.rateHz * ratio * block / rate;
            slowTurns -= std::floor (slowTurns);
            fastTurns -= std::floor (fastTurns);
        }
    }
}

// SPDX-License-Identifier: AGPL-3.0-only
#include "Dsp.h"

#include <algorithm>
#include <cmath>
#include <numeric>

namespace dsp
{
    namespace
    {
        constexpr double pi = 3.14159265358979323846;

        double power (double lufs)        { return std::pow (10.0, (lufs + 0.691) / 10.0); }
        double lufsFromPower (double p)   { return p > 0.0 ? -0.691 + 10.0 * std::log10 (p) : -200.0; }
    }

    float fadeGain (double t, FadeShape shape)
    {
        t = std::clamp (t, 0.0, 1.0);

        switch (shape)
        {
            case FadeShape::linear:      return (float) t;
            case FadeShape::equalPower:  return (float) std::sin (t * pi * 0.5);
            case FadeShape::exponential: return (float) (t * t);
            case FadeShape::sCurve:      return (float) (0.5 - 0.5 * std::cos (t * pi));
        }

        return (float) t;
    }

    AudioDataPtr applyGain (const AudioData& d, Frame start, Frame end, double gainDb)
    {
        const auto g = (float) dbToGain (gainDb);
        return d.processed (start, end, [g] (auto& block, Frame)
        {
            for (auto& ch : block)
                for (auto& s : ch)
                    s *= g;
        });
    }

    static AudioDataPtr fade (const AudioData& d, Frame start, Frame end, FadeShape shape, bool rising)
    {
        const auto length = (double) std::max<Frame> (1, end - start - 1);
        return d.processed (start, end, [=] (auto& block, Frame blockStart)
        {
            for (auto& ch : block)
                for (size_t i = 0; i < ch.size(); ++i)
                {
                    const auto t = (double) (blockStart + (Frame) i - start) / length;
                    ch[i] *= fadeGain (rising ? t : 1.0 - t, shape);
                }
        });
    }

    AudioDataPtr fadeIn (const AudioData& d, Frame s, Frame e, FadeShape shape)  { return fade (d, s, e, shape, true); }
    AudioDataPtr fadeOut (const AudioData& d, Frame s, Frame e, FadeShape shape) { return fade (d, s, e, shape, false); }

    AudioDataPtr invert (const AudioData& d, Frame start, Frame end)
    {
        return d.processed (start, end, [] (auto& block, Frame)
        {
            for (auto& ch : block)
                for (auto& s : ch)
                    s = -s;
        });
    }

    AudioDataPtr reverse (const AudioData& d, Frame start, Frame end)
    {
        const auto n = end - start;
        std::vector<std::vector<float>> buffer ((size_t) d.numChannels(), std::vector<float> ((size_t) n));
        std::vector<float*> ptrs;
        for (auto& b : buffer)
            ptrs.push_back (b.data());

        d.read (start, (int) n, ptrs.data());
        for (auto& b : buffer)
            std::reverse (b.begin(), b.end());

        AudioData::Builder builder (d.numChannels(), d.sampleRate());
        builder.append (ptrs.data(), (int) n);
        const auto reversed = builder.finish();
        return d.replaced (start, end, reversed.get());
    }

    AudioDataPtr removeDcOffset (const AudioData& d, Frame start, Frame end)
    {
        std::vector<double> sums ((size_t) d.numChannels(), 0.0);
        const auto n = std::max<Frame> (1, end - start);

        d.processed (start, end, [&] (auto& block, Frame)
        {
            for (size_t ch = 0; ch < block.size(); ++ch)
                sums[ch] += std::accumulate (block[ch].begin(), block[ch].end(), 0.0);
        });

        return d.processed (start, end, [&] (auto& block, Frame)
        {
            for (size_t ch = 0; ch < block.size(); ++ch)
            {
                const auto mean = (float) (sums[ch] / (double) n);
                for (auto& s : block[ch])
                    s -= mean;
            }
        });
    }

    AudioDataPtr silence (int numChannels, double sampleRate, Frame length)
    {
        AudioData::Builder builder (numChannels, sampleRate);
        std::vector<float> zeros ((size_t) AudioChunk::maxFrames, 0.0f);
        std::vector<const float*> ptrs ((size_t) numChannels, zeros.data());

        for (Frame pos = 0; pos < length; pos += AudioChunk::maxFrames)
            builder.append (ptrs.data(), (int) std::min<Frame> (AudioChunk::maxFrames, length - pos));

        return builder.finish();
    }

    //==============================================================================
    float KWeighting::Biquad::process (float x)
    {
        const double y = b0 * x + z1;
        z1 = b1 * x - a1 * y + z2;
        z2 = b2 * x - a2 * y;
        return (float) y;
    }

    void KWeighting::prepare (double sampleRate)
    {
        {
            // Stage 1: high shelf (+4 dB above ~1.7 kHz), coefficients per ITU-R BS.1770 at any rate.
            const double f0 = 1681.974450955533, gainDb = 3.999843853973347, q = 0.7071752369554196;
            const double k = std::tan (pi * f0 / sampleRate);
            const double vh = std::pow (10.0, gainDb / 20.0);
            const double vb = std::pow (vh, 0.4996667741545416);
            const double a0 = 1.0 + k / q + k * k;
            shelf.b0 = (vh + vb * k / q + k * k) / a0;
            shelf.b1 = 2.0 * (k * k - vh) / a0;
            shelf.b2 = (vh - vb * k / q + k * k) / a0;
            shelf.a1 = 2.0 * (k * k - 1.0) / a0;
            shelf.a2 = (1.0 - k / q + k * k) / a0;
        }
        {
            // Stage 2: high pass at ~38 Hz.
            const double f0 = 38.13547087602444, q = 0.5003270373238773;
            const double k = std::tan (pi * f0 / sampleRate);
            const double a0 = 1.0 + k / q + k * k;
            highPass.b0 = 1.0;
            highPass.b1 = -2.0;
            highPass.b2 = 1.0;
            highPass.a1 = 2.0 * (k * k - 1.0) / a0;
            highPass.a2 = (1.0 - k / q + k * k) / a0;
        }
        reset();
    }

    float KWeighting::process (float x) { return highPass.process (shelf.process (x)); }

    void KWeighting::reset()
    {
        shelf.z1 = shelf.z2 = highPass.z1 = highPass.z2 = 0.0;
    }

    //==============================================================================
    TruePeakDetector::TruePeakDetector()
    {
        // Windowed-sinc interpolation filter, 48 taps split into 4 polyphase branches.
        constexpr int totalTaps = tapsPerPhase * 4;
        for (int i = 0; i < totalTaps; ++i)
        {
            const double x = (i - (totalTaps - 1) / 2.0) / 4.0;
            const double sinc = std::abs (x) < 1.0e-12 ? 1.0 : std::sin (pi * x) / (pi * x);
            const double window = 0.5 - 0.5 * std::cos (2.0 * pi * (i + 0.5) / totalTaps);
            phases[(size_t) (i % 4)][(size_t) (i / 4)] = (float) (sinc * window);
        }
    }

    float TruePeakDetector::process (float x)
    {
        history[(size_t) writeIndex] = x;
        writeIndex = (writeIndex + 1) % tapsPerPhase;

        float peak = 0.0f;
        for (const auto& phase : phases)
        {
            float y = 0.0f;
            for (int t = 0; t < tapsPerPhase; ++t)
                y += phase[(size_t) t] * history[(size_t) ((writeIndex + tapsPerPhase - 1 - t) % tapsPerPhase)];

            peak = std::max (peak, std::abs (y));
        }

        return peak;
    }

    void TruePeakDetector::reset()
    {
        history.fill (0.0f);
        writeIndex = 0;
    }

    //==============================================================================
    double loudnessChannelWeight (int channel, int numChannels)
    {
        if (numChannels == 6)
        {
            if (channel == 3) return 0.0;              // LFE
            if (channel >= 4) return 1.41;             // Ls, Rs
        }

        return 1.0;
    }

    double integratedLoudness (const std::vector<double>& blockPowers)
    {
        // 400 ms gating blocks with 75% overlap = 4 consecutive 100 ms blocks.
        std::vector<double> gating;
        for (size_t i = 3; i < blockPowers.size(); ++i)
            gating.push_back ((blockPowers[i] + blockPowers[i - 1] + blockPowers[i - 2] + blockPowers[i - 3]) / 4.0);

        const auto absoluteGate = power (-70.0);
        std::vector<double> aboveAbsolute;
        std::copy_if (gating.begin(), gating.end(), std::back_inserter (aboveAbsolute),
                      [&] (double p) { return p > absoluteGate; });

        if (aboveAbsolute.empty())
            return -200.0;

        const auto relativeGate = std::accumulate (aboveAbsolute.begin(), aboveAbsolute.end(), 0.0)
                                  / (double) aboveAbsolute.size() * power (-10.0 - 0.691) / power (-0.691);

        double sum = 0.0;
        int count = 0;
        for (auto p : aboveAbsolute)
            if (p > relativeGate) { sum += p; ++count; }

        return count > 0 ? lufsFromPower (sum / count) : -200.0;
    }

    static double loudnessRange (const std::vector<double>& blockPowers)
    {
        // EBU Tech 3342: 3 s short-term windows (30 blocks), absolute -70 and relative -20 LU gates,
        // range between the 10th and 95th percentiles.
        std::vector<double> shortTerm;
        for (size_t i = 29; i < blockPowers.size(); i += 10)
        {
            double sum = 0.0;
            for (size_t j = i - 29; j <= i; ++j)
                sum += blockPowers[j];
            shortTerm.push_back (sum / 30.0);
        }

        std::vector<double> gated;
        std::copy_if (shortTerm.begin(), shortTerm.end(), std::back_inserter (gated),
                      [] (double p) { return p > power (-70.0); });
        if (gated.size() < 2)
            return 0.0;

        const auto mean = std::accumulate (gated.begin(), gated.end(), 0.0) / (double) gated.size();
        const auto relGate = lufsFromPower (mean) - 20.0;

        std::vector<double> lufs;
        for (auto p : gated)
            if (lufsFromPower (p) > relGate)
                lufs.push_back (lufsFromPower (p));

        if (lufs.size() < 2)
            return 0.0;

        std::sort (lufs.begin(), lufs.end());
        auto at = [&] (double q) { return lufs[(size_t) std::lround (q * (double) (lufs.size() - 1))]; };
        return at (0.95) - at (0.10);
    }

    LoudnessReport measureLoudness (const AudioData& d, Frame start, Frame end)
    {
        LoudnessReport report;
        const auto numChannels = d.numChannels();
        const auto blockSize = std::max<Frame> (1, (Frame) std::lround (d.sampleRate() * 0.1));

        std::vector<KWeighting> filters ((size_t) numChannels);
        std::vector<TruePeakDetector> truePeaks ((size_t) numChannels);
        for (auto& f : filters)
            f.prepare (d.sampleRate());

        std::vector<double> blockPowers, sums ((size_t) numChannels, 0.0);
        Frame inBlock = 0;
        float samplePeak = 0.0f, truePeak = 0.0f;

        d.processed (start, end, [&] (auto& block, Frame)
        {
            const auto n = block[0].size();
            for (size_t i = 0; i < n; ++i)
            {
                for (size_t ch = 0; ch < (size_t) numChannels; ++ch)
                {
                    const auto x = block[ch][i];
                    samplePeak = std::max (samplePeak, std::abs (x));
                    truePeak = std::max (truePeak, truePeaks[ch].process (x));
                    const auto k = filters[ch].process (x);
                    sums[ch] += (double) k * k;
                }

                if (++inBlock == blockSize)
                {
                    double p = 0.0;
                    for (int ch = 0; ch < numChannels; ++ch)
                        p += loudnessChannelWeight (ch, numChannels) * sums[(size_t) ch] / (double) blockSize;

                    blockPowers.push_back (p);
                    std::fill (sums.begin(), sums.end(), 0.0);
                    inBlock = 0;
                }
            }
        });

        report.integratedLufs = integratedLoudness (blockPowers);
        report.loudnessRange = loudnessRange (blockPowers);
        report.samplePeakDb = gainToDb (samplePeak);
        report.truePeakDb = gainToDb (std::max (truePeak, samplePeak));
        return report;
    }

    void addTpdfDither (std::vector<std::vector<float>>& block, int bitDepth, std::mt19937& rng)
    {
        const auto lsb = 1.0f / (float) (1 << (bitDepth - 1));
        std::uniform_real_distribution<float> dist (-0.5f, 0.5f);

        for (auto& ch : block)
            for (auto& s : ch)
                s += (dist (rng) + dist (rng)) * lsb;
    }
}

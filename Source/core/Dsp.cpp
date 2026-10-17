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

    void Stereoizer::prepare (double sampleRate)
    {
        rate = sampleRate;
        ring.assign ((size_t) std::ceil (maxDelayMs * rate / 1000.0) + 1, 0.0f);
        bassHz = -1.0;
        reset();
    }

    void Stereoizer::set (const StereoizeSettings& settings)
    {
        delay = std::clamp<size_t> ((size_t) std::lround (std::clamp (settings.delayMs, 1.0, maxDelayMs) * rate / 1000.0), 1, ring.size() - 1);

        if (std::abs (settings.bassHz - bassHz) < 1.0e-9)
            return;
        bassHz = settings.bassHz;

        // Second-order Butterworth high pass, so the bass is left out of the side signal.
        b0 = 1; b1 = b2 = a1 = a2 = 0;
        if (bassHz > 0.0)
        {
            const auto w0 = 2.0 * M_PI * std::min (bassHz, rate * 0.45) / rate;
            const auto alpha = std::sin (w0) / (2.0 * M_SQRT1_2);
            const auto c = std::cos (w0), a0 = 1.0 + alpha;
            b0 = (1.0 + c) / 2.0 / a0;
            b1 = -(1.0 + c) / a0;
            b2 = b0;
            a1 = -2.0 * c / a0;
            a2 = (1.0 - alpha) / a0;
        }
    }

    float Stereoizer::process (float mid)
    {
        const auto high = b0 * mid + b1 * x1 + b2 * x2 - a1 * y1 - a2 * y2;
        x2 = x1; x1 = mid; y2 = y1; y1 = high;

        ring[ringPos] = (float) high;
        const auto delayed = ring[(ringPos + ring.size() - delay) % ring.size()];
        ringPos = (ringPos + 1) % ring.size();
        return delayed;
    }

    void Stereoizer::reset()
    {
        std::fill (ring.begin(), ring.end(), 0.0f);
        ringPos = 0;
        x1 = x2 = y1 = y2 = 0;
    }

    AudioDataPtr stereoize (const AudioData& d, Frame start, Frame end, const StereoizeSettings& settings)
    {
        const auto rate = d.sampleRate();
        const auto inChannels = d.numChannels();
        start = std::clamp<Frame> (start, 0, d.length());
        end = std::clamp<Frame> (end, start, d.length());

        Stereoizer core;
        core.prepare (rate);
        core.set (settings);
        const auto width = (float) std::clamp (settings.width, 0.0, 1.0);
        const auto edge = std::max<Frame> (1, (Frame) (rate * 0.01));   // 10 ms fade in and out of the effect

        AudioData::Builder out (2, rate);
        std::vector<std::vector<float>> in ((size_t) inChannels, std::vector<float> (AudioChunk::maxFrames));
        std::vector<float*> inPtrs;
        for (auto& v : in)
            inPtrs.push_back (v.data());
        std::vector<float> left (AudioChunk::maxFrames), right (AudioChunk::maxFrames);
        const float* outPtrs[2] { left.data(), right.data() };

        for (Frame pos = 0; pos < d.length();)
        {
            // Blocks never straddle the range edges.
            const auto limit = pos < start ? start : (pos < end ? end : d.length());
            const auto n = (int) std::min<Frame> (AudioChunk::maxFrames, limit - pos);
            d.read (pos, n, inPtrs.data());
            const bool inside = pos >= start && pos < end;

            for (int i = 0; i < n; ++i)
            {
                if (! inside)
                {
                    left[(size_t) i] = in[0][(size_t) i];
                    right[(size_t) i] = in[inChannels > 1 ? 1 : 0][(size_t) i];
                    continue;
                }

                float mid = 0.0f;
                for (int ch = 0; ch < inChannels; ++ch)
                    mid += in[(size_t) ch][(size_t) i];
                mid /= (float) inChannels;

                const auto at = pos + i;
                const auto ramp = (float) std::min ({ 1.0, (double) (at - start + 1) / (double) edge, (double) (end - at) / (double) edge });
                const auto side = width * ramp * core.process (mid);
                left[(size_t) i] = mid + side;
                right[(size_t) i] = mid - side;
            }

            out.append (outPtrs, n);
            pos += n;
        }

        return out.finish();
    }

    AudioDataPtr toStereo (const AudioDataPtr& d)
    {
        if (d == nullptr || d->numChannels() != 1)
            return d;

        AudioData::Builder out (2, d->sampleRate());
        std::vector<float> mono (AudioChunk::maxFrames);
        float* inPtr = mono.data();
        const float* outPtrs[2] { mono.data(), mono.data() };
        for (Frame pos = 0; pos < d->length(); pos += AudioChunk::maxFrames)
        {
            const auto n = (int) std::min<Frame> (AudioChunk::maxFrames, d->length() - pos);
            d->read (pos, n, &inPtr);
            out.append (outPtrs, n);
        }
        return out.finish();
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
    Speaker speakerAt (int channel, int n)
    {
        using S = Speaker;
        static constexpr S lcr[] { S::left, S::right, S::centre };
        static constexpr S quad[] { S::left, S::right, S::rearLeft, S::rearRight };
        static constexpr S five[] { S::left, S::right, S::centre, S::sideLeft, S::sideRight };
        static constexpr S fiveOne[] { S::left, S::right, S::centre, S::lfe, S::sideLeft, S::sideRight };
        static constexpr S sixOne[] { S::left, S::right, S::centre, S::lfe, S::rearCentre, S::sideLeft, S::sideRight };
        static constexpr S sevenOneFour[] { S::left, S::right, S::centre, S::lfe, S::rearLeft, S::rearRight, S::sideLeft, S::sideRight,
                                            S::topFrontLeft, S::topFrontRight, S::topRearLeft, S::topRearRight };
        if (channel < 0 || channel >= n)
            return S::other;

        switch (n)
        {
            case 1:  return S::mono;
            case 2:  return channel == 0 ? S::left : S::right;
            case 3:  return lcr[channel];
            case 4:  return quad[channel];
            case 5:  return five[channel];
            case 6:  return fiveOne[channel];
            case 7:  return sixOne[channel];
            case 8:                              // 7.1, 7.1.2 and 7.1.4 share their first channels
            case 10:
            case 12: return sevenOneFour[channel];
            default: return S::other;
        }
    }

    SpeakerLayout speakerLayout (int n)
    {
        SpeakerLayout layout;
        switch (n)
        {
            case 1:  layout.name = "Mono"; break;
            case 2:  layout.name = "Stereo"; break;
            case 3:  layout.name = "LCR"; break;
            case 4:  layout.name = "Quad"; break;
            case 5:  layout.name = "5.0"; break;
            case 6:  layout.name = "5.1"; break;
            case 7:  layout.name = "6.1"; break;
            case 8:  layout.name = "7.1"; break;
            case 10: layout.name = "7.1.2"; break;
            case 12: layout.name = "7.1.4"; break;
            default: layout.name = std::to_string (std::max (0, n)) + " channels"; break;
        }

        for (int ch = 0; ch < n; ++ch)
            layout.speakers.push_back (speakerAt (ch, n));
        return layout;
    }

    std::string channelName (int channel, int numChannels)
    {
        const auto layout = speakerLayout (numChannels);
        if (channel < 0 || channel >= (int) layout.speakers.size())
            return {};

        switch (layout.speakers[(size_t) channel])
        {
            case Speaker::mono:          return "M";
            case Speaker::left:          return "L";
            case Speaker::right:         return "R";
            case Speaker::centre:        return "C";
            case Speaker::lfe:           return "LFE";
            case Speaker::sideLeft:      return "Ls";
            case Speaker::sideRight:     return "Rs";
            case Speaker::rearLeft:      return numChannels == 4 ? "Ls" : "Lb";
            case Speaker::rearRight:     return numChannels == 4 ? "Rs" : "Rb";
            case Speaker::rearCentre:    return "Cs";
            case Speaker::topFrontLeft:  return "Ltf";
            case Speaker::topFrontRight: return "Rtf";
            case Speaker::topRearLeft:   return "Ltb";
            case Speaker::topRearRight:  return "Rtb";
            case Speaker::other:         break;
        }
        return "Ch " + std::to_string (channel + 1);
    }

    std::array<std::vector<float>, 2> stereoDownmix (int numChannels)
    {
        constexpr float minus3 = 0.70710678f;
        const auto layout = speakerLayout (numChannels);
        std::array<std::vector<float>, 2> m { std::vector<float> ((size_t) numChannels), std::vector<float> ((size_t) numChannels) };

        for (int ch = 0; ch < numChannels; ++ch)
        {
            auto& l = m[0][(size_t) ch];
            auto& r = m[1][(size_t) ch];
            switch (layout.speakers[(size_t) ch])
            {
                case Speaker::mono:          l = r = 1.0f; break;
                case Speaker::left:          l = 1.0f; break;
                case Speaker::right:         r = 1.0f; break;
                case Speaker::centre:        l = r = minus3; break;
                case Speaker::lfe:           break;
                case Speaker::rearCentre:    l = r = 0.5f; break;
                case Speaker::sideLeft:
                case Speaker::rearLeft:
                case Speaker::topFrontLeft:
                case Speaker::topRearLeft:   l = minus3; break;
                case Speaker::sideRight:
                case Speaker::rearRight:
                case Speaker::topFrontRight:
                case Speaker::topRearRight:  r = minus3; break;
                case Speaker::other:
                    // Unknown layout: odd channels left, even right, a last odd one in the middle.
                    if (ch == numChannels - 1 && numChannels % 2 == 1) l = r = minus3;
                    else (ch % 2 == 0 ? l : r) = 1.0f;
                    break;
            }
        }
        return m;
    }

    double loudnessChannelWeight (int channel, int numChannels)
    {
        // No allocation: the live meters call this on the audio thread.
        const auto speaker = speakerAt (channel, numChannels);
        if (speaker == Speaker::lfe)
            return 0.0;
        const bool surround = speaker == Speaker::sideLeft || speaker == Speaker::sideRight || speaker == Speaker::rearLeft
                           || speaker == Speaker::rearRight || speaker == Speaker::rearCentre;
        return surround ? 1.41 : 1.0;
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

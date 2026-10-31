// SPDX-License-Identifier: AGPL-3.0-only
#include "PhiDynamics.h"

#include <algorithm>
#include <cmath>

namespace phi
{
    namespace
    {
        constexpr double goldenRatio = 1.6180339887498949;
        constexpr double pi = 3.14159265358979323846;

        double coefficient (double ms, double rate)
        {
            return 1.0 - std::exp (-1000.0 / (std::max (0.01, ms) * rate));
        }

        // The gain computer: dB of reduction for a level, with a soft knee (Giannoulis,
        // Massberg and Reiss, "Digital Dynamic Range Compressor Design", 2012).
        double reduction (double levelDb, double thresholdDb, double ratio, double kneeDb)
        {
            const auto over = levelDb - thresholdDb;
            const auto slope = 1.0 - 1.0 / ratio;
            if (kneeDb > 0.0 && std::abs (over) <= kneeDb / 2.0)
                return slope * (over + kneeDb / 2.0) * (over + kneeDb / 2.0) / (2.0 * kneeDb);
            return over > 0.0 ? slope * over : 0.0;
        }

        double levelOf (float peak) { return 20.0 * std::log10 (std::max (1.0e-9f, peak)); }
    }

    double ratioOf (int step)
    {
        return std::pow (goldenRatio, std::clamp (step, 1, maxRatioStep) / 2.0);
    }

    //==========================================================================
    void GoldenRelease::set (double sampleRate, double attackMs, double releaseMs)
    {
        attackCoef = coefficient (attackMs, sampleRate);
        fastCoef = coefficient (releaseMs / goldenRatio, sampleRate);
        slowCoef = coefficient (releaseMs * goldenRatio, sampleRate);
    }

    double GoldenRelease::next (double target)
    {
        fast += (target - fast) * (target > fast ? attackCoef : fastCoef);
        slow += (target - slow) * (target > slow ? attackCoef : slowCoef);
        return current();
    }

    //==========================================================================
    void ReductionHistory::prepare (double sampleRate)
    {
        samplesPerPoint = std::max (1, (int) std::lround (sampleRate * seconds / historySize));
        for (auto& p : points)
            p.store (0.0f);
        newest.store (0);
        count = 0;
        deepest = 0.0f;
    }

    void ReductionHistory::push (double reductionDb)
    {
        deepest = std::max (deepest, (float) reductionDb);
        if (++count < samplesPerPoint)
            return;
        const auto next = (newest.load (std::memory_order_relaxed) + 1) % historySize;
        points[(size_t) next].store (deepest, std::memory_order_relaxed);
        newest.store (next, std::memory_order_release);
        count = 0;
        deepest = 0.0f;
    }

    void ReductionHistory::read (float* out) const
    {
        const auto last = newest.load (std::memory_order_acquire);
        for (int i = 0; i < historySize; ++i)
            out[i] = points[(size_t) ((last + 1 + i) % historySize)].load (std::memory_order_relaxed);
    }

    //==========================================================================
    TruePeak::TruePeak()
    {
        constexpr int totalTaps = tapsPerPhase * 4;
        for (int i = 0; i < totalTaps; ++i)
        {
            // Centred on a whole sample, so the four phases land on the sample itself and a
            // quarter, a half and three quarters after it: a peak exactly between two samples
            // is read, not missed (EBU Tech 3341, signals 16 to 19).
            const double x = (i - totalTaps / 2.0) / 4.0;
            const double sinc = std::abs (x) < 1.0e-12 ? 1.0 : std::sin (pi * x) / (pi * x);
            const double window = 0.5 - 0.5 * std::cos (2.0 * pi * (i + 0.5) / totalTaps);
            phases[(size_t) (i % 4)][(size_t) (i / 4)] = (float) (sinc * window);
        }
    }

    float TruePeak::process (float x)
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

    void TruePeak::reset()
    {
        history.fill (0.0f);
        writeIndex = 0;
    }

    //==========================================================================
    double Compressor::curveDb (const Settings& s, double inDb)
    {
        return inDb - reduction (inDb, s.thresholdDb, ratioOf (s.ratioStep), s.kneeDb) + s.makeupDb;
    }

    void Compressor::prepare (double sampleRate, int)
    {
        rate = sampleRate;
        past.prepare (sampleRate);
        envelope.set (rate, settings.attackMs, settings.releaseMs);
        reset();
    }

    void Compressor::set (const Settings& s)
    {
        settings = s;
        envelope.set (rate, s.attackMs, s.releaseMs);
    }

    void Compressor::reset()
    {
        envelope.reset();
        shownReduction.store (0.0);
    }

    void Compressor::process (float* const* channels, int numChannels, int numFrames)
    {
        const auto ratio = ratioOf (settings.ratioStep);
        const auto mix = std::clamp (settings.mix, 0.0, 1.0);
        double gr = envelope.current();
        for (int i = 0; i < numFrames; ++i)
        {
            float peak = 0.0f;
            for (int ch = 0; ch < numChannels; ++ch)
                peak = std::max (peak, std::abs (channels[ch][i]));
            gr = envelope.next (reduction (levelOf (peak), settings.thresholdDb, ratio, settings.kneeDb));
            past.push (gr);
            const auto wet = std::pow (10.0, (settings.makeupDb - gr) / 20.0);
            const auto gain = (float) (1.0 - mix + mix * wet);
            for (int ch = 0; ch < numChannels; ++ch)
                channels[ch][i] *= gain;
        }
        shownReduction.store (gr);
    }

    //==========================================================================
    double Multiband::crossoverHz (int step)
    {
        return 80.0 * std::pow (goldenRatio, std::clamp (step, minCrossoverStep, maxCrossoverStep));
    }

    std::array<double, Multiband::numBands - 1> Multiband::crossovers (const Settings& s)
    {
        auto steps = s.crossoverSteps;
        std::sort (steps.begin(), steps.end());
        std::array<double, numBands - 1> hz {};
        for (size_t c = 0; c < hz.size(); ++c)
            hz[c] = crossoverHz (steps[c]);
        return hz;
    }

    double Multiband::timeScale (int band)
    {
        return std::pow (goldenRatio, 1.5 - band);
    }

    void Multiband::prepare (double sampleRate, int numChannels)
    {
        rate = sampleRate;
        channelState.assign ((size_t) std::max (1, numChannels), Channel {});
        scratch.assign ((size_t) std::max (1, numChannels), {});
        design();
        reset();
    }

    void Multiband::set (const Settings& s)
    {
        const bool moved = s.crossoverSteps != settings.crossoverSteps;
        settings = s;
        if (moved)
            design();
        for (int b = 0; b < numBands; ++b)
            envelopes[(size_t) b].set (rate, s.attackMs * timeScale (b), s.releaseMs * timeScale (b));
    }

    void Multiband::reset()
    {
        for (auto& c : channelState)
            c = Channel {};
        for (auto& e : envelopes)
            e.reset();
        for (auto& r : shownReduction)
            r.store (0.0);
    }

    void Multiband::design()
    {
        // Butterworth low- and high-passes (each run twice: Linkwitz-Riley) and the
        // all-pass their sum equals, from the Audio EQ Cookbook.
        const auto hz = crossovers (settings);
        for (size_t c = 0; c < splits.size(); ++c)
        {
            const auto w = 2.0 * pi * std::min (hz[c], 0.45 * rate) / rate;
            const auto cw = std::cos (w), alpha = std::sin (w) / (2.0 * 0.7071067811865476), a0 = 1.0 + alpha;
            auto& s = splits[c];
            s.low = { (1 - cw) / 2 / a0, (1 - cw) / a0, (1 - cw) / 2 / a0, -2 * cw / a0, (1 - alpha) / a0 };
            s.high = { (1 + cw) / 2 / a0, -(1 + cw) / a0, (1 + cw) / 2 / a0, -2 * cw / a0, (1 - alpha) / a0 };
            s.all = { (1 - alpha) / a0, -2 * cw / a0, 1.0, -2 * cw / a0, (1 - alpha) / a0 };
        }
        for (int b = 0; b < numBands; ++b)
            envelopes[(size_t) b].set (rate, settings.attackMs * timeScale (b), settings.releaseMs * timeScale (b));
    }

    void Multiband::process (float* const* channels, int numChannels, int numFrames)
    {
        numChannels = std::min (numChannels, (int) channelState.size());
        const auto ratio = ratioOf (settings.ratioStep);
        const auto mix = std::clamp (settings.mix, 0.0, 1.0);
        std::array<double, numBands> gains {};
        std::array<double, numBands> gr {};
        for (int b = 0; b < numBands; ++b)
            gr[(size_t) b] = envelopes[(size_t) b].current();

        for (int i = 0; i < numFrames; ++i)
        {
            std::array<float, numBands> peaks {};
            for (int ch = 0; ch < numChannels; ++ch)
            {
                auto& st = channelState[(size_t) ch];
                auto& out = scratch[(size_t) ch];
                const double x = channels[ch][i];
                auto lr = [&] (int c, bool low, double v)
                {
                    const auto& k = low ? splits[(size_t) c].low : splits[(size_t) c].high;
                    auto& s = low ? st.low[(size_t) c] : st.high[(size_t) c];
                    return s[1].run (k, s[0].run (k, v));
                };
                const auto low0 = lr (0, true, x), rest0 = lr (0, false, x);
                const auto low1 = lr (1, true, rest0), rest1 = lr (1, false, rest0);
                out[0] = (float) st.all[1].run (splits[2].all, st.all[0].run (splits[1].all, low0));
                out[1] = (float) st.all[2].run (splits[2].all, low1);
                out[2] = (float) lr (2, true, rest1);
                out[3] = (float) lr (2, false, rest1);
                for (int b = 0; b < numBands; ++b)
                    peaks[(size_t) b] = std::max (peaks[(size_t) b], std::abs (out[(size_t) b]));
            }
            for (int b = 0; b < numBands; ++b)
            {
                const auto& band = settings.bands[(size_t) b];
                gr[(size_t) b] = envelopes[(size_t) b].next (reduction (levelOf (peaks[(size_t) b]), band.thresholdDb, ratio, settings.kneeDb));
                gains[(size_t) b] = std::pow (10.0, (band.gainDb - gr[(size_t) b] * mix) / 20.0);
            }
            for (int ch = 0; ch < numChannels; ++ch)
            {
                double sum = 0.0;
                for (int b = 0; b < numBands; ++b)
                    sum += scratch[(size_t) ch][(size_t) b] * gains[(size_t) b];
                channels[ch][i] = (float) sum;
            }
        }
        for (int b = 0; b < numBands; ++b)
            shownReduction[(size_t) b].store (gr[(size_t) b]);
    }

    //==========================================================================
    void Limiter::prepare (double sampleRate, int numChannels)
    {
        rate = sampleRate;
        window = std::max (8, (int) std::lround (lookaheadMs * sampleRate / 1000.0));
        hold = window + 4;   // past the smoothing window on both sides, as in dsp::TruePeakLimiter
        detectors.assign ((size_t) std::max (1, numChannels), TruePeak());
        delay.assign ((size_t) std::max (1, numChannels), std::vector<float> ((size_t) latency() + 1, 0.0f));
        minQueue.assign ((size_t) hold + 1, { 0, 1.0f });
        box.assign ((size_t) window, 1.0f);
        past.prepare (sampleRate);
        set (settings);
        reset();
    }

    void Limiter::set (const Settings& s)
    {
        settings = s;
        ceiling = (float) std::pow (10.0, std::min (s.ceilingDb, 0.0) / 20.0);
        drive = (float) std::pow (10.0, s.driveDb / 20.0);
        fastCoef = (float) coefficient (s.releaseMs / goldenRatio, rate);
        slowCoef = (float) coefficient (s.releaseMs * goldenRatio, rate);
    }

    void Limiter::reset()
    {
        for (auto& d : detectors)
            d.reset();
        for (auto& d : delay)
            std::fill (d.begin(), d.end(), 0.0f);
        std::fill (box.begin(), box.end(), 1.0f);
        boxSum = (double) window;
        writePos = queueHead = queueTail = boxPos = 0;
        counter = 0;
        fast = slow = 1.0f;
        shownReduction.store (0.0);
    }

    float Limiter::nextGain (float required)
    {
        // The smallest gain needed over the last `hold` samples.
        const auto capacity = (int) minQueue.size();
        while (queueTail != queueHead && minQueue[(size_t) ((queueTail + capacity - 1) % capacity)].second >= required)
            queueTail = (queueTail + capacity - 1) % capacity;
        minQueue[(size_t) queueTail] = { counter, required };
        queueTail = (queueTail + 1) % capacity;
        while (minQueue[(size_t) queueHead].first <= counter - hold)
            queueHead = (queueHead + 1) % capacity;
        const auto held = minQueue[(size_t) queueHead].second;
        ++counter;

        // Instant attack; the golden release: a fast and a slow stage, weighed 1/φ and
        // 1/φ² in dB (a geometric mean of the gains). Both stay at or under the held gain.
        fast = held < fast ? held : fast + (held - fast) * fastCoef;
        slow = held < slow ? held : slow + (held - slow) * slowCoef;
        const auto released = std::pow (fast, 0.6180339887498949f) * std::pow (slow, 0.3819660112501051f);

        // Averaged over the look-ahead, the steps become ramps that finish as the peak comes out.
        boxSum += (double) released - (double) box[(size_t) boxPos];
        box[(size_t) boxPos] = released;
        boxPos = (boxPos + 1) % window;
        return (float) std::min (1.0, boxSum / (double) window);
    }

    void Limiter::process (float* const* channels, int numChannels, int numFrames)
    {
        numChannels = std::min (numChannels, (int) delay.size());
        const auto length = (int) delay[0].size();
        float gain = 1.0f;
        for (int i = 0; i < numFrames; ++i)
        {
            const auto aligned = (writePos + length - detectorDelay) % length;
            float peak = 0.0f;
            for (int ch = 0; ch < numChannels; ++ch)
            {
                const auto x = channels[ch][i] * drive;
                peak = std::max ({ peak, detectors[(size_t) ch].process (x), std::abs (delay[(size_t) ch][(size_t) aligned]) });
                delay[(size_t) ch][(size_t) writePos] = x;
            }

            gain = nextGain (peak > ceiling ? ceiling / peak : 1.0f);
            const auto readPos = (writePos + 1) % length;   // latency() samples ago
            for (int ch = 0; ch < numChannels; ++ch)
                channels[ch][i] = std::clamp (delay[(size_t) ch][(size_t) readPos] * gain, -ceiling, ceiling);
            writePos = readPos;
            past.push (-20.0 * std::log10 (std::max (1.0e-9f, gain)));
        }
        shownReduction.store (-20.0 * std::log10 (std::max (1.0e-9f, gain)));
    }
}

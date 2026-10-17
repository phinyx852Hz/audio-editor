// SPDX-License-Identifier: AGPL-3.0-only
// Restoration over a time range: noise reduction from a learned profile,
// DeClick, DeClip, DeHum and DeEss.
#include "Repair.h"
#include "Stft.h"

#include <algorithm>
#include <array>
#include <cmath>
#include <map>

namespace repair
{
namespace
{
    using Bins = Stft::Bins;
    constexpr double pi = 3.14159265358979323846;
    constexpr Frame blockFrames = 1 << 16;   // detect-and-fix tools work through the range in blocks this long
    constexpr int blockMargin = 4096;        // audio either side of a block, for context

    //==========================================================================
    // Autoregressive (linear prediction) modelling, for rebuilding samples.

    // Adds the autocorrelation of a Hann-windowed stretch to r[0..order].
    void addAutocorrelation (const float* x, int length, int order, std::vector<double>& r)
    {
        if (length <= order)
            return;

        std::vector<double> w ((size_t) length);
        for (int i = 0; i < length; ++i)
            w[(size_t) i] = x[i] * (0.5 - 0.5 * std::cos (2.0 * pi * (i + 0.5) / length));
        for (int lag = 0; lag <= order; ++lag)
        {
            double sum = 0.0;
            for (int i = lag; i < length; ++i)
                sum += w[(size_t) i] * w[(size_t) (i - lag)];
            r[(size_t) lag] += sum;
        }
    }

    // Levinson-Durbin: prediction coefficients a[0..order] with a[0] = 1, so that
    // sum a[i] * x[n - i] is the prediction error. Empty when there is no signal.
    std::vector<double> levinson (std::vector<double> r, int order)
    {
        if (r[0] <= 1.0e-20)
            return {};

        r[0] *= 1.0 + 1.0e-6;   // a touch of white noise keeps the model stable
        std::vector<double> a ((size_t) order + 1, 0.0), previous;
        a[0] = 1.0;
        double error = r[0];
        for (int i = 1; i <= order; ++i)
        {
            double acc = r[(size_t) i];
            for (int j = 1; j < i; ++j)
                acc += a[(size_t) j] * r[(size_t) (i - j)];
            const auto k = -acc / error;
            previous = a;
            for (int j = 1; j < i; ++j)
                a[(size_t) j] = previous[(size_t) j] + k * previous[(size_t) (i - j)];
            a[(size_t) i] = k;
            error *= 1.0 - k * k;
            if (error <= r[0] * 1.0e-12)
                break;
        }
        return a;
    }

    // Least-squares AR interpolation: replaces x at the sorted indices `missing`
    // with the values that make the prediction error smallest. Every index must
    // have p = a.size() - 1 samples of x before it and after it.
    void fillMissing (std::vector<float>& x, const std::vector<int>& missing, const std::vector<double>& a)
    {
        const auto m = (int) missing.size();
        if (m == 0)
            return;
        if (a.empty())
        {
            for (auto i : missing)
                x[(size_t) i] = 0.0f;   // silence around it: silence in it
            return;
        }

        const auto p = (int) a.size() - 1;
        const auto lo = missing.front(), hi = missing.back() + p + 1;

        // Prediction error from the known samples alone (missing ones counted as zero).
        std::vector<char> isMissing ((size_t) (hi - lo + p), 0);
        for (auto i : missing)
            isMissing[(size_t) (i - lo + p)] = 1;
        auto known = [&] (int i) { return i >= lo - p && i < hi && isMissing[(size_t) (i - lo + p)] ? 0.0 : (double) x[(size_t) i]; };
        std::vector<double> error ((size_t) (hi - lo));
        for (int n = lo; n < hi; ++n)
        {
            double e = 0.0;
            for (int i = 0; i <= p; ++i)
                e += a[(size_t) i] * known (n - i);
            error[(size_t) (n - lo)] = e;
        }

        std::vector<double> c ((size_t) p + 1, 0.0);   // autocorrelation of the coefficients
        for (int lag = 0; lag <= p; ++lag)
            for (int k = 0; k + lag <= p; ++k)
                c[(size_t) lag] += a[(size_t) k] * a[(size_t) (k + lag)];

        // The normal equations are banded (two samples interact only within p of each other).
        const auto band = p;
        auto matrix = [&] (int i, int j)
        {
            const auto gap = std::abs (missing[(size_t) i] - missing[(size_t) j]);
            return gap <= p ? c[(size_t) gap] : 0.0;
        };
        std::vector<double> rhs ((size_t) m);
        for (int u = 0; u < m; ++u)
        {
            double sum = 0.0;
            for (int j = 0; j <= p; ++j)
                sum += a[(size_t) j] * error[(size_t) (missing[(size_t) u] + j - lo)];
            rhs[(size_t) u] = -sum;
        }

        // Banded Cholesky, L stored row by row with `band + 1` entries.
        std::vector<double> L ((size_t) m * (size_t) (band + 1), 0.0);
        auto at = [&] (int i, int j) -> double& { return L[(size_t) i * (size_t) (band + 1) + (size_t) (j - i + band)]; };
        const auto ridge = c[0] * 1.0e-9;
        for (int i = 0; i < m; ++i)
            for (int j = std::max (0, i - band); j <= i; ++j)
            {
                auto sum = matrix (i, j) + (i == j ? ridge : 0.0);
                for (int k = std::max (0, i - band); k < j; ++k)
                    sum -= at (i, k) * at (j, k);
                at (i, j) = i == j ? std::sqrt (std::max (sum, 1.0e-30)) : sum / at (j, j);
            }

        std::vector<double> y ((size_t) m);
        for (int i = 0; i < m; ++i)
        {
            auto sum = rhs[(size_t) i];
            for (int k = std::max (0, i - band); k < i; ++k)
                sum -= at (i, k) * y[(size_t) k];
            y[(size_t) i] = sum / at (i, i);
        }
        for (int i = m - 1; i >= 0; --i)
        {
            auto sum = y[(size_t) i];
            for (int k = i + 1; k <= std::min (m - 1, i + band); ++k)
                sum -= at (k, i) * y[(size_t) k];
            y[(size_t) i] = sum / at (i, i);
        }

        for (int u = 0; u < m; ++u)
            x[(size_t) missing[(size_t) u]] = (float) y[(size_t) u];
    }

    //==========================================================================
    // Reads [first, first + length) of every channel; zeros outside the file.
    std::vector<std::vector<float>> readChannels (const AudioData& d, Frame first, int length)
    {
        std::vector<std::vector<float>> x ((size_t) d.numChannels(), std::vector<float> ((size_t) length, 0.0f));
        std::vector<float*> ptrs;
        for (auto& c : x)
            ptrs.push_back (c.data());
        d.read (first, length, ptrs.data());
        return x;
    }

    // Changes the spectrum of [start, end) frame by frame. `shape (channel, bins)`
    // is called for each frame of each channel in time order and changes bins
    // 0..n/2 in place. Frames at the ends count only as far as they lie inside the
    // range, so the edges blend into the untouched audio. Long ranges are worked
    // through a piece at a time.
    template <typename Shape>
    AudioDataPtr shapeRange (const AudioData& d, Frame start, Frame end, int order, Shape&& shape, Progress progress)
    {
        start = std::clamp<Frame> (start, 0, d.length());
        end = std::clamp<Frame> (end, start, d.length());
        if (end <= start)
            return nullptr;

        const Stft stft (order);
        const auto n = stft.size(), hop = stft.step(), bins = stft.numBins();
        double windowEnergy = 0.0;
        for (int i = 0; i < n; ++i)
            windowEnergy += (double) stft.windowAt (i) * stft.windowAt (i);
        const auto norm = (float) (windowEnergy * 4.0 / n);   // summed squared windows at 75 % overlap

        auto framePosition = [&] (Frame k) { return start - n + k * hop; };
        // At the very ends of the file there is no untouched audio to blend into.
        const auto from = start > 0 ? start : start - n, to = end < d.length() ? end : end + n;
        auto weight = [&] (Frame k)
        {
            const auto p = framePosition (k);
            if (p >= from && p + n <= to)
                return 1.0f;
            double inside = 0.0;
            for (auto f = std::max (p, from); f < std::min (p + n, to); ++f)
                inside += (double) stft.windowAt ((int) (f - p)) * stft.windowAt ((int) (f - p));
            return (float) (inside / windowEnergy);
        };
        const Frame lastFrame = (end - 1 - (start - n)) / hop;

        const auto channels = d.numChannels();
        AudioData::Builder out (channels, d.sampleRate());
        std::map<std::pair<int, Frame>, Bins> done;   // changes already worked out, for frames shared by two pieces
        constexpr Frame piece = 1 << 18;
        std::vector<std::vector<float>> result ((size_t) channels);
        Bins spectrum, shaped;

        for (Frame a = start; a < end; a += piece)
        {
            const auto b = std::min (end, a + piece);
            const auto firstK = (a - start) / hop + 1;
            const auto lastK = std::min (lastFrame, (b - 1 - (start - n)) / hop);
            const auto first = framePosition (firstK);
            const auto length = (int) (framePosition (lastK) + n - first);
            auto x = readChannels (d, first, length);

            for (int ch = 0; ch < channels; ++ch)
            {
                std::vector<float> change ((size_t) length, 0.0f);
                for (auto k = firstK; k <= lastK; ++k)
                {
                    auto& delta = done[{ ch, k }];
                    if (delta.empty())
                    {
                        stft.analyse (x[(size_t) ch], (int) (k - firstK), spectrum);
                        shaped = spectrum;
                        shape (ch, shaped);
                        const auto w = weight (k);
                        delta.resize ((size_t) bins);
                        for (int j = 0; j < bins; ++j)
                            delta[(size_t) j] = (shaped[(size_t) j] - spectrum[(size_t) j]) * w;
                    }
                    stft.addChange (delta, (int) (k - firstK), change);
                }

                auto& r = result[(size_t) ch];
                r.resize ((size_t) (b - a));
                for (Frame f = a; f < b; ++f)
                    r[(size_t) (f - a)] = x[(size_t) ch][(size_t) (f - first)] + change[(size_t) (f - first)] / norm;
            }

            std::vector<const float*> rows;
            for (auto& r : result)
                rows.push_back (r.data());
            out.append (rows.data(), (int) (b - a));

            // Keep only the frames that also reach into the next piece.
            const auto keepFrom = (b - start) / hop + 1;
            for (auto it = done.begin(); it != done.end();)
                it = it->first.second < keepFrom ? done.erase (it) : std::next (it);

            progress.report ((float) (b - start) / (float) (end - start));
            if (progress.cancelled())
                return nullptr;
        }

        auto middle = out.finish();
        return d.replaced (start, end, middle.get());
    }

    // Runs `fix (channelSamples, from, to)` over [start, end) in blocks with
    // context either side; `fix` may change samples in [from, to) and returns
    // nothing. Only the block itself is written back.
    template <typename Fix>
    AudioDataPtr fixBlocks (const AudioData& d, Frame start, Frame end, Fix&& fix, Progress progress)
    {
        start = std::clamp<Frame> (start, 0, d.length());
        end = std::clamp<Frame> (end, start, d.length());
        if (end <= start)
            return nullptr;

        AudioData::Builder out (d.numChannels(), d.sampleRate());
        for (Frame a = start; a < end; a += blockFrames)
        {
            const auto b = std::min (end, a + blockFrames);
            const auto length = (int) (b - a) + 2 * blockMargin;
            auto x = readChannels (d, a - blockMargin, length);
            for (auto& channel : x)
                fix (channel, blockMargin, blockMargin + (int) (b - a));

            std::vector<const float*> rows;
            for (auto& channel : x)
                rows.push_back (channel.data() + blockMargin);
            out.append (rows.data(), (int) (b - a));

            progress.report ((float) (b - start) / (float) (end - start));
            if (progress.cancelled())
                return nullptr;
        }

        auto middle = out.finish();
        return d.replaced (start, end, middle.get());
    }

    // Typical size of x over [from, to): 1.4826 times the median absolute value
    // (equal to the standard deviation for Gaussian noise, unmoved by a few spikes).
    double robustSpread (const std::vector<double>& values, int from, int to)
    {
        std::vector<double> v;
        v.reserve ((size_t) (to - from));
        for (int i = from; i < to; ++i)
            v.push_back (std::abs (values[(size_t) i]));
        if (v.empty())
            return 0.0;
        std::nth_element (v.begin(), v.begin() + (long) v.size() / 2, v.end());
        return 1.4826 * v[v.size() / 2];
    }

    //==========================================================================
    // Detection shared by the fix tools and the Diagnostics scans.
    constexpr int clickOrder = 20, clickWindow = 1024;
    constexpr int clipOrder = 40, maxClipRun = 512;

    int maxClickLength (double rate) { return std::max (8, (int) (0.0025 * rate)); }   // longer than 2.5 ms is not a click

    struct Span
    {
        int start = 0, end = 0;
        double strength = 0.0;   // clicks: how many times the local spread
    };

    // Clicks in x[from, to), each with two samples of room either side to rebuild.
    // `threshold` is in robust standard deviations of the prediction error.
    std::vector<Span> findClickSpans (const std::vector<float>& x, int from, int to, double threshold)
    {
        std::vector<Span> clicks;
        // Prediction error of a model fitted to this stretch: clicks stand out as spikes.
        std::vector<double> r ((size_t) clickOrder + 1, 0.0);
        addAutocorrelation (x.data() + from, to - from, clickOrder, r);
        const auto a = levinson (r, clickOrder);
        if (a.empty())
            return clicks;

        std::vector<double> error ((size_t) x.size(), 0.0);
        for (int i = std::max (from, clickOrder); i < to; ++i)
        {
            double e = 0.0;
            for (int j = 0; j <= clickOrder; ++j)
                e += a[(size_t) j] * x[(size_t) (i - j)];
            error[(size_t) i] = e;
        }

        // Flag errors far above the local typical size; group flags close together.
        for (int w = from; w < to; w += clickWindow)
        {
            const auto spread = robustSpread (error, std::max (from, w - clickWindow), std::min (to, w + 2 * clickWindow));
            if (spread <= 1.0e-9)
                continue;
            for (int i = w; i < std::min (to, w + clickWindow); ++i)
            {
                const auto strength = std::abs (error[(size_t) i]) / spread;
                if (strength > threshold)
                {
                    if (! clicks.empty() && i - clicks.back().end < 24)
                    {
                        clicks.back().end = i + 1;
                        clicks.back().strength = std::max (clicks.back().strength, strength);
                    }
                    else
                        clicks.push_back ({ i, i + 1, strength });
                }
            }
        }
        for (auto& c : clicks)
        {
            c.start = std::max (0, c.start - 2);
            c.end = std::min ((int) x.size(), c.end + 2);
        }
        return clicks;
    }

    // Runs of two or more samples at or above `level`, all on one side, starting in [from, to).
    // Runs too long, or too near the start of x, to rebuild are counted in `tooLong`.
    std::vector<Span> findClipRuns (const std::vector<float>& x, int from, int to, float level, int& tooLong)
    {
        std::vector<Span> runs;
        for (int i = from; i < to;)
        {
            if (std::abs (x[(size_t) i]) < level)
            {
                ++i;
                continue;
            }
            auto j = i + 1;
            while (j < (int) x.size() - clipOrder && std::abs (x[(size_t) j]) >= level && (x[(size_t) j] > 0) == (x[(size_t) i] > 0))
                ++j;
            if (j - i >= 2)
            {
                if (j - i > maxClipRun || i - clipOrder < 0)
                    ++tooLong;
                else
                    runs.push_back ({ i, j, 0.0 });
            }
            i = j;
        }
        return runs;
    }

    // Rebuilds clipped runs of x together from a model of the audio around them.
    void rebuildClipRuns (std::vector<float>& x, const std::vector<Span>& runs, float level)
    {
        if (runs.empty())
            return;
        std::vector<int> missing;
        std::vector<float> signs;
        for (const auto& r : runs)
        {
            signs.push_back (x[(size_t) r.start] > 0.0f ? 1.0f : -1.0f);
            for (int k = r.start; k < r.end; ++k)
                missing.push_back (k);
        }

        // Model the stretch (twice: the second time from the rebuilt audio, which is closer to the truth).
        const auto lo = std::max (0, missing.front() - 2048), hi = std::min ((int) x.size(), missing.back() + 2048);
        for (int pass = 0; pass < 2; ++pass)
        {
            std::vector<double> r ((size_t) clipOrder + 1, 0.0);
            addAutocorrelation (x.data() + lo, hi - lo, clipOrder, r);
            fillMissing (x, missing, levinson (r, clipOrder));
        }

        // A clipped sample was at least as loud as the clip level.
        for (size_t i = 0; i < runs.size(); ++i)
            for (int k = runs[i].start; k < runs[i].end; ++k)
                x[(size_t) k] = signs[i] * std::max (std::abs (x[(size_t) k]), level);
    }

    // The clip level for a range: `levelPercent` of its loudest sample, or 0 when it is silent.
    float clipLevel (const AudioData& d, Frame start, Frame end, double levelPercent)
    {
        const auto peak = d.peakAbs (start, end);
        return peak < 1.0e-6f ? 0.0f : (float) (peak * std::clamp (levelPercent, 50.0, 100.0) / 100.0 - 1.0e-7);
    }

    // Calls `scan (channel, x, from, to, first)` for every block of [start, end) and
    // channel, where x[i] is the audio at frame first + i. False when cancelled.
    template <typename Scan>
    bool scanBlocks (const AudioData& d, Frame start, Frame end, Scan&& scan, Progress progress)
    {
        start = std::clamp<Frame> (start, 0, d.length());
        end = std::clamp<Frame> (end, start, d.length());
        for (Frame a = start; a < end; a += blockFrames)
        {
            const auto b = std::min (end, a + blockFrames);
            const auto first = a - blockMargin;
            auto x = readChannels (d, first, (int) (b - a) + 2 * blockMargin);
            for (int ch = 0; ch < d.numChannels(); ++ch)
                scan (ch, x[(size_t) ch], blockMargin, blockMargin + (int) (b - a), first);
            progress.report ((float) (b - start) / (float) (end - start));
            if (progress.cancelled())
                return false;
        }
        return true;
    }

    uint64_t channelBit (int ch) { return ch < 64 ? (uint64_t) 1 << ch : 0; }

    // Sorts problems and joins those that overlap (the same event seen on several channels).
    std::vector<Problem> merged (std::vector<Problem> found)
    {
        std::sort (found.begin(), found.end(), [] (const Problem& a, const Problem& b) { return a.start < b.start; });
        std::vector<Problem> out;
        for (const auto& p : found)
        {
            if (! out.empty() && out.back().kind == p.kind && p.start <= out.back().end)
            {
                auto& last = out.back();
                last.end = std::max (last.end, p.end);
                last.channels |= p.channels;
                last.size = std::max (last.size, p.size);
            }
            else
                out.push_back (p);
        }
        return out;
    }
}

//==============================================================================
bool interpolateGap (std::vector<float>& x, int gapStart, int gapLength)
{
    if (gapLength <= 0)
        return true;
    if (gapLength > 512)
        return false;

    const auto order = std::clamp (2 * gapLength + 10, 16, 64);
    const auto context = std::max (8 * order, 256);
    const auto before = std::min (context, gapStart - order);
    const auto after = std::min (context, (int) x.size() - (gapStart + gapLength) - order);
    if (before < order || after < order)
        return false;

    std::vector<double> r ((size_t) order + 1, 0.0);
    addAutocorrelation (x.data() + gapStart - before, before, order, r);
    addAutocorrelation (x.data() + gapStart + gapLength, after, order, r);
    const auto a = levinson (r, order);

    std::vector<int> missing ((size_t) gapLength);
    for (int i = 0; i < gapLength; ++i)
        missing[(size_t) i] = gapStart + i;
    fillMissing (x, missing, a);
    return true;
}

//==============================================================================
NoiseProfile learnNoise (const AudioData& d, Frame start, Frame end)
{
    NoiseProfile profile;
    start = std::clamp<Frame> (start, 0, d.length());
    end = std::clamp<Frame> (end, start, d.length());
    const Stft stft (profile.fftOrder);
    const auto n = stft.size(), bins = stft.numBins();
    if (end - start < n)
        return profile;

    const auto length = (int) std::min<Frame> (end - start, 60 * (Frame) d.sampleRate());   // a minute is plenty
    auto x = readChannels (d, start, length);
    profile.sampleRate = d.sampleRate();
    profile.learnedFrom = length;
    profile.power.assign ((size_t) d.numChannels(), std::vector<float> ((size_t) bins, 0.0f));

    Bins spectrum;
    const auto frames = stft.numFrames (length);
    for (int ch = 0; ch < d.numChannels(); ++ch)
    {
        std::vector<double> sum ((size_t) bins, 0.0);
        for (int t = 0; t < frames; ++t)
        {
            stft.analyse (x[(size_t) ch], t, spectrum);
            for (int k = 0; k < bins; ++k)
                sum[(size_t) k] += std::norm (spectrum[(size_t) k]);
        }
        for (int k = 0; k < bins; ++k)
            profile.power[(size_t) ch][(size_t) k] = (float) (sum[(size_t) k] / frames);
    }
    return profile;
}

AudioDataPtr reduceNoise (const AudioData& d, Frame start, Frame end, const NoiseProfile& profile,
                          const NoiseReductionSettings& settings, Progress progress)
{
    if (! profile.isValid())
        return nullptr;

    const Stft stft (profile.fftOrder);
    const auto hop = stft.step(), bins = stft.numBins();
    const auto floorGain = (float) std::pow (10.0, -std::max (0.0, settings.reductionDb) / 20.0);
    const auto above = (float) std::pow (10.0, settings.sensitivityDb / 10.0);
    // Per frame, the gain may fall back towards the floor by this factor (it rises at once).
    const auto releaseFrames = std::max (1.0, settings.releaseMs / 1000.0 * d.sampleRate() / hop);
    const auto fall = (float) std::pow (10.0, -std::max (0.0, settings.reductionDb) / 20.0 / releaseFrames);

    std::vector<std::vector<float>> previous ((size_t) d.numChannels(), std::vector<float> ((size_t) bins, floorGain));
    std::vector<float> raw ((size_t) bins);

    return shapeRange (d, start, end, profile.fftOrder, [&] (int ch, Bins& spectrum)
    {
        const auto& noise = profile.power[(size_t) ch % profile.power.size()];
        auto& gain = previous[(size_t) ch];
        // How much of each bin to keep: none up to the sensitivity above the noise,
        // all from 6 dB beyond that (a soft gate).
        for (int k = 0; k < bins; ++k)
        {
            const auto power = std::norm (spectrum[(size_t) k]);
            const auto threshold = noise[(size_t) k] * above;
            const auto keep = power > threshold && threshold > 0.0f
                                  ? std::min (1.0f, 10.0f * std::log10 (power / threshold) / 6.0f)
                                  : (threshold > 0.0f ? 0.0f : 1.0f);
            raw[(size_t) k] = floorGain + (1.0f - floorGain) * keep;
        }

        for (int k = 0; k < bins; ++k)
        {
            // Average with the neighbouring bins, so lone bins of noise do not twitter ("musical noise").
            float sum = 0.0f;
            int count = 0;
            for (int j = std::max (0, k - 1); j <= std::min (bins - 1, k + 1); ++j, ++count)
                sum += raw[(size_t) j];
            const auto target = sum / (float) count;
            gain[(size_t) k] = std::max ({ target, gain[(size_t) k] * fall, floorGain });
            spectrum[(size_t) k] *= gain[(size_t) k];
        }
    }, progress);
}

//==============================================================================
AudioDataPtr declick (const AudioData& d, Frame start, Frame end, double sensitivity, Report& report, Progress progress)
{
    report = {};
    const auto maxClick = maxClickLength (d.sampleRate());
    const auto threshold = std::max (2.0, sensitivity);

    return fixBlocks (d, start, end, [&] (std::vector<float>& x, int from, int to)
    {
        for (const auto& c : findClickSpans (x, from, to, threshold))
        {
            if (c.end - c.start > maxClick || ! interpolateGap (x, c.start, c.end - c.start))
                ++report.skipped;
            else
                ++report.fixed;
        }
    }, progress);
}

//==============================================================================
AudioDataPtr declip (const AudioData& d, Frame start, Frame end, double levelPercent, Report& report, Progress progress)
{
    report = {};
    start = std::clamp<Frame> (start, 0, d.length());
    end = std::clamp<Frame> (end, start, d.length());
    const auto level = clipLevel (d, start, end, levelPercent);
    if (level <= 0.0f)
        return nullptr;

    return fixBlocks (d, start, end, [&] (std::vector<float>& x, int from, int to)
    {
        const auto runs = findClipRuns (x, from, to, level, report.skipped);
        rebuildClipRuns (x, runs, level);
        report.fixed += (int) runs.size();
    }, progress);
}

//==============================================================================
double findHum (const AudioData& d, Frame start, Frame end)
{
    start = std::clamp<Frame> (start, 0, d.length());
    end = std::clamp<Frame> (end, start, d.length());
    const auto length = (int) std::min<Frame> (end - start, (Frame) (8.0 * d.sampleRate()));
    if (length < (int) d.sampleRate())   // a second at least, for 0.1 Hz precision
        return 0.0;

    auto x = readChannels (d, start, length);
    std::vector<double> mono ((size_t) length, 0.0);
    for (auto& c : x)
        for (int i = 0; i < length; ++i)
            mono[(size_t) i] += c[(size_t) i];

    // Level at a frequency and its next two harmonics (Goertzel).
    auto level = [&] (double hz)
    {
        double total = 0.0;
        for (int h = 1; h <= 3; ++h)
        {
            const auto coefficient = 2.0 * std::cos (2.0 * pi * h * hz / d.sampleRate());
            double s1 = 0.0, s2 = 0.0;
            for (auto v : mono)
            {
                const auto s0 = v + coefficient * s1 - s2;
                s2 = s1;
                s1 = s0;
            }
            total += s1 * s1 + s2 * s2 - coefficient * s1 * s2;
        }
        return total;
    };

    double best = 0.0, bestLevel = 0.0;
    std::vector<double> levels;
    for (double hz = 45.0; hz <= 65.0; hz += 0.1)
    {
        const auto l = level (hz);
        levels.push_back (l);
        if (l > bestLevel)
        {
            bestLevel = l;
            best = hz;
        }
    }

    // A hum stands well clear of what is around it.
    std::nth_element (levels.begin(), levels.begin() + (long) levels.size() / 2, levels.end());
    const auto typical = levels[levels.size() / 2];
    if (bestLevel < typical * 20.0 || (std::abs (best - 50.0) > 2.0 && std::abs (best - 60.0) > 2.0))
        return 0.0;

    // Refine to 0.01 Hz.
    for (double hz = best - 0.1; hz <= best + 0.1; hz += 0.01)
        if (const auto l = level (hz); l > bestLevel)
        {
            bestLevel = l;
            best = hz;
        }
    return best;
}

AudioDataPtr dehum (const AudioData& d, Frame start, Frame end, const HumSettings& settings, Progress progress)
{
    start = std::clamp<Frame> (start, 0, d.length());
    end = std::clamp<Frame> (end, start, d.length());
    if (end <= start || settings.frequency <= 0.0)
        return nullptr;

    // One narrow notch per harmonic below the top of the audio band.
    struct Notch { double b0, b1, b2, a1, a2; };
    std::vector<Notch> notches;
    for (int h = 1; h <= std::clamp (settings.harmonics, 1, 40); ++h)
    {
        const auto hz = settings.frequency * h;
        if (hz >= d.sampleRate() * 0.45)
            break;
        const auto w0 = 2.0 * pi * hz / d.sampleRate();
        const auto q = hz / std::max (0.1, settings.widthHz);
        const auto alpha = std::sin (w0) / (2.0 * q), c = std::cos (w0), a0 = 1.0 + alpha;
        notches.push_back ({ 1.0 / a0, -2.0 * c / a0, 1.0 / a0, -2.0 * c / a0, (1.0 - alpha) / a0 });
    }

    const auto channels = d.numChannels();
    using State = std::vector<std::array<double, 2>>;
    std::vector<State> state ((size_t) channels, State (notches.size(), { 0.0, 0.0 }));
    auto run = [&] (State& filterState, float in)
    {
        double v = in;
        for (size_t i = 0; i < notches.size(); ++i)
        {
            const auto& f = notches[i];
            auto& s = filterState[i];
            const auto y = f.b0 * v + s[0];
            s[0] = f.b1 * v - f.a1 * y + s[1];
            s[1] = f.b2 * v - f.a2 * y;
            v = y;
        }
        return (float) v;
    };
    auto filter = [&] (int ch, float in) { return run (state[(size_t) ch], in); };

    // Settle the filters on a little audio before the range, then fade in and out of the result.
    const auto settle = (int) std::min<Frame> (start, (Frame) (2.0 * d.sampleRate()));
    if (settle >= (int) (1.5 * d.sampleRate()))
    {
        auto pre = readChannels (d, start - settle, settle);
        for (int ch = 0; ch < channels; ++ch)
            for (auto v : pre[(size_t) ch])
                filter (ch, v);
    }
    const auto fade = std::max<Frame> (1, std::min<Frame> ((Frame) (0.02 * d.sampleRate()), (end - start) / 2));

    // Near the start of the file there is nothing to settle on. Measure the hum
    // over the first half second and feed the filters that hum as if it had been
    // playing before, so they start out already cancelling it.
    if (settle < (int) (1.5 * d.sampleRate()))
    {
        const auto length = (int) std::min<Frame> (end - start, (Frame) (0.5 * d.sampleRate()));
        const auto lead = (int) (2.0 * d.sampleRate()) - settle;
        auto x = readChannels (d, start, length);
        for (int ch = 0; ch < channels; ++ch)
        {
            std::vector<double> madeUp ((size_t) lead, 0.0);
            for (size_t h = 0; h < notches.size(); ++h)
            {
                const auto w = 2.0 * pi * settings.frequency * (double) (h + 1) / d.sampleRate();
                double c = 0.0, sn = 0.0;
                for (int i = 0; i < length; ++i)
                {
                    c += x[(size_t) ch][(size_t) i] * std::cos (w * i);
                    sn += x[(size_t) ch][(size_t) i] * std::sin (w * i);
                }
                c *= 2.0 / length;
                sn *= 2.0 / length;
                for (int i = 0; i < lead; ++i)
                {
                    const auto t = (double) (i - lead - settle);   // samples before the start of the range
                    madeUp[(size_t) i] += c * std::cos (w * t) + sn * std::sin (w * t);
                }
            }
            for (auto v : madeUp)
                filter (ch, (float) v);
        }
        if (settle > 0)   // then the little real audio there is before the range
        {
            auto pre = readChannels (d, start - settle, settle);
            for (int ch = 0; ch < channels; ++ch)
                for (auto v : pre[(size_t) ch])
                    filter (ch, v);
        }
    }

    AudioData::Builder out (channels, d.sampleRate());
    for (Frame a = start; a < end; a += blockFrames)
    {
        const auto b = std::min (end, a + blockFrames);
        auto x = readChannels (d, a, (int) (b - a));
        for (int ch = 0; ch < channels; ++ch)
            for (Frame f = a; f < b; ++f)
            {
                auto& v = x[(size_t) ch][(size_t) (f - a)];
                const auto filtered = filter (ch, v);
                // Fade in and out where the range meets untouched audio (not at the ends of the file).
                const auto mix = (float) std::min ({ 1.0, start > 0 ? (double) (f - start + 1) / (double) fade : 1.0,
                                                     end < d.length() ? (double) (end - f) / (double) fade : 1.0 });
                v += (filtered - v) * mix;
            }

        std::vector<const float*> rows;
        for (auto& c : x)
            rows.push_back (c.data());
        out.append (rows.data(), (int) (b - a));
        progress.report ((float) (b - start) / (float) (end - start));
        if (progress.cancelled())
            return nullptr;
    }

    auto middle = out.finish();
    return d.replaced (start, end, middle.get());
}

//==============================================================================
AudioDataPtr deess (const AudioData& d, Frame start, Frame end, const DeEssSettings& settings, Progress progress)
{
    constexpr int order = 10;   // 1024 points: about 20 ms, quick enough to follow each "s"
    const Stft stft (order);
    const auto n = stft.size(), hop = stft.step(), bins = stft.numBins();

    // The band, with a soft lower edge a sixth of an octave wide.
    std::vector<float> mask ((size_t) bins, 0.0f);
    for (int k = 0; k < bins; ++k)
    {
        const auto hz = k * d.sampleRate() / n;
        const auto below = hz <= 0.0 ? 10.0 : std::log2 (settings.fromHz / hz);
        mask[(size_t) k] = below <= 0.0 ? 1.0f : below >= 1.0 / 6.0 ? 0.0f : (float) (0.5 + 0.5 * std::cos (pi * below * 6.0));
    }

    // A full-scale sine in the band reads 0 dB.
    double windowEnergy = 0.0;
    for (int i = 0; i < n; ++i)
        windowEnergy += (double) stft.windowAt (i) * stft.windowAt (i);
    const auto fullScale = windowEnergy * n / 4.0;
    const auto releasePerFrame = 100.0 * hop / d.sampleRate();   // the reduction eases off at 100 dB a second
    std::vector<double> reduction ((size_t) d.numChannels(), 0.0);

    return shapeRange (d, start, end, order, [&] (int ch, Bins& spectrum)
    {
        double power = 0.0;
        for (int k = 0; k < bins; ++k)
            power += std::norm (spectrum[(size_t) k]) * mask[(size_t) k];
        const auto levelDb = 10.0 * std::log10 (power / fullScale + 1.0e-20);
        const auto wanted = std::clamp (levelDb - settings.thresholdDb, 0.0, std::max (0.0, settings.maxReductionDb));

        auto& current = reduction[(size_t) ch];
        current = wanted >= current ? wanted : std::max (wanted, current - releasePerFrame);
        if (current <= 0.0)
            return;

        const auto gain = (float) std::pow (10.0, -current / 20.0);
        for (int k = 0; k < bins; ++k)
            spectrum[(size_t) k] *= 1.0f + (gain - 1.0f) * mask[(size_t) k];
    }, progress);
}

//==============================================================================
std::vector<Problem> findClicks (const AudioData& d, Frame start, Frame end, double sensitivity, Progress progress)
{
    std::vector<Problem> found;
    const auto maxClick = maxClickLength (d.sampleRate());
    const auto threshold = std::max (2.0, sensitivity);
    const auto finished = scanBlocks (d, start, end, [&] (int ch, const std::vector<float>& x, int from, int to, Frame first)
    {
        for (const auto& c : findClickSpans (x, from, to, threshold))
            if (c.end - c.start <= maxClick)
                found.push_back ({ Problem::Kind::click, first + c.start, first + c.end, channelBit (ch), c.strength });
    }, progress);
    return finished ? merged (std::move (found)) : std::vector<Problem>();
}

std::vector<Problem> findClipping (const AudioData& d, Frame start, Frame end, double levelPercent, Progress progress)
{
    std::vector<Problem> found;
    start = std::clamp<Frame> (start, 0, d.length());
    end = std::clamp<Frame> (end, start, d.length());
    const auto level = clipLevel (d, start, end, levelPercent);
    if (level <= 0.0f)
        return found;

    const auto finished = scanBlocks (d, start, end, [&] (int ch, const std::vector<float>& x, int from, int to, Frame first)
    {
        int tooLong = 0;
        for (const auto& r : findClipRuns (x, from, to, level, tooLong))
            found.push_back ({ Problem::Kind::clipping, first + r.start, first + r.end, channelBit (ch), level });
    }, progress);
    return finished ? merged (std::move (found)) : std::vector<Problem>();
}

std::vector<Problem> findDropouts (const AudioData& d, Frame start, Frame end, Progress progress)
{
    std::vector<Problem> found;
    start = std::clamp<Frame> (start, 0, d.length());
    end = std::clamp<Frame> (end, start, d.length());
    const auto rate = d.sampleRate();
    const auto shortest = std::max<Frame> (8, (Frame) (0.001 * rate)), longest = (Frame) rate;
    const auto around = std::max (16, (int) (0.01 * rate));

    // Loud enough to count as sound: -60 dBFS RMS over 10 ms.
    auto sounding = [&] (Frame from)
    {
        auto x = readChannels (d, from, around);
        for (const auto& c : x)
        {
            double sum = 0.0;
            for (auto v : c)
                sum += (double) v * v;
            if (sum / around > 1.0e-6)
                return true;
        }
        return false;
    };

    Frame runStart = -1;
    for (Frame a = start; a < end; a += blockFrames)
    {
        const auto b = std::min (end, a + blockFrames);
        auto x = readChannels (d, a, (int) (b - a));
        for (Frame f = a; f < b; ++f)
        {
            bool silent = true;
            for (const auto& c : x)
                if (std::abs (c[(size_t) (f - a)]) > 1.0e-7f)
                {
                    silent = false;
                    break;
                }
            if (silent)
            {
                if (runStart < 0)
                    runStart = f;
                continue;
            }
            if (runStart > start && f - runStart >= shortest && f - runStart <= longest
                && sounding (runStart - around) && sounding (f))
                found.push_back ({ Problem::Kind::dropout, runStart, f, 0, 0.0 });
            runStart = -1;
        }
        progress.report ((float) (b - start) / (float) (end - start));
        if (progress.cancelled())
            return {};
    }
    return found;
}

std::vector<Problem> findSilence (const AudioData& d, Frame start, Frame end, double thresholdDb, double minSeconds, Progress progress)
{
    std::vector<Problem> found;
    start = std::clamp<Frame> (start, 0, d.length());
    end = std::clamp<Frame> (end, start, d.length());
    const auto window = std::max<Frame> (16, (Frame) (0.01 * d.sampleRate()));
    const auto limit = std::pow (10.0, thresholdDb / 10.0);   // mean square
    const auto shortest = std::max<Frame> (window, (Frame) (minSeconds * d.sampleRate()));
    const auto perBlock = std::max<Frame> (1, blockFrames / window) * window;

    Frame runStart = -1;
    auto close = [&] (Frame at)
    {
        if (runStart >= 0 && at - runStart >= shortest)
            found.push_back ({ Problem::Kind::silence, runStart, at, 0, 0.0 });
        runStart = -1;
    };

    for (Frame a = start; a < end; a += perBlock)
    {
        const auto b = std::min (end, a + perBlock);
        auto x = readChannels (d, a, (int) (b - a));
        for (Frame w = a; w < b; w += window)
        {
            const auto n = (int) std::min (window, b - w);
            double loudest = 0.0;
            for (const auto& c : x)
            {
                double sum = 0.0;
                for (int i = 0; i < n; ++i)
                    sum += (double) c[(size_t) (w - a + i)] * c[(size_t) (w - a + i)];
                loudest = std::max (loudest, sum / n);
            }
            if (loudest < limit)
            {
                if (runStart < 0)
                    runStart = w;
            }
            else
                close (w);
        }
        progress.report ((float) (b - start) / (float) (end - start));
        if (progress.cancelled())
            return {};
    }
    close (end);
    return found;
}

//==============================================================================
AudioDataPtr repairProblems (const AudioData& d, const std::vector<Problem>& problems, Report& report, Progress progress)
{
    report = {};
    std::vector<Problem> list, longDropouts;
    constexpr int longestGap = 512;   // interpolateGap's limit; longer dropouts are healed spectrally
    for (const auto& p : problems)
    {
        if (p.end <= p.start || p.start < 0 || p.end > d.length() || p.kind == Problem::Kind::silence)
            continue;
        if (p.kind == Problem::Kind::dropout && p.end - p.start > longestGap)
            longDropouts.push_back (p);
        else
            list.push_back (p);
    }
    std::sort (list.begin(), list.end(), [] (const Problem& a, const Problem& b) { return a.start < b.start; });

    // Problems close together are fixed in one stretch with room for context either
    // side; the audio between stretches is shared with the original, not copied.
    constexpr Frame room = blockMargin;
    const auto channels = d.numChannels();
    AudioData::Builder out (channels, d.sampleRate());
    Frame written = 0;
    for (size_t i = 0; i < list.size();)
    {
        auto first = list[i].start, last = list[i].end;
        auto j = i + 1;
        while (j < list.size() && list[j].start < last + 2 * room && last - first < blockFrames)
            last = std::max (last, list[j++].end);

        const auto base = first - room;
        auto x = readChannels (d, base, (int) (last - first + 2 * room));
        for (auto k = i; k < j; ++k)
        {
            const auto& p = list[k];
            const auto s = (int) (p.start - base), e = (int) (p.end - base);
            bool fixed = false, failed = false;
            for (int ch = 0; ch < channels; ++ch)
            {
                if (p.channels != 0 && ch < 64 && (p.channels & channelBit (ch)) == 0)
                    continue;
                auto& c = x[(size_t) ch];
                if (p.kind == Problem::Kind::clipping)
                {
                    int tooLong = 0;
                    const auto runs = findClipRuns (c, s, e, (float) p.size, tooLong);
                    rebuildClipRuns (c, runs, (float) p.size);
                    fixed = fixed || ! runs.empty();
                }
                else if (interpolateGap (c, s, e - s))
                    fixed = true;
                else
                    failed = true;
            }
            if (fixed && ! failed)
                ++report.fixed;
            else
                ++report.skipped;
        }

        out.appendShared (d, written, first);
        std::vector<const float*> rows;
        for (auto& c : x)
            rows.push_back (c.data() + room);
        out.append (rows.data(), (int) (last - first));
        written = last;
        i = j;

        progress.report ((float) i / (float) (list.size() + longDropouts.size()));
        if (progress.cancelled())
            return nullptr;
    }
    out.appendShared (d, written, d.length());
    AudioDataPtr result = out.finish();

    for (size_t k = 0; k < longDropouts.size(); ++k)
    {
        std::string error;
        SpectralRegion r;
        r.start = longDropouts[k].start;
        r.end = longDropouts[k].end;
        if (auto healed = autoHeal (*result, r, error))
        {
            result = std::move (healed);
            ++report.fixed;
        }
        else
            ++report.skipped;
        progress.report ((float) (list.size() + k + 1) / (float) (list.size() + longDropouts.size()));
        if (progress.cancelled())
            return nullptr;
    }
    return report.fixed > 0 ? result : nullptr;
}
}

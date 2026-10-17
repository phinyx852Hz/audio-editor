// SPDX-License-Identifier: AGPL-3.0-only
#include "Repair.h"
#include "Stft.h"

#include <algorithm>
#include <cmath>
#include <complex>
#include <random>

namespace repair
{
namespace
{
    using Bins = Stft::Bins;
    constexpr double pi = 3.14159265358979323846;
    constexpr double featherOctaves = 1.0 / 6.0;   // soft edge above and below a band
    constexpr int contextFrames = 6;               // frames each side that Auto Heal learns from

    int orderFor (int size)
    {
        int order = 0;
        while ((1 << order) < size)
            ++order;
        return order;
    }

    double wrap (double phase)
    {
        return phase - 2.0 * pi * std::round (phase / (2.0 * pi));
    }

    // How much of each bin the edit touches: 1 inside the band, easing to 0 over the feather.
    std::vector<float> bandMask (const SpectralRegion& r, int numBins, int fftSize, double rate)
    {
        std::vector<float> mask ((size_t) numBins, 1.0f);
        if (! r.hasBand())
            return mask;

        for (int k = 0; k < numBins; ++k)
        {
            const auto hz = std::max (1.0e-3, k * rate / fftSize);
            double outside = 0.0;   // octaves beyond the band
            if (hz < r.lowHz)
                outside = std::log2 (r.lowHz / hz);
            else if (hz > r.highHz)
                outside = std::log2 (hz / r.highHz);
            mask[(size_t) k] = outside <= 0.0 ? 1.0f
                             : outside >= featherOctaves ? 0.0f
                             : (float) (0.5 + 0.5 * std::cos (pi * outside / featherOctaves));
        }
        return mask;
    }

    // The stretch around a region that is read, changed and written back.
    struct Stretch
    {
        Frame first = 0;    // file position of sample 0 (may be before the file starts)
        int length = 0;
    };

    // Runs `edit` on every channel. `edit` gets the channel's samples over the
    // stretch and adds its change to `change`; the result is the file with the
    // change added over the part of the stretch inside the file.
    template <typename Edit>
    AudioDataPtr applyChange (const AudioData& d, Stretch s, const Edit& edit, const Stft& stft)
    {
        const auto channels = d.numChannels();
        std::vector<std::vector<float>> x ((size_t) channels, std::vector<float> ((size_t) s.length, 0.0f));
        std::vector<float*> ptrs;
        for (auto& c : x)
            ptrs.push_back (c.data());
        d.read (s.first, s.length, ptrs.data());   // zeros outside the file

        const auto norm = stft.normalisation (s.length);
        for (int ch = 0; ch < channels; ++ch)
        {
            std::vector<float> change ((size_t) s.length, 0.0f);
            edit (x[(size_t) ch], change);
            for (int i = 0; i < s.length; ++i)
                if (norm[(size_t) i] > 1.0e-4f)
                    x[(size_t) ch][(size_t) i] += change[(size_t) i] / norm[(size_t) i];
        }

        const auto from = std::max<Frame> (0, s.first);
        const auto to = std::min<Frame> (d.length(), s.first + s.length);
        AudioData::Builder out (channels, d.sampleRate());
        std::vector<const float*> rows;
        for (auto& c : x)
            rows.push_back (c.data() + (from - s.first));
        out.append (rows.data(), (int) (to - from));
        auto middle = out.finish();
        return d.replaced (from, to, middle.get());
    }
}

AudioDataPtr spectralGain (const AudioData& d, const SpectralRegion& region, double gainDb)
{
    const auto start = std::clamp<Frame> (region.start, 0, d.length());
    const auto end = std::clamp<Frame> (region.end, start, d.length());
    if (end <= start)
        return nullptr;

    const Stft stft (region.hasBand() ? 12 : 11);
    const auto n = stft.size(), hop = stft.step();
    const auto g = gainDb <= removeDb ? 0.0f : (float) std::pow (10.0, gainDb / 20.0);
    const auto mask = bandMask (region, stft.numBins(), n, d.sampleRate());

    // Two windows of room each side, so every changed frame lies where the windows fully overlap.
    Stretch s;
    s.first = start - 2 * n;
    s.length = (int) ((end - start) + 4 * n);
    s.length += (hop - (s.length - n) % hop) % hop;

    double windowEnergy = 0.0;
    for (int i = 0; i < n; ++i)
        windowEnergy += (double) stft.windowAt (i) * stft.windowAt (i);

    return applyChange (d, s, [&] (const std::vector<float>& x, std::vector<float>& change)
    {
        Bins bins, delta ((size_t) stft.numBins());
        for (int t = 0; t < stft.numFrames (s.length); ++t)
        {
            // How much of this frame's window lies inside the region.
            const auto p = s.first + (Frame) t * hop;
            double inside = 0.0;
            for (auto f = std::max (p, start); f < std::min (p + n, end); ++f)
                inside += (double) stft.windowAt ((int) (f - p)) * stft.windowAt ((int) (f - p));
            const auto weight = (float) (inside / windowEnergy) * (g - 1.0f);
            if (std::abs (weight) < 1.0e-9f)
                continue;

            stft.analyse (x, t, bins);
            for (int k = 0; k < stft.numBins(); ++k)
                delta[(size_t) k] = bins[(size_t) k] * (weight * mask[(size_t) k]);
            stft.addChange (delta, t, change);
        }
    }, stft);
}

AudioDataPtr autoHeal (const AudioData& d, const SpectralRegion& region, std::string& error)
{
    const auto start = std::clamp<Frame> (region.start, 0, d.length());
    const auto end = std::clamp<Frame> (region.end, start, d.length());
    if (end <= start)
    {
        error = "Select the sound to heal first.";
        return nullptr;
    }
    if ((double) (end - start) > maxHealSeconds * d.sampleRate())
    {
        error = "Auto Heal works on up to 4 seconds at a time. Select a shorter part.";
        return nullptr;
    }

    // Short clicks get short windows so the repair stays tight; a band needs finer frequencies.
    const auto length = (int) (end - start);
    const auto order = region.hasBand() ? 12 : std::clamp (orderFor (4 * length), 8, 11);
    const Stft stft (order);
    const auto n = stft.size(), hop = stft.step(), bins = stft.numBins();
    const auto mask = bandMask (region, bins, n, d.sampleRate());

    Stretch s;
    s.first = start - 2 * n - (Frame) contextFrames * hop;
    s.length = (int) ((end - start) + 2 * (2 * n + contextFrames * hop));
    s.length += (hop - (s.length - n) % hop) % hop;
    const auto frames = stft.numFrames (s.length);

    // Frames touching the region are rebuilt; whole frames inside the file around them are the context.
    std::vector<int> before, after;
    int firstMissing = -1, lastMissing = -1;
    for (int t = 0; t < frames; ++t)
    {
        const auto p = s.first + (Frame) t * hop;
        if (p + n <= start)
        {
            if (p >= 0)
                before.push_back (t);
        }
        else if (p >= end)
        {
            if (p + n <= d.length())
                after.push_back (t);
        }
        else
        {
            if (firstMissing < 0)
                firstMissing = t;
            lastMissing = t;
        }
    }
    if (before.size() > (size_t) contextFrames)
        before.erase (before.begin(), before.end() - contextFrames);
    if (after.size() > (size_t) contextFrames)
        after.resize ((size_t) contextFrames);

    if (firstMissing < 0 || (before.empty() && after.empty()))
    {
        error = "There is not enough audio around the selection to heal it from.";
        return nullptr;
    }

    return applyChange (d, s, [&] (const std::vector<float>& x, std::vector<float>& change)
    {
        std::vector<Bins> spectra ((size_t) frames);
        for (int t = 0; t < frames; ++t)
            stft.analyse (x, t, spectra[(size_t) t]);

        // Per bin and side: typical level (ignoring stray peaks such as a nearby
        // click), the phase and how fast it turns, and how steady that turning is
        // (1 for a held tone, near 0 for noise).
        struct Side { std::vector<float> level; std::vector<double> phase, turn, steady; int frame = -1; };
        auto learn = [&] (const std::vector<int>& context, bool isBefore)
        {
            Side side;
            if (context.empty())
                return side;

            side.level.resize ((size_t) bins);
            side.phase.resize ((size_t) bins);
            side.turn.resize ((size_t) bins);
            side.steady.assign ((size_t) bins, 1.0);
            side.frame = isBefore ? context.back() : context.front();
            std::vector<float> levels (context.size());
            for (int k = 0; k < bins; ++k)
            {
                for (size_t i = 0; i < context.size(); ++i)
                    levels[i] = std::abs (spectra[(size_t) context[i]][(size_t) k]);
                auto sorted = levels;
                std::nth_element (sorted.begin(), sorted.begin() + (long) sorted.size() / 2, sorted.end());
                const auto median = sorted[sorted.size() / 2];
                double power = 0.0;
                int counted = 0;
                for (auto v : levels)
                    if (v <= 3.0f * median)
                    {
                        power += (double) v * v;
                        ++counted;
                    }
                side.level[(size_t) k] = (float) std::sqrt (power / std::max (1, counted));

                // Phase turn per hop between neighbouring context frames, and how much it varies.
                const auto expected = 2.0 * pi * k * hop / n;
                std::complex<double> turns (0.0, 0.0);
                for (size_t i = 1; i < context.size(); ++i)
                {
                    const auto step = wrap ((double) std::arg (spectra[(size_t) context[i]][(size_t) k])
                                            - (double) std::arg (spectra[(size_t) context[i - 1]][(size_t) k]) - expected);
                    turns += std::polar (1.0, step);
                }
                side.phase[(size_t) k] = (double) std::arg (spectra[(size_t) side.frame][(size_t) k]);
                side.turn[(size_t) k] = expected;
                if (context.size() > 1)
                {
                    side.turn[(size_t) k] = expected + std::arg (turns);
                    side.steady[(size_t) k] = std::abs (turns) / (double) (context.size() - 1);
                }
            }
            return side;
        };
        const auto pre = learn (before, true);
        const auto post = learn (after, false);

        // Noise is rebuilt as fresh noise of the same level. Independent frames add
        // up more quietly than overlapping real ones, hence the correction.
        double windowSquares = 0.0;
        for (int i = 0; i < n; ++i)
            windowSquares += (double) stft.windowAt (i) * stft.windowAt (i);
        const auto noiseGain = (float) std::sqrt (1.5 / (windowSquares / n)) / std::sqrt (2.0f);
        std::mt19937 random (12345);
        std::normal_distribution<float> gauss (0.0f, 1.0f);

        Bins delta ((size_t) bins);
        const auto from = pre.frame >= 0 ? pre.frame : firstMissing - 1;
        const auto to = post.frame >= 0 ? post.frame : lastMissing + 1;
        for (int t = firstMissing; t <= lastMissing; ++t)
        {
            // Fade from the sound before to the sound after across the gap.
            const auto a = (float) (t - from) / (float) (to - from);
            const auto towardsAfter = pre.frame < 0 ? 1.0f : post.frame < 0 ? 0.0f : a;
            for (int k = 0; k < bins; ++k)
            {
                const auto levelBefore = pre.frame >= 0 ? pre.level[(size_t) k] : post.level[(size_t) k];
                const auto levelAfter = post.frame >= 0 ? post.level[(size_t) k] : pre.level[(size_t) k];
                const auto level = levelBefore + (levelAfter - levelBefore) * towardsAfter;
                const auto steadyBefore = pre.frame >= 0 ? pre.steady[(size_t) k] : post.steady[(size_t) k];
                const auto steadyAfter = post.frame >= 0 ? post.steady[(size_t) k] : pre.steady[(size_t) k];
                const auto tonal = (float) std::clamp ((std::min (steadyBefore, steadyAfter) - 0.6) / 0.3, 0.0, 1.0);

                // A tone: carry each side's phase on into the gap and blend, so it stays continuous at both ends.
                std::complex<float> tone (0.0f, 0.0f);
                if (tonal > 0.0f)
                {
                    std::complex<double> dir (0.0, 0.0);
                    if (pre.frame >= 0)
                        dir += (1.0 - towardsAfter) * std::polar (1.0, pre.phase[(size_t) k] + (t - pre.frame) * pre.turn[(size_t) k]);
                    if (post.frame >= 0)
                        dir += (double) towardsAfter * std::polar (1.0, post.phase[(size_t) k] - (post.frame - t) * post.turn[(size_t) k]);
                    const auto size = std::abs (dir);
                    tone = size > 1.0e-6 ? std::complex<float> ((float) (dir.real() / size), (float) (dir.imag() / size)) * level
                                         : std::complex<float> (level, 0.0f);
                }
                const std::complex<float> hiss (gauss (random) * level * noiseGain, gauss (random) * level * noiseGain);
                const auto rebuilt = tone * tonal + hiss * (1.0f - tonal);

                delta[(size_t) k] = (rebuilt - spectra[(size_t) t][(size_t) k]) * mask[(size_t) k];
            }
            stft.addChange (delta, t, change);
        }
    }, stft);
}
}

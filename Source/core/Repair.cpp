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

    // For a mask: how much of each bin a frame at p (n samples long) touches.
    // `peak` takes the most the mask selects anywhere in the window (for healing,
    // so nothing selected leaks through); otherwise the window-weighted average.
    void maskForFrame (const SpectralMask& mask, Frame p, int n, double rate, bool peak, std::vector<float>& out)
    {
        const auto bins = n / 2 + 1;
        out.assign ((size_t) bins, 0.0f);
        const auto step = std::min (n / 8, SpectralMask::framesPerColumn);
        double total = 0.0;
        for (int j = step / 2; j < n; j += step)
        {
            const auto w = peak ? 1.0 : std::pow (std::sin (pi * (j + 0.5) / n), 4.0);   // Hann squared
            total += w;
            for (int k = 0; k < bins; ++k)
            {
                const auto v = mask.at (p + j, k * rate / n);
                out[(size_t) k] = peak ? std::max (out[(size_t) k], v) : out[(size_t) k] + (float) (w * v);
            }
        }
        if (! peak && total > 0.0)
            for (auto& v : out)
                v = (float) (v / total);
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

//==============================================================================
SpectralMask::SpectralMask (double sampleRate)
    : rate (sampleRate), rowCount (std::max (1, (int) std::ceil (std::log2 (sampleRate / 2.0 / lowestHz) * rowsPerOctave)))
{
}

double SpectralMask::rowToHz (double row) const { return lowestHz * std::pow (2.0, row / rowsPerOctave); }
double SpectralMask::hzToRow (double hz) const  { return std::log2 (std::max (hz, 1.0e-3) / lowestHz) * rowsPerOctave; }

void SpectralMask::set (int64_t column, int row, float value)
{
    if (row < 0 || row >= rowCount || value <= 0.0f)
        return;
    auto& c = cells[column];
    if (c.empty())
        c.assign ((size_t) rowCount, 0.0f);
    c[(size_t) row] = std::max (c[(size_t) row], std::min (1.0f, value));
}

void SpectralMask::paint (Frame frame, double hz, double radiusFrames, double radiusRows)
{
    radiusFrames = std::max (radiusFrames, (double) framesPerColumn);
    radiusRows = std::max (radiusRows, 1.0);
    const auto centreColumn = (double) frame / framesPerColumn;
    const auto centreRow = hzToRow (hz);
    const auto rc = radiusFrames / framesPerColumn;
    for (auto col = (int64_t) std::floor (centreColumn - rc); col <= (int64_t) std::ceil (centreColumn + rc); ++col)
        for (auto row = (int) std::floor (centreRow - radiusRows); row <= (int) std::ceil (centreRow + radiusRows); ++row)
        {
            const auto dx = ((double) col + 0.5 - centreColumn) / rc, dy = (row + 0.5 - centreRow) / radiusRows;
            const auto distance = std::sqrt (dx * dx + dy * dy);
            // Solid in the middle, easing out over the outer quarter.
            if (distance < 1.0)
                set (col, row, distance < 0.75 ? 1.0f : (float) (0.5 + 0.5 * std::cos (pi * (distance - 0.75) / 0.25)));
        }
}

void SpectralMask::fillPolygon (const std::vector<std::pair<Frame, double>>& points)
{
    if (points.size() < 3)
        return;

    std::vector<std::pair<double, double>> poly;   // (column, row)
    double minC = 1.0e300, maxC = -1.0e300, minR = 1.0e300, maxR = -1.0e300;
    for (const auto& [frame, hz] : points)
    {
        poly.push_back ({ (double) frame / framesPerColumn, hzToRow (hz) });
        minC = std::min (minC, poly.back().first);
        maxC = std::max (maxC, poly.back().first);
        minR = std::min (minR, poly.back().second);
        maxR = std::max (maxR, poly.back().second);
    }

    for (auto col = (int64_t) std::floor (minC); col <= (int64_t) std::ceil (maxC); ++col)
        for (auto row = std::max (0, (int) std::floor (minR)); row <= std::min (rowCount - 1, (int) std::ceil (maxR)); ++row)
        {
            // Even-odd rule at the cell's centre.
            const auto x = (double) col + 0.5, y = row + 0.5;
            bool inside = false;
            for (size_t i = 0, j = poly.size() - 1; i < poly.size(); j = i++)
            {
                const auto& [xi, yi] = poly[i];
                const auto& [xj, yj] = poly[j];
                if ((yi > y) != (yj > y) && x < (xj - xi) * (y - yi) / (yj - yi) + xi)
                    inside = ! inside;
            }
            if (inside)
                set (col, row, 1.0f);
        }
}

void SpectralMask::addHarmonics (int count)
{
    for (auto& [column, values] : cells)
    {
        const auto original = values;
        for (int h = 2; h <= count; ++h)
        {
            const auto shift = std::log2 ((double) h) * rowsPerOctave;
            for (int row = 0; row < rowCount; ++row)
            {
                if (original[(size_t) row] <= 0.0f)
                    continue;
                // Spread over the two rows the shifted row falls between.
                const auto target = row + shift;
                const auto lower = (int) std::floor (target);
                if (lower >= rowCount)
                    break;
                values[(size_t) lower] = std::max (values[(size_t) lower], original[(size_t) row]);
                if (lower + 1 < rowCount)
                    values[(size_t) lower + 1] = std::max (values[(size_t) lower + 1], original[(size_t) row]);
            }
        }
    }
}

float SpectralMask::at (Frame frame, double hz) const
{
    const auto it = cells.find ((int64_t) std::floor ((double) frame / framesPerColumn));
    if (it == cells.end())
        return 0.0f;
    const auto row = hzToRow (hz) - 0.5;
    const auto lower = (int) std::floor (row);
    const auto t = (float) (row - lower);
    const auto get = [&] (int r) { return r >= 0 && r < rowCount ? it->second[(size_t) r] : 0.0f; };
    return get (lower) * (1.0f - t) + get (lower + 1) * t;
}

Frame SpectralMask::start() const { return cells.empty() ? 0 : cells.begin()->first * framesPerColumn; }
Frame SpectralMask::end() const   { return cells.empty() ? 0 : (cells.rbegin()->first + 1) * framesPerColumn; }

double SpectralMask::lowestHzSelected() const
{
    int lowest = rowCount;
    for (const auto& [column, values] : cells)
        for (int r = 0; r < lowest; ++r)
            if (values[(size_t) r] > 0.0f)
            {
                lowest = r;
                break;
            }
    return rowToHz (lowest);
}

double SpectralMask::highestHzSelected() const
{
    int highest = -1;
    for (const auto& [column, values] : cells)
        for (int r = rowCount - 1; r > highest; --r)
            if (values[(size_t) r] > 0.0f)
            {
                highest = r;
                break;
            }
    return rowToHz (highest + 1);
}

AudioDataPtr spectralGain (const AudioData& d, const SpectralRegion& region, double gainDb)
{
    const auto* shape = region.mask.get();
    const auto start = std::clamp<Frame> (shape != nullptr ? shape->start() : region.start, 0, d.length());
    const auto end = std::clamp<Frame> (shape != nullptr ? shape->end() : region.end, start, d.length());
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
        std::vector<float> touched;
        for (int t = 0; t < stft.numFrames (s.length); ++t)
        {
            if (shape != nullptr)
            {
                // A drawn shape says, bin by bin, how much of this frame it covers.
                const auto p = s.first + (Frame) t * hop;
                maskForFrame (*shape, p, n, d.sampleRate(), false, touched);
                if (*std::max_element (touched.begin(), touched.end()) < 1.0e-6f)
                    continue;
                stft.analyse (x, t, bins);
                for (int k = 0; k < stft.numBins(); ++k)
                    delta[(size_t) k] = bins[(size_t) k] * ((g - 1.0f) * touched[(size_t) k]);
                stft.addChange (delta, t, change);
                continue;
            }

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
    const auto* shape = region.mask.get();
    const auto start = std::clamp<Frame> (shape != nullptr ? shape->start() : region.start, 0, d.length());
    const auto end = std::clamp<Frame> (shape != nullptr ? shape->end() : region.end, start, d.length());
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
        std::vector<float> touched;
        const auto from = pre.frame >= 0 ? pre.frame : firstMissing - 1;
        const auto to = post.frame >= 0 ? post.frame : lastMissing + 1;
        for (int t = firstMissing; t <= lastMissing; ++t)
        {
            // Fade from the sound before to the sound after across the gap.
            const auto a = (float) (t - from) / (float) (to - from);
            const auto towardsAfter = pre.frame < 0 ? 1.0f : post.frame < 0 ? 0.0f : a;
            if (shape != nullptr)
            {
                maskForFrame (*shape, s.first + (Frame) t * hop, n, d.sampleRate(), true, touched);
                if (*std::max_element (touched.begin(), touched.end()) < 1.0e-6f)
                    continue;
            }
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

                delta[(size_t) k] = (rebuilt - spectra[(size_t) t][(size_t) k])
                                  * (shape != nullptr ? touched[(size_t) k] : mask[(size_t) k]);
            }
            stft.addChange (delta, t, change);
        }
    }, stft);
}

//==============================================================================
std::vector<SpectralRegion> findSimilar (const AudioData& d, const SpectralRegion& example, Frame rangeStart, Frame rangeEnd,
                                         double strictness, Progress progress)
{
    std::vector<SpectralRegion> found;
    const auto* shape = example.mask.get();
    const auto exStart = shape != nullptr ? shape->start() : example.start;
    const auto exEnd = shape != nullptr ? shape->end() : example.end;
    rangeStart = std::clamp<Frame> (rangeStart, 0, d.length());
    rangeEnd = std::clamp<Frame> (rangeEnd, rangeStart, d.length());
    if (exEnd <= exStart || rangeEnd <= rangeStart || exStart < rangeStart || exEnd > rangeEnd)
        return found;

    constexpr int order = 10, n = 1 << order, hop = n / 4;
    const auto rate = d.sampleRate();
    const auto nyquist = rate / 2.0;
    auto lowHz = 40.0, highHz = std::min (20000.0, nyquist);
    if (shape != nullptr)
    {
        lowHz = shape->lowestHzSelected();
        highHz = shape->highestHzSelected();
    }
    else if (example.hasBand())
    {
        lowHz = example.lowHz;
        highHz = example.highHz;
    }

    // Sixth-octave bands over the example's frequencies, at least one bin each.
    std::vector<int> bandOf ((size_t) (n / 2 + 1), -1);
    int bands = 0, lastBand = -1;
    for (int k = 1; k <= n / 2; ++k)
    {
        const auto hz = k * rate / n;
        if (hz < lowHz * std::pow (2.0, -featherOctaves) || hz > highHz * std::pow (2.0, featherOctaves))
            continue;
        const auto b = (int) std::floor (std::log2 (std::max (hz, lowHz) / lowHz) * 6.0);
        if (b != lastBand)
        {
            lastBand = b;
            ++bands;
        }
        bandOf[(size_t) k] = bands - 1;
    }
    if (bands == 0)
        return found;

    // Band levels in dB for frames centred every hop through the range (a mono mix).
    const auto frames = (int) ((rangeEnd - rangeStart + hop - 1) / hop);
    std::vector<float> level ((size_t) frames * (size_t) bands);
    {
        const Stft stft (order);
        const auto channels = d.numChannels();
        constexpr int framesPerBlock = 512;
        Stft::Bins bins;
        for (int first = 0; first < frames; first += framesPerBlock)
        {
            if (progress.cancelled())
                return {};
            progress.report (0.6f * (float) first / (float) frames);
            const auto count = std::min (framesPerBlock, frames - first);
            const auto length = (count - 1) * hop + n;
            std::vector<std::vector<float>> x ((size_t) channels, std::vector<float> ((size_t) length));
            std::vector<float*> ptrs;
            for (auto& c : x)
                ptrs.push_back (c.data());
            d.read (rangeStart + (Frame) first * hop - n / 2, length, ptrs.data());
            std::vector<float> mono ((size_t) length, 0.0f);
            for (auto& c : x)
                for (int i = 0; i < length; ++i)
                    mono[(size_t) i] += c[(size_t) i] / (float) channels;

            std::vector<double> power ((size_t) bands);
            for (int t = 0; t < count; ++t)
            {
                stft.analyse (mono, t, bins);
                std::fill (power.begin(), power.end(), 0.0);
                for (int k = 1; k <= n / 2; ++k)
                    if (bandOf[(size_t) k] >= 0)
                        power[(size_t) bandOf[(size_t) k]] += (double) std::norm (bins[(size_t) k]);
                for (int b = 0; b < bands; ++b)
                    level[(size_t) (first + t) * (size_t) bands + (size_t) b] = (float) (10.0 * std::log10 (power[(size_t) b] + 1.0e-12));
            }
        }
    }

    // How far each band stands out from its background: the median level over a
    // second either side, taken every few frames and joined up.
    std::vector<float> excess (level.size());
    {
        const auto reach = std::max (8, (int) (rate / hop));
        constexpr int every = 8;
        const auto anchors = (frames + every - 1) / every + 1;
        std::vector<float> background ((size_t) anchors * (size_t) bands);
        std::vector<float> values;
        for (int a = 0; a < anchors; ++a)
        {
            if (progress.cancelled())
                return {};
            const auto centre = std::min (a * every, frames - 1);
            const auto from = std::max (0, centre - reach), to = std::min (frames, centre + reach + 1);
            const auto stride = std::max (1, (to - from) / 96);
            for (int b = 0; b < bands; ++b)
            {
                values.clear();
                for (int t = from; t < to; t += stride)
                    values.push_back (level[(size_t) t * (size_t) bands + (size_t) b]);
                std::nth_element (values.begin(), values.begin() + (long) values.size() / 2, values.end());
                background[(size_t) a * (size_t) bands + (size_t) b] = values[values.size() / 2];
            }
        }
        progress.report (0.75f);
        for (int t = 0; t < frames; ++t)
        {
            const auto a = t / every;
            const auto f = (float) (t % every) / (float) every;
            for (int b = 0; b < bands; ++b)
            {
                const auto lo = background[(size_t) a * (size_t) bands + (size_t) b];
                const auto hi = background[(size_t) std::min (a + 1, anchors - 1) * (size_t) bands + (size_t) b];
                const auto i = (size_t) t * (size_t) bands + (size_t) b;
                excess[i] = std::max (0.0f, level[i] - (lo + (hi - lo) * f));
            }
        }
    }

    // The example's pattern: the frames centred inside it.
    auto firstEx = (int) ((exStart - rangeStart + hop - 1) / hop);
    auto lastEx = (int) ((exEnd - rangeStart + hop - 1) / hop) - 1;
    if (lastEx < firstEx)
        firstEx = lastEx = std::clamp ((int) (((exStart + exEnd) / 2 - rangeStart) / hop), 0, frames - 1);
    lastEx = std::min (lastEx, frames - 1);
    const auto span = lastEx - firstEx + 1;
    const auto cells = (size_t) span * (size_t) bands;
    const auto* pattern = excess.data() + (size_t) firstEx * (size_t) bands;
    double patternEnergy = 0.0;
    for (size_t i = 0; i < cells; ++i)
        patternEnergy += (double) pattern[i] * pattern[i];
    if (patternEnergy < 2.25 * (double) cells)   // stands out by less than 1.5 dB on average
        return found;

    // Slide the pattern along; keep places that match its shape and at least half its strength.
    struct Match { int frame; double score; };
    std::vector<Match> matches;
    for (int t = 0; t + span <= frames; ++t)
    {
        if ((t & 4095) == 0)
        {
            if (progress.cancelled())
                return {};
            progress.report (0.75f + 0.25f * (float) t / (float) frames);
        }
        if (t + span + n / hop > firstEx && t < lastEx + 1 + n / hop)
            continue;   // the example itself
        const auto* here = excess.data() + (size_t) t * (size_t) bands;
        double dot = 0.0, energy = 0.0;
        for (size_t i = 0; i < cells; ++i)
        {
            dot += (double) here[i] * pattern[i];
            energy += (double) here[i] * here[i];
        }
        if (energy <= 0.0 || dot < 0.5 * patternEnergy)
            continue;
        const auto score = dot / std::sqrt (energy * patternEnergy);
        if (score >= strictness)
            matches.push_back ({ t, score });
    }

    // The best match in each neighbourhood (frames a window apart see the same sound),
    // never overlapping another or the example.
    std::sort (matches.begin(), matches.end(), [] (const Match& a, const Match& b) { return a.score > b.score; });
    std::vector<int> taken;
    for (const auto& m : matches)
    {
        const auto clash = std::any_of (taken.begin(), taken.end(), [&] (int t) { return std::abs (t - m.frame) < span + n / hop; });
        if (! clash)
            taken.push_back (m.frame);
    }
    std::sort (taken.begin(), taken.end());

    const auto offset = exStart - (rangeStart + (Frame) firstEx * hop);
    const auto room = (Frame) hop;   // matches are found to within a hop
    for (auto t : taken)
    {
        SpectralRegion r;
        const auto at = rangeStart + (Frame) t * hop + offset;
        r.start = std::max (rangeStart, at - room);
        r.end = std::min (rangeEnd, at + (exEnd - exStart) + room);
        if (shape != nullptr || example.hasBand())
        {
            r.lowHz = lowHz;
            r.highHz = highHz;
        }
        found.push_back (r);
    }
    progress.report (1.0f);
    return found;
}
}

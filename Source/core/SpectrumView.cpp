// SPDX-License-Identifier: AGPL-3.0-only
#include "SpectrumView.h"

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <sstream>
#include <thread>

namespace spectrum
{
    namespace
    {
        constexpr double pi = 3.14159265358979323846;
        constexpr double logFloorHz = 10.0;
        constexpr int maxFramesPerColumn = 8;   // when zoomed out, a column takes the loudest of this many frames at most

        int orderOf (int size)
        {
            int order = 0;
            while ((1 << order) < size)
                ++order;
            return order;
        }

        int powerOfTwo (int value, int lowest, int highest)
        {
            return 1 << orderOf (std::clamp (value, lowest, highest));
        }

        double windowAt (Window shape, double t)   // t from 0 to 1 over the window
        {
            switch (shape)
            {
                case Window::hamming:
                    return 0.54 - 0.46 * std::cos (2.0 * pi * t);
                case Window::blackmanHarris:
                    return 0.35875 - 0.48829 * std::cos (2.0 * pi * t) + 0.14128 * std::cos (4.0 * pi * t) - 0.01168 * std::cos (6.0 * pi * t);
                case Window::hann:
                default:
                    return 0.5 - 0.5 * std::cos (2.0 * pi * t);
            }
        }
    }

    //==========================================================================
    std::string toString (const Settings& s)
    {
        std::ostringstream out;
        out << "window=" << s.windowSize << ";shape=" << (int) s.window << ";overlap=" << s.overlap
            << ";oversample=" << s.oversample << ";scale=" << (int) s.scale << ";bins=" << (int) s.bins
            << ";normalise=" << (int) s.normalise << ";gain=" << s.gainDb << ";range=" << s.rangeDb
            << ";colours=" << s.colourMap;
        return out.str();
    }

    Settings fromString (const std::string& text)
    {
        Settings s;
        std::istringstream in (text);
        std::string item;
        while (std::getline (in, item, ';'))
        {
            const auto eq = item.find ('=');
            if (eq == std::string::npos)
                continue;
            const auto key = item.substr (0, eq);
            const auto value = std::strtod (item.c_str() + eq + 1, nullptr);
            const auto whole = (int) std::lround (value);
            if (key == "window")          s.windowSize = powerOfTwo (whole, 256, 32768);
            else if (key == "shape")      s.window = (Window) std::clamp (whole, 0, 2);
            else if (key == "overlap")    s.overlap = powerOfTwo (whole, 1, 16);
            else if (key == "oversample") s.oversample = powerOfTwo (whole, 1, 8);
            else if (key == "scale")      s.scale = (Scale) std::clamp (whole, 0, 2);
            else if (key == "bins")       s.bins = (Bins) std::clamp (whole, 0, 2);
            else if (key == "normalise")  s.normalise = (Normalise) std::clamp (whole, 0, 2);
            else if (key == "gain")       s.gainDb = std::clamp (value, -50.0, 50.0);
            else if (key == "range")      s.rangeDb = std::clamp (value, 20.0, 200.0);
            else if (key == "colours")    s.colourMap = std::clamp (whole, 0, (int) colourMapNames().size() - 1);
        }
        return s;
    }

    const std::vector<std::string>& colourMapNames()
    {
        static const std::vector<std::string> names { "Theme", "Sunset", "Ice", "Green", "Grey", "Grey on White", "Rainbow", "Cherry" };
        return names;
    }

    std::vector<unsigned> colourMapStops (int map)
    {
        switch (map)
        {
            case 1:  return { 0x000004, 0x2c105c, 0x711f81, 0xb63679, 0xee605e, 0xfdae78, 0xfcfdbf };   // magma-like
            case 2:  return { 0x000000, 0x0b1e4f, 0x1f5fa8, 0x47a6d8, 0xa6e3f5, 0xffffff };
            case 3:  return { 0x000000, 0x003300, 0x006b1f, 0x2ea84a, 0x9be27a, 0xf2ffd8 };
            case 4:  return { 0x000000, 0xffffff };
            case 5:  return { 0xffffff, 0x000000 };
            case 6:  return { 0x000000, 0x30107a, 0x0050ff, 0x00c8c8, 0x30e030, 0xffe000, 0xff6000, 0xff0000, 0xffffff };
            case 7:  return { 0x000000, 0x3a0010, 0x8c0030, 0xd8204a, 0xff8070, 0xffe6d8 };
            default: return {};
        }
    }

    //==========================================================================
    double hzToMel (double hz)  { return 2595.0 * std::log10 (1.0 + hz / 700.0); }
    double melToHz (double mel) { return 700.0 * (std::pow (10.0, mel / 2595.0) - 1.0); }

    double lowestFor (Scale scale, double low)
    {
        return scale == Scale::log ? std::max (low, logFloorHz) : std::max (low, 0.0);
    }

    double position (Scale scale, double hz, double low, double high)
    {
        low = lowestFor (scale, low);
        if (high <= low)
            return 0.0;
        switch (scale)
        {
            case Scale::linear: return (hz - low) / (high - low);
            case Scale::mel:    return (hzToMel (hz) - hzToMel (low)) / (hzToMel (high) - hzToMel (low));
            case Scale::log:
            default:            return std::log (std::max (hz, logFloorHz * 0.5) / low) / std::log (high / low);
        }
    }

    double frequency (Scale scale, double pos, double low, double high)
    {
        low = lowestFor (scale, low);
        switch (scale)
        {
            case Scale::linear: return low + pos * (high - low);
            case Scale::mel:    return melToHz (hzToMel (low) + pos * (hzToMel (high) - hzToMel (low)));
            case Scale::log:
            default:            return low * std::pow (high / low, pos);
        }
    }

    std::string noteName (double hz)
    {
        if (hz <= 0.0)
            return {};
        static const char* names[] = { "C", "C#", "D", "D#", "E", "F", "F#", "G", "G#", "A", "A#", "B" };
        const auto midi = 69.0 + 12.0 * std::log2 (hz / 440.0);
        const auto nearest = (int) std::lround (midi);
        const auto cents = (int) std::lround ((midi - nearest) * 100.0);
        const auto octave = (int) std::floor (nearest / 12.0) - 1;
        std::string text = names[((nearest % 12) + 12) % 12] + std::to_string (octave);
        if (cents != 0)
            text += (cents > 0 ? " +" : " \xe2\x88\x92") + std::to_string (std::abs (cents)) + "\xc2\xa2";
        return text;
    }

    //==========================================================================
    Analyzer::Analyzer (const Settings& s, double sampleRate)
        : w (powerOfTwo (s.windowSize, 256, 32768)),
          n (w * powerOfTwo (s.oversample, 1, 8)),
          rate (sampleRate),
          fft (orderOf (n)),
          window ((size_t) w), slope ((size_t) w),
          a ((size_t) n), b ((size_t) n)
    {
        double sum = 0.0;
        for (int i = 0; i < w; ++i)
        {
            const auto t = (double) i / w;   // periodic window
            window[(size_t) i] = (float) windowAt (s.window, t);
            sum += window[(size_t) i];
        }
        // The window's slope per sample, for reassignment (central difference, wrapping: the window is periodic).
        for (int i = 0; i < w; ++i)
            slope[(size_t) i] = 0.5f * (window[(size_t) ((i + 1) % w)] - window[(size_t) ((i + w - 1) % w)]);
        scale = (float) (2.0 / sum);
    }

    void Analyzer::analyse (const float* input, float* outDb, float* outHz)
    {
        std::fill (a.begin(), a.end(), std::complex<float>());
        for (int i = 0; i < w; ++i)
            a[(size_t) i] = { input[i] * window[(size_t) i], 0.0f };
        fft.forward (a);

        if (outHz != nullptr)
        {
            std::fill (b.begin(), b.end(), std::complex<float>());
            for (int i = 0; i < w; ++i)
                b[(size_t) i] = { input[i] * slope[(size_t) i], 0.0f };
            fft.forward (b);
        }

        const auto bins = numBins();
        for (int k = 0; k < bins; ++k)
        {
            const auto power = std::norm (a[(size_t) k]);
            const auto mag = std::sqrt (power) * scale;
            outDb[k] = mag > 1.0e-9f ? 20.0f * std::log10 (mag) : -180.0f;

            if (outHz != nullptr)
            {
                // Reassigned frequency: the bin's frequency moved by the phase's rate of
                // change, -Im(X_dh * conj(X_h)) / |X_h|^2 radians per sample.
                double hz = (double) k * rate / n;
                if (power > 1.0e-20f)
                {
                    const auto shift = -(double) std::imag (b[(size_t) k] * std::conj (a[(size_t) k])) / power;
                    hz += shift * rate / (2.0 * pi);
                }
                outHz[k] = (float) hz;
            }
        }
    }

    //==========================================================================
    std::vector<std::vector<float>> render (const AudioData& data, const Settings& settings, double viewStart, double viewLength,
                                            int columns, int rows, double low, double high, const std::atomic<bool>* cancel)
    {
        const auto channels = data.numChannels();
        std::vector<std::vector<float>> out ((size_t) channels, std::vector<float> ((size_t) std::max (0, columns * rows), silent));
        if (channels == 0 || columns <= 0 || rows <= 0 || viewLength <= 0.0 || data.sampleRate() <= 0.0)
            return out;

        const auto nyquist = data.sampleRate() / 2.0;
        high = high <= 0.0 ? nyquist : std::min (high, nyquist);
        low = lowestFor (settings.scale, std::min (low, high * 0.5));
        const auto scale = settings.scale;

        // Each row's frequency band, top row first.
        std::vector<double> edge ((size_t) rows + 1);
        for (int y = 0; y <= rows; ++y)
            edge[(size_t) y] = frequency (scale, 1.0 - (double) y / rows, low, high);
        auto rowOf = [&] (double hz)
        {
            const auto p = position (scale, hz, low, high);
            return p < 0.0 || p > 1.0 ? -1 : std::clamp ((int) ((1.0 - p) * rows), 0, rows - 1);
        };

        const auto framesPerColumn = viewLength / columns;
        const auto windowSize = powerOfTwo (settings.windowSize, 256, 32768);
        const auto hop = std::max (1, windowSize / powerOfTwo (settings.overlap, 1, 16));
        const auto frames = std::clamp ((int) std::ceil (framesPerColumn / hop), 1, maxFramesPerColumn);
        const auto gain = (float) settings.gainDb;

        auto work = [&] (int firstColumn, int endColumn)
        {
            Analyzer analyzer (settings, data.sampleRate());
            const auto bins = analyzer.numBins();
            const auto binHz = analyzer.binHz();
            const auto reassign = settings.bins == Bins::frequencies;

            std::vector<std::vector<float>> input ((size_t) channels, std::vector<float> ((size_t) windowSize));
            std::vector<float*> ptrs;
            for (auto& c : input)
                ptrs.push_back (c.data());
            std::vector<float> db ((size_t) bins), hz ((size_t) bins);
            std::vector<std::vector<float>> best ((size_t) channels, std::vector<float> ((size_t) bins));
            std::vector<std::vector<float>> rowBest ((size_t) channels, std::vector<float> ((size_t) rows));

            for (int x = firstColumn; x < endColumn; ++x)
            {
                if (cancel != nullptr && cancel->load (std::memory_order_relaxed))
                    return;

                const auto start = viewStart + x * framesPerColumn;
                if (start >= (double) data.length())
                    break;

                for (int ch = 0; ch < channels; ++ch)
                {
                    std::fill (best[(size_t) ch].begin(), best[(size_t) ch].end(), silent);
                    std::fill (rowBest[(size_t) ch].begin(), rowBest[(size_t) ch].end(), silent);
                }

                for (int f = 0; f < frames; ++f)
                {
                    const auto centre = (Frame) (start + (f + 0.5) * framesPerColumn / frames);
                    data.read (centre - windowSize / 2, windowSize, ptrs.data());

                    for (int ch = 0; ch < channels; ++ch)
                    {
                        analyzer.analyse (input[(size_t) ch].data(), db.data(), reassign ? hz.data() : nullptr);
                        if (settings.bins == Bins::all)
                        {
                            auto& keep = best[(size_t) ch];
                            for (int k = 0; k < bins; ++k)
                                keep[(size_t) k] = std::max (keep[(size_t) k], db[(size_t) k]);
                            continue;
                        }

                        // Only the peaks, at their bin's frequency or their reassigned one.
                        auto& keep = rowBest[(size_t) ch];
                        for (int k = 1; k + 1 < bins; ++k)
                        {
                            if (db[(size_t) k] < db[(size_t) k - 1] || db[(size_t) k] < db[(size_t) k + 1])
                                continue;
                            const auto row = rowOf (reassign ? hz[(size_t) k] : k * binHz);
                            if (row >= 0)
                                keep[(size_t) row] = std::max (keep[(size_t) row], db[(size_t) k]);
                        }
                    }
                }

                for (int ch = 0; ch < channels; ++ch)
                {
                    auto* column = out[(size_t) ch].data();
                    if (settings.bins == Bins::all)
                    {
                        const auto& keep = best[(size_t) ch];
                        for (int y = 0; y < rows; ++y)
                        {
                            const auto top = edge[(size_t) y] / binHz, bottom = edge[(size_t) y + 1] / binHz;
                            const auto lo = std::clamp ((int) std::ceil (bottom), 0, bins - 1);
                            const auto hi = std::clamp ((int) std::floor (top), 0, bins - 1);
                            float level;
                            if (hi >= lo)
                                level = *std::max_element (keep.begin() + lo, keep.begin() + hi + 1);
                            else
                            {
                                // A row narrower than a bin: between the two nearest bins, in dB.
                                const auto centre = std::clamp (0.5 * (top + bottom), 0.0, (double) bins - 1.0);
                                const auto k = std::min ((int) centre, bins - 2);
                                const auto t = (float) (centre - k);
                                level = keep[(size_t) k] + (keep[(size_t) k + 1] - keep[(size_t) k]) * t;
                            }
                            column[(size_t) (y * columns + x)] = level + gain;
                        }
                    }
                    else
                    {
                        for (int y = 0; y < rows; ++y)
                            column[(size_t) (y * columns + x)] = rowBest[(size_t) ch][(size_t) y] > silent ? rowBest[(size_t) ch][(size_t) y] + gain : silent;
                    }

                    if (settings.normalise == Normalise::column)
                    {
                        float top = silent;
                        for (int y = 0; y < rows; ++y)
                            top = std::max (top, column[(size_t) (y * columns + x)]);
                        if (top > silent)
                            for (int y = 0; y < rows; ++y)
                                if (column[(size_t) (y * columns + x)] > silent)
                                    column[(size_t) (y * columns + x)] -= top - gain;
                    }
                }
            }
        };

        const auto threads = std::clamp ((int) std::thread::hardware_concurrency(), 1, 8);
        const auto per = (columns + threads - 1) / threads;
        std::vector<std::thread> pool;
        for (int t = 1; t < threads && t * per < columns; ++t)
            pool.emplace_back (work, t * per, std::min (columns, (t + 1) * per));
        work (0, std::min (columns, per));
        for (auto& t : pool)
            t.join();

        if (settings.normalise == Normalise::view)
        {
            float top = silent;
            for (const auto& c : out)
                for (auto v : c)
                    top = std::max (top, v);
            if (top > silent)
                for (auto& c : out)
                    for (auto& v : c)
                        if (v > silent)
                            v -= top - gain;
        }
        return out;
    }
}

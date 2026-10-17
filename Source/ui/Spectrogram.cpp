// SPDX-License-Identifier: AGPL-3.0-only
#include "Spectrogram.h"
#include "../core/Fft.h"

#include <algorithm>
#include <cmath>

namespace
{
    constexpr int fftOrder = 11;  // 2048 points
    constexpr float floorDb = -120.0f;
    constexpr double lowestHz = 20.0;

    QVector<QRgb> makeLookup (const QVector<QColor>& gradient)
    {
        QVector<QRgb> lut (256);
        const auto stops = std::max<qsizetype> (2, gradient.size());

        for (int i = 0; i < 256; ++i)
        {
            if (gradient.size() < 2)
            {
                lut[i] = qRgb (i, i, i);
                continue;
            }

            const auto pos = i / 255.0 * (double) (stops - 1);
            const auto index = std::min<qsizetype> ((qsizetype) pos, stops - 2);
            const auto t = pos - (double) index;
            const auto& a = gradient[index];
            const auto& b = gradient[index + 1];
            lut[i] = qRgb ((int) (a.red() + (b.red() - a.red()) * t),
                           (int) (a.green() + (b.green() - a.green()) * t),
                           (int) (a.blue() + (b.blue() - a.blue()) * t));
        }

        return lut;
    }
}

QImage renderSpectrogram (const AudioData& data, double viewStart, double viewLength, QSize size, const QVector<QColor>& gradient)
{
    QImage image (size, QImage::Format_RGB32);
    const auto lut = makeLookup (gradient);
    image.fill (lut[0]);

    const auto numChannels = data.numChannels();
    if (numChannels == 0 || size.isEmpty() || viewLength <= 0.0)
        return image;

    SpectrumAnalyzer analyzer (fftOrder);
    const auto n = analyzer.size();
    const auto bins = analyzer.numBins();
    const auto nyquist = data.sampleRate() / 2.0;
    const auto binHz = data.sampleRate() / n;
    const auto laneHeight = size.height() / numChannels;
    if (laneHeight < 1)
        return image;

    // Each pixel row covers a band of bins on a log frequency scale; the row shows the loudest bin in its band.
    std::vector<int> rowLo ((size_t) laneHeight), rowHi ((size_t) laneHeight);
    for (int y = 0; y < laneHeight; ++y)
    {
        auto freqAt = [&] (double row) { return lowestHz * std::pow (nyquist / lowestHz, 1.0 - row / laneHeight); };
        const auto hi = std::clamp ((int) std::ceil (freqAt (y) / binHz), 0, bins - 1);
        const auto lo = std::clamp ((int) std::floor (freqAt (y + 1) / binHz), 0, hi);
        rowLo[(size_t) y] = lo;
        rowHi[(size_t) y] = hi;
    }

    std::vector<std::vector<float>> input ((size_t) numChannels, std::vector<float> ((size_t) n));
    std::vector<float*> ptrs;
    for (auto& c : input)
        ptrs.push_back (c.data());

    std::vector<float> db ((size_t) bins);
    const auto framesPerPixel = viewLength / size.width();

    for (int x = 0; x < size.width(); ++x)
    {
        const auto centre = (Frame) (viewStart + (x + 0.5) * framesPerPixel);
        if (centre >= data.length())
            break;

        data.read (centre - n / 2, n, ptrs.data());

        for (int ch = 0; ch < numChannels; ++ch)
        {
            analyzer.magnitudesDb (input[(size_t) ch].data(), db.data());
            const auto top = ch * laneHeight;

            for (int y = 0; y < laneHeight; ++y)
            {
                const auto loudest = *std::max_element (db.begin() + rowLo[(size_t) y], db.begin() + rowHi[(size_t) y] + 1);
                const auto level = std::clamp ((loudest - floorDb) / -floorDb, 0.0f, 1.0f);
                image.setPixel (x, top + y, lut[(int) (level * 255.0f)]);
            }
        }
    }

    return image;
}

// SPDX-License-Identifier: AGPL-3.0-only
#include "Spectrogram.h"

#include <algorithm>
#include <cmath>

namespace
{
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

float SpectrogramPicture::levelAt (int channel, int x, int y) const
{
    if (channel < 0 || channel >= (int) levels.size() || x < 0 || x >= columns || y < 0 || y >= rows)
        return spectrum::silent;
    return levels[(size_t) channel][(size_t) (y * columns + x)];
}

SpectrogramPicture renderSpectrogram (const AudioData& data, double viewStart, double viewLength, QSize size,
                                      const QVector<QColor>& themeGradient, const spectrum::Settings& settings,
                                      double low, double high)
{
    QVector<QColor> gradient = themeGradient;
    if (const auto stops = spectrum::colourMapStops (settings.colourMap); ! stops.empty())
    {
        gradient.clear();
        for (auto rgb : stops)
            gradient << QColor::fromRgb ((QRgb) rgb);
    }
    const auto lut = makeLookup (gradient);

    SpectrogramPicture picture;
    picture.image = QImage (size, QImage::Format_RGB32);
    picture.image.fill (lut[0]);

    const auto numChannels = data.numChannels();
    if (numChannels == 0 || size.isEmpty() || viewLength <= 0.0)
        return picture;

    picture.columns = size.width();
    picture.rows = size.height() / numChannels;
    if (picture.rows < 1)
        return picture;

    picture.levels = spectrum::render (data, settings, viewStart, viewLength, picture.columns, picture.rows, low, high);

    const auto range = (float) settings.rangeDb;
    for (int ch = 0; ch < numChannels; ++ch)
    {
        const auto& lane = picture.levels[(size_t) ch];
        for (int y = 0; y < picture.rows; ++y)
        {
            auto* line = reinterpret_cast<QRgb*> (picture.image.scanLine (ch * picture.rows + y));
            for (int x = 0; x < picture.columns; ++x)
            {
                const auto level = std::clamp ((lane[(size_t) (y * picture.columns + x)] + range) / range, 0.0f, 1.0f);
                line[x] = lut[(int) (level * 255.0f)];
            }
        }
    }

    return picture;
}

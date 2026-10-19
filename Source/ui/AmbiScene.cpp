// SPDX-License-Identifier: AGPL-3.0-only
#include "AmbiScene.h"
#include "Theme.h"
#include "../core/Ambisonics.h"

#include <QPainter>

#include <algorithm>
#include <cmath>

AmbiScene::AmbiScene (QWidget* parent) : QWidget (parent)
{
    setAccessibleName (tr ("Ambisonic scene"));
    setFocusPolicy (Qt::StrongFocus);
    setMinimumSize (240, 140);
    setToolTip (tr ("Where the sound is, all around: front in the middle, behind at both edges, above at the top"));
    map.assign ((size_t) (cols * rows), 0.0f);
}

void AmbiScene::analyse (const std::vector<std::vector<float>>& channels, bool ambisonic)
{
    active = ambisonic && ! channels.empty() && ! channels[0].empty();
    if (! active)
    {
        std::fill (map.begin(), map.end(), 0.0f);
        update();
        return;
    }
    std::vector<const float*> ptrs;
    for (const auto& c : channels)
        ptrs.push_back (c.data());
    std::vector<float> now;
    ambi::energyMap (ptrs.data(), (int) ptrs.size(), (int) channels[0].size(), cols, rows, now);

    // Shown in dB over a 12 dB range under the loudest direction, and smoothed so it glows.
    const auto peak = *std::max_element (now.begin(), now.end());
    for (size_t i = 0; i < map.size(); ++i)
    {
        const auto level = peak > 1e-10f ? (float) std::clamp (1.0 + 10.0 * std::log10 (std::max (now[i], 1e-12f) / peak) / 12.0, 0.0, 1.0) : 0.0f;
        const auto loud = peak > 1e-7f ? level : 0.0f;
        map[i] = loud > map[i] ? map[i] + (loud - map[i]) * 0.6f : map[i] * 0.85f + loud * 0.15f;
    }
    update();
}

void AmbiScene::paintEvent (QPaintEvent*)
{
    const auto& theme = Theme::current();
    QPainter g (this);
    g.fillRect (rect(), Theme::withOpacity (theme.color (QStringLiteral ("waveformBackground"))));
    const QRectF area (28.0, 6.0, width() - 34.0, height() - 26.0);
    auto small = font();
    small.setPointSizeF (small.pointSizeF() * 0.8);
    g.setFont (small);
    const auto text = theme.color (QStringLiteral ("placeholderText"));

    if (! active)
    {
        g.setPen (text);
        g.drawText (rect(), Qt::AlignCenter | Qt::TextWordWrap,
                    tr ("Play an ambisonic file to see where its sound is.\n(Spatial > Channel Layout marks a file as Ambisonics.)"));
        return;
    }

    // The map: the scope colour, brighter and more opaque where the energy is.
    QImage image (cols, rows, QImage::Format_ARGB32_Premultiplied);
    const auto colour = theme.color (QStringLiteral ("scope"));
    for (int row = 0; row < rows; ++row)
        for (int col = 0; col < cols; ++col)
        {
            const auto v = map[(size_t) (row * cols + col)];
            auto c = colour.lighter (100 + (int) (60.0f * v * v));
            c.setAlphaF (std::clamp (v * v, 0.0f, 1.0f));
            image.setPixelColor (col, row, c);
        }
    g.setRenderHint (QPainter::SmoothPixmapTransform);
    g.drawImage (area, image);

    // Grid: every 45 degrees across, 30 up and down, with names.
    g.setRenderHint (QPainter::Antialiasing);
    const auto line = theme.color (QStringLiteral ("mid"));
    const auto xFor = [&] (double az) { return area.left() + (az + 180.0) / 360.0 * area.width(); };
    const auto yFor = [&] (double el) { return area.top() + (90.0 - el) / 180.0 * area.height(); };
    for (int az = -180; az <= 180; az += 45)
    {
        g.setPen (QPen (line, 1.0, az == 0 ? Qt::SolidLine : Qt::DotLine));
        g.drawLine (QPointF (xFor (az), area.top()), QPointF (xFor (az), area.bottom()));
    }
    for (int el = -60; el <= 60; el += 30)
    {
        g.setPen (QPen (line, 1.0, el == 0 ? Qt::SolidLine : Qt::DotLine));
        g.drawLine (QPointF (area.left(), yFor (el)), QPointF (area.right(), yFor (el)));
    }
    g.setPen (QPen (line, 1.0));
    g.setBrush (Qt::NoBrush);
    g.drawRect (area);

    g.setPen (text);
    for (const auto& [az, name] : { std::pair { -180, tr ("behind") }, std::pair { -90, tr ("left") }, std::pair { 0, tr ("front") },
                                    std::pair { 90, tr ("right") }, std::pair { 180, tr ("behind") } })
        g.drawText (QRectF (xFor (az) - 30, area.bottom() + 2, 60, 14), Qt::AlignCenter, name);
    for (int el : { 60, 30, 0, -30, -60 })
        g.drawText (QRectF (0, yFor (el) - 7, 24, 14), Qt::AlignRight | Qt::AlignVCenter, QStringLiteral ("%1°").arg (el));
}

// SPDX-License-Identifier: AGPL-3.0-only
#include "Goniometer.h"
#include "Theme.h"

#include <QEvent>
#include <QPainter>
#include <QPainterPath>

#include <algorithm>
#include <cmath>

namespace
{
    constexpr int maxPointsPerFrame = 4096;   // the newest ones, when more arrive at once
    constexpr double fadePerFrame = 0.80;     // how much of the trace each frame keeps
    constexpr double silence = 1.0e-4;        // about -80 dBFS
}

Goniometer::Goniometer (QWidget* parent) : QWidget (parent)
{
    setAccessibleName (tr ("Goniometer and phase correlation"));
    setFocusPolicy (Qt::NoFocus);
    setSizePolicy (QSizePolicy::Preferred, QSizePolicy::Expanding);
}

void Goniometer::reset()
{
    trace.fill (Qt::transparent);
    correlation = 0.0;
    hasSignal = false;
    update();
}

QRectF Goniometer::plotArea() const
{
    const auto side = std::max (40.0, std::min (width() - 16.0, height() - 54.0));
    return { (width() - side) / 2.0, 8.0, side, side };
}

QRectF Goniometer::correlationArea() const
{
    const auto plot = plotArea();
    return { plot.left(), plot.bottom() + 14.0, plot.width(), 8.0 };
}

void Goniometer::resizeEvent (QResizeEvent*)
{
    const auto side = (int) plotArea().width();
    const auto ratio = devicePixelRatioF();
    trace = QImage (QSize (side, side) * ratio, QImage::Format_ARGB32_Premultiplied);
    trace.setDevicePixelRatio (ratio);
    trace.fill (Qt::transparent);
}

void Goniometer::changeEvent (QEvent* e)
{
    if (e->type() == QEvent::PaletteChange || e->type() == QEvent::StyleChange)
        update();
    QWidget::changeEvent (e);
}

void Goniometer::addSamples (const std::vector<float>& left, const std::vector<float>& right)
{
    if (trace.isNull())
        return;

    const auto n = std::min (left.size(), right.size());
    const auto first = n > (size_t) maxPointsPerFrame ? n - maxPointsPerFrame : 0;

    // Correlation over everything that arrived, smoothed so the needle is readable.
    double lr = 0.0, ll = 0.0, rr = 0.0;
    for (size_t i = 0; i < n; ++i)
    {
        lr += (double) left[i] * right[i];
        ll += (double) left[i] * left[i];
        rr += (double) right[i] * right[i];
    }
    hasSignal = n > 0 && std::max (ll, rr) / (double) n > silence * silence;
    if (hasSignal)
    {
        const auto now = ll > 0.0 && rr > 0.0 ? lr / std::sqrt (ll * rr) : 0.0;
        correlation += (now - correlation) * 0.25;
    }

    QPainter g (&trace);
    g.setRenderHint (QPainter::Antialiasing);

    // Fade what is there, like phosphor.
    g.setCompositionMode (QPainter::CompositionMode_DestinationIn);
    g.fillRect (QRectF (QPointF(), trace.deviceIndependentSize()), QColor (0, 0, 0, (int) std::lround (255 * fadePerFrame)));

    // New points add up, so dense areas glow brighter.
    g.setCompositionMode (QPainter::CompositionMode_Plus);
    auto colour = Theme::current().color (QStringLiteral ("waveform"));
    colour.setAlpha (60);
    g.setPen (QPen (colour, 1.6, Qt::SolidLine, Qt::RoundCap));

    const auto size = trace.deviceIndependentSize();
    const auto centre = QPointF (size.width() / 2.0, size.height() / 2.0);
    const auto radius = size.width() / 2.0 - 2.0;
    std::vector<QPointF> points;
    points.reserve (n - first);
    for (auto i = first; i < n; ++i)
    {
        // Mid up, side across; full scale (0 dBFS) at the rim. The radius is
        // compressed (square root) so quiet passages still fill the screen.
        const auto side = ((double) right[i] - left[i]) * M_SQRT1_2;
        const auto mid = ((double) right[i] + left[i]) * M_SQRT1_2;
        const auto r = std::hypot (side, mid);
        if (r < silence)
            continue;
        const auto scale = std::min (1.0, std::sqrt (r)) / r * radius;
        points.emplace_back (centre.x() + side * scale, centre.y() - mid * scale);
    }
    g.drawPoints (points.data(), (int) points.size());
    g.end();

    update();
}

void Goniometer::paintEvent (QPaintEvent*)
{
    const auto& theme = Theme::current();
    QPainter g (this);
    g.setRenderHint (QPainter::Antialiasing);
    g.fillRect (rect(), theme.color (QStringLiteral ("window")));

    const auto plot = plotArea();
    const auto centre = plot.center();
    const auto radius = plot.width() / 2.0;

    // Screen
    g.setPen (Qt::NoPen);
    g.setBrush (theme.color (QStringLiteral ("meterBackground")));
    g.drawEllipse (centre, radius, radius);

    // Guides: half scale, the M and S axes, and the L and R diagonals.
    auto grid = theme.color (QStringLiteral ("mid"));
    g.setBrush (Qt::NoBrush);
    g.setPen (QPen (grid, 1.0));
    g.drawEllipse (centre, radius, radius);
    grid.setAlpha (150);
    g.setPen (QPen (grid, 1.0, Qt::DotLine));
    g.drawEllipse (centre, radius * std::sqrt (0.5), radius * std::sqrt (0.5));   // -6 dB with the compression
    g.drawLine (QPointF (centre.x(), plot.top()), QPointF (centre.x(), plot.bottom()));
    g.drawLine (QPointF (plot.left(), centre.y()), QPointF (plot.right(), centre.y()));
    const auto d = radius * M_SQRT1_2;
    g.drawLine (centre + QPointF (-d, -d), centre + QPointF (d, d));
    g.drawLine (centre + QPointF (d, -d), centre + QPointF (-d, d));

    // Trace
    g.drawImage (plot.topLeft(), trace);

    // Labels
    auto small = font();
    small.setPointSizeF (small.pointSizeF() * 0.8);
    g.setFont (small);
    g.setPen (theme.color (QStringLiteral ("placeholderText")));
    const auto label = [&] (QPointF at, const QString& text)
    {
        g.drawText (QRectF (at.x() - 12, at.y() - 8, 24, 16), Qt::AlignCenter, text);
    };
    label (centre + QPointF (-d - 8, -d - 8), QStringLiteral ("L"));
    label (centre + QPointF (d + 8, -d - 8), QStringLiteral ("R"));
    label (QPointF (centre.x() + 12, plot.top() + 8), QStringLiteral ("M"));
    label (QPointF (plot.left() + 10, centre.y() - 10), QStringLiteral ("S"));

    // Correlation meter
    const auto bar = correlationArea();
    g.setPen (Qt::NoPen);
    g.setBrush (theme.color (QStringLiteral ("meterBackground")));
    g.drawRoundedRect (bar, 3, 3);

    const auto value = std::clamp (correlation, -1.0, 1.0);
    const auto zero = bar.center().x();
    const auto x = zero + value * bar.width() / 2.0;
    auto fill = theme.color (QString::fromLatin1 (value < -0.05 ? "meterHigh" : value < 0.3 ? "meterMid" : "meterLow"));
    if (! hasSignal)
        fill.setAlpha (90);
    g.setBrush (fill);
    g.drawRoundedRect (QRectF (QPointF (std::min (zero, x), bar.top()), QPointF (std::max (zero, x), bar.bottom())), 3, 3);
    g.setPen (QPen (theme.color (QStringLiteral ("windowText")), 2.0));
    g.drawLine (QPointF (x, bar.top() - 3), QPointF (x, bar.bottom() + 3));
    g.setPen (QPen (grid, 1.0));
    g.drawLine (QPointF (zero, bar.top()), QPointF (zero, bar.bottom()));

    g.setPen (theme.color (QStringLiteral ("placeholderText")));
    const QRectF labels (bar.left(), bar.bottom() + 2, bar.width(), 16);
    g.drawText (labels, Qt::AlignLeft | Qt::AlignVCenter, QStringLiteral ("-1"));
    g.drawText (labels, Qt::AlignRight | Qt::AlignVCenter, QStringLiteral ("+1"));
    g.setPen (theme.color (QStringLiteral ("windowText")));
    g.drawText (labels, Qt::AlignCenter, hasSignal ? tr ("Correlation %1").arg (value, 0, 'f', 2) : tr ("Correlation"));
}

// SPDX-License-Identifier: AGPL-3.0-only
#include "Goniometer.h"
#include "InfoStrip.h"
#include "Theme.h"

#include <QEvent>
#include <QMouseEvent>
#include <QPainter>
#include <QPainterPath>
#include <QSettings>
#include <QStandardPaths>

#include <algorithm>
#include <cmath>

namespace
{
    constexpr int maxPointsPerFrame = 4096;   // the newest ones, when more arrive at once
    constexpr double fadePerFrame = 0.80;     // how much of the trace each frame keeps
    constexpr double silence = 1.0e-4;        // about -80 dBFS
    constexpr float rayFall = 0.82f;          // Polar Level: how much of a ray each frame keeps
    constexpr float skinFall = 0.006f;        // and how far its outline contracts per frame

    QSettings settings()
    {
        return QSettings (QStandardPaths::writableLocation (QStandardPaths::GenericConfigLocation)
                              + QStringLiteral ("/" APP_ID "/settings.ini"), QSettings::IniFormat);
    }

    // A sample as mid (up) and side (across), each from -1.41 to 1.41 at full scale.
    struct MidSide { double mid, side; };
    MidSide midSide (float l, float r) { return { ((double) r + l) * M_SQRT1_2, ((double) r - l) * M_SQRT1_2 }; }

    // The polar views fold a sample and its opposite together (they sound the
    // same): the angle from straight up runs from -90 (side, out of phase) to
    // +90 degrees, left negative. Returns false for silence.
    bool polar (MidSide ms, double& angle, double& radius)
    {
        radius = std::hypot (ms.mid, ms.side);
        if (radius < silence)
            return false;
        if (ms.mid < 0.0)
            ms = { -ms.mid, -ms.side };
        angle = std::atan2 (ms.side, ms.mid);   // -pi/2 .. pi/2
        return true;
    }

    // Square-root scale, so quiet passages still fill the screen: 0 dBFS at the rim.
    double compress (double r) { return std::min (1.0, std::sqrt (r)); }
}

Goniometer::Goniometer (QWidget* parent) : QWidget (parent)
{
    setAccessibleName (tr ("Stereo imaging and phase correlation"));
    setFocusPolicy (Qt::NoFocus);
    setSizePolicy (QSizePolicy::Preferred, QSizePolicy::Expanding);
    setCursor (Qt::PointingHandCursor);
    current = static_cast<View> (std::clamp (settings().value (QStringLiteral ("meters/imagingView"), 0).toInt(), 0, numViews - 1));
    setToolTip (tr ("%1: click (or Ctrl+Alt+V) for the next view").arg (viewName (current)));
}

QString Goniometer::viewName (View v)
{
    switch (v)
    {
        case View::goniometer:  return tr ("Goniometer");
        case View::polarSample: return tr ("Polar Sample");
        case View::polarLevel:  return tr ("Polar Level");
        case View::lissajous:   return tr ("Lissajous");
    }
    return {};
}

void Goniometer::nextView()
{
    current = static_cast<View> (((int) current + 1) % numViews);
    settings().setValue (QStringLiteral ("meters/imagingView"), (int) current);
    setToolTip (tr ("%1: click (or Ctrl+Alt+V) for the next view").arg (viewName (current)));
    reset();
}

void Goniometer::mousePressEvent (QMouseEvent* e)
{
    if (e->button() == Qt::LeftButton)
        nextView();
}

void Goniometer::reset()
{
    trace.fill (Qt::transparent);
    rays.fill (0.0f);
    skin.fill (0.0f);
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

    // Polar Level keeps rays, not a trace.
    if (current == View::polarLevel)
    {
        std::array<float, numRays> loudest {};
        for (auto i = first; i < n; ++i)
        {
            double angle = 0.0, radius = 0.0;
            if (! polar (midSide (left[i], right[i]), angle, radius))
                continue;
            const auto ray = std::clamp ((int) ((angle / M_PI + 0.5) * numRays), 0, numRays - 1);
            loudest[(size_t) ray] = std::max (loudest[(size_t) ray], (float) compress (radius));
        }
        for (size_t i = 0; i < rays.size(); ++i)
        {
            rays[i] = std::max (loudest[i], rays[i] * rayFall);
            skin[i] = std::max (rays[i], skin[i] - skinFall);
        }
        update();
        return;
    }

    const auto size = trace.deviceIndependentSize();
    const QRectF plot (QPointF(), size);
    QPainter g (&trace);
    g.setRenderHint (QPainter::Antialiasing);

    // Fade what is there, like phosphor.
    g.setCompositionMode (QPainter::CompositionMode_DestinationIn);
    g.fillRect (plot, QColor (0, 0, 0, (int) std::lround (255 * fadePerFrame)));

    // New points add up, so dense areas glow brighter.
    g.setCompositionMode (QPainter::CompositionMode_Plus);
    auto colour = Theme::current().color (QStringLiteral ("waveform"));
    colour.setAlpha (current == View::lissajous ? 45 : 60);
    g.setPen (QPen (colour, current == View::lissajous ? 1.2 : 1.6, Qt::SolidLine, Qt::RoundCap, Qt::RoundJoin));

    const auto centre = plot.center();
    const auto radius = plot.width() / 2.0 - 2.0;
    std::vector<QPointF> points;
    points.reserve (n - first);

    for (auto i = first; i < n; ++i)
    {
        const auto ms = midSide (left[i], right[i]);
        switch (current)
        {
            case View::goniometer:
            {
                const auto r = std::hypot (ms.side, ms.mid);
                if (r < silence)
                    break;
                const auto scale = compress (r) / r * radius;
                points.emplace_back (centre.x() + ms.side * scale, centre.y() - ms.mid * scale);
                break;
            }
            case View::polarSample:
            {
                double angle = 0.0, r = 0.0;
                if (! polar (ms, angle, r))
                    break;
                const auto origin = polarOrigin (plot);
                const auto length = compress (r) * polarRadius (plot);
                points.emplace_back (origin.x() + std::sin (angle) * length, origin.y() - std::cos (angle) * length);
                break;
            }
            case View::lissajous:
            {
                // Linear, with the diamond's corners (both channels at full scale) at the rim.
                const auto scale = radius / M_SQRT2;
                points.emplace_back (centre.x() + ms.side * scale, centre.y() - ms.mid * scale);
                break;
            }
            case View::polarLevel:
                break;
        }
    }

    if (current == View::lissajous)
        g.drawPolyline (points.data(), (int) points.size());
    else
        g.drawPoints (points.data(), (int) points.size());
    g.end();

    update();
}

//==============================================================================
void Goniometer::drawGuides (QPainter& g, const QRectF& plot) const
{
    const auto& theme = Theme::current();
    const auto centre = plot.center();
    const auto radius = plot.width() / 2.0;
    auto line = theme.color (QStringLiteral ("mid"));
    auto dotted = line;
    dotted.setAlpha (150);

    auto small = font();
    small.setPointSizeF (small.pointSizeF() * 0.8);
    g.setFont (small);
    const auto label = [&] (QPointF at, const QString& text)
    {
        g.setPen (theme.color (QStringLiteral ("placeholderText")));
        g.drawText (QRectF (at.x() - 14, at.y() - 8, 28, 16), Qt::AlignCenter, text);
    };

    g.setBrush (Qt::NoBrush);
    const auto d = radius * M_SQRT1_2;

    switch (current)
    {
        case View::goniometer:
            g.setPen (QPen (line, 1.0));
            g.drawEllipse (centre, radius, radius);
            g.setPen (QPen (dotted, 1.0, Qt::DotLine));
            g.drawEllipse (centre, radius * M_SQRT1_2, radius * M_SQRT1_2);   // -6 dB with the compression
            g.drawLine (QPointF (centre.x(), plot.top()), QPointF (centre.x(), plot.bottom()));
            g.drawLine (QPointF (plot.left(), centre.y()), QPointF (plot.right(), centre.y()));
            g.drawLine (centre + QPointF (-d, -d), centre + QPointF (d, d));
            g.drawLine (centre + QPointF (d, -d), centre + QPointF (-d, d));
            label (centre + QPointF (-d - 8, -d - 8), QStringLiteral ("L"));
            label (centre + QPointF (d + 8, -d - 8), QStringLiteral ("R"));
            label (QPointF (centre.x() + 12, plot.top() + 8), QStringLiteral ("M"));
            label (QPointF (plot.left() + 10, centre.y() - 10), QStringLiteral ("S"));
            break;

        case View::polarSample:
        case View::polarLevel:
        {
            const auto origin = polarOrigin (plot);
            const auto r = polarRadius (plot);
            QPainterPath arc;
            arc.moveTo (origin + QPointF (-r, 0));
            arc.arcTo (QRectF (origin.x() - r, origin.y() - r, 2 * r, 2 * r), 180, -180);
            arc.closeSubpath();
            g.setPen (QPen (line, 1.0));
            g.drawPath (arc);
            g.setPen (QPen (dotted, 1.0, Qt::DotLine));
            g.drawArc (QRectF (origin.x() - r * M_SQRT1_2, origin.y() - r * M_SQRT1_2, 2 * r * M_SQRT1_2, 2 * r * M_SQRT1_2), 0, 180 * 16);
            g.drawLine (origin, origin + QPointF (0, -r));
            // The 45 degree lines: hard left and right. Inside them is mono-safe; beyond is out of phase.
            g.setPen (QPen (line, 1.0, Qt::DashLine));
            g.drawLine (origin, origin + QPointF (-r * M_SQRT1_2, -r * M_SQRT1_2));
            g.drawLine (origin, origin + QPointF (r * M_SQRT1_2, -r * M_SQRT1_2));
            label (origin + QPointF (-r * M_SQRT1_2 - 8, -r * M_SQRT1_2 - 8), QStringLiteral ("L"));
            label (origin + QPointF (r * M_SQRT1_2 + 8, -r * M_SQRT1_2 - 8), QStringLiteral ("R"));
            label (origin + QPointF (12, -r + 8), QStringLiteral ("M"));
            label (origin + QPointF (-r + 14, 10), QStringLiteral ("-S"));
            label (origin + QPointF (r - 14, 10), QStringLiteral ("+S"));
            break;
        }

        case View::lissajous:
        {
            const QPointF top (centre.x(), centre.y() - radius), bottom (centre.x(), centre.y() + radius);
            const QPointF leftCorner (centre.x() - radius, centre.y()), rightCorner (centre.x() + radius, centre.y());
            g.setPen (QPen (line, 1.0));
            const QPointF diamond[] { top, rightCorner, bottom, leftCorner, top };
            g.drawPolyline (diamond, 5);
            g.setPen (QPen (dotted, 1.0, Qt::DotLine));
            g.drawLine (top, bottom);
            g.drawLine (leftCorner, rightCorner);
            const QPointF inner[] { (centre + top) / 2, (centre + rightCorner) / 2, (centre + bottom) / 2, (centre + leftCorner) / 2, (centre + top) / 2 };
            g.drawPolyline (inner, 5);   // half scale
            label ((top + leftCorner) / 2 + QPointF (-8, -8), QStringLiteral ("L"));
            label ((top + rightCorner) / 2 + QPointF (8, -8), QStringLiteral ("R"));
            label (top + QPointF (12, 8), QStringLiteral ("M"));
            label (leftCorner + QPointF (10, -10), QStringLiteral ("S"));
            break;
        }
    }
}

void Goniometer::drawRays (QPainter& g, const QRectF& plot) const
{
    const auto& theme = Theme::current();
    const auto origin = polarOrigin (plot);
    const auto r = polarRadius (plot);
    const auto pointAt = [&] (double angle, double length)
    {
        return QPointF (origin.x() + std::sin (angle) * length * r, origin.y() - std::cos (angle) * length * r);
    };
    const auto angleOf = [] (int ray, double part) { return ((ray + part) / numRays - 0.5) * M_PI; };

    // The rays: a wedge per direction, brighter towards the tip.
    auto colour = theme.color (QStringLiteral ("waveform"));
    QRadialGradient glow (origin, r);
    auto inner = colour;
    inner.setAlpha (60);
    colour.setAlpha (220);
    glow.setColorAt (0.0, inner);
    glow.setColorAt (1.0, colour);
    QPainterPath wedges;
    for (int i = 0; i < numRays; ++i)
    {
        if (rays[(size_t) i] <= 0.005f)
            continue;
        wedges.moveTo (origin);
        wedges.lineTo (pointAt (angleOf (i, 0.0), rays[(size_t) i]));
        wedges.lineTo (pointAt (angleOf (i, 1.0), rays[(size_t) i]));
        wedges.closeSubpath();
    }
    g.setPen (Qt::NoPen);
    g.setBrush (glow);
    g.drawPath (wedges);

    // The skin: the recent loudest in every direction, contracting slowly.
    QPolygonF outline;
    for (int i = 0; i < numRays; ++i)
        outline << pointAt (angleOf (i, 0.5), skin[(size_t) i]);
    g.setBrush (Qt::NoBrush);
    g.setPen (QPen (theme.color (QStringLiteral ("windowText")), 1.2));
    g.drawPolyline (outline);
}

void Goniometer::paintEvent (QPaintEvent*)
{
    const auto& theme = Theme::current();
    QPainter g (this);
    g.setRenderHint (QPainter::Antialiasing);

    const auto plot = plotArea();
    drawGuides (g, plot);
    if (current == View::polarLevel)
        drawRays (g, plot);
    else
        g.drawImage (plot.topLeft(), trace);

    // The view's name, top left
    auto small = font();
    small.setPointSizeF (small.pointSizeF() * 0.8);
    g.setFont (small);
    g.setPen (theme.color (QStringLiteral ("placeholderText")));
    g.drawText (QRectF (4, 2, width() - 8, 16), Qt::AlignLeft | Qt::AlignVCenter, viewName (current));

    // Correlation meter
    const auto bar = correlationArea();
    // No fill behind the bar, so the window's opacity shows through; a faint outline keeps its size readable.
    auto outline = theme.color (QStringLiteral ("mid"));
    outline.setAlphaF (0.6f);
    g.setPen (QPen (outline, 1.0));
    g.setBrush (Qt::NoBrush);
    g.drawRoundedRect (bar.adjusted (0.5, 0.5, -0.5, -0.5), 3, 3);
    g.setPen (Qt::NoPen);

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
    g.setPen (QPen (theme.color (QStringLiteral ("mid")), 1.0));
    g.drawLine (QPointF (zero, bar.top()), QPointF (zero, bar.bottom()));

    g.setPen (theme.color (QStringLiteral ("placeholderText")));
    const QRectF labels (bar.left(), bar.bottom() + 2, bar.width(), 16);
    g.drawText (labels, Qt::AlignLeft | Qt::AlignVCenter, QStringLiteral ("-1"));
    g.drawText (labels, Qt::AlignRight | Qt::AlignVCenter, QStringLiteral ("+1"));
    g.setPen (theme.color (QStringLiteral ("windowText")));
    // The number sits right-aligned in a slot as wide as "-0.00", in even-width digits, so it never moves sideways.
    const QFontMetricsF fm (g.font());
    const auto name = tr ("Correlation");
    const auto slot = steady::width (fm, QStringLiteral ("-0.00"));
    const auto gap = fm.horizontalAdvance (QLatin1Char (' '));
    const auto left = labels.center().x() - (fm.horizontalAdvance (name) + gap + slot) / 2.0;
    const auto baseline = labels.center().y() + (fm.ascent() - fm.descent()) / 2.0;
    g.drawText (QPointF (left, baseline), name);
    if (hasSignal)
    {
        const auto number = QString::number (std::abs (value) < 0.005 ? 0.0 : value, 'f', 2);
        steady::draw (g, left + fm.horizontalAdvance (name) + gap + slot - steady::width (fm, number), baseline, number);
    }
}

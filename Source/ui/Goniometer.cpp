// SPDX-License-Identifier: AGPL-3.0-only
#include "Goniometer.h"
#include "InfoStrip.h"
#include "Theme.h"
#include "../core/Fft.h"

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
        case View::binaural:    return tr ("Binaural");
        case View::midSide:     return tr ("Mid/Side Levels");
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
    bands.fill ({});
    midPower = sidePower = midPeak = sidePeak = 0.0;
    midHold = sideHold = 0;
    sideHistory.clear();
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

    if (current == View::binaural)
    {
        analyseBinaural (left, right);
        update();
        return;
    }

    if (current == View::midSide)
    {
        double mm = 0.0, ss = 0.0, mp = 0.0, sp = 0.0;
        for (size_t i = 0; i < n; ++i)
        {
            const auto m = 0.5 * ((double) left[i] + right[i]), s = 0.5 * ((double) left[i] - right[i]);
            mm += m * m;
            ss += s * s;
            mp = std::max (mp, std::abs (m));
            sp = std::max (sp, std::abs (s));
        }
        if (n > 0)
        {
            // About 300 ms of smoothing at the meters' 30 frames a second.
            const auto k = std::min (1.0, (double) n / (0.3 * sampleRate));
            midPower += (mm / (double) n - midPower) * k;
            sidePower += (ss / (double) n - sidePower) * k;
        }
        // Peaks hold for a second, then fall about 20 dB a second.
        auto peak = [] (double now, double& held, int& hold)
        {
            if (now >= held)
            {
                held = now;
                hold = 30;
            }
            else if (hold > 0)
                --hold;
            else
                held *= 0.93;
        };
        peak (mp, midPeak, midHold);
        peak (sp, sidePeak, sideHold);
        if (hasSignal)
        {
            const auto ratio = 10.0 * std::log10 (std::max (sidePower, 1.0e-12) / std::max (midPower, 1.0e-12));
            sideHistory.push_back ((float) std::clamp (ratio, -40.0, 20.0));
            if (sideHistory.size() > 300)
                sideHistory.erase (sideHistory.begin(), sideHistory.begin() + (std::ptrdiff_t) (sideHistory.size() - 300));
        }
        update();
        return;
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
    auto colour = Theme::current().color (QStringLiteral ("scope"));
    colour.setAlpha (current == View::lissajous ? 45 : 60);
    g.setPen (QPen (colour, current == View::lissajous ? 1.2 : 1.6, Qt::SolidLine, Qt::RoundCap, Qt::RoundJoin));

    const auto centre = plot.center();
    const auto radius = plot.width() / 2.0 - 2.0;
    std::vector<QPointF> points;
    points.reserve (n - first);

    // Lissajous zooms in on quiet or narrow material by itself (up to 8x), so the shape fills
    // the plot; the peak it follows falls back slowly, so the zoom does not pump.
    if (current == View::lissajous)
    {
        double peak = 0.0;
        for (auto i = first; i < n; ++i)
        {
            const auto ms = midSide (left[i], right[i]);
            peak = std::max ({ peak, std::abs (ms.mid), std::abs (ms.side) });
        }
        lissajousPeak = std::max (peak, lissajousPeak * 0.985);
        const auto target = lissajousPeak > silence ? std::clamp (0.9 * M_SQRT2 / lissajousPeak, 1.0, 8.0) : lissajousZoom;
        lissajousZoom += (target - lissajousZoom) * 0.15;
    }

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
                const auto scale = radius / M_SQRT2 * lissajousZoom;
                points.emplace_back (centre.x() + ms.side * scale, centre.y() - ms.mid * scale);
                break;
            }
            case View::polarLevel:
            case View::binaural:
            case View::midSide:
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
    small.setPointSizeF (small.pointSizeF() * 0.85);
    small.setBold (true);
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

        case View::binaural:
        case View::midSide:
            break;   // drawn with their levels
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
    auto colour = theme.color (QStringLiteral ("scope"));
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
    else if (current == View::binaural)
        drawBinaural (g, plot);
    else if (current == View::midSide)
        drawMidSide (g, plot);
    else
        g.drawImage (plot.topLeft(), trace);

    // The view's name, top left
    auto small = font();
    small.setPointSizeF (small.pointSizeF() * 0.8);
    g.setFont (small);
    g.setPen (theme.color (QStringLiteral ("placeholderText")));
    g.drawText (QRectF (4, 2, width() - 8, 16), Qt::AlignLeft | Qt::AlignVCenter, viewName (current));
    if (current == View::lissajous && lissajousZoom > 1.05)
        g.drawText (QRectF (4, 2, width() - 8, 16), Qt::AlignRight | Qt::AlignVCenter, tr ("zoom %1x").arg (lissajousZoom, 0, 'f', 1));

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

//==============================================================================
namespace
{
    constexpr double lowestBand = 150.0, highestBand = 12000.0;
    constexpr int binauralOrder = 11;   // 2048 samples

    // The lateral angle (degrees, right positive) for a time difference between the ears
    // (seconds, positive when the right ear hears it first), by Woodworth's spherical head.
    double angleForDelay (double seconds)
    {
        constexpr double headRadius = 0.0875, speedOfSound = 343.0;
        const auto target = std::abs (seconds) * speedOfSound / headRadius;
        double lo = 0.0, hi = M_PI / 2.0;
        if (target >= hi + 1.0)
            return std::copysign (90.0, seconds);
        for (int i = 0; i < 30; ++i)
        {
            const auto mid = (lo + hi) / 2.0;
            (mid + std::sin (mid) < target ? lo : hi) = mid;
        }
        return std::copysign (lo * 180.0 / M_PI, seconds);
    }
}

void Goniometer::analyseBinaural (const std::vector<float>& left, const std::vector<float>& right)
{
    const auto size = (size_t) 1 << binauralOrder;
    recentLeft.insert (recentLeft.end(), left.begin(), left.end());
    recentRight.insert (recentRight.end(), right.begin(), right.end());
    if (recentLeft.size() > size)
    {
        recentLeft.erase (recentLeft.begin(), recentLeft.end() - (std::ptrdiff_t) size);
        recentRight.erase (recentRight.begin(), recentRight.end() - (std::ptrdiff_t) size);
    }

    // Nothing new, or silence: the dots fade.
    if (left.empty() || ! hasSignal || recentLeft.size() < size)
    {
        for (auto& b : bands)
            b.level *= 0.9;
        return;
    }

    static const Fft fft (binauralOrder);
    std::vector<std::complex<float>> l (size), r (size);
    for (size_t i = 0; i < size; ++i)
    {
        const auto w = (float) (0.5 - 0.5 * std::cos (2.0 * M_PI * (double) i / (double) size));
        l[i] = recentLeft[i] * w;
        r[i] = recentRight[i] * w;
    }
    fft.forward (l);
    fft.forward (r);

    const auto binHz = sampleRate / (double) size;
    for (int b = 0; b < numBands; ++b)
    {
        const auto from = lowestBand * std::pow (highestBand / lowestBand, (double) b / numBands);
        const auto to = lowestBand * std::pow (highestBand / lowestBand, (double) (b + 1) / numBands);
        const auto centre = std::sqrt (from * to);
        std::complex<double> cross;
        double pl = 0.0, pr = 0.0;
        for (auto k = std::max<size_t> (1, (size_t) (from / binHz)); k <= (size_t) (to / binHz) && k < size / 2; ++k)
        {
            cross += std::complex<double> (l[k]) * std::conj (std::complex<double> (r[k]));
            pl += std::norm (l[k]);
            pr += std::norm (r[k]);
        }
        auto& band = bands[(size_t) b];
        const auto power = (pl + pr) / (double) size;
        // Level on a 60 dB scale from about -66 dBFS per band.
        const auto level = power > 1e-12 ? std::clamp ((10.0 * std::log10 (power) + 66.0) / 60.0, 0.0, 1.0) : 0.0;
        band.level = level > band.level ? level : band.level * 0.9 + level * 0.1;
        if (pl <= 1e-12 || pr <= 1e-12)
            continue;

        // Below about 1.4 kHz the ears go by time, above it by level.
        double angle = 0.0;
        if (centre < 1400.0)
            angle = angleForDelay (-std::arg (cross) / (2.0 * M_PI * centre));
        else
        {
            const auto ildDb = 10.0 * std::log10 (pr / pl);
            const auto most = 6.0 + 14.0 * std::clamp ((centre - 1400.0) / 6600.0, 0.0, 1.0);
            angle = std::asin (std::clamp (ildDb / most, -1.0, 1.0)) * 180.0 / M_PI;
        }
        const auto coherence = std::abs (cross) / std::sqrt (pl * pr);
        band.angle += (angle - band.angle) * 0.3;
        band.coherence += (coherence - band.coherence) * 0.3;
    }
}

void Goniometer::drawBinaural (QPainter& g, const QRectF& plot) const
{
    const auto& theme = Theme::current();
    const auto centre = polarOrigin (plot);
    const auto radius = polarRadius (plot) - 2.0;
    const auto line = theme.color (QStringLiteral ("mid"));
    const auto text = theme.color (QStringLiteral ("placeholderText"));
    const auto radiusFor = [&] (double hz)
    {
        return radius * (0.24 + 0.76 * std::log (hz / lowestBand) / std::log (highestBand / lowestBand));
    };

    // The front half: rings at 200 Hz, 1 kHz and 5 kHz, the 45 degree lines and the head.
    auto small = font();
    small.setPointSizeF (small.pointSizeF() * 0.75);
    g.setFont (small);
    g.setBrush (Qt::NoBrush);
    for (const auto& [hz, name] : { std::pair { 200.0, QStringLiteral ("200") }, std::pair { 1000.0, QStringLiteral ("1k") },
                                    std::pair { 5000.0, QStringLiteral ("5k") } })
    {
        const auto r = radiusFor (hz);
        g.setPen (QPen (line, 1.0, Qt::DotLine));
        g.drawArc (QRectF (centre.x() - r, centre.y() - r, 2 * r, 2 * r), 0, 180 * 16);
        g.setPen (text);
        g.drawText (QPointF (centre.x() + 3, centre.y() - r + 10), name);
    }
    g.setPen (QPen (line, 1.0));
    g.drawArc (QRectF (centre.x() - radius, centre.y() - radius, 2 * radius, 2 * radius), 0, 180 * 16);
    g.drawLine (QPointF (centre.x() - radius, centre.y()), QPointF (centre.x() + radius, centre.y()));
    g.setPen (QPen (line, 1.0, Qt::DashLine));
    g.drawLine (centre, centre + QPointF (-radius * M_SQRT1_2, -radius * M_SQRT1_2));
    g.drawLine (centre, centre + QPointF (radius * M_SQRT1_2, -radius * M_SQRT1_2));
    const auto head = radius * 0.13;
    g.setPen (QPen (theme.color (QStringLiteral ("windowText")), 1.4));
    g.drawEllipse (centre, head, head);
    g.drawLine (QPointF (centre.x() - head * 0.35, centre.y() - head * 0.92), QPointF (centre.x(), centre.y() - head * 1.45));
    g.drawLine (QPointF (centre.x() + head * 0.35, centre.y() - head * 0.92), QPointF (centre.x(), centre.y() - head * 1.45));
    auto bold = small;
    bold.setBold (true);
    bold.setPointSizeF (small.pointSizeF() * 1.15);
    g.setFont (bold);
    g.setPen (text);
    g.drawText (QRectF (centre.x() - radius, centre.y() + 2, 20, 14), Qt::AlignLeft, QStringLiteral ("L"));
    g.drawText (QRectF (centre.x() + radius - 20, centre.y() + 2, 20, 14), Qt::AlignRight, QStringLiteral ("R"));
    g.drawText (QRectF (centre.x() - 30, centre.y() - radius - 14, 60, 14), Qt::AlignCenter, tr ("front"));

    // Each band: a dot where it seems to come from, as big as it is loud; diffuse sound spreads faint.
    auto colour = theme.color (QStringLiteral ("scope"));
    for (int b = 0; b < numBands; ++b)
    {
        const auto& band = bands[(size_t) b];
        if (band.level < 0.02)
            continue;
        const auto hz = lowestBand * std::pow (highestBand / lowestBand, (b + 0.5) / numBands);
        const auto r = radiusFor (hz);
        const auto a = band.angle * M_PI / 180.0;
        const auto p = QPointF (centre.x() + r * std::sin (a), centre.y() - r * std::cos (a));
        const auto coherence = std::clamp (band.coherence, 0.0, 1.0);
        const auto spread = (1.0 - coherence) * 70.0;
        if (spread > 4.0)
        {
            auto haze = colour;
            haze.setAlphaF (0.25 * band.level);
            g.setPen (QPen (haze, 2.0 + 6.0 * band.level, Qt::SolidLine, Qt::RoundCap));
            g.setBrush (Qt::NoBrush);
            g.drawArc (QRectF (centre.x() - r, centre.y() - r, 2 * r, 2 * r),
                       (int) ((90.0 - band.angle - spread / 2.0) * 16.0), (int) (spread * 16.0));
        }
        auto dot = colour;
        dot.setAlphaF (0.35 + 0.65 * coherence);
        g.setPen (Qt::NoPen);
        g.setBrush (dot);
        const auto size = 1.5 + 3.5 * band.level;
        g.drawEllipse (p, size, size);
    }
}

//==============================================================================
void Goniometer::drawMidSide (QPainter& g, const QRectF& plot) const
{
    const auto& theme = Theme::current();
    constexpr double floorDb = -60.0;
    const auto toDb = [] (double amplitude) { return amplitude > 1.0e-6 ? 20.0 * std::log10 (amplitude) : -120.0; };
    auto small = font();
    small.setPointSizeF (small.pointSizeF() * 0.8);
    g.setFont (small);
    const QFontMetricsF fm (small);
    const auto line = theme.color (QStringLiteral ("mid"));
    const auto text = theme.color (QStringLiteral ("placeholderText"));

    // The two meters, with a dB scale between them.
    const QRectF meters (plot.left() + 4, plot.top() + 20, plot.width() - 8, plot.height() * 0.5);
    const auto barWidth = std::min (40.0, meters.width() * 0.25);
    const QRectF midBar (meters.center().x() - barWidth * 1.6, meters.top(), barWidth, meters.height());
    const QRectF sideBar (meters.center().x() + barWidth * 0.6, meters.top(), barWidth, meters.height());
    const auto yFor = [&] (double db) { return meters.bottom() - std::clamp ((db - floorDb) / -floorDb, 0.0, 1.0) * meters.height(); };
    for (double db = 0.0; db >= floorDb; db -= 12.0)
    {
        const auto y = yFor (db);
        g.setPen (QPen (line, 1.0, Qt::DotLine));
        g.drawLine (QPointF (midBar.right() + 2, y), QPointF (sideBar.left() - 2, y));
        g.setPen (text);
        g.drawText (QRectF (midBar.right(), y - 7, sideBar.left() - midBar.right(), 14), Qt::AlignCenter, QString::number ((int) db));
    }
    const auto drawBar = [&] (const QRectF& bar, double power, double peak, const QString& name)
    {
        auto outline = line;
        outline.setAlphaF (0.6f);
        g.setPen (QPen (outline, 1.0));
        g.setBrush (Qt::NoBrush);
        g.drawRect (bar);
        const auto rmsDb = 10.0 * std::log10 (std::max (power, 1.0e-12));
        auto fill = theme.color (QString::fromLatin1 (rmsDb > -6.0 ? "meterHigh" : rmsDb > -18.0 ? "meterMid" : "meterLow"));
        if (! hasSignal)
            fill.setAlpha (90);
        g.setPen (Qt::NoPen);
        g.setBrush (fill);
        g.drawRect (QRectF (QPointF (bar.left() + 1, yFor (rmsDb)), QPointF (bar.right() - 1, bar.bottom())));
        g.setPen (QPen (theme.color (QStringLiteral ("meterPeakHold")), 2.0));
        const auto y = yFor (toDb (peak));
        if (peak > 1.0e-6)
            g.drawLine (QPointF (bar.left(), y), QPointF (bar.right(), y));
        g.setPen (theme.color (QStringLiteral ("windowText")));
        g.drawText (QRectF (bar.left() - 20, bar.top() - 18, bar.width() + 40, 16), Qt::AlignCenter, name);
        g.drawText (QRectF (bar.left() - 20, bar.bottom() + 2, bar.width() + 40, 16), Qt::AlignCenter,
                    hasSignal && rmsDb > -100.0 ? QString::number (rmsDb, 'f', 1) : QStringLiteral ("-"));
    };
    drawBar (midBar, midPower, midPeak, tr ("Mid"));
    drawBar (sideBar, sidePower, sidePeak, tr ("Side"));

    // The side against the mid: now, and over the last 10 seconds.
    const auto ratio = 10.0 * std::log10 (std::max (sidePower, 1.0e-12) / std::max (midPower, 1.0e-12));
    const QRectF history (plot.left() + 4, meters.bottom() + 38, plot.width() - 8, plot.bottom() - meters.bottom() - 38);
    g.setPen (theme.color (QStringLiteral ("windowText")));
    g.drawText (QRectF (plot.left(), meters.bottom() + 20, plot.width(), 16), Qt::AlignCenter,
                hasSignal ? tr ("S/M %1 dB").arg (ratio > -40.0 ? QString::number (ratio, 'f', 1) : QStringLiteral ("< -40")) : tr ("S/M"));
    if (history.height() < 16)
        return;
    constexpr double top = 6.0, bottom = -30.0;
    const auto yRatio = [&] (double db) { return history.top() + (top - std::clamp (db, bottom, top)) / (top - bottom) * history.height(); };
    auto outline = line;
    outline.setAlphaF (0.6f);
    g.setPen (QPen (outline, 1.0));
    g.setBrush (Qt::NoBrush);
    g.drawRect (history);
    for (const double db : { 0.0, -12.0, -24.0 })
    {
        g.setPen (QPen (db > -1.0 ? theme.color (QStringLiteral ("meterHigh")) : line, 1.0, Qt::DotLine));
        g.drawLine (QPointF (history.left(), yRatio (db)), QPointF (history.right(), yRatio (db)));
        g.setPen (text);
        g.drawText (QRectF (history.left() + 2, yRatio (db) - 13, 40, 12), Qt::AlignLeft | Qt::AlignBottom, QString::number ((int) db));
    }
    if (sideHistory.size() > 1)
    {
        QPolygonF curve;
        const auto step = history.width() / 299.0;
        const auto start = history.right() - step * (double) (sideHistory.size() - 1);
        for (size_t i = 0; i < sideHistory.size(); ++i)
            curve << QPointF (start + step * (double) i, yRatio (sideHistory[i]));
        g.setPen (QPen (theme.color (QStringLiteral ("scope")), 1.5));
        g.drawPolyline (curve);
    }
}

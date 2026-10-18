// SPDX-License-Identifier: AGPL-3.0-only
#include "TimeRuler.h"
#include "InfoStrip.h"
#include "Theme.h"

#include <QMouseEvent>
#include <QPainter>
#include <QPainterPath>

#include <algorithm>
#include <cmath>

namespace
{
    // Steps between numbered ticks, in seconds, and how many parts each is cut into.
    struct Step { double seconds; int parts; };
    constexpr Step steps[] {
        { 1e-6, 10 }, { 2e-6, 4 }, { 5e-6, 5 }, { 1e-5, 10 }, { 2e-5, 4 }, { 5e-5, 5 },
        { 1e-4, 10 }, { 2e-4, 4 }, { 5e-4, 5 }, { 1e-3, 10 }, { 2e-3, 4 }, { 5e-3, 5 },
        { 0.01, 10 }, { 0.02, 4 }, { 0.05, 5 }, { 0.1, 10 }, { 0.2, 4 }, { 0.5, 5 },
        { 1, 10 }, { 2, 4 }, { 5, 5 }, { 10, 10 }, { 15, 3 }, { 30, 3 },
        { 60, 6 }, { 120, 4 }, { 300, 5 }, { 600, 10 }, { 900, 3 }, { 1800, 3 },
        { 3600, 4 }, { 7200, 4 }, { 18000, 5 }, { 36000, 10 },
    };

    int decimalsFor (double step) { return std::max (0, (int) std::ceil (-std::log10 (step) - 1e-9)); }
}

TimeRuler::TimeRuler (QWidget* parent) : QWidget (parent)
{
    setAccessibleName (tr ("Time ruler"));
    setSizePolicy (QSizePolicy::Ignored, QSizePolicy::Fixed);
    setCursor (Qt::PointingHandCursor);
}

QSize TimeRuler::sizeHint() const
{
    return { 400, (int) std::ceil (QFontMetricsF (font()).height()) + 12 };
}

void TimeRuler::setView (double s, double l, double r, Frame fileLen, Frame c, Frame a, Frame b)
{
    // Exact comparison on purpose: the values come straight from the editor.
    if (std::abs (s - start) <= 0.0 && std::abs (l - length) <= 0.0 && std::abs (r - rate) <= 0.0 && fileLen == fileLength && c == cursor && a == selStart && b == selEnd)
        return;
    start = s; length = l; rate = r > 0.0 ? r : 48000.0;
    fileLength = fileLen; cursor = c; selStart = a; selEnd = b;
    update();
}

double TimeRuler::frameToX (double frame) const
{
    return length > 0.0 ? (frame - start) / length * width() : 0.0;
}

QString TimeRuler::label (double seconds, int decimals)
{
    const auto scale = std::pow (10.0, decimals);
    const auto ticks = std::llround (seconds * scale);                 // in units of the last decimal
    const auto unitsPerSecond = (long long) std::llround (scale);
    const auto whole = ticks / unitsPerSecond, fraction = ticks % unitsPerSecond;
    const auto h = whole / 3600, m = (whole / 60) % 60, s = whole % 60;
    if (ticks == 0)
        return QStringLiteral ("0");

    QStringList parts;
    if (h > 0)
        parts << tr ("%1 h").arg (h);
    if (m > 0)
        parts << tr ("%1 min").arg (m);
    if (s > 0 || fraction > 0 || parts.isEmpty())
    {
        auto sec = QString::number (s);
        if (decimals > 0)
            sec += QLatin1Char ('.') + QStringLiteral ("%1").arg (fraction, decimals, 10, QLatin1Char ('0'));
        parts << tr ("%1 s").arg (sec);
    }
    return parts.join (QLatin1Char (' '));
}

void TimeRuler::paintEvent (QPaintEvent*)
{
    QPainter g (this);
    const auto& theme = Theme::current();
    const auto ink = theme.color (QStringLiteral ("windowText"));
    auto faint = ink;
    faint.setAlphaF (0.45f);

    g.setPen (faint);
    g.drawLine (QPointF (0, height() - 0.5), QPointF (width(), height() - 0.5));
    if (length <= 0.0 || width() <= 0)
        return;

    const auto t0 = start / rate, t1 = (start + length) / rate;
    const auto pixelsPerSecond = width() / (t1 - t0);
    const QFontMetricsF fm (font());

    // The smallest step whose numbers fit between ticks.
    auto step = steps[std::size (steps) - 1];
    for (const auto& candidate : steps)
    {
        const auto widest = steady::width (fm, label (std::max (t1, 59.0 * 60.0 + 59.0), decimalsFor (candidate.seconds)));
        if (candidate.seconds * pixelsPerSecond >= widest + 18.0)
        {
            step = candidate;
            break;
        }
    }
    const auto decimals = decimalsFor (step.seconds);
    const auto minor = step.seconds / step.parts;
    const auto bottom = (double) height();

    // Shade the selection, so it reads across both the ruler and the waveform.
    if (selEnd > selStart)
    {
        auto shade = theme.color (QStringLiteral ("selection"));
        g.fillRect (QRectF (QPointF (frameToX ((double) selStart), 0), QPointF (frameToX ((double) selEnd), bottom)), shade);
    }

    g.setPen (faint);
    const auto lastSecond = std::min (t1, (double) fileLength / rate);
    for (auto j = (long long) std::floor (t0 / minor); ; ++j)
    {
        const auto t = (double) j * minor;
        if (t > lastSecond + 1e-12)
            break;
        if (t < 0.0 || j % step.parts == 0)
            continue;
        const auto x = std::round ((t - t0) * pixelsPerSecond) + 0.5;
        g.drawLine (QPointF (x, bottom - 4), QPointF (x, bottom));
    }

    const auto baseline = 2.0 + fm.ascent();
    for (auto i = (long long) std::floor (t0 / step.seconds); ; ++i)
    {
        const auto t = (double) i * step.seconds;
        if (t > lastSecond + 1e-12)
            break;
        if (t < 0.0)
            continue;
        const auto x = std::round ((t - t0) * pixelsPerSecond) + 0.5;
        g.setPen (faint);
        g.drawLine (QPointF (x, 0), QPointF (x, bottom));
        g.setPen (ink);
        steady::draw (g, x + 4.0, baseline, label (t, decimals));
    }

    // The cursor: a small marker pointing down at the waveform.
    const auto cx = frameToX ((double) cursor);
    if (cx >= -6 && cx <= width() + 6)
    {
        QPainterPath p;
        p.moveTo (cx - 5, bottom - 7);
        p.lineTo (cx + 5, bottom - 7);
        p.lineTo (cx, bottom);
        p.closeSubpath();
        g.setRenderHint (QPainter::Antialiasing);
        g.setPen (Qt::NoPen);
        g.setBrush (theme.color (QStringLiteral ("cursor")));
        g.drawPath (p);
    }
}

void TimeRuler::mousePressEvent (QMouseEvent* e)
{
    if (length <= 0.0 || e->button() != Qt::LeftButton)
        return;
    const auto f = (Frame) std::llround (start + e->position().x() / std::max (1, width()) * length);
    Q_EMIT cursorRequested (std::clamp<Frame> (f, 0, fileLength));
}

void TimeRuler::mouseMoveEvent (QMouseEvent* e)
{
    if (e->buttons() & Qt::LeftButton)
        mousePressEvent (e);
}

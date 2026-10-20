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
    const auto row = (int) std::ceil (QFontMetricsF (font()).height());
    return { 400, row + 12 + (showsBeats() && showsTime() ? row + 2 : 0) };
}

void TimeRuler::setGrid (const BeatGrid& g, Mode m)
{
    if (g == grid && m == mode)
        return;
    const auto oldHeight = sizeHint().height();
    grid = g;
    mode = m;
    if (sizeHint().height() != oldHeight)
        updateGeometry();
    update();
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
    auto faint = theme.color (QStringLiteral ("windowText"));
    faint.setAlphaF (0.45f);

    g.setPen (faint);
    g.drawLine (QPointF (0, height() - 0.5), QPointF (width(), height() - 0.5));
    if (length <= 0.0 || width() <= 0)
        return;
    const auto bottom = (double) height();

    // Shade the selection, so it reads across both the ruler and the waveform.
    if (selEnd > selStart)
        g.fillRect (QRectF (QPointF (frameToX ((double) selStart), 0), QPointF (frameToX ((double) selEnd), bottom)),
                    theme.color (QStringLiteral ("selection")));

    if (showsBeats() && showsTime())
    {
        const auto middle = std::round (bottom / 2.0);
        drawBeats (g, 0.0, middle);
        g.setPen (faint);
        g.drawLine (QPointF (0, middle - 0.5), QPointF (width(), middle - 0.5));
        drawTime (g, middle, bottom);
    }
    else if (showsBeats())
        drawBeats (g, 0.0, bottom);
    else
        drawTime (g, 0.0, bottom);

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

void TimeRuler::drawTime (QPainter& g, double top, double bottom) const
{
    const auto& theme = Theme::current();
    const auto ink = theme.color (QStringLiteral ("windowText"));
    auto faint = ink;
    faint.setAlphaF (0.45f);

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

    const auto baseline = top + 2.0 + fm.ascent();
    for (auto i = (long long) std::floor (t0 / step.seconds); ; ++i)
    {
        const auto t = (double) i * step.seconds;
        if (t > lastSecond + 1e-12)
            break;
        if (t < 0.0)
            continue;
        const auto x = std::round ((t - t0) * pixelsPerSecond) + 0.5;
        g.setPen (faint);
        g.drawLine (QPointF (x, top), QPointF (x, bottom));
        g.setPen (ink);
        steady::draw (g, x + 4.0, baseline, label (t, decimals));
    }
}

void TimeRuler::drawBeats (QPainter& g, double top, double bottom) const
{
    const auto& theme = Theme::current();
    const auto ink = theme.color (QStringLiteral ("windowText"));
    auto faint = ink;
    faint.setAlphaF (0.45f);

    const auto t0 = start / rate, t1 = (start + length) / rate;
    const auto pixelsPerSecond = width() / (t1 - t0);
    const auto lastSecond = std::min (t1, (double) fileLength / rate);
    const QFontMetricsF fm (font());
    const auto perBar = std::max (1, grid.beatsPerBar);
    const auto beatPixels = grid.beatSeconds() * pixelsPerSecond;
    const auto widest = steady::width (fm, QStringLiteral ("888.8.8"));

    // Numbered every bar (or every 2nd, 4th... bar when crowded), every beat, or every sixteenth.
    double numbered = perBar;   // in beats
    if (beatPixels / 4.0 >= widest + 14.0)
        numbered = 0.25;
    else if (beatPixels >= widest + 14.0)
        numbered = 1.0;
    else
        while (numbered * beatPixels < steady::width (fm, QStringLiteral ("888")) + 14.0 && numbered < 4096.0)
            numbered *= 2.0;
    // Unnumbered ticks: beats between numbered bars, sixteenths between numbered beats.
    double tick = 0.0;
    if (numbered >= perBar)
        tick = beatPixels >= 6.0 ? 1.0 : (numbered > perBar ? (double) perBar : 0.0);
    else if (numbered >= 1.0 && beatPixels / 4.0 >= 6.0)
        tick = 0.25;

    auto xOf = [&] (double beat) { return std::round ((grid.secondsAt (beat) - t0) * pixelsPerSecond) + 0.5; };
    const auto first = std::floor (grid.beatAt (std::max (0.0, t0)));
    const auto last = grid.beatAt (lastSecond);

    if (tick > 0.0)
    {
        g.setPen (faint);
        for (auto b = std::ceil (first / tick) * tick; b <= last + 1e-9; b += tick)
            if (grid.secondsAt (b) >= -1e-9 && std::abs (std::remainder (b, numbered)) > 1e-6)
                g.drawLine (QPointF (xOf (b), bottom - (std::abs (std::remainder (b, 1.0)) < 1e-6 ? 6 : 3)), QPointF (xOf (b), bottom));
    }

    // Bar 1 is where the grid's downbeat is; earlier bars count 0, -1...
    const auto baseline = top + 2.0 + fm.ascent();
    for (auto b = std::ceil (first / numbered) * numbered; b <= last + 1e-9; b += numbered)
    {
        if (grid.secondsAt (b) < -1e-9)
            continue;
        const auto beatIndex = (long long) std::llround (std::floor (b + 1e-9));
        const auto bar = (long long) std::floor ((double) beatIndex / perBar) + 1;
        const auto beatInBar = (int) (beatIndex - (bar - 1) * perBar) + 1;
        const auto sixteenth = (int) std::llround ((b - (double) beatIndex) * 4.0) + 1;
        auto text = QString::number (bar);
        if (beatInBar > 1 || sixteenth > 1)
            text += QLatin1Char ('.') + QString::number (beatInBar);
        if (sixteenth > 1)
            text += QLatin1Char ('.') + QString::number (sixteenth);
        const auto x = xOf (b);
        const bool isBar = beatInBar == 1 && sixteenth == 1;
        g.setPen (faint);
        g.drawLine (QPointF (x, isBar ? top : bottom - 8), QPointF (x, bottom));
        g.setPen (ink);
        steady::draw (g, x + 4.0, baseline, text);
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

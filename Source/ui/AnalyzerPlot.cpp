// SPDX-License-Identifier: AGPL-3.0-only
#include "AnalyzerPlot.h"
#include "Theme.h"

#include <QMouseEvent>
#include <QPainter>
#include <QPainterPath>

#include <algorithm>
#include <cmath>

namespace
{
    QString number (double v)
    {
        const auto a = std::abs (v);
        if (a >= 1000.0)
            return QString::number (v / 1000.0, 'g', 3) + QStringLiteral ("k");
        return QString::number (v, 'g', a >= 100.0 ? 3 : a >= 1.0 ? 3 : 2);
    }

    // Grid steps for a linear axis: 1, 2 or 5 times a power of ten, about `count` of them.
    double niceStep (double span, int count)
    {
        const auto raw = span / std::max (1, count);
        const auto power = std::pow (10.0, std::floor (std::log10 (raw)));
        for (auto m : { 1.0, 2.0, 5.0, 10.0 })
            if (raw <= m * power)
                return m * power;
        return 10.0 * power;
    }
}

AnalyzerPlot::AnalyzerPlot (QWidget* parent) : QWidget (parent)
{
    setMouseTracking (true);
    setAttribute (Qt::WA_OpaquePaintEvent);
}

void AnalyzerPlot::setAxes (bool log, double from, double to, double low, double high, const QString& xu, const QString& yu,
                            bool fit, double span)
{
    logX = log;
    x0 = from;
    x1 = to;
    y0 = yLow = low;
    y1 = yHigh = high;
    xUnit = xu;
    yUnit = yu;
    fitY = fit;
    minimumSpan = span;
    this->fit();
    update();
}

void AnalyzerPlot::setSeries (std::vector<Series> s)
{
    series = std::move (s);
    fit();
    update();
}

void AnalyzerPlot::setMarkers (std::vector<Marker> m)
{
    markers = std::move (m);
    update();
}

void AnalyzerPlot::setEmptyText (const QString& text)
{
    emptyText = text;
    update();
}

void AnalyzerPlot::clear()
{
    series.clear();
    markers.clear();
    update();
}

void AnalyzerPlot::fit()
{
    if (! fitY)
    {
        y0 = yLow;
        y1 = yHigh;
        return;
    }
    double lo = 1.0e300, hi = -1.0e300;
    for (const auto& s : series)
        for (std::size_t i = 0; i < s.x.size() && i < s.y.size(); ++i)
            if (s.x[i] >= x0 && s.x[i] <= x1 && std::isfinite (s.y[i]) && s.y[i] > yLow - 1.0e6)
            {
                lo = std::min (lo, s.y[i]);
                hi = std::max (hi, s.y[i]);
            }
    if (lo > hi)
    {
        y0 = yLow;
        y1 = yHigh;
        return;
    }
    const auto margin = std::max (0.5, 0.08 * (hi - lo));
    lo -= margin;
    hi += margin;
    if (hi - lo < minimumSpan)
    {
        const auto mid = 0.5 * (hi + lo);
        lo = mid - minimumSpan / 2;
        hi = mid + minimumSpan / 2;
    }
    y0 = std::max (yLow, lo);
    y1 = std::min (yHigh, hi);
    if (y1 <= y0)
        y1 = y0 + minimumSpan;
}

QRectF AnalyzerPlot::plotArea() const
{
    const auto fm = fontMetrics();
    return QRectF (fm.horizontalAdvance (QStringLiteral ("-100.0 dB")) + 6.0, 6.0,
                   std::max (10.0, width() - fm.horizontalAdvance (QStringLiteral ("-100.0 dB")) - 14.0),
                   std::max (10.0, height() - fm.height() * 2.0 - 10.0));
}

double AnalyzerPlot::toX (double v, const QRectF& r) const
{
    const auto t = logX ? std::log (std::max (v, 1.0e-9) / x0) / std::log (x1 / x0) : (v - x0) / (x1 - x0);
    return r.left() + t * r.width();
}

double AnalyzerPlot::fromX (double px, const QRectF& r) const
{
    const auto t = (px - r.left()) / r.width();
    return logX ? x0 * std::pow (x1 / x0, t) : x0 + t * (x1 - x0);
}

double AnalyzerPlot::toY (double v, const QRectF& r) const
{
    return r.bottom() - (std::clamp (v, y0, y1) - y0) / (y1 - y0) * r.height();
}

void AnalyzerPlot::paintEvent (QPaintEvent*)
{
    const auto& theme = Theme::current();
    QPainter g (this);
    g.fillRect (rect(), theme.color (QStringLiteral ("meterBackground")));
    g.setRenderHint (QPainter::Antialiasing);
    const auto r = plotArea();
    const auto gridColour = theme.color (QStringLiteral ("mid"));
    auto textColour = theme.color (QStringLiteral ("text"));
    auto dim = textColour;
    dim.setAlphaF (0.6);
    const auto fm = fontMetrics();

    // Grid and labels.
    g.setPen (QPen (gridColour, 1.0));
    g.setFont (font());
    if (logX)
    {
        for (double decade = std::pow (10.0, std::floor (std::log10 (x0))); decade <= x1; decade *= 10.0)
            for (int m = 1; m <= 9; ++m)
            {
                const auto v = decade * m;
                if (v < x0 || v > x1)
                    continue;
                const auto x = toX (v, r);
                g.setPen (QPen (gridColour, m == 1 ? 1.0 : 0.5));
                g.drawLine (QPointF (x, r.top()), QPointF (x, r.bottom()));
                if (m == 1 || m == 2 || m == 5)
                {
                    g.setPen (dim);
                    g.drawText (QPointF (x - fm.horizontalAdvance (number (v)) / 2.0, r.bottom() + fm.ascent() + 3), number (v));
                }
            }
    }
    else
    {
        const auto step = niceStep (x1 - x0, std::max (2, (int) (r.width() / 80)));
        for (auto v = std::ceil (x0 / step) * step; v <= x1 + 1.0e-9; v += step)
        {
            const auto x = toX (v, r);
            g.setPen (QPen (gridColour, 0.5));
            g.drawLine (QPointF (x, r.top()), QPointF (x, r.bottom()));
            g.setPen (dim);
            g.drawText (QPointF (x - fm.horizontalAdvance (number (v)) / 2.0, r.bottom() + fm.ascent() + 3), number (v));
        }
    }
    {
        const auto step = niceStep (y1 - y0, std::max (2, (int) (r.height() / 36)));
        for (auto v = std::ceil (y0 / step) * step; v <= y1 + 1.0e-9; v += step)
        {
            const auto y = toY (v, r);
            g.setPen (QPen (gridColour, std::abs (v) < 1.0e-9 ? 1.0 : 0.5));
            g.drawLine (QPointF (r.left(), y), QPointF (r.right(), y));
            g.setPen (dim);
            const auto label = number (v) + (yUnit.isEmpty() ? QString() : QStringLiteral (" ") + yUnit);
            g.drawText (QPointF (r.left() - fm.horizontalAdvance (label) - 4, y + fm.ascent() / 2.0 - 1), label);
        }
    }
    g.setPen (dim);
    g.drawText (QPointF (r.right() - fm.horizontalAdvance (xUnit), r.bottom() + fm.height() * 2 + 2), xUnit);

    if (series.empty() || std::all_of (series.begin(), series.end(), [] (const Series& s) { return s.x.empty(); }))
    {
        g.setPen (dim);
        g.drawText (r, Qt::AlignCenter | Qt::TextWordWrap, emptyText);
        return;
    }

    g.save();
    g.setClipRect (r.adjusted (-1, -1, 1, 1));
    for (const auto& m : markers)
    {
        if (m.x < x0 || m.x > x1)
            continue;
        const auto x = toX (m.x, r);
        g.setPen (QPen (m.colour, 1.0, Qt::DashLine));
        g.drawLine (QPointF (x, r.top()), QPointF (x, r.bottom()));
        if (! m.label.isEmpty())
        {
            g.setPen (m.colour);
            g.drawText (QPointF (x + 3, r.top() + fm.ascent() + 2), m.label);
        }
    }
    for (const auto& s : series)
    {
        QPen pen (s.colour, 1.5);
        if (s.dashed)
            pen.setStyle (Qt::DashLine);
        g.setPen (pen);
        if (s.stems)
        {
            for (std::size_t i = 0; i < s.x.size() && i < s.y.size(); ++i)
                if (s.x[i] >= x0 && s.x[i] <= x1)
                    g.drawLine (QPointF (toX (s.x[i], r), r.bottom()), QPointF (toX (s.x[i], r), toY (s.y[i], r)));
            continue;
        }
        QPainterPath path;
        bool started = false;
        for (std::size_t i = 0; i < s.x.size() && i < s.y.size(); ++i)
        {
            if (s.x[i] < x0 || s.x[i] > x1 || ! std::isfinite (s.y[i]))
            {
                started = false;
                continue;
            }
            const QPointF p (toX (s.x[i], r), toY (s.y[i], r));
            if (! started)
                path.moveTo (p);
            else
                path.lineTo (p);
            started = true;
        }
        g.drawPath (path);
    }
    g.restore();

    // The legend, then the readout under the mouse.
    double lx = r.left() + 6;
    for (const auto& s : series)
    {
        if (s.name.isEmpty())
            continue;
        g.setPen (QPen (s.colour, 2.0));
        g.drawLine (QPointF (lx, r.bottom() - 8), QPointF (lx + 14, r.bottom() - 8));
        g.setPen (textColour);
        g.drawText (QPointF (lx + 18, r.bottom() - 4), s.name);
        lx += 30 + fm.horizontalAdvance (s.name);
    }
    if (mouseX >= r.left() && mouseX <= r.right())
    {
        const auto v = fromX (mouseX, r);
        g.setPen (QPen (dim, 1.0, Qt::DotLine));
        g.drawLine (QPointF (mouseX, r.top()), QPointF (mouseX, r.bottom()));
        QString text = number (v) + QStringLiteral (" ") + xUnit;
        for (const auto& s : series)
        {
            if (s.x.empty())
                continue;
            const auto it = std::lower_bound (s.x.begin(), s.x.end(), v);
            auto i = (std::size_t) std::clamp<std::ptrdiff_t> (it - s.x.begin(), 0, (std::ptrdiff_t) s.x.size() - 1);
            if (i > 0 && std::abs (s.x[i - 1] - v) < std::abs (s.x[i] - v))
                --i;
            text += QStringLiteral ("   ") + (s.name.isEmpty() ? QString() : s.name + QStringLiteral (" "))
                  + QString::number (s.y[i], 'f', std::abs (s.y[i]) < 10.0 ? 2 : 1) + QStringLiteral (" ") + yUnit;
        }
        const auto box = QRectF (r.left() + 4, r.top() + 4, fm.horizontalAdvance (text) + 10, fm.height() + 4);
        auto back = theme.color (QStringLiteral ("base"));
        back.setAlphaF (0.85);
        g.fillRect (box, back);
        g.setPen (textColour);
        g.drawText (box.adjusted (5, 2, 0, 0), Qt::AlignLeft | Qt::AlignTop, text);
    }
}

void AnalyzerPlot::mouseMoveEvent (QMouseEvent* e)
{
    mouseX = e->position().x();
    update();
}

void AnalyzerPlot::leaveEvent (QEvent*)
{
    mouseX = -1.0;
    update();
}

// SPDX-License-Identifier: AGPL-3.0-only
#include "WaveformView.h"

#include <QMouseEvent>
#include <QPainter>
#include <QWheelEvent>
#include <algorithm>
#include <cmath>

WaveformView::WaveformView (QWidget* parent) : QWidget (parent)
{
    setFocusPolicy (Qt::StrongFocus);
    setAccessibleName (tr ("Waveform"));
    setAccessibleDescription (tr ("Audio waveform. Arrow keys move the cursor, Shift+arrows select, Space plays."));
    setMinimumSize (320, 160);
}

void WaveformView::setData (AudioDataPtr d, bool keepView)
{
    const bool sameLength = data && d && data->length() == d->length();
    data = std::move (d);
    loading = false;

    if (keepView && sameLength)
        update();
    else if (keepView)
    {
        clampView();
        update();
    }
    else
        zoomToFit();
}

void WaveformView::setMarkers (std::vector<Marker> m)
{
    markers = std::move (m);
    update();
}

void WaveformView::clear()
{
    data.reset();
    markers.clear();
    viewStart = viewLength = 0.0;
    cursor = selStart = selEnd = 0;
    update();
}

void WaveformView::setLoading (bool isLoading, float progress)
{
    loading = isLoading;
    loadProgress = progress;
    update();
}

void WaveformView::setCursorFrame (Frame f)
{
    if (f == cursor)
        return;

    cursor = f;
    keepVisible (cursor);
    update();
}

void WaveformView::setSelection (Frame start, Frame end)
{
    if (start == selStart && end == selEnd)
        return;

    selStart = start;
    selEnd = end;
    update();
}

void WaveformView::clampView()
{
    const auto total = (double) length();
    if (total <= 0.0)
    {
        viewStart = viewLength = 0.0;
        return;
    }

    viewLength = std::clamp (viewLength, std::min (total, 16.0), total);
    viewStart = std::clamp (viewStart, 0.0, total - viewLength);
}

void WaveformView::zoomBy (double factor)
{
    if (length() <= 0)
        return;

    // Keep the cursor at the same place on screen while zooming.
    const auto anchorX = frameToX (cursor);
    const auto ratio = std::clamp (anchorX / std::max (1, width()), 0.0, 1.0);
    viewLength *= factor;
    clampView();
    viewStart = (double) cursor - ratio * viewLength;
    clampView();
    update();
}

void WaveformView::zoomToFit()
{
    viewStart = 0.0;
    viewLength = (double) length();
    update();
}

void WaveformView::zoomToSelection()
{
    if (selEnd <= selStart)
        return;

    viewStart = (double) selStart;
    viewLength = (double) (selEnd - selStart);
    clampView();
    update();
}

void WaveformView::keepVisible (Frame f)
{
    if (viewLength <= 0.0 || (f >= viewStart && f <= viewStart + viewLength))
        return;

    viewStart = (double) f - viewLength * 0.1;
    clampView();
}

Frame WaveformView::xToFrame (double x) const
{
    const auto f = (Frame) std::llround (viewStart + x / std::max (1, width()) * viewLength);
    return std::clamp<Frame> (f, 0, length());
}

double WaveformView::frameToX (Frame f) const
{
    return viewLength > 0.0 ? ((double) f - viewStart) / viewLength * width() : 0.0;
}

void WaveformView::paintEvent (QPaintEvent*)
{
    QPainter g (this);
    const auto pal = palette();
    g.fillRect (rect(), pal.color (QPalette::Base));

    if (! data || data->numChannels() == 0)
    {
        g.setPen (pal.color (QPalette::PlaceholderText));
        g.drawText (rect(), Qt::AlignCenter,
                    loading ? tr ("Reading file... %1%").arg ((int) (loadProgress * 100.0f))
                            : tr ("Press Ctrl+O to open a file, or Ctrl+Shift+P to search all commands"));
    }
    else
    {
        const auto& d = *data;
        const auto numChannels = d.numChannels();
        const auto laneHeight = (double) height() / numChannels;

        if (selEnd > selStart)
        {
            auto sel = pal.color (QPalette::Highlight);
            sel.setAlpha (60);
            const auto x1 = frameToX (selStart), x2 = frameToX (selEnd);
            g.fillRect (QRectF (x1, 0, std::max (1.0, x2 - x1), height()), sel);
        }

        for (int ch = 0; ch < numChannels; ++ch)
        {
            const auto mid = laneHeight * (ch + 0.5);
            const auto half = laneHeight * 0.45;

            g.setPen (pal.color (QPalette::Mid));
            g.drawLine (QPointF (0, mid), QPointF (width(), mid));
            if (ch > 0)
                g.drawLine (QPointF (0, laneHeight * ch), QPointF (width(), laneHeight * ch));

            g.setPen (pal.color (QPalette::Highlight));
            const auto framesPerPixel = viewLength / std::max (1, width());

            if (framesPerPixel < 1.0)
            {
                // Zoomed in past one frame per pixel: draw the samples as a line.
                const auto first = (Frame) std::floor (viewStart);
                const auto count = (int) std::ceil (viewLength) + 2;
                std::vector<std::vector<float>> all ((size_t) numChannels, std::vector<float> ((size_t) count));
                std::vector<float*> ptrs;
                for (auto& c : all)
                    ptrs.push_back (c.data());

                d.read (first, count, ptrs.data());
                QPointF prev;
                for (int i = 0; i < count && first + i <= d.length(); ++i)
                {
                    const QPointF p (frameToX (first + i), mid - all[(size_t) ch][(size_t) i] * half);
                    if (i > 0)
                        g.drawLine (prev, p);
                    prev = p;
                }
                continue;
            }

            for (int x = 0; x < width(); ++x)
            {
                const auto s = (Frame) (viewStart + x * framesPerPixel);
                const auto e = std::max (s + 1, (Frame) (viewStart + (x + 1) * framesPerPixel));
                if (s >= d.length())
                    break;

                float lo = 0.0f, hi = 0.0f;
                d.minMax (ch, s, std::min (e, d.length()), lo, hi);
                g.drawLine (QPointF (x, mid - hi * half), QPointF (x, mid - lo * half));
            }
        }

        for (const auto& m : markers)
        {
            const auto x = frameToX (m.start);
            auto colour = pal.color (QPalette::Link);
            g.setPen (QPen (colour, 1.0, m.isRegion() ? Qt::DashLine : Qt::SolidLine));
            g.drawLine (QPointF (x, 0), QPointF (x, height()));
            if (m.isRegion())
            {
                const auto x2 = frameToX (m.end);
                g.drawLine (QPointF (x2, 0), QPointF (x2, height()));
            }
            g.drawText (QPointF (x + 3, 12), QString::fromStdString (m.name));
        }

        g.setPen (QPen (pal.color (QPalette::BrightText), 1.0));
        const auto cx = frameToX (cursor);
        g.drawLine (QPointF (cx, 0), QPointF (cx, height()));
    }

    if (hasFocus())
    {
        g.setPen (QPen (pal.color (QPalette::Link), 2.0));
        g.drawRect (rect().adjusted (1, 1, -1, -1));
    }
}

void WaveformView::mousePressEvent (QMouseEvent* e)
{
    setFocus (Qt::MouseFocusReason);
    if (! data || e->button() != Qt::LeftButton)
        return;

    const auto f = xToFrame (e->position().x());

    if (e->modifiers() & Qt::ShiftModifier)
    {
        // Extend from whichever selection edge (or the cursor) is farther away.
        Frame anchor = cursor;
        if (selEnd > selStart)
            anchor = std::llabs (f - selStart) > std::llabs (f - selEnd) ? selStart : selEnd;

        dragAnchor = anchor;
        Q_EMIT selectionRequested (anchor, f);
        return;
    }

    dragAnchor = f;
    Q_EMIT cursorRequested (f);
}

void WaveformView::mouseMoveEvent (QMouseEvent* e)
{
    if (dragAnchor < 0 || ! (e->buttons() & Qt::LeftButton))
        return;

    Q_EMIT selectionRequested (dragAnchor, xToFrame (e->position().x()));
}

void WaveformView::mouseReleaseEvent (QMouseEvent*)
{
    dragAnchor = -1;
}

void WaveformView::wheelEvent (QWheelEvent* e)
{
    if (! data)
        return;

    const auto steps = e->angleDelta().y() / 120.0;

    if (e->modifiers() & Qt::ControlModifier)
    {
        viewLength *= std::pow (0.8, steps);
        clampView();
    }
    else
    {
        viewStart -= steps * viewLength * 0.1;
        clampView();
    }

    update();
    e->accept();
}

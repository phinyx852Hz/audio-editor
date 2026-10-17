// SPDX-License-Identifier: AGPL-3.0-only
#include "WaveformView.h"
#include "../core/Dsp.h"
#include "Spectrogram.h"
#include "Theme.h"

#include <QMouseEvent>
#include <QPainter>
#include <QWheelEvent>
#include <QtConcurrent/QtConcurrentRun>
#include <algorithm>
#include <cmath>

WaveformView::WaveformView (QWidget* parent) : QWidget (parent)
{
    setFocusPolicy (Qt::StrongFocus);
    setAccessibleName (tr ("Waveform"));
    setAccessibleDescription (tr ("Audio waveform. Arrow keys move the cursor, Shift+arrows select, Space plays."));
    setMinimumSize (320, 160);

    connect (&spectrogramWatcher, &QFutureWatcher<QImage>::finished, this, [this]
    {
        spectrogram = spectrogramWatcher.result();
        spectrogramKey = pendingKey;
        update();  // paints, and asks again if the view changed meanwhile
    });
}

void WaveformView::setSpectral (bool on)
{
    spectral = on;
    update();
}

void WaveformView::setBand (double lowHz, double highHz)
{
    if (std::abs (lowHz - bandLow) < 1.0e-9 && std::abs (highHz - bandHigh) < 1.0e-9)
        return;

    bandLow = lowHz;
    bandHigh = highHz;
    update();
}

double WaveformView::laneHeight() const
{
    return (double) height() / std::max (1, data ? data->numChannels() : 1);
}

// The same log scale as the spectrogram: 20 Hz at the bottom of a lane, half the sample rate at the top.
double WaveformView::yToHz (double y) const
{
    const auto nyquist = data ? data->sampleRate() / 2.0 : 24000.0;
    const auto t = std::clamp (y / std::max (1.0, laneHeight()), 0.0, 1.0);
    return 20.0 * std::pow (nyquist / 20.0, 1.0 - t);
}

double WaveformView::hzToY (double hz) const
{
    const auto nyquist = data ? data->sampleRate() / 2.0 : 24000.0;
    hz = std::clamp (hz, 20.0, nyquist);
    return (1.0 - std::log (hz / 20.0) / std::log (nyquist / 20.0)) * laneHeight();
}

WaveformView::SpectrogramKey WaveformView::currentKey() const
{
    return { data.get(), viewStart, viewLength, size(), Theme::current().name };
}

void WaveformView::requestSpectrogram()
{
    if (! data || spectrogramWatcher.isRunning() || currentKey() == spectrogramKey)
        return;

    pendingKey = currentKey();
    spectrogramWatcher.setFuture (QtConcurrent::run ([d = data, key = pendingKey, gradient = Theme::current().spectrogram]
    {
        return renderSpectrogram (*d, key.viewStart, key.viewLength, key.size, gradient);
    }));
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
    spectrogram = QImage();
    spectrogramKey = {};
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

void WaveformView::setView (double start, double length)
{
    if (length <= 0.0)
        return;

    viewStart = start;
    viewLength = length;
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
    const auto& theme = Theme::current();
    g.setCompositionMode (QPainter::CompositionMode_Source);  // replace, so the window's opacity shows through
    g.fillRect (rect(), Theme::withOpacity (theme.color (QStringLiteral ("waveformBackground"))));
    g.setCompositionMode (QPainter::CompositionMode_SourceOver);

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

        if (spectral)
        {
            requestSpectrogram();
            if (! spectrogram.isNull())
            {
                g.setOpacity (Theme::opacity());
                g.drawImage (rect(), spectrogram);  // stretched while a fresh one renders
                g.setOpacity (1.0);
            }
            else
            {
                g.setPen (pal.color (QPalette::PlaceholderText));
                g.drawText (rect(), Qt::AlignCenter, tr ("Computing spectrogram..."));
            }
        }
        else
        {
            for (int ch = 0; ch < numChannels; ++ch)
            {
                const auto mid = laneHeight * (ch + 0.5);
                const auto half = laneHeight * 0.45;

                g.setPen (theme.color (QStringLiteral ("centerLine")));
                g.drawLine (QPointF (0, mid), QPointF (width(), mid));
                if (ch > 0)
                    g.drawLine (QPointF (0, laneHeight * ch), QPointF (width(), laneHeight * ch));

                g.setPen (theme.color (QStringLiteral ("waveform")));
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
        }

        // Speaker names (L, R, C, LFE...) at the top left of each lane.
        if (numChannels > 1)
        {
            auto small = font();
            small.setPointSizeF (small.pointSizeF() * 0.8);
            g.setFont (small);
            g.setPen (pal.color (QPalette::PlaceholderText));
            for (int ch = 0; ch < numChannels; ++ch)
                g.drawText (QRectF (6, laneHeight * ch + 2, 80, 16), Qt::AlignLeft | Qt::AlignTop,
                            QString::fromStdString (dsp::channelName (ch, numChannels)));
            g.setFont (font());
        }

        if (selEnd > selStart)
        {
            const auto x1 = frameToX (selStart), x2 = frameToX (selEnd);
            const auto colour = theme.color (QStringLiteral ("selection"));
            if (spectral && bandHigh > bandLow)
            {
                // A box per lane around the selected frequencies.
                auto edge = colour;
                edge.setAlpha (255);
                for (int ch = 0; ch < numChannels; ++ch)
                {
                    const auto top = laneHeight * ch + hzToY (bandHigh), bottom = laneHeight * ch + hzToY (bandLow);
                    const QRectF box (x1, top, std::max (1.0, x2 - x1), std::max (1.0, bottom - top));
                    g.fillRect (box, colour);
                    g.setPen (QPen (edge, 1.0));
                    g.drawRect (box);
                }
            }
            else
                g.fillRect (QRectF (x1, 0, std::max (1.0, x2 - x1), height()), colour);
        }

        for (const auto& m : markers)
        {
            const auto x = frameToX (m.start);
            const auto colour = theme.color (m.isRegion() ? QStringLiteral ("region") : QStringLiteral ("marker"));

            if (m.isRegion())
            {
                const auto x2 = frameToX (m.end);
                auto band = colour;
                band.setAlpha (40);
                g.fillRect (QRectF (x, 0, x2 - x, 16), band);
                g.setPen (QPen (colour, 1.0, Qt::DashLine));
                g.drawLine (QPointF (x2, 0), QPointF (x2, height()));
            }

            g.setPen (QPen (colour, 1.0, m.isRegion() ? Qt::DashLine : Qt::SolidLine));
            g.drawLine (QPointF (x, 0), QPointF (x, height()));
            g.setPen (colour);
            g.drawText (QPointF (x + 3, 12), QString::fromStdString (m.name));
        }

        g.setPen (QPen (theme.color (QStringLiteral ("cursor")), 1.0));
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
    dragLane = std::clamp ((int) (e->position().y() / laneHeight()), 0, std::max (0, data->numChannels() - 1));
    dragAnchorY = e->position().y() - dragLane * laneHeight();
    Q_EMIT cursorRequested (f);
}

void WaveformView::mouseMoveEvent (QMouseEvent* e)
{
    if (dragAnchor < 0 || ! (e->buttons() & Qt::LeftButton))
        return;

    Q_EMIT selectionRequested (dragAnchor, xToFrame (e->position().x()));

    if (spectral)
    {
        const auto y = std::clamp (e->position().y() - dragLane * laneHeight(), 0.0, laneHeight());
        const auto tall = std::abs (y - dragAnchorY) >= laneHeight() * 0.9;
        if (tall)
            Q_EMIT bandRequested (0.0, 0.0);
        else
            Q_EMIT bandRequested (yToHz (std::max (y, dragAnchorY)), yToHz (std::min (y, dragAnchorY)));
    }
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

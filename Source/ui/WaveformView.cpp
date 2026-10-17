// SPDX-License-Identifier: AGPL-3.0-only
#include "WaveformView.h"

#include <QMouseEvent>
#include <QPainter>
#include <algorithm>

WaveformView::WaveformView (QWidget* parent) : QWidget (parent)
{
    setFocusPolicy (Qt::StrongFocus);
    setAccessibleName (tr ("Waveform"));
    setAccessibleDescription (tr ("Audio waveform. Arrow keys move the cursor, Space plays."));
    setMinimumSize (320, 160);
}

void WaveformView::setPeaks (std::shared_ptr<const WaveformPeaks> p)
{
    peaks = std::move (p);
    loading = false;
    zoomToFit();
}

void WaveformView::clear()
{
    peaks.reset();
    viewStart = viewLength = cursor = 0.0;
    update();
}

void WaveformView::setLoading (bool isLoading)
{
    loading = isLoading;
    update();
}

double WaveformView::lengthSeconds() const { return peaks ? peaks->lengthSeconds() : 0.0; }

void WaveformView::setCursorSeconds (double seconds)
{
    if (qFuzzyCompare (seconds + 1.0, cursor + 1.0))
        return;

    cursor = seconds;
    keepCursorVisible();
    update();
}

void WaveformView::zoomBy (double factor)
{
    const auto total = lengthSeconds();
    if (total <= 0.0)
        return;

    viewLength = std::clamp (viewLength * factor, 0.001, total);
    viewStart = std::clamp (cursor - viewLength * 0.5, 0.0, total - viewLength);
    update();
}

void WaveformView::zoomToFit()
{
    viewStart = 0.0;
    viewLength = lengthSeconds();
    update();
}

void WaveformView::keepCursorVisible()
{
    if (viewLength <= 0.0 || (cursor >= viewStart && cursor <= viewStart + viewLength))
        return;

    viewStart = std::clamp (cursor - viewLength * 0.1, 0.0, std::max (0.0, lengthSeconds() - viewLength));
}

double WaveformView::xToTime (double x) const { return viewStart + x / std::max (1, width()) * viewLength; }
double WaveformView::timeToX (double t) const { return viewLength > 0.0 ? (t - viewStart) / viewLength * width() : 0.0; }

void WaveformView::paintEvent (QPaintEvent*)
{
    QPainter g (this);
    const auto pal = palette();
    g.fillRect (rect(), pal.color (QPalette::Base));

    if (! peaks || peaks->numChannels == 0)
    {
        g.setPen (pal.color (QPalette::PlaceholderText));
        g.drawText (rect(), Qt::AlignCenter,
                    loading ? tr ("Reading file...")
                            : tr ("Press Ctrl+O to open a file, or Ctrl+Shift+P to search all commands"));
    }
    else
    {
        const auto& p = *peaks;
        const auto numPeaks = (long long) p.minimums[0].size();
        const auto laneHeight = (double) height() / p.numChannels;
        const auto peaksPerSecond = p.sampleRate / p.samplesPerPeak;

        g.setPen (pal.color (QPalette::Highlight));

        for (int ch = 0; ch < p.numChannels; ++ch)
        {
            const auto mid = laneHeight * (ch + 0.5);
            const auto half = laneHeight * 0.45;
            const auto& mins = p.minimums[(size_t) ch];
            const auto& maxs = p.maximums[(size_t) ch];

            for (int x = 0; x < width(); ++x)
            {
                auto first = (long long) (xToTime (x) * peaksPerSecond);
                auto last  = std::max (first + 1, (long long) (xToTime (x + 1) * peaksPerSecond));
                first = std::clamp (first, 0LL, numPeaks);
                last  = std::clamp (last, 0LL, numPeaks);

                if (first >= last)
                    continue;

                const auto lo = *std::min_element (mins.begin() + first, mins.begin() + last);
                const auto hi = *std::max_element (maxs.begin() + first, maxs.begin() + last);
                g.drawLine (QPointF (x, mid - hi * half), QPointF (x, mid - lo * half));
            }

            g.setPen (pal.color (QPalette::Mid));
            g.drawLine (QPointF (0, mid), QPointF (width(), mid));
            g.setPen (pal.color (QPalette::Highlight));
        }

        g.setPen (QPen (pal.color (QPalette::BrightText), 1.0));
        const auto cx = timeToX (cursor);
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
    if (peaks)
        Q_EMIT cursorRequested (std::clamp (xToTime (e->position().x()), 0.0, lengthSeconds()));
}

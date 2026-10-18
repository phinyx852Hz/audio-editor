// SPDX-License-Identifier: AGPL-3.0-only
#include "Overview.h"
#include "Theme.h"
#include "TimeRuler.h"

#include <QMouseEvent>
#include <QPainter>
#include <QVBoxLayout>

#include <algorithm>
#include <cmath>

// The waveform part: the whole file, with the editor's range boxed.
class Overview::Wave final : public QWidget
{
public:
    explicit Wave (Overview& o) : QWidget (&o), owner (o)
    {
        setSizePolicy (QSizePolicy::Ignored, QSizePolicy::Fixed);
        setFixedHeight (64);
        setCursor (Qt::PointingHandCursor);
        setMouseTracking (true);
        setToolTip (tr ("The whole file. Click or drag to move the editor there, drag an edge of the box to zoom, "
                        "or turn the mouse wheel to zoom around the mouse."));
    }

    bool setState (AudioDataPtr d, double start, double length, Frame c, Frame a, Frame b)
    {
        // Compare by value: the doubles come straight from the editor, so equal means unchanged.
        const bool same = d == data && std::abs (start - viewStart) <= 0.0 && std::abs (length - viewLength) <= 0.0
                          && c == cursor && a == selStart && b == selEnd;
        if (same)
            return false;
        data = std::move (d);
        viewStart = start; viewLength = length; cursor = c; selStart = a; selEnd = b;
        update();
        return true;
    }

    double frameToX (double f) const { return data && data->length() > 0 ? f / (double) data->length() * width() : 0.0; }
    double xToFrame (double x) const { return data ? std::clamp (x / std::max (1, width()), 0.0, 1.0) * (double) data->length() : 0.0; }

protected:
    void paintEvent (QPaintEvent*) override
    {
        QPainter g (this);
        const auto& theme = Theme::current();
        g.setCompositionMode (QPainter::CompositionMode_Source);
        g.fillRect (rect(), Theme::withOpacity (theme.color (QStringLiteral ("waveformBackground"))));
        g.setCompositionMode (QPainter::CompositionMode_SourceOver);
        if (! data || data->length() == 0 || data->numChannels() == 0)
            return;

        // The waveform is drawn once per file version, size and theme.
        if (image.size() != size() * devicePixelRatioF() || imageData != data.get() || imageTheme != theme.name)
            render();
        g.drawImage (rect(), image);

        const auto x1 = frameToX (viewStart), x2 = frameToX (viewStart + viewLength);
        if (selEnd > selStart)
            g.fillRect (QRectF (QPointF (frameToX ((double) selStart), 0), QPointF (frameToX ((double) selEnd), height())),
                        theme.color (QStringLiteral ("selection")));

        // Dim what the editor does not show, and box what it does.
        auto dim = theme.color (QStringLiteral ("window"));
        dim.setAlphaF (0.55f);
        g.fillRect (QRectF (0, 0, x1, height()), dim);
        g.fillRect (QRectF (x2, 0, width() - x2, height()), dim);
        g.setPen (QPen (theme.color (QStringLiteral ("highlight")), 1.0));
        g.setBrush (Qt::NoBrush);
        g.drawRect (QRectF (std::round (x1) + 0.5, 0.5, std::max (1.0, std::round (x2 - x1) - 1.0), height() - 1.0));

        const auto cx = std::round (frameToX ((double) cursor)) + 0.5;
        g.setPen (theme.color (QStringLiteral ("cursor")));
        g.drawLine (QPointF (cx, 0), QPointF (cx, height()));
    }

    void mousePressEvent (QMouseEvent* e) override
    {
        if (! data || e->button() != Qt::LeftButton)
            return;
        drag = edgeAt (e->position().x());
        if (drag != Drag::move)
            return;
        // Grab the box where it was clicked, or centre it on the click.
        const auto f = xToFrame (e->position().x());
        grabOffset = (f >= viewStart && f <= viewStart + viewLength) ? f - viewStart : viewLength / 2.0;
        Q_EMIT owner.viewRequested (f - grabOffset, viewLength);
    }

    void mouseMoveEvent (QMouseEvent* e) override
    {
        if (! data)
            return;
        if (! (e->buttons() & Qt::LeftButton))
        {
            setCursor (edgeAt (e->position().x()) == Drag::move ? Qt::PointingHandCursor : Qt::SizeHorCursor);
            return;
        }
        const auto f = xToFrame (e->position().x());
        const auto end = viewStart + viewLength;
        if (drag == Drag::left)
            Q_EMIT owner.viewRequested (std::min (f, end - 16.0), end - std::min (f, end - 16.0));
        else if (drag == Drag::right)
            Q_EMIT owner.viewRequested (viewStart, std::max (16.0, f - viewStart));
        else
            Q_EMIT owner.viewRequested (f - grabOffset, viewLength);
    }

    void wheelEvent (QWheelEvent* e) override
    {
        if (! data || e->angleDelta().y() == 0)
            return;
        // Zoom around the frame under the mouse: it stays where it is in the
        // editor if the editor shows it, otherwise the editor centres on it.
        const auto f = xToFrame (e->position().x());
        const auto length = viewLength * std::pow (0.8, e->angleDelta().y() / 120.0);
        const auto inside = f >= viewStart && f <= viewStart + viewLength;
        const auto ratio = inside && viewLength > 0.0 ? (f - viewStart) / viewLength : 0.5;
        Q_EMIT owner.viewRequested (f - ratio * length, length);
        viewStart = f - ratio * length;   // so quick wheel turns build on each other before the next tick
        viewLength = length;
        update();
        e->accept();
    }

    void mouseDoubleClickEvent (QMouseEvent* e) override
    {
        if (data && e->button() == Qt::LeftButton)
            Q_EMIT owner.cursorRequested ((Frame) std::llround (xToFrame (e->position().x())));
    }

private:
    enum class Drag { move, left, right };

    // Within a few pixels of an edge of the box, a drag resizes it.
    Drag edgeAt (double x) const
    {
        const auto x1 = frameToX (viewStart), x2 = frameToX (viewStart + viewLength);
        if (x2 - x1 >= 10.0)
        {
            if (std::abs (x - x1) <= 4.0)
                return Drag::left;
            if (std::abs (x - x2) <= 4.0)
                return Drag::right;
        }
        return Drag::move;
    }

    void render()
    {
        const auto& theme = Theme::current();
        const auto ratio = devicePixelRatioF();
        image = QImage (size() * ratio, QImage::Format_ARGB32_Premultiplied);
        image.setDevicePixelRatio (ratio);
        image.fill (Qt::transparent);
        imageData = data.get();
        imageTheme = theme.name;

        QPainter g (&image);
        const auto& d = *data;
        const auto channels = d.numChannels();
        const auto pixels = (int) std::ceil (width() * ratio);
        const auto lane = (double) height() / channels;
        const auto perPixel = (double) d.length() / std::max (1, pixels);

        g.setPen (theme.color (QStringLiteral ("centerLine")));
        for (int ch = 0; ch < channels; ++ch)
            g.drawLine (QPointF (0, lane * (ch + 0.5)), QPointF (width(), lane * (ch + 0.5)));

        g.setPen (QPen (theme.color (QStringLiteral ("waveform")), 1.0 / ratio));
        for (int ch = 0; ch < channels; ++ch)
        {
            const auto mid = lane * (ch + 0.5), half = lane * 0.45;
            for (int px = 0; px < pixels; ++px)
            {
                const auto s = (Frame) (px * perPixel);
                const auto e = std::min (d.length(), std::max (s + 1, (Frame) ((px + 1) * perPixel)));
                if (s >= d.length())
                    break;
                float lo = 0.0f, hi = 0.0f;
                d.minMax (ch, s, e, lo, hi);
                const auto x = (px + 0.5) / ratio;
                g.drawLine (QPointF (x, mid - hi * half), QPointF (x, mid - lo * half));
            }
        }
    }

    Overview& owner;
    AudioDataPtr data;
    double viewStart = 0.0, viewLength = 0.0, grabOffset = 0.0;
    Drag drag = Drag::move;
    Frame cursor = 0, selStart = 0, selEnd = 0;
    QImage image;
    const AudioData* imageData = nullptr;
    QString imageTheme;
};

//==============================================================================
Overview::Overview (QWidget* parent) : QWidget (parent)
{
    setAccessibleName (tr ("Overview"));
    auto* column = new QVBoxLayout (this);
    column->setContentsMargins (0, 0, 0, 6);
    column->setSpacing (0);
    column->addWidget (ruler = new TimeRuler (this));
    column->addWidget (wave = new Wave (*this));
    connect (ruler, &TimeRuler::cursorRequested, this, &Overview::cursorRequested);
}

void Overview::setState (AudioDataPtr d, double viewStart, double viewLength, Frame cursor, Frame selStart, Frame selEnd)
{
    const auto length = d ? d->length() : 0;
    ruler->setView (0.0, (double) length, d ? d->sampleRate() : 48000.0, length, cursor, selStart, selEnd);
    wave->setState (std::move (d), viewStart, viewLength, cursor, selStart, selEnd);
}

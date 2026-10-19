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
#include <array>
#include <cmath>

WaveformView::WaveformView (QWidget* parent) : QWidget (parent)
{
    setFocusPolicy (Qt::StrongFocus);
    setAccessibleName (tr ("Waveform"));
    setMouseTracking (true);
    setAccessibleDescription (tr ("Audio waveform. Arrow keys move the cursor, Shift+arrows select, Space plays."));
    setMinimumSize (320, 160);

    connect (&spectrogramWatcher, &QFutureWatcher<SpectrogramPicture>::finished, this, [this]
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

void WaveformView::setSpectrumSettings (const spectrum::Settings& s)
{
    if (s == spectrumSet)
        return;
    const auto rescaled = s.scale != spectrumSet.scale;
    spectrumSet = s;
    if (rescaled)
    {
        ++shapeVersion;   // the drawn shape's rows move
        Q_EMIT frequencyRangeChanged();
    }
    update();
}

std::pair<double, double> WaveformView::frequencyRange() const
{
    const auto nyquist = data ? data->sampleRate() / 2.0 : 24000.0;
    const auto high = freqHigh <= 0.0 ? nyquist : std::min (freqHigh, nyquist);
    auto low = spectrum::lowestFor (spectrumSet.scale, freqLow);
    if (spectrumSet.scale == spectrum::Scale::log && freqLow <= 0.0)
        low = 20.0;   // the log scale starts at 20 Hz unless zoomed lower
    return { std::min (low, high * 0.5), high };
}

bool WaveformView::showsAllFrequencies() const
{
    const auto [low, high] = frequencyRange();
    const auto nyquist = data ? data->sampleRate() / 2.0 : 24000.0;
    return high >= nyquist - 1.0 && low <= (spectrumSet.scale == spectrum::Scale::log ? 20.0 : 1.0);
}

void WaveformView::setFrequencyRange (double low, double high)
{
    freqLow = std::max (0.0, low);
    freqHigh = std::max (0.0, high);
    ++shapeVersion;
    Q_EMIT frequencyRangeChanged();
    update();
}

void WaveformView::zoomFrequency (double factor, double around)
{
    const auto [low, high] = frequencyRange();
    const auto nyquist = data ? data->sampleRate() / 2.0 : 24000.0;
    const auto scale = spectrumSet.scale;
    around = std::clamp (around, 0.0, 1.0);
    const auto fullLow = spectrum::lowestFor (scale, 0.0);
    // Zoom in the scale's own spacing, keeping the frequency under `around` in place.
    auto newLow = spectrum::frequency (scale, around - around / factor, low, high);
    auto newHigh = spectrum::frequency (scale, around + (1.0 - around) / factor, low, high);
    newLow = std::max (newLow, scale == spectrum::Scale::log ? fullLow : 0.0);
    newHigh = std::min (newHigh, nyquist);
    if (newHigh - newLow < 20.0 || newHigh / std::max (newLow, 1.0) < 1.02)
        return;   // as close as it goes
    setFrequencyRange (newLow, newHigh >= nyquist - 0.5 ? 0.0 : newHigh);
}

void WaveformView::setBand (double lowHz, double highHz)
{
    if (std::abs (lowHz - bandLow) < 1.0e-9 && std::abs (highHz - bandHigh) < 1.0e-9)
        return;

    bandLow = lowHz;
    bandHigh = highHz;
    update();
}

void WaveformView::setTool (Tool t)
{
    currentTool = t;
    setMouseTracking (true);   // to show the brush outline and the fade handles' cursor
    QWidget::setCursor (t == Tool::box ? Qt::ArrowCursor : Qt::CrossCursor);
    update();
}

void WaveformView::setBrushSize (int pixels)
{
    brushRadius = std::clamp (pixels, 2, 200);
    update();
}

void WaveformView::setShape (std::shared_ptr<const repair::SpectralMask> s)
{
    shape = std::move (s);
    ++shapeVersion;
    update();
}

void WaveformView::brushAt (QPointF p)
{
    if (! stroke || ! data)
        return;
    const auto y = std::clamp (p.y() - dragLane * laneHeight(), 0.0, laneHeight());
    const auto radiusFrames = brushRadius * viewLength / std::max (1, width());
    const auto radiusRows = brushRadius * stroke->rows() / std::max (1.0, laneHeight());
    stroke->paint (xToFrame (p.x()), yToHz (y), radiusFrames, radiusRows);
    ++shapeVersion;
}

void WaveformView::drawShape (QPainter& g, double lane)
{
    const repair::SpectralMask* mask = stroke ? stroke.get() : shape.get();
    if (mask != nullptr && ! mask->empty())
    {
        const QSize size (width(), std::max (1, (int) lane));
        if (shapeImageVersion != shapeVersion || shapeImageView != view() || shapeImageSize != size)
        {
            shapeImage = QImage (size, QImage::Format_ARGB32_Premultiplied);
            shapeImage.fill (Qt::transparent);
            auto colour = Theme::current().color (QStringLiteral ("selection"));
            std::vector<double> rowHz ((size_t) size.height());
            for (int y = 0; y < size.height(); ++y)
                rowHz[(size_t) y] = yToHz (y + 0.5);
            for (int x = 0; x < size.width(); ++x)
            {
                const auto frame = (Frame) (viewStart + (x + 0.5) * viewLength / std::max (1, size.width()));
                if (frame < mask->start() || frame >= mask->end())
                    continue;
                for (int y = 0; y < size.height(); ++y)
                    if (const auto v = mask->at (frame, rowHz[(size_t) y]); v > 0.0f)
                    {
                        colour.setAlpha ((int) (v * 150.0f));
                        shapeImage.setPixelColor (x, y, colour);
                    }
            }
            shapeImageVersion = shapeVersion;
            shapeImageView = view();
            shapeImageSize = size;
        }
        for (int ch = 0; ch < data->numChannels(); ++ch)
            g.drawImage (QPointF (0.0, lane * ch), shapeImage);
    }

    auto edge = Theme::current().color (QStringLiteral ("selection"));
    edge.setAlpha (255);
    if (lassoPoints.size() > 1)
    {
        g.setPen (QPen (edge, 1.5));
        for (int ch = 0; ch < data->numChannels(); ++ch)
            g.drawPolyline (lassoPoints.translated (0.0, lane * ch));
    }
    if ((currentTool == Tool::brush || currentTool == Tool::spotHeal) && hover.x() >= 0.0)
    {
        g.setPen (QPen (edge, 1.0));
        g.setBrush (Qt::NoBrush);
        g.drawEllipse (hover, brushRadius, brushRadius);
    }
}

double WaveformView::laneHeight() const
{
    return (double) height() / std::max (1, data ? data->numChannels() : 1);
}

// The spectrogram's scale: the low end of the range at the bottom of a lane, the high end at the top.
double WaveformView::yToHz (double y) const
{
    const auto [low, high] = frequencyRange();
    const auto t = std::clamp (y / std::max (1.0, laneHeight()), 0.0, 1.0);
    return spectrum::frequency (spectrumSet.scale, 1.0 - t, low, high);
}

double WaveformView::hzToY (double hz) const
{
    const auto [low, high] = frequencyRange();
    const auto p = spectrum::position (spectrumSet.scale, std::clamp (hz, low, high), low, high);
    return (1.0 - p) * laneHeight();
}

WaveformView::SpectrogramKey WaveformView::currentKey() const
{
    const auto [low, high] = frequencyRange();
    return { data.get(), viewStart, viewLength, size(), Theme::current().name, spectrumSet, low, high };
}

void WaveformView::requestSpectrogram()
{
    if (! data || spectrogramWatcher.isRunning() || currentKey() == spectrogramKey)
        return;

    pendingKey = currentKey();
    spectrogramWatcher.setFuture (QtConcurrent::run ([d = data, key = pendingKey, gradient = Theme::current().spectrogram]
    {
        return renderSpectrogram (*d, key.viewStart, key.viewLength, key.size, gradient, key.settings, key.low, key.high);
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
    spectrogram = {};
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
            if (! spectrogram.image.isNull())
            {
                g.setOpacity (Theme::opacity());
                g.drawImage (rect(), spectrogram.image);  // stretched while a fresh one renders
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

                if (levelColours)
                {
                    // One gradient over the lane, mirrored around the centre line: a sample's
                    // colour depends on how far it reaches, i.e. its level in dBFS.
                    QLinearGradient levels (0.0, mid - half, 0.0, mid + half);
                    static const std::array<std::pair<double, const char*>, 6> stops {{
                        { -48.0, "levelQuiet" }, { -24.0, "levelSoft" }, { -12.0, "levelMedium" },
                        { -6.0, "levelLoud" }, { -3.0, "levelHot" }, { 0.0, "levelPeak" } }};
                    levels.setColorAt (0.5, theme.color (QString::fromLatin1 (stops.front().second)));
                    for (const auto& [db, key] : stops)
                    {
                        const auto reach = 0.5 * std::pow (10.0, db / 20.0);
                        const auto colour = theme.color (QString::fromLatin1 (key));
                        levels.setColorAt (0.5 - reach, colour);
                        levels.setColorAt (0.5 + reach, colour);
                    }
                    g.setPen (QPen (QBrush (levels), 1.0));
                }
                else
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

        if (spectral)
        {
            drawFrequencyAxis (g, laneHeight);
            drawShape (g, laneHeight);
        }

        if (selEnd > selStart && ! (spectral && shape))
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

        drawFades (g);
        if (spectral)
            drawReadout (g, laneHeight);
    }

    if (hasFocus())
    {
        g.setPen (QPen (pal.color (QPalette::Link), 2.0));
        g.drawRect (rect().adjusted (1, 1, -1, -1));
    }
}

namespace
{
    QString hzText (double hz)
    {
        if (hz >= 1000.0)
            return QString::number (hz / 1000.0, 'g', hz >= 10000.0 ? 3 : 2) + QStringLiteral (" k");
        return QString::number (hz, 'g', 3);
    }

    // Text that reads over any spectrogram colour: a dark outline under light text.
    void outlinedText (QPainter& g, const QRectF& box, int flags, const QString& text)
    {
        g.setPen (QColor (0, 0, 0, 200));
        for (const auto& d : { QPointF (-1, 0), QPointF (1, 0), QPointF (0, -1), QPointF (0, 1) })
            g.drawText (box.translated (d), flags, text);
        g.setPen (QColor (235, 235, 235));
        g.drawText (box, flags, text);
    }
}

void WaveformView::drawFrequencyAxis (QPainter& g, double lane)
{
    const auto [low, high] = frequencyRange();
    auto small = font();
    small.setPointSizeF (small.pointSizeF() * 0.75);
    g.setFont (small);
    const auto textHeight = QFontMetricsF (small).height();

    // Round frequencies, from the bottom up, at least two lines apart: on the log and
    // mel scales 1, 2 and 5 times a power of ten; on the linear scale (or a narrow
    // range) even steps of the smallest 1-2-5 size that leaves room for the labels.
    std::vector<double> marks;
    const auto span = high - low;
    const auto even = spectrumSet.scale == spectrum::Scale::linear || high / std::max (low, 1.0) < 4.0;
    if (even)
    {
        double step = 1.0;
        for (double decade = 1.0; decade <= 100000.0 && step <= 1.0; decade *= 10.0)
            for (double m : { 1.0, 2.0, 5.0 })
                if (step <= 1.0 && std::abs (hzToY (low + m * decade) - hzToY (low)) >= textHeight * 2.2)
                    step = m * decade;
        if (step <= 1.0)
            step = std::max (1.0, span / 4.0);
        for (double hz = std::ceil (low / step) * step; hz < high; hz += step)
            if (hz > low)
                marks.push_back (hz);
    }
    else
    {
        for (double decade = 1.0; decade <= 100000.0; decade *= 10.0)
            for (double m : { 1.0, 2.0, 5.0 })
                if (const auto hz = m * decade; hz > low && hz < high)
                    marks.push_back (hz);
    }

    for (int ch = 0; ch < (data ? data->numChannels() : 1); ++ch)
    {
        double lastY = 1.0e9;
        for (const auto hz : marks)
        {
            const auto y = hzToY (hz);
            if (y < textHeight * 2.2 || y > lane - 4.0 || lastY - y < textHeight * 2.0)
                continue;
            lastY = y;
            const auto top = lane * ch + y;
            g.setPen (QColor (255, 255, 255, 90));
            g.drawLine (QPointF (0.0, top), QPointF (5.0, top));
            outlinedText (g, QRectF (8.0, top - textHeight / 2.0, 60.0, textHeight), Qt::AlignLeft | Qt::AlignVCenter, hzText (hz));
        }
    }
    g.setFont (font());
}

void WaveformView::drawReadout (QPainter& g, double lane)
{
    if (! data || hover.x() < 0.0 || ! underMouse() || lane < 1.0)
        return;

    const auto ch = std::clamp ((int) (hover.y() / lane), 0, data->numChannels() - 1);
    const auto y = hover.y() - ch * lane;
    const auto hz = yToHz (y);
    const auto seconds = (double) xToFrame (hover.x()) / data->sampleRate();
    auto text = tr ("%1 s   %2 Hz  %3").arg (seconds, 0, 'f', 3).arg (hz, 0, 'f', hz < 100.0 ? 1 : 0)
                    .arg (QString::fromStdString (spectrum::noteName (hz)));
    if (spectrogramKey == currentKey())
    {
        const auto px = (int) (hover.x() * spectrogram.columns / std::max (1, width()));
        const auto py = (int) (y * spectrogram.rows / lane);
        if (const auto level = spectrogram.levelAt (ch, px, py); level > spectrum::silent)
            text += tr ("   %1 dB").arg (level, 0, 'f', 1);
    }

    auto small = font();
    small.setPointSizeF (small.pointSizeF() * 0.85);
    g.setFont (small);
    const QFontMetricsF metrics (small);
    const QRectF box (width() - metrics.horizontalAdvance (text) - 24.0, (ch + 1) * lane - metrics.height() - 12.0, metrics.horizontalAdvance (text) + 14.0, metrics.height() + 6.0);
    g.setPen (Qt::NoPen);
    g.setBrush (QColor (0, 0, 0, 160));
    g.drawRoundedRect (box, 4.0, 4.0);
    g.setPen (QColor (235, 235, 235));
    g.drawText (box, Qt::AlignCenter, text);
    g.setFont (font());
}

void WaveformView::setFadeHandles (bool shown)
{
    fadeHandles = shown;
    fadeDrag = 0;
    update();
}

QRectF WaveformView::fadeHandle (bool fadeIn) const
{
    constexpr double size = 10.0;
    if (! fadeHandles || ! data || length() == 0 || drawing())
        return {};
    // While dragging, the handle travels with the fade's inner end.
    const auto dragged = fadeDrag == (fadeIn ? 1 : 2);
    const auto x = fadeIn ? frameToX (dragged ? fadeLength : 0) : frameToX (length() - (dragged ? fadeLength : 0));
    if (x < -1.0 || x > width() + 1.0)
        return {};
    const auto left = fadeIn ? std::clamp (x + 3.0, 3.0, width() - size - 3.0) : std::clamp (x - size - 3.0, 3.0, width() - size - 3.0);
    return { left, 20.0, size, size };
}

void WaveformView::drawFades (QPainter& g)
{
    const auto& theme = Theme::current();
    g.setRenderHint (QPainter::Antialiasing);
    for (const bool in : { true, false })
    {
        const auto box = fadeHandle (in);
        if (box.isEmpty())
            continue;
        // Stands out on the waveform: the window colour with an accent edge, filled with the accent while dragged.
        g.setPen (QPen (theme.color (QStringLiteral ("accent")), 1.5));
        g.setBrush (fadeDrag == (in ? 1 : 2) ? theme.color (QStringLiteral ("accent")) : theme.color (QStringLiteral ("window")));
        g.drawRect (box);
    }

    g.setBrush (Qt::NoBrush);
    if (fadeDrag == 0 || fadeLength <= 0)
    {
        g.setRenderHint (QPainter::Antialiasing, false);
        return;
    }

    // The fade's shape over the whole height, and what it is.
    const bool in = fadeDrag == 1;
    const auto from = in ? Frame (0) : length() - fadeLength;
    QPolygonF curve;
    for (int i = 0; i <= 64; ++i)
    {
        const auto t = i / 64.0;
        const auto gain = dsp::curvedFadeGain (in ? t : 1.0 - t, fadeCurve);
        curve << QPointF (frameToX (from + (Frame) std::llround (t * (double) fadeLength)), (1.0 - gain) * (height() - 2.0) + 1.0);
    }
    g.setPen (QPen (theme.color (QStringLiteral ("accent")), 2.0));
    g.setBrush (Qt::NoBrush);
    g.drawPolyline (curve);

    const auto seconds = (double) fadeLength / data->sampleRate();
    const auto text = (in ? tr ("Fade in %1 s") : tr ("Fade out %1 s")).arg (seconds, 0, 'f', 2)
                    + (std::abs (fadeCurve) < 0.02 ? tr (", straight") : tr (", curve %1%2%").arg (fadeCurve > 0 ? QStringLiteral ("+") : QString())
                                                                                 .arg (std::lround (fadeCurve * 100.0)));
    const QFontMetricsF fm (font());
    const auto w = fm.horizontalAdvance (text) + 12.0;
    const auto x = in ? std::clamp (frameToX (fadeLength) + 18.0, 4.0, width() - w - 4.0) : std::clamp (frameToX (length() - fadeLength) - w - 18.0, 4.0, width() - w - 4.0);
    auto panel = theme.color (QStringLiteral ("window"));
    panel.setAlphaF (0.85f);
    g.setPen (Qt::NoPen);
    g.setBrush (panel);
    g.drawRoundedRect (QRectF (x, 34, w, fm.height() + 6), 4, 4);
    g.setPen (theme.color (QStringLiteral ("windowText")));
    g.drawText (QRectF (x, 34, w, fm.height() + 6), Qt::AlignCenter, text);
    g.setBrush (Qt::NoBrush);
    g.setRenderHint (QPainter::Antialiasing, false);
}

void WaveformView::mousePressEvent (QMouseEvent* e)
{
    setFocus (Qt::MouseFocusReason);
    if (! data || e->button() != Qt::LeftButton)
        return;

    for (const bool in : { true, false })
        if (const auto box = fadeHandle (in); ! box.isEmpty() && box.adjusted (-5, -5, 5, 5).contains (e->position()))
        {
            fadeDrag = in ? 1 : 2;
            fadeLength = 0;
            fadeCurve = 0.0;
            fadePress = e->position();
            update();
            return;
        }

    const auto f = xToFrame (e->position().x());
    dragLane = std::clamp ((int) (e->position().y() / laneHeight()), 0, std::max (0, data->numChannels() - 1));

    if (drawing())
    {
        dragAnchor = f;
        adding = (e->modifiers() & Qt::ShiftModifier) && shape;
        const auto inLane = e->position() - QPointF (0.0, dragLane * laneHeight());
        if (currentTool == Tool::lasso)
            lassoPoints = QPolygonF ({ inLane });
        else
        {
            stroke = adding ? std::make_shared<repair::SpectralMask> (*shape) : std::make_shared<repair::SpectralMask> (data->sampleRate());
            lastBrush = e->position();
            brushAt (lastBrush);
        }
        update();
        return;
    }

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
    dragAnchorY = e->position().y() - dragLane * laneHeight();
    Q_EMIT cursorRequested (f);
}

void WaveformView::mouseDoubleClickEvent (QMouseEvent* e)
{
    // Double-click selects the stretch between the markers (or region edges) on either side.
    if (! data || e->button() != Qt::LeftButton || drawing() || fadeDrag != 0)
        return;
    const auto f = xToFrame (e->position().x());
    Frame from = 0, to = length();
    for (const auto& m : markers)
        for (const auto edge : { m.start, m.end })
        {
            if (edge <= f)
                from = std::max (from, edge);
            else
                to = std::min (to, edge);
        }
    Q_EMIT rangeRequested (from, to);
}

void WaveformView::mouseMoveEvent (QMouseEvent* e)
{
    if (fadeDrag != 0)
    {
        const bool in = fadeDrag == 1;
        const auto at = xToFrame (e->position().x());
        fadeLength = std::clamp<Frame> (in ? at : length() - at, 0, length());
        // Up bends it towards a fast (logarithmic) fade, down towards a slow (exponential) one.
        fadeCurve = std::clamp ((fadePress.y() - e->position().y()) / std::max (40.0, height() * 0.4), -1.0, 1.0);
        if (std::abs (fadeCurve) < 0.04)
            fadeCurve = 0.0;
        update();
        Q_EMIT fadeDragged (in, fadeLength, fadeCurve);
        return;
    }

    if (! (e->buttons() & Qt::LeftButton))
    {
        const auto over = [&] (bool in) { const auto b = fadeHandle (in); return ! b.isEmpty() && b.adjusted (-5, -5, 5, 5).contains (e->position()); };
        if (over (true) || over (false))
            QWidget::setCursor (Qt::SizeHorCursor);
        else
            QWidget::setCursor (currentTool == Tool::box ? Qt::ArrowCursor : Qt::CrossCursor);
    }

    if (spectral && ! drawing())
    {
        hover = e->position();
        update();
    }

    if (drawing())
    {
        hover = e->position();
        if (dragAnchor >= 0 && (e->buttons() & Qt::LeftButton))
        {
            if (currentTool == Tool::lasso)
            {
                const auto y = std::clamp (e->position().y() - dragLane * laneHeight(), 0.0, laneHeight());
                lassoPoints << QPointF (std::clamp (e->position().x(), 0.0, (double) width()), y);
            }
            else
            {
                // Dabs close enough together to make a smooth stroke.
                const auto to = e->position();
                const auto distance = std::hypot (to.x() - lastBrush.x(), to.y() - lastBrush.y());
                const auto dabs = std::max (1, (int) std::ceil (distance / std::max (1.0, brushRadius / 3.0)));
                for (int i = 1; i <= dabs; ++i)
                    brushAt (lastBrush + (to - lastBrush) * ((double) i / dabs));
                lastBrush = to;
            }
        }
        update();
        return;
    }

    if (dragAnchor < 0 || ! (e->buttons() & Qt::LeftButton))
        return;

    Q_EMIT selectionRequested (dragAnchor, xToFrame (e->position().x()));

    if (spectral)
    {
        const auto y = std::clamp (e->position().y() - dragLane * laneHeight(), 0.0, laneHeight());
        const auto tall = std::abs (y - dragAnchorY) >= laneHeight() * 0.9;
        if (tall && ! showsAllFrequencies())
            Q_EMIT bandRequested (frequencyRange().first, frequencyRange().second);
        else if (tall)
            Q_EMIT bandRequested (0.0, 0.0);
        else
            Q_EMIT bandRequested (yToHz (std::max (y, dragAnchorY)), yToHz (std::min (y, dragAnchorY)));
    }
}

void WaveformView::mouseReleaseEvent (QMouseEvent*)
{
    if (fadeDrag != 0)
    {
        const bool in = fadeDrag == 1;
        fadeDrag = 0;
        update();
        Q_EMIT fadeReleased (in, fadeLength, fadeCurve);
        return;
    }

    if (drawing() && dragAnchor >= 0 && data)
    {
        if (currentTool == Tool::lasso)
        {
            if (lassoPoints.size() >= 3)
            {
                auto mask = adding ? std::make_shared<repair::SpectralMask> (*shape) : std::make_shared<repair::SpectralMask> (data->sampleRate());
                std::vector<std::pair<Frame, double>> points;
                for (const auto& p : lassoPoints)
                    points.push_back ({ xToFrame (p.x()), yToHz (p.y()) });
                mask->fillPolygon (points);
                if (! mask->empty())
                    Q_EMIT shapeDrawn (std::move (mask), false);
            }
            lassoPoints.clear();
        }
        else if (stroke)
        {
            auto painted = std::move (stroke);
            stroke.reset();
            ++shapeVersion;
            if (! painted->empty())
                Q_EMIT shapeDrawn (std::move (painted), currentTool == Tool::spotHeal);
        }
        update();
    }
    dragAnchor = -1;
}

void WaveformView::wheelEvent (QWheelEvent* e)
{
    if (! data)
        return;

    // Alt turns the wheel sideways on some systems: take whichever way it moved.
    const auto delta = e->angleDelta().y() != 0 ? e->angleDelta().y() : e->angleDelta().x();
    const auto steps = delta / 120.0;

    if (spectral && (e->modifiers() & Qt::AltModifier))
    {
        const auto lane = laneHeight();
        const auto y = std::fmod (e->position().y(), std::max (1.0, lane));
        zoomFrequency (std::pow (1.25, steps), 1.0 - y / std::max (1.0, lane));
    }
    else if (e->modifiers() & Qt::ControlModifier)
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

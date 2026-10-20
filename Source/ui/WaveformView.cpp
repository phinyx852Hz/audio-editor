// SPDX-License-Identifier: AGPL-3.0-only
#include "WaveformView.h"
#include "../core/Dsp.h"
#include "Spectrogram.h"
#include "Theme.h"

#include <QMouseEvent>
#include <QPainter>
#include <QPainterPath>
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
    connect (&pitchWatcher, &QFutureWatcher<std::shared_ptr<pitch::Track>>::finished, this, [this]
    {
        const bool current = pitchData.get() == data.get() && ! pitchCancel->load();
        if (current)
            pitchTrack = pitchWatcher.result();
        else
            pitchFor = nullptr;   // the take changed meanwhile: work it out again
        pitchData.reset();
        update();
    });
}

void WaveformView::setPitchLine (bool on)
{
    pitchShown = on;
    update();
}

void WaveformView::requestPitch()
{
    if (! data || pitchFor == data.get())
        return;
    if (pitchWatcher.isRunning())
    {
        pitchCancel->store (true);   // finishes soon, then asks again
        return;
    }

    pitchFor = data.get();
    pitchData = data;
    pitchTrack.reset();
    pitchCancel = std::make_shared<std::atomic<bool>> (false);
    pitchWatcher.setFuture (QtConcurrent::run ([d = data, cancel = pitchCancel]
    {
        return pitch::track (*d, {}, nullptr, cancel.get());
    }));
}

void WaveformView::setSpectral (bool on)
{
    setBlend (on ? 1.0 : 0.0);
}

void WaveformView::setBlend (double amount)
{
    mix = std::clamp (std::round (amount * 20.0) / 20.0, 0.0, 1.0);   // in steps of 5 %
    spectral = mix >= 0.5;
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

void WaveformView::setAmplitudeZoom (double zoom)
{
    ampZoom = std::clamp (zoom, 1.0, 1000.0);
    update();
}

void WaveformView::zoomVertically (double factor)
{
    if (spectral)
        zoomFrequency (factor);
    else
        setAmplitudeZoom (ampZoom * factor);
}

void WaveformView::resetVerticalZoom()
{
    if (spectral)
        setFrequencyRange (0.0, 0.0);
    else
        setAmplitudeZoom (1.0);
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

void WaveformView::setGrid (const BeatGrid& g, bool shown)
{
    if (g == grid && shown == gridShown)
        return;
    grid = g;
    gridShown = shown;
    update();
}

void WaveformView::drawGrid (QPainter& g)
{
    if (! gridShown || ! grid.isSet() || ! data || viewLength <= 0.0)
        return;
    const auto rate = data->sampleRate();
    const auto pixelsPerBeat = grid.beatSeconds() * rate / viewLength * width();
    const auto pixelsPerBar = pixelsPerBeat * grid.beatsPerBar;
    // Bars always (every 2nd, 4th... bar when they crowd), beats from 8 px apart, sixteenths from 48 px.
    int barStep = 1;
    while (pixelsPerBar * barStep < 8.0 && barStep < 1 << 20)
        barStep *= 2;
    const double step = pixelsPerBeat >= 48.0 ? 0.25 : pixelsPerBeat >= 8.0 ? 1.0 : (double) (grid.beatsPerBar * barStep);

    auto colour = palette().color (QPalette::Text);
    const auto first = std::floor (grid.beatAt (viewStart / rate) / step) * step;
    const auto last = grid.beatAt ((viewStart + viewLength) / rate);
    g.save();
    QFont small = font();
    small.setPointSizeF (std::max (6.0, small.pointSizeF() * 0.8));
    g.setFont (small);
    double lastLabel = -1e9;
    for (auto beat = first; beat <= last + step; beat += step)
    {
        const auto x = frameToX ((Frame) std::llround (grid.secondsAt (beat) * rate));
        const auto inBar = std::fmod (std::fmod (beat, grid.beatsPerBar) + grid.beatsPerBar, grid.beatsPerBar);
        const bool bar = std::abs (inBar) < 1e-6 || std::abs (inBar - grid.beatsPerBar) < 1e-6;
        const bool onBeat = std::abs (beat - std::round (beat)) < 1e-6;
        // Bars stand out (brighter and 2 px), beats less, sixteenths faintly.
        colour.setAlpha (bar ? 170 : onBeat ? 55 : 22);
        g.setPen (QPen (colour, bar ? 2.0 : 1.0));
        g.drawLine (QPointF (x, 0), QPointF (x, height()));
        if (bar && x - lastLabel >= 28.0)
        {
            lastLabel = x;
            const auto number = (long) std::llround (std::floor (beat / grid.beatsPerBar + 1e-6)) + 1;
            colour.setAlpha (150);
            g.setPen (colour);
            g.drawText (QPointF (x + 3, height() - 4), QString::number (number));
        }
    }
    g.restore();
}

void WaveformView::setWarp (std::shared_ptr<const WarpEdit> w, bool editing)
{
    warp = std::move (w);
    warpEditing = editing && warp != nullptr;
    if (! warpEditing)
    {
        warpHover.reset();
        warpDrag.reset();
    }
    update();
}

std::optional<WaveformView::WarpHit> WaveformView::warpHitAt (QPointF at) const
{
    if (! warpEditing || at.y() > warpStrip)
        return std::nullopt;
    const auto x = at.x();
    std::optional<WarpHit> best;
    double distance = 6.0;   // pixels
    for (size_t i = 0; i < warp->markers.size(); ++i)
        if (const auto d = std::abs (frameToX (warp->markers[i].target) - x); d < distance)
        {
            distance = d;
            best = WarpHit { (int) i, warp->markers[i].source, warp->markers[i].target };
        }
    if (best)
        return best;
    for (const auto t : warp->transients)
    {
        const auto target = warp->toTarget (t);
        if (const auto d = std::abs (frameToX (target) - x); d < distance)
        {
            distance = d;
            best = WarpHit { -1, t, target };
        }
    }
    return best;
}

double WaveformView::gridStep() const
{
    if (! grid.isSet() || ! data || width() <= 0)
        return 0.25;
    // The finest grid line at least 10 pixels from the next: sixteenths, beats, bars, then 2, 4, 8... bars;
    // Narrow and Widen Grid move from there (down to 1/64 notes).
    const auto pixelsPerBeat = grid.beatSeconds() * data->sampleRate() / std::max (1e-9, viewLength) * width();
    double step = 0.25;
    while (step * pixelsPerBeat < 10.0 && step < 4096.0)
        step = step < 1.0 ? 1.0 : step < grid.beatsPerBar ? (double) grid.beatsPerBar : step * 2.0;
    for (int i = 0; i < gridDensity; ++i)
        step = step < 1.0 ? step * 2.0 : step < grid.beatsPerBar ? (double) grid.beatsPerBar : step * 2.0;
    for (int i = 0; i > gridDensity; --i)
        step = step > grid.beatsPerBar ? step / 2.0 : step > 1.0 ? 1.0 : std::max (1.0 / 16.0, step / 2.0);
    return step;
}

Frame WaveformView::snapToGrid (Frame f) const
{
    if (! snapGrid || ! grid.isSet() || ! data || width() <= 0)
        return f;
    const auto step = gridStep();
    const auto rate = data->sampleRate();
    return std::clamp<Frame> ((Frame) std::llround (grid.snap ((double) f / rate, step) * rate), 0, length());
}

Frame WaveformView::warpSnap (double x, bool free) const
{
    auto f = std::clamp<Frame> (xToFrame (x), 0, length());
    if (! free && grid.isSet() && data)
        f = (Frame) std::llround (grid.snap ((double) f / data->sampleRate(), 0.25) * data->sampleRate());
    return std::clamp<Frame> (f, 0, length());
}

void WaveformView::drawWarp (QPainter& g)
{
    if (! warpEditing)
        return;
    auto tick = palette().color (QPalette::Text);
    tick.setAlpha (140);
    g.setPen (QPen (tick, 1.0));
    for (const auto t : warp->transients)
    {
        const auto x = frameToX (warp->toTarget (t));
        if (x >= -2.0 && x <= width() + 2.0)
            g.drawLine (QPointF (x, 0), QPointF (x, 9));
    }
    const auto flag = palette().color (QPalette::Highlight);
    for (const auto& m : warp->markers)
    {
        const auto x = frameToX (m.target);
        if (x < -8.0 || x > width() + 8.0)
            continue;
        // A short stem under the flag, so the bar and beat lines stay readable behind many markers.
        g.setPen (QPen (flag, 1.0));
        g.drawLine (QPointF (x, 0), QPointF (x, warpStrip));
        auto stem = flag;
        stem.setAlpha (45);
        g.setPen (QPen (stem, 1.0));
        g.drawLine (QPointF (x, warpStrip), QPointF (x, height()));
        g.setPen (QPen (flag, 1.0));
        g.setBrush (flag);
        g.drawPolygon (QPolygonF ({ QPointF (x, 0), QPointF (x + 7, 0), QPointF (x + 7, 7), QPointF (x, 11) }));
        g.setBrush (Qt::NoBrush);
    }
    // The point under the mouse, and where a drag is taking it.
    if (const auto& at = warpDrag ? warpDrag : warpHover; at)
    {
        auto ghost = flag;
        ghost.setAlpha (170);
        g.setPen (QPen (ghost, 1.0, Qt::DashLine));
        const auto x = frameToX (warpDrag ? warpDragTo : at->target);
        g.drawLine (QPointF (x, 0), QPointF (x, height()));
        if (warpDrag)
        {
            ghost.setAlpha (90);
            g.setPen (QPen (ghost, 1.0, Qt::DotLine));
            g.drawLine (QPointF (frameToX (at->target), 0), QPointF (frameToX (at->target), height()));
        }
    }
}

void WaveformView::clear()
{
    data.reset();
    markers.clear();
    pitchTrack.reset();
    pitchFor = nullptr;
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

void WaveformView::paintBase (QPainter& g, double laneHeight)
{
    const auto pal = palette();
    const auto& theme = Theme::current();
    const auto& d = *data;
    const auto numChannels = d.numChannels();
        // A blend shows both: the spectrogram fades in over the first half, and the waveform
        // over it fades out all the way, so at 50 % both read.
        if (mix > 0.0)
        {
            requestSpectrogram();
            if (! spectrogram.image.isNull())
            {
                // See-through like the rest when the window is: the visualizer behind PHINYX shows through.
                g.setOpacity (Theme::opacity() * Theme::opacity() * std::min (1.0, 2.0 * mix));
                g.drawImage (rect(), spectrogram.image);  // stretched while a fresh one renders
                g.setOpacity (1.0);
            }
            else if (mix >= 1.0)
            {
                g.setPen (pal.color (QPalette::PlaceholderText));
                g.drawText (rect(), Qt::AlignCenter, tr ("Computing spectrogram..."));
            }
        }
        if (mix < 1.0)
        {
            g.setOpacity (mix > 0.0 ? std::min (0.85, 1.0 - mix) : 1.0);
            for (int ch = 0; ch < numChannels; ++ch)
            {
                const auto mid = laneHeight * (ch + 0.5);
                const auto half = laneHeight * 0.45 * ampZoom;

                g.setClipping (false);
                g.setPen (theme.color (QStringLiteral ("centerLine")));
                g.drawLine (QPointF (0, mid), QPointF (width(), mid));
                if (ch > 0)
                    g.drawLine (QPointF (0, laneHeight * ch), QPointF (width(), laneHeight * ch));
                g.setClipRect (QRectF (0.0, laneHeight * ch, width(), laneHeight));   // zoomed in, peaks go past the lane

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
                    g.setClipping (false);
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
            g.setClipping (false);
            g.setOpacity (1.0);
            if (ampZoom > 1.001 && ! spectral)
                drawAmplitudeScale (g, laneHeight);
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
                            QString::fromStdString (dsp::channelName (ch, numChannels, layoutName)));
            g.setFont (font());
        }

}

void WaveformView::paintEvent (QPaintEvent*)
{
    QPainter g (this);
    const auto pal = palette();
    const auto& theme = Theme::current();

    if (! data || data->numChannels() == 0)
    {
        // Over the window's own background (and the visualizer when it is behind PHINYX), not replacing it.
        g.fillRect (rect(), Theme::withOpacity (theme.color (QStringLiteral ("waveformBackground"))));
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

        // The background, the waveform (or spectrogram) and the lane names change only when
        // the view or the audio does; they are kept in a picture so that playback, which
        // redraws for the cursor many times a second, only draws what moves.
        const auto ratio = devicePixelRatioF();
        // Every colour drawn there, so a changed theme or colour is drawn afresh.
        quint64 colours = 0;
        for (const auto* name : { "waveform", "waveformBackground", "centerLine", "levelQuiet", "levelSoft", "levelMedium",
                                  "levelLoud", "levelHot", "levelPeak" })
            colours = colours * 1099511628211ull ^ theme.color (QString::fromLatin1 (name)).rgba();
        colours = colours * 1099511628211ull ^ pal.color (QPalette::PlaceholderText).rgba();
        const BaseKey key { data.get(), viewStart, viewLength, width(), height(), ratio, mix, ampZoom, levelColours, spectral,
                            Theme::opacity(), spectrogram.image.cacheKey(), colours, layoutName };
        if (! (key == baseKey) || base.isNull())
        {
            base = QPixmap ((QSizeF (size()) * ratio).toSize());
            base.setDevicePixelRatio (ratio);
            base.fill (Qt::transparent);
            QPainter b (&base);
            b.fillRect (rect(), Theme::withOpacity (theme.color (QStringLiteral ("waveformBackground"))));
            paintBase (b, laneHeight);
            baseKey = key;
        }
        g.drawPixmap (0, 0, base);

        if (spectral)
        {
            drawFrequencyAxis (g, laneHeight);
            drawShape (g, laneHeight);
        }

        if (spectral && pitchShown)
            drawPitch (g, laneHeight);

        drawGrid (g);

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

        // Where a dragged selection will land.
        if (movePress >= 0 && moving)
        {
            const QRectF ghost (QPointF (frameToX (moveStart), 0.0), QPointF (frameToX (moveStart + selEnd - selStart), height()));
            auto fill = theme.color (QStringLiteral ("accent"));
            fill.setAlphaF (0.18f);
            g.fillRect (ghost, fill);
            g.setPen (QPen (theme.color (QStringLiteral ("accent")), 1.5, Qt::DashLine));
            g.drawRect (ghost.adjusted (0.5, 0.5, -0.5, -0.5));
        }

        drawWarp (g);
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

void WaveformView::drawAmplitudeScale (QPainter& g, double lane)
{
    auto small = font();
    small.setPointSizeF (small.pointSizeF() * 0.75);
    g.setFont (small);
    const auto textHeight = QFontMetricsF (small).height();
    const auto half = lane * 0.45 * ampZoom;

    for (int ch = 0; ch < (data ? data->numChannels() : 1); ++ch)
    {
        const auto mid = lane * (ch + 0.5);
        // Levels every 6 dB from full scale down, labelled above the centre line, where there is room.
        double lastY = -1.0e9;
        for (int db = 0; db >= -96; db -= 6)
        {
            const auto y = mid - std::pow (10.0, db / 20.0) * half;
            if (y < lane * ch + textHeight * 1.2)
                continue;
            if (y > mid - textHeight * 0.6)
                break;
            if (y - lastY < textHeight * 1.6)
                continue;
            lastY = y;
            g.setPen (QColor (255, 255, 255, 40));
            g.drawLine (QPointF (0.0, y), QPointF (width(), y));
            g.drawLine (QPointF (0.0, 2.0 * mid - y), QPointF (width(), 2.0 * mid - y));
            outlinedText (g, QRectF (8.0, y - textHeight / 2.0, 70.0, textHeight), Qt::AlignLeft | Qt::AlignVCenter,
                          db == 0 ? tr ("0 dB") : tr ("%1 dB").arg (db));
        }
    }

    // How far it is zoomed, at the top right.
    const auto text = tr ("Vertical zoom %1x (+%2 dB)").arg (ampZoom, 0, 'f', ampZoom < 10.0 ? 1 : 0).arg (20.0 * std::log10 (ampZoom), 0, 'f', 1);
    const QFontMetricsF metrics (small);
    const QRectF box (width() - metrics.horizontalAdvance (text) - 28.0, 4.0, metrics.horizontalAdvance (text) + 14.0, metrics.height() + 4.0);
    g.setPen (Qt::NoPen);
    g.setBrush (QColor (0, 0, 0, 140));
    g.drawRoundedRect (box, 4.0, 4.0);
    g.setPen (QColor (235, 235, 235));
    g.drawText (box, Qt::AlignCenter, text);
    g.setBrush (Qt::NoBrush);   // shapes drawn after this (the focus frame) would otherwise be filled
    g.setFont (font());
}

void WaveformView::drawPitch (QPainter& g, double lane)
{
    requestPitch();
    if (! pitchTrack)
    {
        if (pitchWatcher.isRunning())
        {
            g.setPen (QColor (235, 235, 235));
            g.drawText (QRectF (0, 0, width() - 8.0, 20.0), Qt::AlignRight | Qt::AlignVCenter, tr ("Tracing pitch..."));
        }
        return;
    }

    // One path per voiced run, split where the pitch stops or the run jumps.
    const auto& points = pitchTrack->points;
    const auto first = (Frame) std::max (0.0, viewStart - pitchTrack->hop);
    const auto last = (Frame) (viewStart + viewLength + pitchTrack->hop);
    auto from = std::lower_bound (points.begin(), points.end(), first, [] (const pitch::Point& p, Frame f) { return p.at < f; });
    const auto pixelsPerPoint = pitchTrack->hop * width() / std::max (1.0, viewLength);
    const auto stride = std::max<std::ptrdiff_t> (1, (std::ptrdiff_t) (0.5 / std::max (1e-9, pixelsPerPoint)));

    QPainterPath path;
    bool open = false;
    Frame previous = 0;
    for (auto it = from; it < points.end() && it->at <= last; it += std::min (stride, points.end() - it))
    {
        if (it->hz <= 0.0f || (open && it->at - previous > pitchTrack->hop * stride * 2))
        {
            open = false;
            if (it->hz <= 0.0f)
                continue;
        }
        const QPointF p (frameToX (it->at), hzToY (it->hz));
        if (open)
            path.lineTo (p);
        else
            path.moveTo (p);
        open = true;
        previous = it->at;
    }

    g.save();
    g.setRenderHint (QPainter::Antialiasing);
    g.setBrush (Qt::NoBrush);
    for (int ch = 0; ch < data->numChannels(); ++ch)
    {
        g.setClipRect (QRectF (0.0, lane * ch, width(), lane));
        const auto lanePath = path.translated (0.0, lane * ch);
        // White with a dark edge, so it reads on every colour map.
        g.setPen (QPen (QColor (0, 0, 0, 200), 4.5, Qt::SolidLine, Qt::RoundCap, Qt::RoundJoin));
        g.drawPath (lanePath);
        g.setPen (QPen (QColor (255, 255, 255), 2.0, Qt::SolidLine, Qt::RoundCap, Qt::RoundJoin));
        g.drawPath (lanePath);
    }
    g.restore();
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
    if (pitchShown && pitchTrack)
        if (const auto sung = pitchTrack->at (xToFrame (hover.x())); sung > 0.0f)
            text += tr ("   pitch %1 Hz %2").arg ((double) sung, 0, 'f', sung < 100.0f ? 1 : 0)
                        .arg (QString::fromStdString (spectrum::noteName (sung)));
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
    g.setBrush (Qt::NoBrush);   // the focus frame drawn after this would otherwise be filled, darkening the whole view
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

    if (const auto hit = warpHitAt (e->position()); hit && ! drawing())
    {
        warpDrag = hit;
        warpDragTo = hit->target;
        warpReanchor = (e->modifiers() & Qt::ShiftModifier) && hit->marker >= 0;
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

    // Grabbing the selection (away from its edges) drags it to a new place; a click without moving places the cursor.
    if (! spectral && selEnd > selStart && f > selStart && f < selEnd
        && std::abs (e->position().x() - frameToX (selStart)) > 5.0 && std::abs (e->position().x() - frameToX (selEnd)) > 5.0)
    {
        movePress = f;
        movePressX = e->position().x();
        moveStart = selStart;
        moving = false;
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
    if (warpEditing)
    {
        // On a tick or flag along the top, a double-click adds or removes a marker;
        // anywhere else it selects the slice between the hits or markers on either side.
        if (const auto hit = warpHitAt (e->position()))
        {
            warpDrag.reset();
            Q_EMIT warpToggled (hit->source);
            return;
        }
        warpDrag.reset();
        Frame from = 0, to = length();
        auto edge = [&] (Frame at)
        {
            if (at <= f)
                from = std::max (from, at);
            else
                to = std::min (to, at);
        };
        for (const auto& m : warp->markers)
            edge (m.target);
        for (const auto t : warp->transients)
            edge (warp->toTarget (t));
        Q_EMIT rangeRequested (from, to);
        return;
    }
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
    if (movePress >= 0 && (e->buttons() & Qt::LeftButton))
    {
        moving = moving || std::abs (e->position().x() - movePressX) > 4.0;
        if (moving)
        {
            const auto start = selStart + xToFrame (e->position().x()) - movePress;
            moveStart = std::max<Frame> (0, snapToGrid (start));
            QWidget::setCursor ((e->modifiers() & (Qt::ControlModifier | Qt::AltModifier)) ? Qt::DragCopyCursor : Qt::ClosedHandCursor);
            update();
        }
        return;
    }
    if (warpDrag)
    {
        warpDragTo = warpSnap (e->position().x(), e->modifiers() & Qt::AltModifier);
        update();
        return;
    }
    if (warpEditing && ! (e->buttons() & Qt::LeftButton))
    {
        const auto hit = warpHitAt (e->position());
        if (hit.has_value() != warpHover.has_value() || (hit && warpHover && hit->source != warpHover->source))
        {
            warpHover = hit;
            update();
        }
        QWidget::setCursor (hit ? Qt::SizeHorCursor : Qt::ArrowCursor);
        if (hit)
            return;
    }
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
        const auto x = e->position().x();
        if (over (true) || over (false))
            QWidget::setCursor (Qt::SizeHorCursor);
        else if (! spectral && ! drawing() && selEnd > selStart && x > frameToX (selStart) + 5.0 && x < frameToX (selEnd) - 5.0)
            QWidget::setCursor (Qt::OpenHandCursor);   // the selection can be dragged
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

void WaveformView::mouseReleaseEvent (QMouseEvent* e)
{
    if (movePress >= 0)
    {
        const auto press = movePress;
        movePress = -1;
        QWidget::setCursor (Qt::ArrowCursor);
        update();
        if (moving && moveStart != selStart)
            Q_EMIT selectionMoved (selStart, selEnd, moveStart, (e->modifiers() & (Qt::ControlModifier | Qt::AltModifier)) != 0);
        else if (! moving)
            Q_EMIT cursorRequested (press);
        moving = false;
        return;
    }
    if (warpDrag)
    {
        const auto hit = *warpDrag;
        const auto to = warpDragTo;
        warpDrag.reset();
        update();
        if (to != hit.target)
            Q_EMIT warpMoved (hit.source, to, hit.marker, warpReanchor);
        return;
    }
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

    if (e->modifiers() & Qt::AltModifier)
    {
        const auto lane = laneHeight();
        const auto y = std::fmod (e->position().y(), std::max (1.0, lane));
        if (spectral)
            zoomFrequency (std::pow (1.25, steps), 1.0 - y / std::max (1.0, lane));
        else
            setAmplitudeZoom (ampZoom * std::pow (1.25, steps));
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

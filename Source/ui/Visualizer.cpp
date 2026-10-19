// SPDX-License-Identifier: AGPL-3.0-only
#include "Visualizer.h"
#include "Theme.h"

#include <QCloseEvent>
#include <QComboBox>
#include <QHBoxLayout>
#include <QKeyEvent>
#include <QLabel>
#include <QLinearGradient>
#include <QPainter>
#include <QPainterPath>
#include <QPushButton>
#include <QRandomGenerator>
#include <QVBoxLayout>
#include <QWheelEvent>

#include <algorithm>
#include <cmath>

namespace
{
    constexpr size_t ringSize = 16384;
    constexpr int numBars = 64, spectrogramRows = 256, spectrogramColumns = 600, waterfallPoints = 160, waterfallDepth = 56;
    constexpr int surfacePoints = 72, surfaceDepth = 64;
    constexpr double lowest = 20.0, highest = 20000.0;

    // Third-octave bands (IEC 61260 nominal centres).
    const std::vector<double> octaveCentres { 20, 25, 31.5, 40, 50, 63, 80, 100, 125, 160, 200, 250, 315, 400, 500, 630, 800,
                                              1000, 1250, 1600, 2000, 2500, 3150, 4000, 5000, 6300, 8000, 10000, 12500, 16000, 20000 };

    QColor colour (const char* key) { return Theme::current().color (QString::fromLatin1 (key)); }

    QColor mix (const QColor& a, const QColor& b, double t)
    {
        t = std::clamp (t, 0.0, 1.0);
        return QColor::fromRgbF ((float) (a.redF() + (b.redF() - a.redF()) * t), (float) (a.greenF() + (b.greenF() - a.greenF()) * t),
                                 (float) (a.blueF() + (b.blueF() - a.blueF()) * t), (float) (a.alphaF() + (b.alphaF() - a.alphaF()) * t));
    }

    QColor withAlpha (QColor c, double a) { c.setAlphaF ((float) std::clamp (a, 0.0, 1.0)); return c; }

    // The artistic views' colours: a sweep of hues starting from the theme's scope colour,
    // turning slowly with `drift` (in turns).
    QColor art (double t, double drift)
    {
        auto hue = colour ("scope").hueF();
        if (hue < 0.0f)
            hue = 0.55f;
        const double h = std::fmod (hue + t * 0.45 + drift + 10.0, 1.0);
        return QColor::fromHsvF ((float) h, 0.78f, 1.0f);
    }

    // A dark-to-bright map (in the manner of inferno) for the spectrogram.
    QRgb heat (double t)
    {
        static const double stops[][3] { { 0, 0, 4 }, { 40, 11, 84 }, { 101, 21, 110 }, { 159, 42, 99 },
                                         { 212, 72, 66 }, { 245, 125, 21 }, { 250, 193, 39 }, { 252, 255, 164 } };
        t = std::clamp (t, 0.0, 1.0) * 7.0;
        const int i = std::min (6, (int) t);
        const double f = t - i;
        return qRgb ((int) (stops[i][0] + (stops[i + 1][0] - stops[i][0]) * f),
                     (int) (stops[i][1] + (stops[i + 1][1] - stops[i][1]) * f),
                     (int) (stops[i][2] + (stops[i + 1][2] - stops[i][2]) * f));
    }

    QString frequencyLabel (double hz)
    {
        return hz >= 1000.0 ? QStringLiteral ("%1k").arg (hz / 1000.0, 0, 'g', 3) : QString::number (hz, 'g', 3);
    }

    QString noteName (double hz)
    {
        if (hz <= 0.0)
            return {};
        static const char* names[] { "C", "C#", "D", "D#", "E", "F", "F#", "G", "G#", "A", "A#", "B" };
        const double midi = 69.0 + 12.0 * std::log2 (hz / 440.0);
        const int nearest = (int) std::lround (midi);
        const int cents = (int) std::lround ((midi - nearest) * 100.0);
        return QStringLiteral ("%1%2 %3%4 ct").arg (QString::fromLatin1 (names[((nearest % 12) + 12) % 12])).arg (nearest / 12 - 1)
                                               .arg (cents >= 0 ? QStringLiteral ("+") : QString()).arg (cents);
    }

    double logPosition (double hz) { return std::log (hz / lowest) / std::log (highest / lowest); }
    double frequencyAt (double position) { return lowest * std::pow (highest / lowest, position); }
}

//==============================================================================
const std::vector<VisualizerView::Mode>& VisualizerView::modes()
{
    static const std::vector<Mode> list {
        { "bars",        QT_TRANSLATE_NOOP ("VisualizerView", "Bars"),              false, QT_TRANSLATE_NOOP ("VisualizerView", "Spectrum bars with falling peaks, in the style of cava") },
        { "radial",      QT_TRANSLATE_NOOP ("VisualizerView", "Radial"),            false, QT_TRANSLATE_NOOP ("VisualizerView", "Bars around a circle that breathes with the bass") },
        { "circleWave",  QT_TRANSLATE_NOOP ("VisualizerView", "Circle Wave"),       false, QT_TRANSLATE_NOOP ("VisualizerView", "The waveform wrapped around a circle, with an afterglow") },
        { "particles",   QT_TRANSLATE_NOOP ("VisualizerView", "Starfield"),         false, QT_TRANSLATE_NOOP ("VisualizerView", "Flying through stars at the speed of the music") },
        { "ribbons",     QT_TRANSLATE_NOOP ("VisualizerView", "Ribbons"),           false, QT_TRANSLATE_NOOP ("VisualizerView", "Layers of the spectrum drifting like an aurora") },
        { "landscape",   QT_TRANSLATE_NOOP ("VisualizerView", "Spectral Landscape"), false, QT_TRANSLATE_NOOP ("VisualizerView", "Gliding over mountains made of the last seconds of sound; drag to turn, wheel to zoom") },
        { "spectrum",    QT_TRANSLATE_NOOP ("VisualizerView", "Spectrum Analyzer"), true,  QT_TRANSLATE_NOOP ("VisualizerView", "FFT (4096 points, Hann) on a log frequency axis, dBFS, peak hold, peak readout with note") },
        { "octaves",     QT_TRANSLATE_NOOP ("VisualizerView", "Third-Octave Bands"), true, QT_TRANSLATE_NOOP ("VisualizerView", "31 bands, 20 Hz to 20 kHz (IEC 61260 centres), dB with peak hold, like a real-time analyzer") },
        { "scope",       QT_TRANSLATE_NOOP ("VisualizerView", "Oscilloscope"),      true,  QT_TRANSLATE_NOOP ("VisualizerView", "Left and right over 40 ms, triggered on a rising zero crossing") },
        { "spectrogram", QT_TRANSLATE_NOOP ("VisualizerView", "Spectrogram"),       true,  QT_TRANSLATE_NOOP ("VisualizerView", "Frequency over time, scrolling, log frequency, -100 to 0 dBFS") },
        { "waterfall",   QT_TRANSLATE_NOOP ("VisualizerView", "Waterfall"),         true,  QT_TRANSLATE_NOOP ("VisualizerView", "Recent spectra stacked in depth, newest in front") },
        { "surface",     QT_TRANSLATE_NOOP ("VisualizerView", "3D Spectrum"),       true,  QT_TRANSLATE_NOOP ("VisualizerView", "Frequency, time and level as a coloured surface, about 4 s deep; drag to turn, wheel to zoom") },
    };
    return list;
}

int VisualizerView::modeIndex (const QString& id)
{
    const auto& list = modes();
    for (size_t i = 0; i < list.size(); ++i)
        if (id == QLatin1String (list[i].id))
            return (int) i;
    return -1;
}

VisualizerView::VisualizerView (QWidget* parent, bool behind) : QWidget (parent), backdrop (behind)
{
    ringLeft.assign (ringSize, 0.0f);
    ringRight.assign (ringSize, 0.0f);
    mono.assign ((size_t) analyzer.size(), 0.0f);
    spectrum.assign ((size_t) analyzer.numBins(), -120.0f);
    smooth = peak = spectrum;
    peakAt.assign (spectrum.size(), 0);
    bars.assign (numBars, 0.0f);
    barPeaks = bars;
    octaves.assign (octaveCentres.size(), -120.0f);
    octavePeaks = octaves;
    spectrogram = QImage (spectrogramColumns, spectrogramRows, QImage::Format_RGB32);
    spectrogram.fill (heat (0.0));
    clock.start();

    auto* random = QRandomGenerator::global();
    stars.resize (400);
    for (auto& s : stars)
        s = { (float) random->bounded (2.0) - 1.0f, (float) random->bounded (2.0) - 1.0f, (float) random->bounded (1.0) + 0.02f };

    setAttribute (Qt::WA_OpaquePaintEvent);
    if (backdrop)
    {
        setAttribute (Qt::WA_TransparentForMouseEvents);
        setFocusPolicy (Qt::NoFocus);
        if (parent != nullptr)
        {
            parent->installEventFilter (this);
            setGeometry (parent->rect());
            lower();
        }
    }
    else
    {
        setFocusPolicy (Qt::StrongFocus);
        setAccessibleName (tr ("Visualizer"));
    }
    setMinimumSize (240, 140);
}

void VisualizerView::setMode (int index)
{
    current = std::clamp (index, 0, (int) modes().size() - 1);
    modeShownAt = clock.elapsed();
    trail = QImage();
    update();
}

void VisualizerView::addSamples (const std::vector<float>& left, const std::vector<float>& right, double sampleRate)
{
    if (sampleRate > 0.0)
        rate = sampleRate;
    const auto n = std::min (left.size(), right.size());
    for (size_t i = 0; i < n; ++i)
    {
        ringLeft[ringPos] = left[i];
        ringRight[ringPos] = right[i];
        ringPos = (ringPos + 1) % ringSize;
    }
    if (n > 0)
        lastSamplesAt = clock.elapsed();
}

void VisualizerView::tick()
{
    analyse();
    update();
}

float VisualizerView::levelAt (double hz) const
{
    const double bin = hz * analyzer.size() / rate;
    const int i = std::clamp ((int) bin, 0, (int) smooth.size() - 2);
    const double f = std::clamp (bin - i, 0.0, 1.0);
    return (float) (smooth[(size_t) i] + (smooth[(size_t) i + 1] - smooth[(size_t) i]) * f);
}

float VisualizerView::xForFrequency (double hz, double left, double width) const
{
    return (float) (left + logPosition (hz) * width);
}

void VisualizerView::analyse()
{
    const auto now = clock.elapsed();
    const double dt = std::clamp ((now - lastTick) / 1000.0, 0.001, 0.2);
    lastTick = now;

    // Nothing playing: let the views fall to silence instead of freezing.
    if (now - lastSamplesAt > 120)
    {
        const auto silent = (size_t) std::min<double> (ringSize, rate * dt);
        for (size_t i = 0; i < silent; ++i)
        {
            ringLeft[ringPos] = ringRight[ringPos] = 0.0f;
            ringPos = (ringPos + 1) % ringSize;
        }
    }

    const auto size = (size_t) analyzer.size();
    for (size_t i = 0; i < size; ++i)
    {
        const auto at = (ringPos + ringSize - size + i) % ringSize;
        mono[i] = 0.5f * (ringLeft[at] + ringRight[at]);
    }
    analyzer.magnitudesDb (mono.data(), spectrum.data());

    for (size_t i = 0; i < spectrum.size(); ++i)
    {
        spectrum[i] = std::max (spectrum[i], -120.0f);
        smooth[i] = std::max (spectrum[i], smooth[i] - (float) (45.0 * dt));
        if (smooth[i] >= peak[i])
        {
            peak[i] = smooth[i];
            peakAt[i] = now;
        }
        else if (now - peakAt[i] > 1500)
            peak[i] = std::max (smooth[i], peak[i] - (float) (24.0 * dt));
    }

    // The bars: the loudest bin in each log-spaced band, falling with gravity.
    const auto bandDb = [this] (double from, double to)
    {
        const int a = std::max (1, (int) std::floor (from * analyzer.size() / rate));
        const int b = std::min ((int) spectrum.size() - 1, (int) std::ceil (to * analyzer.size() / rate));
        float best = -120.0f;
        for (int i = a; i <= b; ++i)
            best = std::max (best, spectrum[(size_t) i]);
        return best;
    };
    for (int i = 0; i < numBars; ++i)
    {
        const double from = 35.0 * std::pow (16000.0 / 35.0, (double) i / numBars);
        const double to = 35.0 * std::pow (16000.0 / 35.0, (double) (i + 1) / numBars);
        const auto value = (float) std::clamp ((bandDb (from, to) + 72.0) / 66.0, 0.0, 1.0);
        bars[(size_t) i] = std::max (value, bars[(size_t) i] - (float) (1.6 * dt));
        barPeaks[(size_t) i] = std::max (bars[(size_t) i], barPeaks[(size_t) i] - (float) (0.35 * dt));
    }

    // Third-octave bands: power summed over each band (a sine reads close to its level).
    for (size_t k = 0; k < octaveCentres.size(); ++k)
    {
        const double from = octaveCentres[k] * std::pow (2.0, -1.0 / 6.0), to = octaveCentres[k] * std::pow (2.0, 1.0 / 6.0);
        const int a = std::max (1, (int) std::ceil (from * analyzer.size() / rate));
        const int b = std::min ((int) spectrum.size() - 1, (int) std::floor (to * analyzer.size() / rate));
        double power = 0.0;
        if (b < a)
            power = std::pow (10.0, spectrum[(size_t) std::clamp ((int) std::lround (octaveCentres[k] * analyzer.size() / rate), 1, (int) spectrum.size() - 1)] / 10.0);
        else
            for (int i = a; i <= b; ++i)
                power += std::pow (10.0, spectrum[(size_t) i] / 10.0) / 1.5;   // a Hann window spreads a sine's power over 1.5 bins
        const auto db = (float) std::max (-120.0, 10.0 * std::log10 (power + 1e-15));
        octaves[k] = std::max (db, octaves[k] - (float) (30.0 * dt));
        octavePeaks[k] = std::max (octaves[k], octavePeaks[k] - (float) (8.0 * dt));
    }

    // Overall level, bass and brightness for the artistic views.
    double sum = 0.0;
    for (size_t i = size - 1024; i < size; ++i)
        sum += (double) mono[i] * mono[i];
    const double rmsDb = 10.0 * std::log10 (sum / 1024.0 + 1e-12);
    level += (float) ((std::clamp ((rmsDb + 50.0) / 50.0, 0.0, 1.0) - level) * std::min (1.0, dt * 8.0));
    double low = 0.0, weighted = 0.0, total = 0.0;
    int lowCount = 0;
    for (size_t i = 1; i < spectrum.size(); ++i)
    {
        const double f = binFrequency ((int) i);
        const double p = std::pow (10.0, spectrum[i] / 10.0);
        if (f < 150.0)
        {
            low += p;
            ++lowCount;
        }
        weighted += std::log (std::max (f, 20.0)) * p;
        total += p;
    }
    const double bassDb = 10.0 * std::log10 (low + 1e-12);
    bass += (float) ((std::clamp ((bassDb + 40.0) / 40.0, 0.0, 1.0) - bass) * std::min (1.0, dt * 10.0));
    const double meanLog = total > 1e-12 ? weighted / total : std::log (100.0);
    centroid += (float) ((std::clamp ((meanLog - std::log (100.0)) / std::log (80.0), 0.0, 1.0) - centroid) * std::min (1.0, dt * 4.0));
    phase += dt * (0.12 + level * 0.5);

    // The spectrogram: one column per tick.
    for (int row = 0; row < spectrogramRows; ++row)
    {
        const double hz = frequencyAt (1.0 - (double) row / (spectrogramRows - 1));
        const double bin = hz * analyzer.size() / rate;
        const int i = std::clamp ((int) bin, 1, (int) spectrum.size() - 2);
        const double db = spectrum[(size_t) i] + (spectrum[(size_t) i + 1] - spectrum[(size_t) i]) * (bin - i);
        spectrogram.setPixel (spectrogramColumn, row, heat ((db + 100.0) / 100.0));
    }
    spectrogramColumn = (spectrogramColumn + 1) % spectrogramColumns;

    // The waterfall's history.
    std::vector<float> line ((size_t) waterfallPoints);
    for (int i = 0; i < waterfallPoints; ++i)
        line[(size_t) i] = (float) std::clamp ((levelAt (frequencyAt ((double) i / (waterfallPoints - 1))) + 90.0) / 90.0, 0.0, 1.0);
    history.push_back (std::move (line));
    while ((int) history.size() > waterfallDepth)
        history.pop_front();

    // The 3D views' history: a row every other tick, peak per log band so narrow tones stand up.
    if (++surfaceTicks % 2 == 0)
    {
        std::vector<float> row ((size_t) surfacePoints);
        for (int i = 0; i < surfacePoints; ++i)
        {
            const double from = frequencyAt ((double) i / surfacePoints), to = frequencyAt ((double) (i + 1) / surfacePoints);
            const int a = std::max (1, (int) std::floor (from * analyzer.size() / rate));
            const int b = std::min ((int) smooth.size() - 1, std::max (a, (int) std::ceil (to * analyzer.size() / rate)));
            float best = -120.0f;
            if (b - a < 2)
            {
                // Narrower than the bins (low down): between the bins, for a smooth slope.
                const double bin = std::sqrt (from * to) * analyzer.size() / rate;
                const int k = std::clamp ((int) bin, 1, (int) spectrum.size() - 2);
                best = (float) (spectrum[(size_t) k] + (spectrum[(size_t) k + 1] - spectrum[(size_t) k]) * (bin - k));
            }
            else
                for (int k = a; k <= b; ++k)
                    best = std::max (best, spectrum[(size_t) k]);
            row[(size_t) i] = (float) std::clamp ((best + 90.0) / 90.0, 0.0, 1.0);
        }
        surface.push_back (std::move (row));
        while ((int) surface.size() > surfaceDepth)
            surface.pop_front();
    }

    // The stars move faster with the music.
    const auto speed = (float) ((0.08 + level * 0.9 + bass * 0.6) * dt);
    auto* random = QRandomGenerator::global();
    for (auto& s : stars)
    {
        s.z -= speed;
        if (s.z <= 0.02f)
            s = { (float) random->bounded (2.0) - 1.0f, (float) random->bounded (2.0) - 1.0f, 1.0f };
    }
}

//==============================================================================
void VisualizerView::drawBackground (QPainter& g)
{
    auto back = colour ("waveformBackground");
    back.setAlpha (255);
    g.fillRect (rect(), back);
}

void VisualizerView::paintEvent (QPaintEvent*)
{
    QPainter g (this);
    g.setRenderHint (QPainter::Antialiasing);
    const auto id = QLatin1String (modes()[(size_t) current].id);

    if (id == QLatin1String ("circleWave"))
        drawCircleWave (g);
    else
    {
        drawBackground (g);
        if (id == QLatin1String ("bars"))             drawBars (g, false);
        else if (id == QLatin1String ("radial"))      drawBars (g, true);
        else if (id == QLatin1String ("particles"))   drawParticles (g);
        else if (id == QLatin1String ("ribbons"))     drawRibbons (g);
        else if (id == QLatin1String ("spectrum"))    drawSpectrum (g);
        else if (id == QLatin1String ("octaves"))     drawOctaves (g);
        else if (id == QLatin1String ("scope"))       drawScope (g);
        else if (id == QLatin1String ("spectrogram")) drawSpectrogram (g);
        else if (id == QLatin1String ("waterfall"))   drawWaterfall (g);
        else if (id == QLatin1String ("surface"))     drawSurface (g, true);
        else if (id == QLatin1String ("landscape"))   drawSurface (g, false);
    }
    if (! backdrop)
        drawTitle (g);
}

void VisualizerView::drawTitle (QPainter& g)
{
    const auto age = clock.elapsed() - modeShownAt;
    if (age > 3000)
        return;
    const double fade = age < 2200 ? 1.0 : 1.0 - (age - 2200) / 800.0;
    const auto& m = modes()[(size_t) current];
    auto font = g.font();
    font.setPointSizeF (font.pointSizeF() * 1.3);
    font.setBold (true);
    g.setFont (font);
    g.setPen (withAlpha (colour ("text"), fade));
    const QRectF box (16, 10, width() - 32, 26);
    g.drawText (box, Qt::AlignLeft | Qt::AlignVCenter,
                QStringLiteral ("%1  ·  %2").arg (tr (m.name), m.science ? tr ("Science") : tr ("Artistic")));
    font.setBold (false);
    font.setPointSizeF (font.pointSizeF() / 1.3);
    g.setFont (font);
    g.setPen (withAlpha (colour ("text"), fade * 0.7));
    g.drawText (QRectF (16, 34, width() - 32, 20), Qt::AlignLeft | Qt::AlignVCenter,
                tr (m.about) + tr ("   (Left/Right: other views, F11: full screen)"));
}

void VisualizerView::drawBars (QPainter& g, bool radial)
{
    const double drift = phase * 0.02;
    const auto shade = [&] (double t) { return art (t, drift); };
    const auto low = shade (0.0), mid = shade (0.5);

    if (! radial)
    {
        const double margin = width() * 0.04, base = height() * 0.78, room = height() * 0.68;
        const double step = (width() - 2.0 * margin) / numBars, barWidth = step * 0.72;
        for (int i = 0; i < numBars; ++i)
        {
            const double x = margin + i * step + (step - barWidth) / 2.0;
            const double h = std::max (2.0, bars[(size_t) i] * room);
            const double across = (double) i / numBars * 0.6;
            QLinearGradient fill (0, base, 0, base - room);
            fill.setColorAt (0.0, art (across, drift));
            fill.setColorAt (1.0, art (across + 0.4, drift));
            g.fillRect (QRectF (x, base - h, barWidth, h), fill);
            // Its reflection on the floor.
            QLinearGradient mirror (0, base, 0, base + room * 0.3);
            mirror.setColorAt (0.0, withAlpha (art (across, drift), 0.35));
            mirror.setColorAt (1.0, withAlpha (art (across, drift), 0.0));
            g.fillRect (QRectF (x, base + 2, barWidth, std::min (h * 0.45, room * 0.3)), mirror);
            g.fillRect (QRectF (x, base - barPeaks[(size_t) i] * room - 3, barWidth, 2.5), art (across + 0.4 * barPeaks[(size_t) i], drift).lighter (130));
        }
        return;
    }

    const QPointF centre (width() / 2.0, height() / 2.0);
    const double size = std::min (width(), height());
    const double inner = size * (0.17 + 0.05 * bass), reach = size * 0.30;
    QRadialGradient glow (centre, inner * 1.6);
    glow.setColorAt (0.0, withAlpha (mid, 0.25 + 0.45 * level));
    glow.setColorAt (1.0, withAlpha (mid, 0.0));
    g.setPen (Qt::NoPen);
    g.setBrush (glow);
    g.drawEllipse (centre, inner * 1.6, inner * 1.6);

    // Mirrored left and right, low notes at the top.
    const double thickness = std::max (2.0, 2.0 * M_PI * inner / (numBars * 2.0) * 0.7);
    for (int side = -1; side <= 1; side += 2)
        for (int i = 0; i < numBars; ++i)
        {
            const double angle = -M_PI / 2.0 + side * (M_PI * (i + 0.5) / numBars) + std::sin (phase) * 0.3;
            const double length = 3.0 + bars[(size_t) i] * reach;
            const QPointF dir (std::cos (angle), std::sin (angle));
            g.setPen (QPen (shade ((double) i / numBars * 0.6 + bars[(size_t) i] * 0.4), thickness, Qt::SolidLine, Qt::RoundCap));
            g.drawLine (centre + dir * inner, centre + dir * (inner + length));
            g.setPen (QPen (withAlpha (shade ((double) i / numBars * 0.6 + barPeaks[(size_t) i] * 0.4), 0.8), thickness * 0.6, Qt::SolidLine, Qt::RoundCap));
            g.drawPoint (centre + dir * (inner + 6.0 + barPeaks[(size_t) i] * reach));
        }
    g.setPen (QPen (withAlpha (low, 0.6), 1.5));
    g.setBrush (Qt::NoBrush);
    g.drawEllipse (centre, inner * 0.92, inner * 0.92);
}

void VisualizerView::drawCircleWave (QPainter& g)
{
    auto back = colour ("waveformBackground");
    back.setAlpha (255);
    if (trail.size() != size())
    {
        trail = QImage (size(), QImage::Format_RGB32);
        trail.fill (back);
    }
    {
        QPainter t (&trail);
        t.setRenderHint (QPainter::Antialiasing);
        back.setAlphaF (0.16f);
        t.fillRect (trail.rect(), back);

        const QPointF centre (width() / 2.0, height() / 2.0);
        const double radius = std::min (width(), height()) * (0.26 + 0.04 * bass);
        constexpr int points = 720;
        const size_t span = 2048;
        for (int channel = 0; channel < 2; ++channel)
        {
            const auto& ring = channel == 0 ? ringLeft : ringRight;
            QPainterPath path;
            for (int k = 0; k <= points; ++k)
            {
                const auto at = (ringPos + ringSize - span + (size_t) (k % points) * span / points) % ringSize;
                const double angle = 2.0 * M_PI * k / points + phase * (channel == 0 ? 1.0 : -1.0);
                const double r = radius * (1.0 + std::clamp ((double) ring[at] * 1.6, -0.9, 0.9) * 0.55) + channel * 6.0;
                const QPointF p (centre.x() + std::cos (angle) * r, centre.y() + std::sin (angle) * r);
                if (k == 0)
                    path.moveTo (p);
                else
                    path.lineTo (p);
            }
            const auto c = art (centroid * 0.5 + channel * 0.3, phase * 0.03);
            t.setPen (QPen (withAlpha (c, 0.9), 1.6 + 2.0 * level));
            t.setBrush (Qt::NoBrush);
            t.drawPath (path);
        }
    }
    g.drawImage (0, 0, trail);
}

void VisualizerView::drawParticles (QPainter& g)
{
    const QPointF centre (width() / 2.0, height() / 2.0);
    const double spread = std::max (width(), height()) * 0.5;
    const auto near = art (0.4, phase * 0.02), far = art (0.0, phase * 0.02);
    g.setPen (Qt::NoPen);
    for (const auto& s : stars)
    {
        const QPointF p = centre + QPointF (s.x / s.z, s.y / s.z) * spread * 0.5;
        if (! rect().contains (p.toPoint()))
            continue;
        const double closeness = 1.0 - s.z;
        const QPointF trailEnd = centre + QPointF (s.x / (s.z + 0.04f + level * 0.08f), s.y / (s.z + 0.04f + level * 0.08f)) * spread * 0.5;
        const auto c = mix (mix (far, near, centroid), art (0.7, phase * 0.02), bass * closeness);
        g.setPen (QPen (withAlpha (c, closeness * 0.8), 1.0 + closeness * 2.0 * (1.0 + bass)));
        g.drawLine (trailEnd, p);
    }
    // A soft pulse in the middle on the beat.
    QRadialGradient glow (centre, std::min (width(), height()) * 0.25);
    glow.setColorAt (0.0, withAlpha (near, 0.35 * bass));
    glow.setColorAt (1.0, withAlpha (near, 0.0));
    g.setPen (Qt::NoPen);
    g.setBrush (glow);
    g.drawEllipse (centre, std::min (width(), height()) * 0.25, std::min (width(), height()) * 0.25);
}

void VisualizerView::drawRibbons (QPainter& g)
{
    const double base = height() * 0.85, room = height() * 0.62;
    constexpr int steps = 48;
    for (int layer = 3; layer >= 0; --layer)
    {
        // Each point is the mean level over a twelfth of the range around it, so the ribbons stay smooth.
        std::vector<QPointF> points;
        for (int k = 0; k <= steps; ++k)
        {
            const double t = (double) k / steps;
            double sum = 0.0;
            for (int j = -4; j <= 4; ++j)
                sum += levelAt (frequencyAt (std::clamp (0.04 + t * 0.9 + layer * 0.015 + j * 0.01, 0.0, 1.0)));
            const double v = std::clamp ((sum / 9.0 + 78.0) / 70.0, 0.0, 1.0);
            const double wave = std::sin (t * 5.0 + phase * (1.4 + layer * 0.5) + layer * 1.7) * height() * (0.03 + 0.03 * bass);
            points.emplace_back (t * width(), base - v * room * (1.0 - layer * 0.16) + wave - layer * height() * 0.05);
        }
        QPainterPath path;
        path.moveTo (points.front());
        for (size_t k = 1; k + 1 < points.size(); ++k)
            path.quadTo (points[k], (points[k] + points[k + 1]) / 2.0);
        path.lineTo (points.back());
        auto fill = path;
        fill.lineTo (width(), height());
        fill.lineTo (0, height());
        fill.closeSubpath();
        const auto c = art (layer * 0.22, phase * 0.02);
        QLinearGradient shade (0, base - room, 0, height());
        shade.setColorAt (0.0, withAlpha (c, 0.5));
        shade.setColorAt (1.0, withAlpha (c, 0.0));
        g.fillPath (fill, shade);
        g.setPen (QPen (withAlpha (c, 0.95), 2.0));
        g.drawPath (path);
    }
}

//==============================================================================
// The scientific views share a plot frame with labelled axes.
namespace
{
    QRectF plotArea (const QWidget& w) { return QRectF (52, 84, w.width() - 70, w.height() - 112); }

    void frequencyGrid (QPainter& g, const QRectF& area, const QColor& text)
    {
        g.setFont (QFont (g.font().family(), 8));
        for (double hz : { 20.0, 50.0, 100.0, 200.0, 500.0, 1000.0, 2000.0, 5000.0, 10000.0, 20000.0 })
        {
            const double x = area.left() + logPosition (hz) * area.width();
            g.setPen (withAlpha (text, 0.18));
            g.drawLine (QPointF (x, area.top()), QPointF (x, area.bottom()));
            g.setPen (withAlpha (text, 0.7));
            g.drawText (QRectF (x - 30, area.bottom() + 4, 60, 14), Qt::AlignCenter, frequencyLabel (hz));
        }
        // Minor lines at 30, 40... 90 of each decade.
        g.setPen (withAlpha (text, 0.07));
        for (double decade = 10.0; decade <= 10000.0; decade *= 10.0)
            for (int k = 2; k <= 9; ++k)
                if (decade * k >= lowest && decade * k <= highest)
                {
                    const double x = area.left() + logPosition (decade * k) * area.width();
                    g.drawLine (QPointF (x, area.top()), QPointF (x, area.bottom()));
                }
        g.setPen (withAlpha (text, 0.6));
        g.drawText (QRectF (2, area.bottom() + 4, area.left() - 22, 14), Qt::AlignRight, QStringLiteral ("Hz"));
    }

    void levelGrid (QPainter& g, const QRectF& area, const QColor& text, double lowDb, double highDb, double step)
    {
        g.setFont (QFont (g.font().family(), 8));
        for (double db = highDb; db >= lowDb - 0.01; db -= step)
        {
            const double y = area.top() + (highDb - db) / (highDb - lowDb) * area.height();
            g.setPen (withAlpha (text, db == 0.0 ? 0.35 : 0.15));
            g.drawLine (QPointF (area.left(), y), QPointF (area.right(), y));
            g.setPen (withAlpha (text, 0.7));
            g.drawText (QRectF (2, y - 7, area.left() - 8, 14), Qt::AlignRight | Qt::AlignVCenter, QString::number (db));
        }
        g.drawText (QRectF (2, area.top() - 22, area.left() + 30, 14), Qt::AlignLeft, QStringLiteral ("dBFS"));
    }
}

void VisualizerView::drawSpectrum (QPainter& g)
{
    const auto area = plotArea (*this);
    const auto text = colour ("text");
    constexpr double top = 0.0, bottom = -100.0;
    frequencyGrid (g, area, text);
    levelGrid (g, area, text, bottom, top, 10.0);

    const auto yFor = [&] (double db) { return area.top() + (top - std::clamp (db, bottom, top)) / (top - bottom) * area.height(); };
    // One point per pixel; where a pixel covers many bins (high up), its loudest.
    const auto curve = [&] (const std::vector<float>& values)
    {
        QPainterPath path;
        const int columns = std::max (2, (int) area.width());
        for (int x = 0; x <= columns; ++x)
        {
            const double f0 = frequencyAt ((double) x / columns), f1 = frequencyAt ((double) (x + 1) / columns);
            const int a = (int) (f0 * analyzer.size() / rate), b = (int) (f1 * analyzer.size() / rate);
            double db;
            if (b > a)
            {
                db = -120.0;
                for (int i = std::max (1, a); i <= std::min (b, (int) values.size() - 1); ++i)
                    db = std::max (db, (double) values[(size_t) i]);
            }
            else
            {
                const double bin = f0 * analyzer.size() / rate;
                const int i = std::clamp ((int) bin, 1, (int) values.size() - 2);
                db = values[(size_t) i] + (values[(size_t) i + 1] - values[(size_t) i]) * (bin - i);
            }
            const QPointF p (area.left() + x * area.width() / columns, yFor (db));
            if (x == 0)
                path.moveTo (p);
            else
                path.lineTo (p);
        }
        return path;
    };

    g.save();
    g.setClipRect (area);
    const auto line = curve (smooth);
    auto fill = line;
    fill.lineTo (area.bottomRight());
    fill.lineTo (area.bottomLeft());
    fill.closeSubpath();
    QLinearGradient shade (0, area.top(), 0, area.bottom());
    shade.setColorAt (0.0, withAlpha (colour ("scope"), 0.45));
    shade.setColorAt (1.0, withAlpha (colour ("scope"), 0.05));
    g.fillPath (fill, shade);
    g.setPen (QPen (colour ("scope"), 1.6));
    g.drawPath (line);
    g.setPen (QPen (withAlpha (colour ("meterPeakHold"), 0.75), 1.0));
    g.drawPath (curve (peak));
    g.restore();

    // The strongest peak, with its note.
    int best = 1;
    for (int i = 1; i < (int) smooth.size(); ++i)
        if (binFrequency (i) >= lowest && smooth[(size_t) i] > smooth[(size_t) best])
            best = i;
    if (smooth[(size_t) best] > -90.0f)
    {
        // Parabolic interpolation between bins for a finer frequency.
        double offset = 0.0;
        if (best > 0 && best + 1 < (int) smooth.size())
        {
            const double a = smooth[(size_t) best - 1], b = smooth[(size_t) best], c = smooth[(size_t) best + 1];
            const double d = a - 2.0 * b + c;
            offset = std::abs (d) > 1e-9 ? std::clamp (0.5 * (a - c) / d, -0.5, 0.5) : 0.0;
        }
        const double hz = (best + offset) * rate / analyzer.size();
        g.setPen (colour ("text"));
        g.setFont (QFont (g.font().family(), 9));
        g.drawText (QRectF (area.right() - 260, area.top() - 22, 260, 16), Qt::AlignRight,
                    tr ("Peak %1 Hz  %2 dB  %3").arg (hz, 0, 'f', hz < 1000.0 ? 1 : 0).arg (smooth[(size_t) best], 0, 'f', 1).arg (noteName (hz)));
        const double x = area.left() + logPosition (std::clamp (hz, lowest, highest)) * area.width();
        g.setPen (QPen (withAlpha (colour ("text"), 0.5), 1.0, Qt::DashLine));
        g.drawLine (QPointF (x, yFor (smooth[(size_t) best]) - 4), QPointF (x, area.top()));
    }
    g.setPen (withAlpha (colour ("text"), 0.6));
    g.setFont (QFont (g.font().family(), 8));
    g.drawText (QRectF (area.left(), area.top() - 22, 400, 14), Qt::AlignLeft,
                tr ("FFT 4096, Hann, %1 Hz bins, peak hold 1.5 s").arg (rate / analyzer.size(), 0, 'f', 1));
}

void VisualizerView::drawOctaves (QPainter& g)
{
    const auto area = plotArea (*this);
    const auto text = colour ("text");
    constexpr double top = 0.0, bottom = -80.0;
    levelGrid (g, area, text, bottom, top, 6.0);
    const auto yFor = [&] (double db) { return area.top() + (top - std::clamp (db, bottom, top)) / (top - bottom) * area.height(); };

    const int n = (int) octaveCentres.size();
    const double step = area.width() / n, barWidth = step * 0.78;
    static const char* labels[] { "20", "25", "31.5", "40", "50", "63", "80", "100", "125", "160", "200", "250", "315", "400", "500",
                                  "630", "800", "1k", "1.25k", "1.6k", "2k", "2.5k", "3.15k", "4k", "5k", "6.3k", "8k", "10k", "12.5k", "16k", "20k" };
    g.setFont (QFont (g.font().family(), 7));
    for (int k = 0; k < n; ++k)
    {
        const double x = area.left() + k * step + (step - barWidth) / 2.0;
        const double db = octaves[(size_t) k];
        const auto c = db > -6.0 ? colour ("meterHigh") : db > -18.0 ? colour ("meterMid") : colour ("meterLow");
        QLinearGradient fill (0, area.bottom(), 0, area.top());
        fill.setColorAt (0.0, withAlpha (c, 0.55));
        fill.setColorAt (1.0, c);
        const double y = yFor (db);
        g.fillRect (QRectF (x, y, barWidth, area.bottom() - y), fill);
        g.fillRect (QRectF (x, yFor (octavePeaks[(size_t) k]) - 1.0, barWidth, 2.0), colour ("meterPeakHold"));
        g.setPen (withAlpha (text, 0.7));
        if (step > 26 || k % 3 == 0)
            g.drawText (QRectF (x - 10, area.bottom() + 4, barWidth + 20, 14), Qt::AlignCenter, QString::fromLatin1 (labels[k]));
    }
    g.setPen (withAlpha (text, 0.6));
    g.setFont (QFont (g.font().family(), 8));
    g.drawText (QRectF (area.left(), area.top() - 22, area.width(), 14), Qt::AlignRight, tr ("1/3 octave, band power, peak hold"));
}

void VisualizerView::drawScope (QPainter& g)
{
    const auto area = plotArea (*this);
    const auto text = colour ("text");
    const auto span = (size_t) std::clamp ((int) (rate * 0.04), 64, (int) ringSize / 4);

    // Grid: ten divisions across, amplitude in quarters.
    g.setFont (QFont (g.font().family(), 8));
    for (int k = 0; k <= 10; ++k)
    {
        const double x = area.left() + area.width() * k / 10.0;
        g.setPen (withAlpha (text, k == 0 || k == 10 ? 0.3 : 0.13));
        g.drawLine (QPointF (x, area.top()), QPointF (x, area.bottom()));
        g.setPen (withAlpha (text, 0.7));
        g.drawText (QRectF (x - 20, area.bottom() + 4, 40, 14), Qt::AlignCenter, QStringLiteral ("%1").arg (k * 4));
    }
    for (int k = -4; k <= 4; ++k)
    {
        const double y = area.center().y() - area.height() / 2.0 * k / 4.0;
        g.setPen (withAlpha (text, k == 0 ? 0.35 : 0.13));
        g.drawLine (QPointF (area.left(), y), QPointF (area.right(), y));
        g.setPen (withAlpha (text, 0.7));
        g.drawText (QRectF (2, y - 7, area.left() - 8, 14), Qt::AlignRight | Qt::AlignVCenter, QString::number (k / 4.0, 'g', 2));
    }
    g.drawText (QRectF (2, area.bottom() + 4, area.left() - 14, 14), Qt::AlignRight, QStringLiteral ("ms"));

    // Trigger: the latest rising zero crossing that leaves a whole span after it.
    const size_t search = std::min (ringSize - span - 2, (size_t) (rate * 0.05));
    size_t start = (ringPos + ringSize - span - 1) % ringSize;
    bool triggered = false;
    for (size_t back = 0; back < search; ++back)
    {
        const auto at = (ringPos + ringSize * 2 - span - 1 - back) % ringSize;
        const auto before = (at + ringSize - 1) % ringSize;
        const float a = ringLeft[before] + ringRight[before], b = ringLeft[at] + ringRight[at];
        if (a < 0.0f && b >= 0.0f)
        {
            start = at;
            triggered = true;
            break;
        }
    }

    g.save();
    g.setClipRect (area);
    for (int channel = 1; channel >= 0; --channel)
    {
        const auto& ring = channel == 0 ? ringLeft : ringRight;
        QPainterPath path;
        for (size_t i = 0; i < span; ++i)
        {
            const double v = ring[(start + i) % ringSize];
            const QPointF p (area.left() + area.width() * i / (span - 1), area.center().y() - std::clamp (v, -1.05, 1.05) * area.height() / 2.0);
            if (i == 0)
                path.moveTo (p);
            else
                path.lineTo (p);
        }
        g.setPen (QPen (channel == 0 ? colour ("scope") : withAlpha (colour ("accent"), 0.8), 1.4));
        g.drawPath (path);
    }
    g.restore();

    g.setPen (colour ("scope"));
    g.drawText (QRectF (area.left(), area.top() - 22, 30, 14), Qt::AlignLeft, QStringLiteral ("L"));
    g.setPen (colour ("accent"));
    g.drawText (QRectF (area.left() + 16, area.top() - 22, 30, 14), Qt::AlignLeft, QStringLiteral ("R"));
    g.setPen (withAlpha (text, 0.6));
    g.drawText (QRectF (area.left(), area.top() - 22, area.width(), 14), Qt::AlignRight,
                triggered ? tr ("40 ms, triggered on a rising zero crossing") : tr ("40 ms, free running"));
}

void VisualizerView::drawSpectrogram (QPainter& g)
{
    const auto area = plotArea (*this);
    const auto text = colour ("text");
    // The newest column at the right edge.
    const double columnWidth = area.width() / spectrogramColumns;
    const int older = spectrogramColumns - spectrogramColumn;
    g.drawImage (QRectF (area.left(), area.top(), older * columnWidth, area.height()), spectrogram,
                 QRectF (spectrogramColumn, 0, older, spectrogramRows));
    if (spectrogramColumn > 0)
        g.drawImage (QRectF (area.left() + older * columnWidth, area.top(), spectrogramColumn * columnWidth, area.height()), spectrogram,
                     QRectF (0, 0, spectrogramColumn, spectrogramRows));

    g.setFont (QFont (g.font().family(), 8));
    for (double hz : { 50.0, 100.0, 200.0, 500.0, 1000.0, 2000.0, 5000.0, 10000.0, 20000.0 })
    {
        const double y = area.bottom() - logPosition (hz) * area.height();
        g.setPen (withAlpha (QColor (Qt::white), 0.18));
        g.drawLine (QPointF (area.left(), y), QPointF (area.right(), y));
        g.setPen (withAlpha (text, 0.7));
        g.drawText (QRectF (2, y - 7, area.left() - 8, 14), Qt::AlignRight | Qt::AlignVCenter, frequencyLabel (hz));
    }
    // Time back from now, at about 33 ms a column.
    const double seconds = spectrogramColumns * 0.033;
    for (int s = 0; s <= (int) seconds; s += 5)
    {
        const double x = area.right() - s / seconds * area.width();
        g.setPen (withAlpha (text, 0.7));
        g.drawText (QRectF (x - 25, area.bottom() + 4, 50, 14), Qt::AlignCenter, s == 0 ? tr ("now") : QStringLiteral ("-%1 s").arg (s));
    }
    // The colour scale.
    const QRectF scale (area.right() - 160, area.top() - 20, 120, 8);
    for (int x = 0; x < (int) scale.width(); ++x)
        g.fillRect (QRectF (scale.left() + x, scale.top(), 1, scale.height()), QColor (heat (x / scale.width())));
    g.setPen (withAlpha (text, 0.7));
    g.drawText (QRectF (scale.left() - 44, scale.top() - 4, 40, 14), Qt::AlignRight, QStringLiteral ("-100"));
    g.drawText (QRectF (scale.right() + 4, scale.top() - 4, 50, 14), Qt::AlignLeft, QStringLiteral ("0 dB"));
    g.drawText (QRectF (2, area.top() - 22, 60, 14), Qt::AlignLeft, QStringLiteral ("Hz"));
}

void VisualizerView::drawWaterfall (QPainter& g)
{
    const auto text = colour ("text");
    const double left = width() * 0.08, right = width() * 0.92, front = height() * 0.88, back = height() * 0.22;
    const double room = height() * 0.30;
    auto background = colour ("waveformBackground");
    background.setAlpha (255);

    const int count = (int) history.size();
    for (int i = 0; i < count; ++i)
    {
        const double depth = count > 1 ? (double) i / (count - 1) : 1.0;   // 0 at the back, 1 in front
        const double scale = 0.55 + 0.45 * depth;
        const double base = back + (front - back) * depth;
        const double middle = (left + right) / 2.0, half = (right - left) / 2.0 * scale;
        const double skew = (1.0 - depth) * width() * 0.06;   // the back leans to the right
        QPainterPath path;
        const auto& line = history[(size_t) i];
        for (int k = 0; k < waterfallPoints; ++k)
        {
            const QPointF p (middle - half + 2.0 * half * k / (waterfallPoints - 1) + skew, base - line[(size_t) k] * room * scale);
            if (k == 0)
                path.moveTo (p);
            else
                path.lineTo (p);
        }
        auto fill = path;
        fill.lineTo (middle + half + skew, base);
        fill.lineTo (middle - half + skew, base);
        fill.closeSubpath();
        g.fillPath (fill, background);   // hides the lines behind it
        const auto c = mix (colour ("scope"), colour ("accent"), depth);
        g.setPen (QPen (withAlpha (c, 0.25 + 0.75 * depth), depth > 0.98 ? 1.8 : 1.0));
        g.drawPath (path);
    }

    g.setFont (QFont (g.font().family(), 8));
    for (double hz : { 50.0, 100.0, 200.0, 500.0, 1000.0, 2000.0, 5000.0, 10000.0, 20000.0 })
    {
        const double x = left + logPosition (hz) * (right - left);
        g.setPen (withAlpha (text, 0.7));
        g.drawText (QRectF (x - 25, front + 6, 50, 14), Qt::AlignCenter, frequencyLabel (hz));
    }
    g.drawText (QRectF (left - 70, front + 6, 40, 14), Qt::AlignRight, QStringLiteral ("Hz"));
    g.drawText (QRectF (right - 200, back - 30, 200, 14), Qt::AlignRight, tr ("older at the back, -90 to 0 dBFS"));
}

//==============================================================================
// The 3D views: the last few seconds as a surface, frequency across, time going
// back, level up, seen through a camera that can be turned and zoomed.
void VisualizerView::drawSurface (QPainter& g, bool science)
{
    const int rows = (int) surface.size();
    if (rows < 2)
        return;

    // World: x -1 (20 Hz) to 1 (20 kHz), z 0 (now) to 2 (oldest), y 0 to 0.8 (level).
    const double yaw = (cameraYaw + (science ? 0.0 : 18.0 * std::sin (phase * 0.35))) * M_PI / 180.0;
    const double pitch = (cameraPitch + (science ? 0.0 : -8.0 + 6.0 * std::sin (phase * 0.23))) * M_PI / 180.0;
    const double cy = std::cos (yaw), sy = std::sin (yaw), cp = std::cos (pitch), sp = std::sin (pitch);
    const double distance = (science ? 4.4 : 3.9) / cameraZoom, focal = std::min ((double) width(), height() * 1.6) * 0.9;
    const QPointF screenCentre (width() / 2.0, height() * (science ? 0.5 : 0.5));
    struct Projected { QPointF p; double depth; };
    const auto project = [&] (double x, double y, double z)
    {
        // Centre the box, turn it, then look at it from in front and above.
        x -= 0.0; z -= 1.0; y -= 0.3;
        const double x1 = x * cy - z * sy, z1 = x * sy + z * cy;
        const double y2 = y * cp + z1 * sp, z2 = z1 * cp - y * sp;   // the camera is above, in front
        const double depth = distance + z2;
        return Projected { QPointF (screenCentre.x() + x1 * focal / depth, screenCentre.y() - y2 * focal / depth), depth };
    };

    const double height3d = science ? 0.8 : 0.65;
    const auto xAt = [] (int i) { return -1.0 + 2.0 * i / (surfacePoints - 1); };
    const auto zAt = [rows] (int r) { return 2.0 * (rows - 1 - r) / (surfaceDepth - 1); };   // newest row at z 0

    // Every point once.
    std::vector<Projected> grid ((size_t) rows * surfacePoints);
    for (int r = 0; r < rows; ++r)
        for (int i = 0; i < surfacePoints; ++i)
            grid[(size_t) (r * surfacePoints + i)] = project (xAt (i), surface[(size_t) r][(size_t) i] * height3d, zAt (r));

    const auto text = colour ("text");
    auto background = colour ("waveformBackground");
    background.setAlpha (255);

    // The floor and walls with their scales, behind the surface.
    if (science)
    {
        g.setFont (QFont (g.font().family(), 8));
        const double back = zAt (0);
        const auto line = [&] (double x0, double y0, double z0, double x1, double y1, double z1)
        {
            g.drawLine (project (x0, y0, z0).p, project (x1, y1, z1).p);
        };
        g.setPen (QPen (withAlpha (text, 0.18), 1.0));
        for (double hz : { 20.0, 50.0, 100.0, 200.0, 500.0, 1000.0, 2000.0, 5000.0, 10000.0, 20000.0 })
        {
            const double x = -1.0 + 2.0 * logPosition (hz);
            line (x, 0, 0, x, 0, back);
            line (x, 0, back, x, height3d, back);
        }
        for (double db = 0.0; db >= -90.0; db -= 15.0)
        {
            const double y = (db + 90.0) / 90.0 * height3d;
            line (-1, y, back, 1, y, back);
            line (-1, y, 0, -1, y, back);
        }
        for (double z = 0.0; z <= back + 1e-6; z += 0.5)
            line (-1, 0, z, 1, 0, z);
        line (-1, 0, 0, -1, height3d, 0);

        // Labels: frequency along the front, time down the side, level up the back.
        g.setPen (withAlpha (text, 0.75));
        for (double hz : { 50.0, 100.0, 200.0, 500.0, 1000.0, 2000.0, 5000.0, 10000.0, 20000.0 })
        {
            const auto p = project (-1.0 + 2.0 * logPosition (hz), 0, -0.08).p;
            g.drawText (QRectF (p.x() - 24, p.y() + 2, 48, 14), Qt::AlignCenter, frequencyLabel (hz));
        }
        const double secondsPerRow = 0.066;
        for (int s = 0; s <= (int) (surfaceDepth * secondsPerRow); ++s)
        {
            const double z = s / secondsPerRow * 2.0 / (surfaceDepth - 1);
            if (z > back + 1e-6)
                break;
            const auto p = project (1.06, 0, z).p;
            g.drawText (QRectF (p.x() + 2, p.y() - 7, 50, 14), Qt::AlignLeft | Qt::AlignVCenter, s == 0 ? tr ("now") : QStringLiteral ("-%1 s").arg (s));
        }
        for (double db = 0.0; db >= -90.0; db -= 15.0)
        {
            const auto p = project (-1.04, (db + 90.0) / 90.0 * height3d, back).p;
            g.drawText (QRectF (p.x() - 44, p.y() - 7, 40, 14), Qt::AlignRight | Qt::AlignVCenter, QString::number (db));
        }
        const auto unit = project (-1.04, height3d * 1.08, back).p;
        g.drawText (QRectF (unit.x() - 44, unit.y() - 7, 40, 14), Qt::AlignRight | Qt::AlignVCenter, QStringLiteral ("dBFS"));
    }

    // The surface's cells, far ones first so near ones cover them.
    struct Cell { int r, i; double depth; };
    std::vector<Cell> cells;
    cells.reserve ((size_t) (rows - 1) * (surfacePoints - 1));
    for (int r = 0; r + 1 < rows; ++r)
        for (int i = 0; i + 1 < surfacePoints; ++i)
        {
            const auto& a = grid[(size_t) (r * surfacePoints + i)];
            const auto& b = grid[(size_t) ((r + 1) * surfacePoints + i + 1)];
            cells.push_back ({ r, i, a.depth + b.depth });
        }
    std::sort (cells.begin(), cells.end(), [] (const Cell& a, const Cell& b) { return a.depth > b.depth; });

    g.setRenderHint (QPainter::Antialiasing, false);
    for (const auto& c : cells)
    {
        const int k = c.r * surfacePoints + c.i;
        const QPointF quad[4] { grid[(size_t) k].p, grid[(size_t) k + 1].p,
                                grid[(size_t) (k + surfacePoints + 1)].p, grid[(size_t) (k + surfacePoints)].p };
        const double v = (surface[(size_t) c.r][(size_t) c.i] + surface[(size_t) c.r][(size_t) c.i + 1]
                          + surface[(size_t) c.r + 1][(size_t) c.i] + surface[(size_t) c.r + 1][(size_t) c.i + 1]) / 4.0;
        if (science)
        {
            const QColor fill (heat (0.08 + 0.92 * v));
            g.setPen (QPen (fill.darker (170), 0.0));
            g.setBrush (fill);
        }
        else
        {
            // Dark ground with glowing ridges: colour from the hue sweep, brighter where it is loud.
            const double age = (double) c.r / (rows - 1);   // 0 oldest, 1 newest
            const auto glow = art ((double) c.i / surfacePoints * 0.55 + v * 0.3, phase * 0.02);
            g.setPen (QPen (withAlpha (glow, 0.15 + 0.85 * age * (0.35 + v)), 0.0));
            g.setBrush (mix (background, glow, 0.08 + v * 0.55 * (0.3 + 0.7 * age)));
        }
        g.drawPolygon (quad, 4);
    }
    g.setRenderHint (QPainter::Antialiasing, true);

    // The newest row drawn on top, as the leading edge.
    QPainterPath edge;
    for (int i = 0; i < surfacePoints; ++i)
    {
        const auto& p = grid[(size_t) ((rows - 1) * surfacePoints + i)].p;
        if (i == 0)
            edge.moveTo (p);
        else
            edge.lineTo (p);
    }
    g.setPen (QPen (science ? withAlpha (colour ("text"), 0.8) : art (0.5, phase * 0.02).lighter (130), science ? 1.2 : 2.0));
    g.setBrush (Qt::NoBrush);
    g.drawPath (edge);
}

void VisualizerView::mousePressEvent (QMouseEvent* e)
{
    dragFrom = e->position();
    QWidget::mousePressEvent (e);
}

void VisualizerView::mouseMoveEvent (QMouseEvent* e)
{
    if ((e->buttons() & Qt::LeftButton) == 0)
        return;
    const auto delta = e->position() - dragFrom;
    dragFrom = e->position();
    cameraYaw = std::clamp (cameraYaw + delta.x() * 0.3, -85.0, 85.0);
    cameraPitch = std::clamp (cameraPitch + delta.y() * 0.25, 5.0, 80.0);
    update();
}

void VisualizerView::wheelEvent (QWheelEvent* e)
{
    cameraZoom = std::clamp (cameraZoom * std::pow (1.1, e->angleDelta().y() / 120.0), 0.5, 2.5);
    update();
}

//==============================================================================
void VisualizerView::keyPressEvent (QKeyEvent* e)
{
    switch (e->key())
    {
        case Qt::Key_Left:
        case Qt::Key_PageUp:
        case Qt::Key_Up:
            Q_EMIT stepMode (-1);
            return;
        case Qt::Key_Right:
        case Qt::Key_PageDown:
        case Qt::Key_Down:
            Q_EMIT stepMode (1);
            return;
        case Qt::Key_F11:
        case Qt::Key_F:
            if (e->modifiers() == Qt::NoModifier)
            {
                Q_EMIT fullScreenRequested();
                return;
            }
            break;
        case Qt::Key_Escape:
            Q_EMIT escapePressed();
            return;
        case Qt::Key_Home:   // the 3D views' camera back where it started
            cameraYaw = -32.0;
            cameraPitch = 28.0;
            cameraZoom = 1.0;
            update();
            return;
        default:
            break;
    }
    QWidget::keyPressEvent (e);
}

void VisualizerView::mouseDoubleClickEvent (QMouseEvent*)
{
    Q_EMIT fullScreenRequested();
}

void VisualizerView::resizeEvent (QResizeEvent*)
{
    trail = QImage();
}

bool VisualizerView::eventFilter (QObject* watched, QEvent* e)
{
    if (backdrop && watched == parentWidget() && (e->type() == QEvent::Resize || e->type() == QEvent::ChildAdded))
    {
        setGeometry (parentWidget()->rect());
        lower();
    }
    return false;
}

//==============================================================================
VisualizerWindow::VisualizerWindow (QWidget* parent) : QWidget (parent, Qt::Dialog)
{
    setWindowTitle (tr ("Visualizer"));
    setObjectName (QStringLiteral ("visualizerWindow"));

    visual = new VisualizerView (this, false);
    picker = new QComboBox (this);
    picker->setAccessibleName (tr ("Visualizer view"));
    picker->setFocusPolicy (Qt::ClickFocus);
    const auto& list = VisualizerView::modes();
    for (bool science : { false, true })
    {
        if (picker->count() > 0)
            picker->insertSeparator (picker->count());
        for (size_t i = 0; i < list.size(); ++i)
            if (list[i].science == science)
            {
                picker->addItem (QStringLiteral ("%1  (%2)").arg (VisualizerView::tr (list[i].name), science ? tr ("science") : tr ("artistic")), (int) i);
                picker->setItemData (picker->count() - 1, VisualizerView::tr (list[i].about), Qt::ToolTipRole);
            }
    }
    connect (picker, &QComboBox::activated, this, [this] (int row)
    {
        Q_EMIT modeChosen (picker->itemData (row).toInt());
        visual->setFocus();
    });

    auto* full = new QPushButton (tr ("Full Screen (F11)"), this);
    full->setFocusPolicy (Qt::NoFocus);
    connect (full, &QPushButton::clicked, this, &VisualizerWindow::toggleFullScreen);
    auto* behind = new QPushButton (tr ("Behind PHINYX (Ctrl+F11)"), this);
    behind->setFocusPolicy (Qt::NoFocus);
    behind->setToolTip (tr ("Shows the visualizer behind the whole PHINYX window; the window opacity sets how much of it you see"));
    connect (behind, &QPushButton::clicked, this, &VisualizerWindow::backdropRequested);

    bar = new QWidget (this);
    auto* row = new QHBoxLayout (bar);
    row->setContentsMargins (8, 6, 8, 6);
    auto* label = new QLabel (tr ("&View:"), bar);
    label->setBuddy (picker);
    row->addWidget (label);
    row->addWidget (picker, 1);
    row->addWidget (full);
    row->addWidget (behind);

    auto* layout = new QVBoxLayout (this);
    layout->setContentsMargins (0, 0, 0, 0);
    layout->setSpacing (0);
    layout->addWidget (bar);
    layout->addWidget (visual, 1);
    resize (960, 560);

    connect (visual, &VisualizerView::fullScreenRequested, this, &VisualizerWindow::toggleFullScreen);
    connect (visual, &VisualizerView::escapePressed, this, [this]
    {
        if (isFullScreen() && ! closeWhenLeavingFullScreen)
            toggleFullScreen();
        else
            close();
    });
}

void VisualizerWindow::syncPicker()
{
    const auto at = picker->findData (visual->mode());
    if (at >= 0)
        picker->setCurrentIndex (at);
}

void VisualizerWindow::toggleFullScreen()
{
    if (isFullScreen())
    {
        closeWhenLeavingFullScreen = false;
        bar->show();
        showNormal();
    }
    else
    {
        bar->hide();
        showFullScreen();
    }
    raise();
    activateWindow();
    visual->setFocus();
}

void VisualizerWindow::closeEvent (QCloseEvent* e)
{
    if (isFullScreen())
    {
        bar->show();
        showNormal();
    }
    closeWhenLeavingFullScreen = false;
    e->accept();
    Q_EMIT closed();
}

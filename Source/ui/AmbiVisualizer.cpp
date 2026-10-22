// SPDX-License-Identifier: AGPL-3.0-only
#include "AmbiVisualizer.h"
#include "Settings.h"
#include "Theme.h"
#include "../core/Ambisonics.h"

#include <QKeyEvent>
#include <QMouseEvent>
#include <QPainter>
#include <QPainterPath>
#include <QRadialGradient>
#include <QWheelEvent>
#include <QtConcurrent>

#include <algorithm>
#include <cmath>
#include <numeric>
#include <random>

namespace
{
    constexpr double pi = 3.14159265358979323846;
    constexpr double toRadians = pi / 180.0;
    constexpr double silent = -200.0;
    constexpr double gridStep = 5.0;
    constexpr int numBuckets = 72;   // the polar plots, 5 degrees each
    constexpr double horizonRing = 0.82;   // Scope: where the horizon is, of the radius

    using Vec = AmbiVisualizer::Vec;

    QColor color (const char* key) { return Theme::current().color (QString::fromLatin1 (key)); }
    QColor faded (QColor c, double alpha) { c.setAlphaF ((float) std::clamp (alpha, 0.0, 1.0)); return c; }
    QColor mix (const QColor& a, const QColor& b, double t)
    {
        t = std::clamp (t, 0.0, 1.0);
        return QColor::fromRgbF ((float) (a.redF() + (b.redF() - a.redF()) * t), (float) (a.greenF() + (b.greenF() - a.greenF()) * t),
                                 (float) (a.blueF() + (b.blueF() - a.blueF()) * t), (float) (a.alphaF() + (b.alphaF() - a.alphaF()) * t));
    }

    Vec dirOf (double azimuth, double elevation)
    {
        const auto a = azimuth * toRadians, e = elevation * toRadians;
        return { std::cos (e) * std::cos (a), -std::cos (e) * std::sin (a), std::sin (e) };
    }
    void anglesOf (const Vec& v, double& azimuth, double& elevation)
    {
        elevation = std::asin (std::clamp (v.z, -1.0, 1.0)) / toRadians;
        azimuth = std::atan2 (-v.y, v.x) / toRadians;
    }
    double dot (const Vec& a, const Vec& b) { return a.x * b.x + a.y * b.y + a.z * b.z; }
    Vec cross (const Vec& a, const Vec& b) { return { a.y * b.z - a.z * b.y, a.z * b.x - a.x * b.z, a.x * b.y - a.y * b.x }; }
    Vec scaled (const Vec& a, double s) { return { a.x * s, a.y * s, a.z * s }; }
    Vec plus (const Vec& a, const Vec& b) { return { a.x + b.x, a.y + b.y, a.z + b.z }; }
    Vec normalised (const Vec& a)
    {
        const auto n = std::sqrt (dot (a, a));
        return n > 1.0e-12 ? scaled (a, 1.0 / n) : Vec { 0.0, 0.0, 1.0 };
    }
    bool same (const Vec& a, const Vec& b) { return std::abs (a.x - b.x) + std::abs (a.y - b.y) + std::abs (a.z - b.z) < 1.0e-12; }
    bool same (double a, double b) { return std::abs (a - b) < 1.0e-9; }

    double wrapAzimuth (double a)
    {
        a = std::fmod (a + 180.0, 360.0);
        return (a < 0.0 ? a + 360.0 : a) - 180.0;
    }

    QString azimuthText (double az)
    {
        const auto a = (int) std::lround (wrapAzimuth (az));
        if (a == 0) return QObject::tr ("front");
        if (std::abs (a) == 180) return QObject::tr ("rear");
        return a > 0 ? QObject::tr ("%1° R").arg (a) : QObject::tr ("%1° L").arg (-a);
    }
    QString elevationText (double el)
    {
        const auto e = (int) std::lround (el);
        if (e == 0) return QObject::tr ("level");
        return e > 0 ? QObject::tr ("%1° up").arg (e) : QObject::tr ("%1° down").arg (-e);
    }
    QString dbText (double db) { return db <= -150.0 ? QStringLiteral ("-inf dB") : QStringLiteral ("%1 dB").arg (db, 0, 'f', 1); }

    // The heat colours (3D Meter's black, blue, green, yellow, orange, red), from the theme.
    struct Palette
    {
        std::array<std::array<float, 4>, 256> entries;   // r, g, b, a (0 to 1)

        Palette()
        {
            const QColor stops[] { color ("levelQuiet"), color ("levelQuiet"), color ("levelSoft"), color ("levelMedium"),
                                   color ("levelLoud"), color ("levelHot"), color ("levelPeak") };
            const double at[] { 0.0, 0.18, 0.36, 0.52, 0.68, 0.84, 1.0 };
            for (int i = 0; i < 256; ++i)
            {
                const auto t = i / 255.0;
                int s = 0;
                while (s < 5 && t > at[s + 1])
                    ++s;
                const auto c = mix (stops[s], stops[s + 1], (t - at[s]) / (at[s + 1] - at[s]));
                const auto alpha = std::clamp (t / 0.16, 0.0, 1.0);
                entries[(size_t) i] = { (float) c.redF(), (float) c.greenF(), (float) c.blueF(), (float) (alpha * alpha * (3.0 - 2.0 * alpha)) };
            }
        }
        const std::array<float, 4>& operator() (double t) const { return entries[(size_t) std::clamp ((int) (t * 255.0), 0, 255)]; }
        QColor colour (double t, double alpha = 1.0) const
        {
            const auto& e = (*this) (t);
            return QColor::fromRgbF (e[0], e[1], e[2], (float) (alpha * e[3]));
        }
    };

    QFont smaller (QFont f, double scale)
    {
        f.setPointSizeF (f.pointSizeF() * scale);
        return f;
    }

    // The fixed views of Four Views: from the listener's place looking left or ahead, and from above.
    const AmbiVisualizer::Projection fromLeft { { 1, 0, 0 }, { 0, 0, 1 }, { 0, 1, 0 }, false };
    const AmbiVisualizer::Projection fromFront { { 0, -1, 0 }, { 0, 0, 1 }, { 1, 0, 0 }, false };
    const AmbiVisualizer::Projection fromAbove { { 0, -1, 0 }, { 1, 0, 0 }, { 0, 0, 1 }, false };

    // The polar plots: where a direction lands, screen right and up (0: above, 1: behind, 2: the right side).
    void inPlane (const Vec& u, int plane, double& across, double& upwards)
    {
        across = plane == 2 ? u.x : -u.y;
        upwards = plane == 0 ? u.x : u.z;
    }

    // 7.1.4 without the LFE (the scene has no low-frequency channel), in PHINYX's channel order.
    const struct { const char* name; double azimuth, elevation; } speakerLayout[] {
        { "L", -30, 0 }, { "R", 30, 0 }, { "C", 0, 0 }, { "Lss", -90, 0 }, { "Rss", 90, 0 }, { "Lrs", -135, 0 }, { "Rrs", 135, 0 },
        { "Ltf", -45, 45 }, { "Rtf", 45, 45 }, { "Ltr", -135, 45 }, { "Rtr", 135, 45 } };
}

//==============================================================================
AmbiVisualizer::AmbiVisualizer (QWidget* parent) : QWidget (parent)
{
    setFocusPolicy (Qt::NoFocus);
    setMouseTracking (true);
    setSizePolicy (QSizePolicy::Expanding, QSizePolicy::Expanding);
    setToolTip (tr ("Drag or use the arrows to turn it, the wheel or Ctrl+Up/Down for depth, Home or double-click to put it back"));

    target.assign ((size_t) (rows * cols), (float) silent);
    shown = target;
    speakerPeak.fill (silent);
    for (int p = 0; p < 3; ++p)
    {
        planeAngle[(size_t) p].resize ((size_t) (rows * cols));
        planeWeight[(size_t) p].resize ((size_t) (rows * cols));
    }
    for (int r = 0; r < rows; ++r)
        for (int c = 0; c < cols; ++c)
        {
            const auto u = dirOf (-180.0 + c * gridStep, 90.0 - r * gridStep);
            const auto d = (size_t) (r * cols + c);
            for (int p = 0; p < 3; ++p)
            {
                double across, upwards;
                inPlane (u, p, across, upwards);
                planeAngle[(size_t) p][d] = (float) std::atan2 (upwards, across);
                planeWeight[(size_t) p][d] = (float) (10.0 * std::log10 (std::max (1.0e-3, across * across + upwards * upwards)));
            }
        }

    auto settings = appSettings();
    const auto key = [] (const char* name) { return QStringLiteral ("analysis/ambi/%1").arg (QLatin1String (name)); };
    current = static_cast<View> (std::clamp (settings.value (key ("view"), 0).toInt(), 0, numViews - 1));
    rangeDb = std::clamp (settings.value (key ("range"), 24.0).toDouble(), 12.0, 96.0);
    relativeScale = settings.value (key ("relative"), true).toBool();
    responseIndex = std::clamp (settings.value (key ("response"), 1).toInt(), 0, 2);
    limit = std::clamp (settings.value (key ("orderLimit"), maxOrder).toInt(), 1, maxOrder);
    showHeight = settings.value (key ("height"), true).toBool();
    cameraAzimuth = settings.value (key ("cameraAzimuth"), 180.0).toDouble();
    cameraElevation = std::clamp (settings.value (key ("cameraElevation"), 28.0).toDouble(), -89.0, 89.0);
    cameraZoom = std::clamp (settings.value (key ("cameraZoom"), 1.0).toDouble(), 0.6, 4.0);
    heading = settings.value (key ("heading"), 0.0).toDouble();
}

void AmbiVisualizer::saveSettings() const
{
    auto settings = appSettings();
    const auto key = [] (const char* name) { return QStringLiteral ("analysis/ambi/%1").arg (QLatin1String (name)); };
    settings.setValue (key ("view"), (int) current);
    settings.setValue (key ("range"), rangeDb);
    settings.setValue (key ("relative"), relativeScale);
    settings.setValue (key ("response"), responseIndex);
    settings.setValue (key ("orderLimit"), limit);
    settings.setValue (key ("height"), showHeight);
    settings.setValue (key ("cameraAzimuth"), cameraAzimuth);
    settings.setValue (key ("cameraElevation"), cameraElevation);
    settings.setValue (key ("cameraZoom"), cameraZoom);
    settings.setValue (key ("heading"), heading);
}

int AmbiVisualizer::orderFor (int channels, int layoutOrder, bool layoutIsAmbisonic, bool assumeAny)
{
    if (layoutOrder > 0)
        return layoutOrder;
    const auto order = (int) std::lround (std::sqrt ((double) channels)) - 1;
    if (order < 1 || (order + 1) * (order + 1) != channels)
        return 0;
    if (layoutIsAmbisonic || assumeAny || order >= 4)
        return std::min (order, maxOrder);
    return 0;
}

QString AmbiVisualizer::viewName (View v)
{
    switch (v)
    {
        case View::sphere:    return tr ("Sphere");
        case View::fourViews: return tr ("Four Views");
        case View::panorama:  return tr ("Panorama");
        case View::head:      return tr ("Head");
        case View::energy:    return tr ("Energy");
        case View::scope:     return tr ("Scope");
        case View::speakers:  return tr ("Speakers");
    }
    return {};
}

void AmbiVisualizer::setView (View v)
{
    if (v == current)
        return;
    current = v;
    scopeTrace.fill (Qt::transparent);
    saveSettings();
    update();
    if (viewChanged)
        viewChanged (v);
}

void AmbiVisualizer::nextView()
{
    setView (static_cast<View> (((int) current + 1) % numViews));
}

void AmbiVisualizer::setRange (double dB)     { rangeDb = dB; saveSettings(); update(); }
void AmbiVisualizer::setRelative (bool on)    { relativeScale = on; saveSettings(); update(); }
void AmbiVisualizer::setResponse (int index)  { responseIndex = std::clamp (index, 0, 2); saveSettings(); }
void AmbiVisualizer::setHeight (bool on)      { showHeight = on; scopeTrace.fill (Qt::transparent); saveSettings(); update(); }
void AmbiVisualizer::setOrderLimit (int o)    { limit = std::clamp (o, 1, maxOrder); saveSettings(); }

void AmbiVisualizer::resetCamera()
{
    cameraAzimuth = 180.0;
    cameraElevation = 28.0;
    cameraZoom = 1.0;
    heading = 0.0;
    scopeTrace.fill (Qt::transparent);
    saveSettings();
    update();
}

void AmbiVisualizer::reset()
{
    std::fill (shown.begin(), shown.end(), (float) silent);
    smoothedTop = silent;
    speakerPeak.fill (silent);
    scopeTrace.fill (Qt::transparent);
    update();
}

//==============================================================================
// A max-rE beam for each direction, scaled so a full-scale plane wave from there reads 0 dB.
void AmbiVisualizer::buildBeams (int newOrder)
{
    beamOrder = newOrder;
    const auto k = (size_t) ambi::channelsFor (newOrder);
    const auto weights = ambi::maxReWeights (newOrder);
    double gain = 0.0;
    for (int l = 0; l <= newOrder; ++l)
        gain += weights[(size_t) l] * (2 * l + 1);
    beams.assign ((size_t) (rows * cols) * k, 0.0f);
    for (int r = 0; r < rows; ++r)
        for (int c = 0; c < cols; ++c)
        {
            const auto y = ambi::harmonics (newOrder, -180.0 + c * gridStep, 90.0 - r * gridStep);
            auto* b = &beams[(size_t) (r * cols + c) * k];
            for (size_t i = 0; i < k; ++i)
            {
                const auto l = (int) std::floor (std::sqrt ((double) i) + 1.0e-9);
                b[i] = (float) (y[i] * weights[(size_t) l] * (2 * l + 1) / gain);
            }
        }
    covariance.assign (k * k, 0.0f);

    // How long the energy vector of one plane wave is at this order, to scale it to 1.
    const auto front = ambi::harmonics (newOrder, 0.0, 0.0);
    Vec sum { 0.0, 0.0, 0.0 };
    double total = 0.0;
    for (int r = 0; r < rows; ++r)
        for (int c = 0; c < cols; ++c)
        {
            const auto* b = &beams[(size_t) (r * cols + c) * k];
            double s = 0.0;
            for (size_t i = 0; i < k; ++i)
                s += b[i] * front[i];
            const auto el = 90.0 - r * gridStep;
            const auto w = s * s * std::cos (el * toRadians);
            sum = plus (sum, scaled (dirOf (-180.0 + c * gridStep, el), w));
            total += w;
        }
    planeWaveSpread = total > 0.0 ? std::max (0.1, std::sqrt (dot (sum, sum)) / total) : 1.0;
}

void AmbiVisualizer::analyse (const std::vector<std::vector<float>>& channels, int fileOrder)
{
    auto o = std::min ({ fileOrder, maxOrder, limit });
    while (o > 0 && ambi::channelsFor (o) > (int) channels.size())
        --o;
    if (o <= 0 || channels.front().empty())
    {
        if (order != 0 || hasSignal)
        {
            order = 0;
            channelCount = 0;
            hasSignal = false;
            std::fill (target.begin(), target.end(), (float) silent);
            update();
        }
        return;
    }
    if (o != beamOrder)
        buildBeams (o);
    order = o;
    fileOrderShown = fileOrder;
    channelCount = (int) channels.size();

    // Above 6th order this is a lot of work: every other call is enough.
    const auto k = (size_t) ambi::channelsFor (o);
    if (k > 49 && (++analyses % 2) != 0)
        return;

    // The covariance of the channels over the block (every few frames is plenty for levels),
    // then each beam's power from it: b' C b.
    const auto frames = channels.front().size();
    const auto stride = std::max<size_t> (1, frames / 384);
    std::fill (covariance.begin(), covariance.end(), 0.0f);
    std::vector<float> s (k);
    int count = 0;
    for (size_t n = 0; n < frames; n += stride, ++count)
    {
        for (size_t i = 0; i < k; ++i)
            s[i] = channels[i][n];
        for (size_t i = 0; i < k; ++i)
        {
            const auto si = s[i];
            auto* row = &covariance[i * k];
            for (size_t j = i; j < k; ++j)
                row[j] += si * s[j];
        }
    }
    const auto scale = 1.0f / (float) std::max (1, count);
    for (size_t i = 0; i < k; ++i)
        for (size_t j = i; j < k; ++j)
        {
            covariance[i * k + j] *= scale;
            covariance[j * k + i] = covariance[i * k + j];
        }

    // The beams' powers, a row of the grid at a time, spread over the cores for the high orders.
    std::vector<int> gridRows ((size_t) rows);
    std::iota (gridRows.begin(), gridRows.end(), 0);
    const auto measureRow = [this, k] (int r)
    {
        std::vector<float> cb (k);
        for (int c = 0; c < cols; ++c)
        {
            const auto d = (size_t) (r * cols + c);
            if ((r == 0 || r == rows - 1) && c > 0)
            {
                target[d] = target[(size_t) (r * cols)];   // the poles: one direction
                continue;
            }
            // C b column by column (C is symmetric), which vectorises, then b . (C b).
            const auto* b = &beams[d * k];
            std::fill (cb.begin(), cb.end(), 0.0f);
            for (size_t j = 0; j < k; ++j)
            {
                const auto bj = b[j];
                const auto* column = &covariance[j * k];
                for (size_t i = 0; i < k; ++i)
                    cb[i] += column[i] * bj;
            }
            double p = 0.0;
            for (size_t i = 0; i < k; ++i)
                p += (double) cb[i] * b[i];
            target[d] = p > 1.0e-20 ? (float) (10.0 * std::log10 (p)) : (float) silent;
        }
    };
    if (k > 25)
        QtConcurrent::blockingMap (gridRows, measureRow);
    else
        std::for_each (gridRows.begin(), gridRows.end(), measureRow);

    // The energy vector from the map.
    Vec sum { 0.0, 0.0, 0.0 };
    double total = 0.0;
    for (int d = 0; d < rows * cols; ++d)
    {
        const auto r = d / cols, c = d % cols;
        const auto el = 90.0 - r * gridStep;
        const auto w = std::pow (10.0, target[(size_t) d] / 10.0) * std::cos (el * toRadians);
        sum = plus (sum, scaled (dirOf (-180.0 + c * gridStep, el), w));
        total += w;
    }
    energy = total > 1.0e-20 ? scaled (sum, 1.0 / (total * planeWaveSpread)) : Vec { 0.0, 0.0, 0.0 };
    if (const auto n = std::sqrt (dot (energy, energy)); n > 1.0)
        energy = scaled (energy, 1.0 / n);

    // Diffuseness from the first order: 1 - |intensity| / energy (0 for one plane wave).
    const double w = covariance[0];
    const double ix = covariance[3], iy = covariance[1], iz = covariance[2];
    const double e = 0.5 * (w + covariance[1 * k + 1] + covariance[2 * k + 2] + covariance[3 * k + 3]);
    diffuseness = e > 1.0e-14 ? std::clamp (1.0 - std::sqrt (ix * ix + iy * iy + iz * iz) / e, 0.0, 1.0) : 1.0;
    hasSignal = w > 1.0e-14;
}

void AmbiVisualizer::step (double dt)
{
    static constexpr double attack[] { 0.02, 0.06, 0.15 }, release[] { 0.2, 0.5, 1.4 };
    const auto up = 1.0 - std::exp (-dt / attack[responseIndex]);
    const auto down = 1.0 - std::exp (-dt / release[responseIndex]);
    float loudest = (float) silent;
    for (size_t d = 0; d < shown.size(); ++d)
    {
        const auto t = order > 0 ? target[d] : (float) silent;
        auto& v = shown[d];
        v += (float) ((t - v) * (t > v ? up : down));
        loudest = std::max (loudest, v);
    }
    smoothedTop += (loudest - smoothedTop) * (loudest > smoothedTop ? up : 1.0 - std::exp (-dt / 1.2));
    const auto e = order > 0 ? energy : Vec { 0.0, 0.0, 0.0 };
    const auto a = 1.0 - std::exp (-dt / (2.0 * release[responseIndex] / 3.0));
    shownEnergy = plus (shownEnergy, scaled (plus (e, scaled (shownEnergy, -1.0)), a));
    shownDiffuseness += ((order > 0 ? diffuseness : 1.0) - shownDiffuseness) * a;
    for (int i = 0; i < numSpeakers; ++i)
    {
        const auto& s = speakerLayout[i];
        const auto level = levelAt (s.azimuth + heading, s.elevation);
        auto& peak = speakerPeak[(size_t) i];
        auto& hold = speakerHold[(size_t) i];
        if (level >= peak)
        {
            peak = level;
            hold = 1.5;   // seconds, then it falls
        }
        else if ((hold -= dt) < 0.0)
            peak = std::max (level, peak - 12.0 * dt);
    }
    if (current == View::scope)
        emitScope (dt);
    update();
}

double AmbiVisualizer::topDb() const
{
    return relativeScale ? std::max (smoothedTop, -100.0) : 0.0;
}

double AmbiVisualizer::levelAt (double azimuth, double elevation) const
{
    const auto fc = (wrapAzimuth (azimuth) + 180.0) / gridStep, fr = std::clamp ((90.0 - elevation) / gridStep, 0.0, rows - 1.0);
    const auto c0 = (int) std::floor (fc), r0 = std::min ((int) std::floor (fr), rows - 2);
    const auto tc = fc - c0, tr = fr - r0;
    const auto at = [this] (int r, int c) { return (double) shown[(size_t) (r * cols + ((c % cols) + cols) % cols)]; };
    return (at (r0, c0) * (1 - tc) + at (r0, c0 + 1) * tc) * (1 - tr) + (at (r0 + 1, c0) * (1 - tc) + at (r0 + 1, c0 + 1) * tc) * tr;
}

//==============================================================================
QRectF AmbiVisualizer::plotArea() const
{
    const auto line = QFontMetricsF (smaller (font(), 0.8)).height();
    return QRectF (rect()).adjusted (4, 4, -4, -(line + 8));
}

std::vector<AmbiVisualizer::Disc> AmbiVisualizer::discs() const
{
    const auto c = dirOf (cameraAzimuth, cameraElevation);
    auto upward = normalised (plus (Vec { 0, 0, 1 }, scaled (c, -c.z)));
    const Projection orbit { cross (upward, c), upward, c, true };

    std::vector<Disc> list;
    const auto plot = plotArea();
    const auto line = QFontMetricsF (smaller (font(), 0.8)).height();
    if (current == View::sphere)
    {
        const auto side = std::min (plot.width(), plot.height()) - 2.0 * line;
        list.push_back ({ QRectF (plot.center().x() - side / 2, plot.center().y() - side / 2, side, side), orbit, cameraZoom, {}, {} });
    }
    else if (current == View::fourViews)
    {
        const auto w = plot.width() / 2.0, h = plot.height() / 2.0;
        const auto side = std::max (10.0, std::min (w, h - line) - 2.0 * line);
        const auto tile = [&] (int i)
        {
            const QPointF centre (plot.left() + w * (i % 2 + 0.5), plot.top() + h * (i / 2) + line + (h - line) / 2.0);
            return QRectF (centre.x() - side / 2, centre.y() - side / 2, side, side);
        };
        list.push_back ({ tile (0), fromLeft, 1.0, tr ("Left"), { tr ("front"), tr ("up"), tr ("rear"), tr ("down") } });
        list.push_back ({ tile (1), fromFront, 1.0, tr ("Front"), { tr ("R"), tr ("up"), tr ("L"), tr ("down") } });
        list.push_back ({ tile (2), fromAbove, 1.0, tr ("Top"), { tr ("R"), tr ("front"), tr ("L"), tr ("rear") } });
        list.push_back ({ tile (3), orbit, cameraZoom, tr ("Perspective"), {} });
    }
    return list;
}

QRectF AmbiVisualizer::panoramaArea() const
{
    const auto plot = plotArea().adjusted (26, 6, -6, -18);
    auto w = plot.width(), h = w / 2.0;
    if (h > plot.height())
    {
        h = plot.height();
        w = h * 2.0;
    }
    return QRectF (plot.center().x() - w / 2, plot.center().y() - h / 2, w, h);
}

// The direction under a point of the widget, in the views that show one.
bool AmbiVisualizer::directionAt (QPointF p, double& azimuth, double& elevation) const
{
    for (const auto& d : discs())
    {
        const auto radius = d.area.width() / 2.0 * d.zoom;
        if (! d.area.contains (p))
            continue;
        const auto sx = (p.x() - d.area.center().x()) / radius, sy = -(p.y() - d.area.center().y()) / radius;
        const auto r2 = sx * sx + sy * sy;
        if (r2 > 1.0)
            return false;
        const auto& pr = d.projection;
        // Through glass the far side is what shows most (the inside, as the listener sees it).
        const auto depth = std::sqrt (1.0 - r2) * (pr.glass ? -1.0 : 1.0);
        anglesOf (plus (plus (scaled (pr.right, sx), scaled (pr.up, sy)), scaled (pr.normal, depth)), azimuth, elevation);
        return true;
    }
    if (current == View::panorama)
    {
        const auto a = panoramaArea();
        const auto x = (p.x() - a.center().x()) / (a.width() / 2.0) * 2.0 * std::sqrt (2.0);
        const auto y = -(p.y() - a.center().y()) / (a.height() / 2.0) * std::sqrt (2.0);
        if (x * x / 8.0 + y * y / 2.0 > 1.0)
            return false;
        const auto z = std::sqrt (1.0 - x * x / 16.0 - y * y / 4.0);
        azimuth = wrapAzimuth (2.0 * std::atan2 (z * x, 2.0 * (2.0 * z * z - 1.0)) / toRadians + heading);
        elevation = std::asin (std::clamp (z * y, -1.0, 1.0)) / toRadians;
        return true;
    }
    if (current == View::scope)
    {
        const auto plot = plotArea();
        const auto radius = std::min (plot.width(), plot.height()) / 2.0 - 4.0;
        const auto dx = (p.x() - plot.center().x()) / radius, dy = -(p.y() - plot.center().y()) / radius;
        const auto r = std::sqrt (dx * dx + dy * dy);
        if (r > 1.0 || ! showHeight)
            return false;
        azimuth = wrapAzimuth (std::atan2 (dx, dy) / toRadians + heading);
        elevation = r <= horizonRing ? 90.0 * (1.0 - r / horizonRing) : -90.0 * (r - horizonRing) / (1.0 - horizonRing);
        return true;
    }
    return false;
}

//==============================================================================
void AmbiVisualizer::paintEvent (QPaintEvent*)
{
    QPainter g (this);
    g.setRenderHint (QPainter::Antialiasing);
    g.setPen (Qt::NoPen);
    g.setBrush (Theme::withOpacity (color ("meterBackground")));
    g.drawRoundedRect (QRectF (rect()), 4.0, 4.0);

    switch (current)
    {
        case View::sphere:
        case View::fourViews:
        {
            int i = 0;
            for (const auto& d : discs())
                paintDisc (g, d, i++);
            break;
        }
        case View::panorama: paintPanorama (g); break;
        case View::head:     paintHead (g); break;
        case View::energy:   paintEnergy (g); break;
        case View::scope:    paintScope (g); break;
        case View::speakers: paintSpeakers (g); break;
    }
    paintReadout (g, QRectF (rect()).adjusted (8, plotArea().bottom() + 2, -8, -2));
    if (order == 0)
        paintHint (g);
}

// A disc of the sphere, drawn pixel by pixel from the levels, then its grid and labels.
void AmbiVisualizer::paintDisc (QPainter& g, const Disc& d, int index)
{
    const Palette palette;
    const auto ratio = devicePixelRatioF();
    const auto radius = d.area.width() / 2.0 * d.zoom;
    const auto centre = d.area.center();
    // What is drawn: the disc, cut to the widget when zoomed in.
    const auto visible = QRectF (centre.x() - radius, centre.y() - radius, 2 * radius, 2 * radius).intersected (plotArea());
    if (visible.width() < 4 || visible.height() < 4)
        return;
    const auto detail = std::min (1.0, 300.0 / std::max (visible.width(), visible.height())) * ratio;
    const QSize size ((int) std::ceil (visible.width() * detail), (int) std::ceil (visible.height() * detail));

    // Where each pixel looks, kept until the view, the size or the camera changes.
    if ((int) caches.size() <= index)
        caches.resize ((size_t) index + 1);
    auto& cache = caches[(size_t) index];
    if (cache.size != size || cache.visible != visible || ! same (cache.radius, radius) || ! same (cache.projection.right, d.projection.right)
        || ! same (cache.projection.up, d.projection.up) || ! same (cache.projection.normal, d.projection.normal))
    {
        cache.size = size;
        cache.visible = visible;
        cache.radius = radius;
        cache.projection = d.projection;
        const auto n = (size_t) size.width() * (size_t) size.height();
        cache.nearC.assign (n, -1.0f);
        cache.nearR.assign (n, 0.0f);
        cache.farC.assign (n, 0.0f);
        cache.farR.assign (n, 0.0f);
        cache.shade.assign (n, 0.0f);
        const auto& pr = d.projection;
        for (int y = 0; y < size.height(); ++y)
            for (int x = 0; x < size.width(); ++x)
            {
                const auto px = visible.left() + (x + 0.5) / detail, py = visible.top() + (y + 0.5) / detail;
                const auto sx = (px - centre.x()) / radius, sy = -(py - centre.y()) / radius;
                const auto r2 = sx * sx + sy * sy;
                if (r2 > 1.0)
                    continue;
                const auto h = std::sqrt (1.0 - r2);
                const auto flat = plus (scaled (pr.right, sx), scaled (pr.up, sy));
                const auto i = (size_t) y * (size_t) size.width() + (size_t) x;
                double az, el;
                anglesOf (plus (flat, scaled (pr.normal, h)), az, el);
                cache.nearC[i] = (float) ((az + 180.0) / gridStep);
                cache.nearR[i] = (float) ((90.0 - el) / gridStep);
                anglesOf (plus (flat, scaled (pr.normal, -h)), az, el);
                cache.farC[i] = (float) ((az + 180.0) / gridStep);
                cache.farR[i] = (float) ((90.0 - el) / gridStep);
                cache.shade[i] = (float) h;
            }
    }

    const auto floor = floorDb();
    const auto range = rangeDb;
    const auto sample = [this, floor, range] (float fc, float fr)
    {
        const auto c0 = (int) std::floor (fc), r0 = std::min ((int) std::floor (fr), rows - 2);
        const auto tc = fc - (float) c0, tr = fr - (float) r0;
        const auto at = [this] (int r, int c) { return shown[(size_t) (r * cols + ((c % cols) + cols) % cols)]; };
        const auto v = (at (r0, c0) * (1 - tc) + at (r0, c0 + 1) * tc) * (1 - tr) + (at (r0 + 1, c0) * (1 - tc) + at (r0 + 1, c0 + 1) * tc) * tr;
        return (v - floor) / range;
    };
    const auto base = mix (color ("meterBackground"), color ("mid"), 0.22);
    const float br = (float) base.redF(), bg = (float) base.greenF(), bb = (float) base.blueF();

    if (pixels.size() != size)
        pixels = QImage (size, QImage::Format_ARGB32);
    for (int y = 0; y < size.height(); ++y)
    {
        auto* line = reinterpret_cast<QRgb*> (pixels.scanLine (y));
        for (int x = 0; x < size.width(); ++x)
        {
            const auto i = (size_t) y * (size_t) size.width() + (size_t) x;
            if (cache.nearC[i] < 0.0f)
            {
                line[x] = 0;
                continue;
            }
            const auto shade = cache.shade[i];
            float r = br * (0.7f + 0.3f * shade), gr = bg * (0.7f + 0.3f * shade), b = bb * (0.7f + 0.3f * shade);
            const auto blend = [&] (const std::array<float, 4>& c, float alpha)
            {
                r += (c[0] - r) * alpha;
                gr += (c[1] - gr) * alpha;
                b += (c[2] - b) * alpha;
            };
            if (d.projection.glass)
            {
                const auto& far = palette (sample (cache.farC[i], cache.farR[i]));
                blend (far, far[3] * 0.92f);   // the inside of the far side, as the listener sees it
                blend ({ br, bg, bb, 1.0f }, 0.18f * shade);   // and the glass of the near side over it
            }
            const auto t = sample (cache.nearC[i], cache.nearR[i]);
            const auto& near = palette (t);
            blend (near, near[3] * (0.72f + 0.28f * shade) * (d.projection.glass ? 0.34f : 1.0f));
            line[x] = qRgb ((int) (std::clamp (r, 0.0f, 1.0f) * 255.0f), (int) (std::clamp (gr, 0.0f, 1.0f) * 255.0f), (int) (std::clamp (b, 0.0f, 1.0f) * 255.0f));
            if (shade < 0.08f)   // a soft edge
                line[x] = (line[x] & 0x00ffffffu) | ((QRgb) (std::clamp (shade / 0.08f, 0.0f, 1.0f) * 255.0f) << 24);
        }
    }

    g.save();
    g.setClipRect (plotArea());
    g.setRenderHint (QPainter::SmoothPixmapTransform);
    g.drawImage (visible, pixels);

    // Grid: every 30 degrees of azimuth and elevation, the horizon and the front stronger.
    const auto& pr = d.projection;
    const auto project = [&] (const Vec& u, double& depth)
    {
        depth = dot (u, pr.normal);
        return QPointF (centre.x() + dot (u, pr.right) * radius, centre.y() - dot (u, pr.up) * radius);
    };
    const auto curve = [&] (auto direction, int steps, const QPen& nearPen, const QPen& farPen)
    {
        QPainterPath front, back;
        bool wasNear = false, started = false;
        for (int s = 0; s <= steps; ++s)
        {
            double depth;
            const auto p = project (direction (s / (double) steps), depth);
            const bool isNear = depth >= 0.0;
            auto& path = isNear ? front : back;
            if (! started || isNear != wasNear)
                path.moveTo (p);
            else
                path.lineTo (p);
            started = true;
            wasNear = isNear;
        }
        g.strokePath (front, nearPen);
        if (pr.glass)
            g.strokePath (back, farPen);
    };
    const auto gridPen = [] (double alpha, double width = 0.8) { return QPen (faded (color ("windowText"), alpha), width); };
    for (int el = -60; el <= 60; el += 30)
        curve ([el] (double t) { return dirOf (-180.0 + 360.0 * t, el); }, 120, gridPen (el == 0 ? 0.42 : 0.16, el == 0 ? 1.1 : 0.8), gridPen (el == 0 ? 0.3 : 0.12));
    for (int az = -180; az < 180; az += 30)
        curve ([az] (double t) { return dirOf (az, -90.0 + 180.0 * t); }, 72, gridPen (az == 0 ? 0.42 : 0.16, az == 0 ? 1.1 : 0.8), gridPen (az == 0 ? 0.3 : 0.12));

    // Where front, back, left, right, up and down are.
    g.setFont (smaller (font(), 0.72));
    const struct { Vec u; const char* name; } axes[] { { { 1, 0, 0 }, "F" }, { { -1, 0, 0 }, "B" }, { { 0, 1, 0 }, "L" },
                                                       { { 0, -1, 0 }, "R" }, { { 0, 0, 1 }, "U" }, { { 0, 0, -1 }, "D" } };
    if (pr.glass)
        for (const auto& a : axes)
        {
            double depth;
            const auto p = project (a.u, depth);
            const auto alpha = depth >= 0.0 ? 0.6 : 0.95;
            g.setPen (Qt::NoPen);
            g.setBrush (faded (color ("windowText"), alpha * 0.8));
            g.drawEllipse (p, 2.2, 2.2);
            g.setPen (faded (color ("windowText"), alpha));
            g.drawText (QRectF (p.x() + 3, p.y() - 13, 20, 12), Qt::AlignLeft | Qt::AlignVCenter, QString::fromLatin1 (a.name));
        }

    // The energy direction: a ring where the sound points, smaller as it gets more diffuse.
    if (order > 0 && hasSignal)
    {
        const auto len = std::sqrt (dot (shownEnergy, shownEnergy));
        if (len > 0.05)
        {
            double depth;
            const auto p = project (normalised (shownEnergy), depth);
            if (depth >= 0.0 || pr.glass)
            {
                g.setBrush (Qt::NoBrush);
                g.setPen (QPen (faded (color ("windowText"), depth >= 0.0 && pr.glass ? 0.5 : 0.95), 1.4));
                g.drawEllipse (p, 4.0 + 6.0 * len, 4.0 + 6.0 * len);
            }
        }
    }
    g.restore();

    g.setBrush (Qt::NoBrush);
    g.setPen (QPen (faded (color ("mid"), 0.9), 1.0));
    g.save();
    g.setClipRect (plotArea());
    g.drawEllipse (centre, radius, radius);
    g.restore();

    g.setPen (faded (color ("placeholderText"), 1.0));
    g.setFont (smaller (font(), 0.75));
    const auto fh = g.fontMetrics().height();
    if (! d.title.isEmpty())
        g.drawText (QRectF (d.area.left() - 2.0 * fh, d.area.top() - fh - 1, d.area.width() / 2.0, fh), Qt::AlignLeft | Qt::AlignVCenter, d.title.toUpper());
    if (! d.edges[0].isEmpty())
    {
        g.drawText (QRectF (d.area.right() + 2, centre.y() - fh / 2.0, 60, fh), Qt::AlignLeft | Qt::AlignVCenter, d.edges[0]);
        g.drawText (QRectF (centre.x() - 40, d.area.top() - fh - 1, 80, fh), Qt::AlignCenter, d.edges[1]);
        g.drawText (QRectF (d.area.left() - 62, centre.y() - fh / 2.0, 60, fh), Qt::AlignRight | Qt::AlignVCenter, d.edges[2]);
        g.drawText (QRectF (centre.x() - 40, d.area.bottom() + 1, 80, fh), Qt::AlignCenter, d.edges[3]);
    }
}

// The whole sphere on an equal-area map, front in the middle, the left on the left.
void AmbiVisualizer::paintPanorama (QPainter& g)
{
    const Palette palette;
    const auto area = panoramaArea();
    if (area.width() < 20)
        return;
    const auto ratio = devicePixelRatioF();
    const auto detail = std::min (1.0, 520.0 / area.width()) * ratio;
    const QSize size ((int) std::ceil (area.width() * detail), (int) std::ceil (area.height() * detail));
    if ((int) caches.size() < 1)
        caches.resize (1);
    auto& cache = caches[0];
    if (cache.size != size || cache.visible != area || ! same (cache.radius, -1.0))
    {
        cache.size = size;
        cache.visible = area;
        cache.radius = -1.0;   // marks it as the map's
        cache.projection = {};
        const auto n = (size_t) size.width() * (size_t) size.height();
        cache.nearC.assign (n, -1.0f);
        cache.nearR.assign (n, 0.0f);
        for (int y = 0; y < size.height(); ++y)
            for (int x = 0; x < size.width(); ++x)
            {
                const auto hx = ((x + 0.5) / size.width() * 2.0 - 1.0) * 2.0 * std::sqrt (2.0);
                const auto hy = -((y + 0.5) / size.height() * 2.0 - 1.0) * std::sqrt (2.0);
                if (hx * hx / 8.0 + hy * hy / 2.0 > 1.0)
                    continue;
                const auto z = std::sqrt (1.0 - hx * hx / 16.0 - hy * hy / 4.0);
                const auto az = 2.0 * std::atan2 (z * hx, 2.0 * (2.0 * z * z - 1.0)) / toRadians;
                const auto el = std::asin (std::clamp (z * hy, -1.0, 1.0)) / toRadians;
                const auto i = (size_t) y * (size_t) size.width() + (size_t) x;
                cache.nearC[i] = (float) ((az + 180.0) / gridStep);
                cache.nearR[i] = (float) ((90.0 - el) / gridStep);
            }
    }

    const auto floor = floorDb();
    const auto shift = (float) (heading / gridStep);
    const auto base = mix (color ("meterBackground"), color ("mid"), 0.2);
    if (pixels.size() != size)
        pixels = QImage (size, QImage::Format_ARGB32);
    for (int y = 0; y < size.height(); ++y)
    {
        auto* line = reinterpret_cast<QRgb*> (pixels.scanLine (y));
        for (int x = 0; x < size.width(); ++x)
        {
            const auto i = (size_t) y * (size_t) size.width() + (size_t) x;
            if (cache.nearC[i] < 0.0f)
            {
                line[x] = 0;
                continue;
            }
            const auto fc = cache.nearC[i] + shift, fr = cache.nearR[i];
            const auto c0 = (int) std::floor (fc), r0 = std::min ((int) std::floor (fr), rows - 2);
            const auto tc = fc - (float) c0, tr = fr - (float) r0;
            const auto at = [this] (int r, int c) { return shown[(size_t) (r * cols + ((c % cols) + cols) % cols)]; };
            const auto v = (at (r0, c0) * (1 - tc) + at (r0, c0 + 1) * tc) * (1 - tr) + (at (r0 + 1, c0) * (1 - tc) + at (r0 + 1, c0 + 1) * tc) * tr;
            const auto& c = palette ((v - floor) / rangeDb);
            const auto r = base.redF() + (c[0] - base.redF()) * c[3], gr = base.greenF() + (c[1] - base.greenF()) * c[3],
                       b = base.blueF() + (c[2] - base.blueF()) * c[3];
            line[x] = qRgb ((int) (r * 255.0), (int) (gr * 255.0), (int) (b * 255.0));
        }
    }
    g.setRenderHint (QPainter::SmoothPixmapTransform);
    g.drawImage (area, pixels);

    // Grid and labels.
    const auto hammer = [&] (double lon, double lat)
    {
        const auto l = lon * toRadians, p = lat * toRadians;
        const auto k = std::sqrt (1.0 + std::cos (p) * std::cos (l / 2.0));
        const auto x = 2.0 * std::sqrt (2.0) * std::cos (p) * std::sin (l / 2.0) / k, y = std::sqrt (2.0) * std::sin (p) / k;
        return QPointF (area.center().x() + x / (2.0 * std::sqrt (2.0)) * area.width() / 2.0, area.center().y() - y / std::sqrt (2.0) * area.height() / 2.0);
    };
    g.setBrush (Qt::NoBrush);
    for (int lat = -60; lat <= 60; lat += 30)
    {
        QPainterPath path;
        for (int lon = -180; lon <= 180; lon += 5)
            lon == -180 ? path.moveTo (hammer (lon, lat)) : path.lineTo (hammer (lon, lat));
        g.setPen (QPen (faded (color ("windowText"), lat == 0 ? 0.4 : 0.15), lat == 0 ? 1.1 : 0.8));
        g.drawPath (path);
    }
    for (int lon = -150; lon <= 150; lon += 30)
    {
        QPainterPath path;
        for (int lat = -90; lat <= 90; lat += 5)
            lat == -90 ? path.moveTo (hammer (lon, lat)) : path.lineTo (hammer (lon, lat));
        g.setPen (QPen (faded (color ("windowText"), lon == 0 ? 0.4 : 0.15), lon == 0 ? 1.1 : 0.8));
        g.drawPath (path);
    }
    g.setPen (QPen (faded (color ("mid"), 0.9), 1.0));
    g.drawEllipse (area);

    g.setFont (smaller (font(), 0.72));
    g.setPen (color ("placeholderText"));
    const auto fh = g.fontMetrics().height();
    for (int lon = -180; lon <= 180; lon += 45)
    {
        const auto p = hammer (lon, 0.0);
        const Qt::Alignment anchor = lon == -180 ? Qt::AlignLeft : lon == 180 ? Qt::AlignRight : Qt::AlignHCenter;
        const auto x = lon == -180 ? p.x() : lon == 180 ? p.x() - 60 : p.x() - 30;
        g.drawText (QRectF (x, area.bottom() + 2, 60, fh), anchor | Qt::AlignTop, azimuthText (lon + heading));
    }
    for (int lat = -60; lat <= 60; lat += 30)
    {
        const auto p = hammer (-180.0, lat);
        g.drawText (QRectF (p.x() - 34, p.y() - fh / 2.0, 30, fh), Qt::AlignRight | Qt::AlignVCenter, QStringLiteral ("%1°").arg (lat));
    }

    // The energy direction.
    if (order > 0 && hasSignal && std::sqrt (dot (shownEnergy, shownEnergy)) > 0.05)
    {
        double az, el;
        anglesOf (normalised (shownEnergy), az, el);
        const auto p = hammer (wrapAzimuth (az - heading), el);
        const auto len = std::sqrt (dot (shownEnergy, shownEnergy));
        g.setPen (QPen (faded (color ("windowText"), 0.9), 1.4));
        g.drawEllipse (p, 4.0 + 6.0 * len, 4.0 + 6.0 * len);
    }
}

// A polar plot: for each direction in the plane, the loudest level that falls there
// (out of the plane counts less), as a lobe coloured by level, with rings every quarter of the range.
std::pair<QPointF, double> AmbiVisualizer::paintPolar (QPainter& g, const QRectF& area, int plane, const QString& title, const std::array<QString, 4>& edges, bool head)
{
    const Palette palette;
    const auto fh = QFontMetricsF (smaller (font(), 0.75)).height();
    const auto side = std::min (area.width() - 2.0 * fh - 16, area.height() - 3.2 * fh);
    if (side < 20)
        return { area.center(), 0.0 };
    const QPointF centre (area.center().x(), area.top() + 2.2 * fh + side / 2.0);
    const auto radius = side / 2.0;

    // Turning the scene (heading) moves the columns.
    const auto shift = (int) std::lround (heading / gridStep);
    std::array<double, numBuckets> lobe;
    lobe.fill (silent);
    for (int r = 0; r < rows; ++r)
        for (int c = 0; c < cols; ++c)
        {
            const auto d = (size_t) (r * cols + c);
            const auto source = (size_t) (r * cols + (((c + shift) % cols) + cols) % cols);
            const auto a = planeAngle[(size_t) plane][d];
            const auto b = (int) std::lround ((a + pi) / (2.0 * pi) * numBuckets) % numBuckets;
            lobe[(size_t) b] = std::max (lobe[(size_t) b], (double) shown[source] + planeWeight[(size_t) plane][d]);
        }

    g.setPen (Qt::NoPen);
    g.setBrush (mix (color ("meterBackground"), color ("mid"), 0.16));
    g.drawEllipse (centre, radius, radius);

    const auto floor = floorDb();
    QPainterPath path;
    for (int b = 0; b <= numBuckets; ++b)
    {
        const auto a = -pi + (b % numBuckets) * 2.0 * pi / numBuckets;
        const auto t = std::clamp ((lobe[(size_t) (b % numBuckets)] - floor) / rangeDb, 0.0, 1.0);
        const QPointF p (centre.x() + std::cos (a) * t * radius, centre.y() - std::sin (a) * t * radius);
        b == 0 ? path.moveTo (p) : path.lineTo (p);
    }
    path.closeSubpath();
    QRadialGradient fill (centre, radius);
    for (int i = 0; i <= 8; ++i)
        fill.setColorAt (i / 8.0, palette.colour (i / 8.0, 0.85));
    g.setBrush (fill);
    g.setPen (QPen (faded (color ("windowText"), 0.75), 1.2));
    g.drawPath (path);

    // Rings, cross and labels.
    g.setBrush (Qt::NoBrush);
    for (int i = 1; i <= 4; ++i)
    {
        g.setPen (QPen (faded (color ("windowText"), i == 4 ? 0.35 : 0.12), i == 4 ? 1.0 : 0.8));
        g.drawEllipse (centre, radius * i / 4.0, radius * i / 4.0);
    }
    g.setPen (QPen (faded (color ("windowText"), 0.12), 0.8));
    g.drawLine (QPointF (centre.x() - radius, centre.y()), QPointF (centre.x() + radius, centre.y()));
    g.drawLine (QPointF (centre.x(), centre.y() - radius), QPointF (centre.x(), centre.y() + radius));
    g.setFont (smaller (font(), 0.68));
    g.setPen (faded (color ("placeholderText"), 0.9));
    for (int i = 1; i <= 3; ++i)
        g.drawText (QPointF (centre.x() + 3, centre.y() - radius * i / 4.0 - 2), QString::number (std::lround (floor + rangeDb * i / 4.0)));

    if (head)
    {
        // The listener: a head, its nose to the front, from above or from the side.
        const auto h = radius * 0.11;
        g.setPen (QPen (faded (color ("windowText"), 0.8), 1.2));
        g.setBrush (mix (color ("meterBackground"), color ("windowText"), 0.25));
        QPainterPath nose;
        if (plane == 0)
        {
            nose.moveTo (centre.x() - h * 0.35, centre.y() - h * 0.9);
            nose.lineTo (centre.x(), centre.y() - h * 1.45);
            nose.lineTo (centre.x() + h * 0.35, centre.y() - h * 0.9);
            g.drawEllipse (QPointF (centre.x() - h, centre.y()), h * 0.22, h * 0.4);
            g.drawEllipse (QPointF (centre.x() + h, centre.y()), h * 0.22, h * 0.4);
        }
        else
        {
            nose.moveTo (centre.x() + h * 0.9, centre.y() - h * 0.35);
            nose.lineTo (centre.x() + h * 1.45, centre.y() + h * 0.05);
            nose.lineTo (centre.x() + h * 0.9, centre.y() + h * 0.35);
        }
        g.drawPath (nose);
        g.drawEllipse (centre, h, h);
    }

    // Where the energy points, in this plane.
    if (order > 0 && hasSignal)
    {
        const auto turn = -heading * toRadians;
        const Vec e { shownEnergy.x * std::cos (turn) - shownEnergy.y * std::sin (turn), shownEnergy.x * std::sin (turn) + shownEnergy.y * std::cos (turn), shownEnergy.z };
        double across, upwards;
        inPlane (e, plane, across, upwards);
        const QPointF p (centre.x() + across * radius, centre.y() - upwards * radius);
        g.setPen (QPen (color ("windowText"), 1.4));
        g.setBrush (faded (color ("highlight"), 0.9));
        g.drawEllipse (p, 3.5, 3.5);
    }

    g.setFont (smaller (font(), 0.75));
    g.setPen (color ("placeholderText"));
    g.drawText (QRectF (area.left(), area.top(), area.width(), fh), Qt::AlignCenter, title.toUpper());
    g.drawText (QRectF (centre.x() + radius + 3, centre.y() - fh / 2.0, area.right() - centre.x() - radius, fh), Qt::AlignLeft | Qt::AlignVCenter, edges[0]);
    g.drawText (QRectF (centre.x() - 50, centre.y() - radius - fh - 1, 100, fh), Qt::AlignCenter, edges[1]);
    g.drawText (QRectF (area.left(), centre.y() - fh / 2.0, centre.x() - radius - 3 - area.left(), fh), Qt::AlignRight | Qt::AlignVCenter, edges[2]);
    g.drawText (QRectF (centre.x() - 50, centre.y() + radius + 1, 100, fh), Qt::AlignCenter, edges[3]);
    return { centre, radius };
}

void AmbiVisualizer::paintHead (QPainter& g)
{
    const auto plot = plotArea();
    const bool across = plot.width() >= plot.height();
    const auto first = across ? QRectF (plot.left(), plot.top(), plot.width() / 2, plot.height()) : QRectF (plot.left(), plot.top(), plot.width(), plot.height() / 2);
    const auto second = across ? first.translated (first.width(), 0) : first.translated (0, first.height());
    paintPolar (g, first, 0, tr ("From Above"), { tr ("R"), tr ("front"), tr ("L"), tr ("rear") }, true);
    paintPolar (g, second, 2, tr ("From the Right"), { tr ("front"), tr ("up"), tr ("rear"), tr ("down") }, true);
}

// ab Visualiser's energy panel: three polar plots, six direction meters and the energy pad.
void AmbiVisualizer::paintEnergy (QPainter& g)
{
    const Palette palette;
    const auto plot = plotArea();
    const auto top = QRectF (plot.left(), plot.top(), plot.width(), plot.height() * 0.56);
    const auto w = top.width() / 3.0;
    paintPolar (g, QRectF (top.left(), top.top(), w, top.height()), 0, tr ("Top"), { tr ("R"), tr ("F"), tr ("L"), tr ("B") }, false);
    paintPolar (g, QRectF (top.left() + w, top.top(), w, top.height()), 1, tr ("Back"), { tr ("R"), tr ("T"), tr ("L"), tr ("Bo") }, false);
    paintPolar (g, QRectF (top.left() + 2 * w, top.top(), w, top.height()), 2, tr ("Right"), { tr ("F"), tr ("T"), tr ("B"), tr ("Bo") }, false);

    const auto bottom = QRectF (plot.left() + 6, top.bottom() + 6, plot.width() - 12, plot.bottom() - top.bottom() - 8);
    g.setFont (smaller (font(), 0.72));
    const auto fh = g.fontMetrics().height();

    // Meters: the level a beam pointing at each of the six measures.
    const struct { const char* name; double az, el; } directions[] { { "F", 0, 0 }, { "B", 180, 0 }, { "L", -90, 0 },
                                                                     { "R", 90, 0 }, { "T", 0, 90 }, { "Bo", 0, -90 } };
    const auto meters = QRectF (bottom.left(), bottom.top(), bottom.width() * 0.58, bottom.height());
    const auto barArea = meters.adjusted (24, fh + 2, 0, -fh - 2);
    const auto pitch = barArea.width() / 6.0;
    const auto floor = floorDb();
    g.setPen (faded (color ("placeholderText"), 0.9));
    for (int i = 0; i <= 4; ++i)
    {
        const auto y = barArea.bottom() - barArea.height() * i / 4.0;
        g.drawText (QRectF (meters.left(), y - fh / 2.0, 22, fh), Qt::AlignRight | Qt::AlignVCenter, QString::number (std::lround (floor + rangeDb * i / 4.0)));
        g.setPen (QPen (faded (color ("windowText"), 0.08), 0.8));
        g.drawLine (QPointF (barArea.left(), y), QPointF (barArea.right(), y));
        g.setPen (faded (color ("placeholderText"), 0.9));
    }
    for (int i = 0; i < 6; ++i)
    {
        const auto& d = directions[i];
        const auto level = levelAt (d.az + (std::abs (d.el) < 1.0 ? heading : 0.0), d.el);
        const auto t = std::clamp ((level - floor) / rangeDb, 0.0, 1.0);
        const QRectF track (barArea.left() + pitch * i + pitch * 0.22, barArea.top(), pitch * 0.56, barArea.height());
        g.setPen (Qt::NoPen);
        g.setBrush (mix (color ("meterBackground"), color ("mid"), 0.2));
        g.drawRoundedRect (track, 2, 2);
        QLinearGradient fill (track.bottomLeft(), track.topLeft());
        for (int s = 0; s <= 6; ++s)
            fill.setColorAt (s / 6.0, palette.colour (std::max (0.2, s / 6.0)));
        g.setBrush (fill);
        g.drawRoundedRect (QRectF (track.left(), track.bottom() - track.height() * t, track.width(), track.height() * t), 2, 2);
        g.setPen (color ("windowText"));
        g.drawText (QRectF (track.center().x() - pitch / 2, meters.top(), pitch, fh), Qt::AlignCenter, tr (d.name));
        g.setPen (color ("placeholderText"));
        g.drawText (QRectF (track.center().x() - pitch / 2, barArea.bottom() + 2, pitch, fh), Qt::AlignCenter,
                    level <= -150.0 ? QStringLiteral ("-inf") : QString::number (level, 'f', 1));
    }

    // The pad: front at the top, right to the right; the slider beside it is the height.
    const auto padSide = std::min (bottom.height() - fh - 4, bottom.width() * 0.42 - 36);
    if (padSide < 30)
        return;
    const QRectF pad (bottom.right() - padSide - 30, bottom.top() + fh + 2, padSide, padSide);
    const QRectF slider (pad.right() + 12, pad.top(), 10, pad.height());
    g.setPen (QPen (faded (color ("mid"), 0.9), 1.0));
    g.setBrush (mix (color ("meterBackground"), color ("mid"), 0.16));
    g.drawRoundedRect (pad, 4, 4);
    g.drawRoundedRect (slider, 3, 3);
    g.setPen (QPen (faded (color ("windowText"), 0.14), 0.8));
    g.drawLine (QPointF (pad.left(), pad.center().y()), QPointF (pad.right(), pad.center().y()));
    g.drawLine (QPointF (pad.center().x(), pad.top()), QPointF (pad.center().x(), pad.bottom()));
    g.drawEllipse (pad.center(), padSide / 4.0, padSide / 4.0);
    g.drawEllipse (pad.center(), padSide / 2.0, padSide / 2.0);
    g.setPen (color ("placeholderText"));
    g.drawText (QRectF (pad.left(), pad.top() - fh - 1, pad.width(), fh), Qt::AlignCenter, tr ("F"));
    g.drawText (QRectF (pad.left() - 14, pad.center().y() - fh / 2.0, 12, fh), Qt::AlignRight | Qt::AlignVCenter, tr ("L"));
    g.drawText (QRectF (pad.right() + 1, pad.center().y() - fh / 2.0, 10, fh), Qt::AlignLeft | Qt::AlignVCenter, tr ("R"));
    g.drawText (QRectF (slider.left() - 4, slider.top() - fh - 1, 18, fh), Qt::AlignCenter, tr ("T"));
    if (order > 0 && hasSignal)
    {
        const auto turn = -heading * toRadians;
        const auto ex = shownEnergy.x * std::cos (turn) - shownEnergy.y * std::sin (turn);
        const auto ey = shownEnergy.x * std::sin (turn) + shownEnergy.y * std::cos (turn);
        const QPointF p (pad.center().x() - ey * padSide / 2.0, pad.center().y() - ex * padSide / 2.0);
        const auto spread = 3.0 + shownDiffuseness * padSide * 0.18;
        g.setPen (Qt::NoPen);
        g.setBrush (faded (color ("highlight"), 0.25));
        g.drawEllipse (p, spread, spread);
        g.setPen (QPen (color ("windowText"), 1.3));
        g.setBrush (color ("highlight"));
        g.drawEllipse (p, 3.5, 3.5);
        const auto y = slider.center().y() - std::clamp (shownEnergy.z, -1.0, 1.0) * slider.height() / 2.0;
        g.drawRoundedRect (QRectF (slider.left() - 3, y - 3, slider.width() + 6, 6), 2, 2);
    }
    g.setPen (color ("placeholderText"));
    g.drawText (QRectF (pad.left() - 20, pad.bottom() + 1, pad.width() + 40, fh), Qt::AlignCenter,
                tr ("diffuse %1%").arg (std::lround (shownDiffuseness * 100.0)));
}

// PRISM's scope: from above, front at the top, a 7.1.4 layout's speakers, and a glowing
// cloud of where the sound is. With Height, what is above sits nearer the middle.
void AmbiVisualizer::emitScope (double dt)
{
    const auto plot = plotArea();
    const auto ratio = devicePixelRatioF();
    const QSize size ((int) std::ceil (plot.width() * ratio), (int) std::ceil (plot.height() * ratio));
    if (scopeTrace.size() != size)
    {
        scopeTrace = QImage (size, QImage::Format_ARGB32_Premultiplied);
        scopeTrace.setDevicePixelRatio (ratio);
        scopeTrace.fill (Qt::transparent);
    }
    QPainter g (&scopeTrace);
    g.setRenderHint (QPainter::Antialiasing);
    g.setCompositionMode (QPainter::CompositionMode_DestinationIn);
    g.fillRect (QRectF (0, 0, plot.width(), plot.height()), QColor (0, 0, 0, (int) std::lround (255.0 * std::exp (-dt / 0.16))));
    g.setCompositionMode (QPainter::CompositionMode_SourceOver);
    if (order <= 0 || ! hasSignal)
        return;

    static std::mt19937 random (7);
    std::uniform_real_distribution<double> unit (0.0, 1.0);
    const Palette palette;
    const QPointF centre (plot.width() / 2.0, plot.height() / 2.0);
    const auto radius = std::min (plot.width(), plot.height()) / 2.0 - 4.0;
    const auto floor = floorDb();
    g.setPen (Qt::NoPen);
    // Dots in proportion to the level, but not so many that a blurry low order fills it.
    const auto expectedAt = [this, floor] (int r, int c)
    {
        const auto t = (shown[(size_t) (r * cols + c)] - floor) / rangeDb;
        return t <= 0.08 ? 0.0 : std::pow (t, 3.0) * 1.3 * std::max (0.15, std::cos ((90.0 - r * gridStep) * toRadians));
    };
    double all = 0.0;
    for (int r = 0; r < rows; ++r)
        for (int c = 0; c < cols; ++c)
            all += expectedAt (r, c);
    const auto thin = std::min (1.0, 220.0 / std::max (1.0, all));
    for (int r = 0; r < rows; ++r)
        for (int c = 0; c < cols; ++c)
        {
            const auto t = (shown[(size_t) (r * cols + c)] - floor) / rangeDb;
            if (t <= 0.08)
                continue;
            const auto el = 90.0 - r * gridStep;
            const auto expected = expectedAt (r, c) * thin;
            const auto dots = (int) (expected + unit (random));
            for (int n = 0; n < dots; ++n)
            {
                const auto az = (-180.0 + c * gridStep + (unit (random) - 0.5) * gridStep - heading) * toRadians;
                const auto e = std::clamp (el + (unit (random) - 0.5) * gridStep, -90.0, 90.0);
                const auto reach = showHeight ? (e >= 0.0 ? horizonRing * (1.0 - e / 90.0) : horizonRing + (1.0 - horizonRing) * (-e / 90.0))
                                              : horizonRing * (0.3 + 0.7 * std::min (1.0, t)) * (0.96 + 0.08 * unit (random));
                const QPointF p (centre.x() + std::sin (az) * reach * radius, centre.y() - std::cos (az) * reach * radius);
                g.setBrush (palette.colour (std::min (1.0, t), (showHeight && e < 0.0 ? 0.35 : 0.7)));
                const auto dot = 0.8 + 1.3 * t;
                g.drawEllipse (p, dot, dot);
            }
        }
}

void AmbiVisualizer::paintScope (QPainter& g)
{
    const auto plot = plotArea();
    const auto centre = plot.center();
    const auto radius = std::min (plot.width(), plot.height()) / 2.0 - 4.0;
    if (radius < 10)
        return;

    g.setPen (Qt::NoPen);
    g.setBrush (mix (color ("meterBackground"), color ("mid"), 0.12));
    g.drawEllipse (centre, radius, radius);
    // PRISM's rings: the horizon, the height ring, four overlapping circles, and the cross.
    g.setBrush (Qt::NoBrush);
    g.setPen (QPen (faded (color ("windowText"), 0.32), 1.0));
    g.drawEllipse (centre, radius * horizonRing, radius * horizonRing);
    g.setPen (QPen (faded (color ("windowText"), 0.16), 0.8));
    g.drawEllipse (centre, radius, radius);
    if (showHeight)
        g.drawEllipse (centre, radius * horizonRing * 0.5, radius * horizonRing * 0.5);
    for (int i = 0; i < 4; ++i)
    {
        const auto a = i * pi / 2.0;
        const auto off = radius * horizonRing * 0.34;
        g.drawEllipse (QPointF (centre.x() + std::sin (a) * off, centre.y() - std::cos (a) * off), radius * horizonRing * 0.5, radius * horizonRing * 0.5);
    }
    g.drawLine (QPointF (centre.x() - radius, centre.y()), QPointF (centre.x() + radius, centre.y()));
    g.drawLine (QPointF (centre.x(), centre.y() - radius), QPointF (centre.x(), centre.y() + radius));

    // The cloud.
    if (! scopeTrace.isNull())
        g.drawImage (plot.topLeft(), scopeTrace);

    // 7.1.4: the bed on the horizon, the four heights at 45 degrees.
    for (const auto& s : speakerLayout)
    {
        const auto a = (s.azimuth - heading) * toRadians;
        const auto reach = s.elevation > 0 && showHeight ? horizonRing * 0.5 : horizonRing;
        const QPointF p (centre.x() + std::sin (a) * reach * radius, centre.y() - std::cos (a) * reach * radius);
        g.setPen (QPen (faded (color ("windowText"), s.elevation > 0 ? 0.45 : 0.7), 1.0));
        g.setBrush (Theme::withOpacity (color ("meterBackground")));
        const auto half = s.elevation > 0 ? 3.0 : 3.8;
        g.drawRect (QRectF (p.x() - half, p.y() - half, 2 * half, 2 * half));
    }

    g.setFont (smaller (font(), 0.72));
    g.setPen (color ("placeholderText"));
    const auto fh = g.fontMetrics().height();
    g.drawText (QRectF (centre.x() - 50, centre.y() - radius * horizonRing - fh - 1, 100, fh), Qt::AlignCenter, azimuthText (heading).toUpper());
    g.drawText (QRectF (centre.x() - 50, centre.y() + radius * horizonRing + 1, 100, fh), Qt::AlignCenter, azimuthText (heading + 180).toUpper());
    g.drawText (QRectF (centre.x() - radius * horizonRing - 52, centre.y() - fh, 50, fh), Qt::AlignRight | Qt::AlignVCenter, azimuthText (heading - 90));
    g.drawText (QRectF (centre.x() + radius * horizonRing + 2, centre.y() - fh, 50, fh), Qt::AlignLeft | Qt::AlignVCenter, azimuthText (heading + 90));
    g.drawText (QRectF (plot.right() - 90, plot.top(), 90, fh), Qt::AlignRight | Qt::AlignTop, showHeight ? tr ("height: on") : tr ("height: off"));
}

// Kaizen's meter: a bar for each speaker of 7.1.4 (the level of a beam pointing at it),
// the peak held above it, and the energy around the listener with the speakers marked.
void AmbiVisualizer::paintSpeakers (QPainter& g)
{
    const Palette palette;
    const auto plot = plotArea();
    const bool beside = plot.width() > plot.height() * 1.2;
    const auto bars = beside ? QRectF (plot.left() + 4, plot.top() + 4, plot.width() * 0.6 - 8, plot.height() - 8)
                             : QRectF (plot.left() + 4, plot.top() + 4, plot.width() - 8, plot.height() * 0.5 - 8);
    const auto radial = beside ? QRectF (bars.right() + 8, plot.top(), plot.right() - bars.right() - 8, plot.height())
                               : QRectF (plot.left(), bars.bottom() + 8, plot.width(), plot.bottom() - bars.bottom() - 8);

    g.setFont (smaller (font(), 0.7));
    const auto fh = g.fontMetrics().height();
    const auto track = bars.adjusted (24, fh + 2, 0, -fh - 2);
    const auto pitch = track.width() / numSpeakers;
    const auto low = floorDb();
    for (int i = 0; i <= 4; ++i)
    {
        const auto y = track.bottom() - track.height() * i / 4.0;
        g.setPen (QPen (faded (color ("windowText"), i == 0 ? 0.2 : 0.08), 0.8));
        g.drawLine (QPointF (track.left(), y), QPointF (track.right(), y));
        g.setPen (faded (color ("placeholderText"), 0.9));
        g.drawText (QRectF (bars.left(), y - fh / 2.0, 22, fh), Qt::AlignRight | Qt::AlignVCenter, QString::number (std::lround (low + rangeDb * i / 4.0)));
    }
    for (int i = 0; i < numSpeakers; ++i)
    {
        const auto& s = speakerLayout[i];
        const auto level = levelAt (s.azimuth + heading, s.elevation);
        const auto t = std::clamp ((level - low) / rangeDb, 0.0, 1.0);
        const QRectF column (track.left() + pitch * i + pitch * 0.12, track.top(), pitch * 0.76, track.height());
        // A gap between the bed and the heights, as the reference groups them.
        const auto x = column.left() + (i >= 7 ? pitch * 0.1 : 0.0);
        const QRectF bar (x, column.top(), column.width() - (i >= 7 ? pitch * 0.1 : 0.0), column.height());
        g.setPen (Qt::NoPen);
        g.setBrush (mix (color ("meterBackground"), color ("mid"), 0.18));
        g.drawRect (bar);
        QLinearGradient fill (bar.bottomLeft(), bar.topLeft());
        for (int k = 0; k <= 6; ++k)
            fill.setColorAt (k / 6.0, palette.colour (std::max (0.2, k / 6.0)));
        g.setBrush (fill);
        g.drawRect (QRectF (bar.left(), bar.bottom() - bar.height() * t, bar.width(), bar.height() * t));
        const auto peak = std::clamp ((speakerPeak[(size_t) i] - low) / rangeDb, 0.0, 1.0);
        if (peak > 0.0)
        {
            g.setPen (QPen (color ("meterPeakHold"), 1.4));
            const auto y = bar.bottom() - bar.height() * peak;
            g.drawLine (QPointF (bar.left(), y), QPointF (bar.right(), y));
        }
        g.setPen (color ("placeholderText"));
        g.drawText (QRectF (bar.center().x() - pitch / 2, bars.top(), pitch, fh), Qt::AlignCenter,
                    speakerPeak[(size_t) i] <= -150.0 || speakerPeak[(size_t) i] < low ? QStringLiteral ("-") : QString::number (speakerPeak[(size_t) i], 'f', 1));
        g.setPen (color ("windowText"));
        g.drawText (QRectF (bar.center().x() - pitch / 2, track.bottom() + 2, pitch, fh), Qt::AlignCenter, QString::fromLatin1 (s.name));
    }

    // The energy around the listener, with the speakers as markers pointing in.
    const auto [centre, radius] = paintPolar (g, radial, 0, tr ("Surround"), { tr ("R"), tr ("front"), tr ("L"), tr ("rear") }, false);
    if (radius <= 0.0)
        return;
    for (const auto& s : speakerLayout)
    {
        const auto a = (s.azimuth - heading) * toRadians;
        const auto reach = s.elevation > 0 ? radius * 0.62 : radius * 1.06;
        const QPointF tip (centre.x() + std::sin (a) * (reach - 6), centre.y() - std::cos (a) * (reach - 6));
        const QPointF side (std::cos (a) * 4.0, std::sin (a) * 4.0);
        const QPointF back (centre.x() + std::sin (a) * (reach + 2), centre.y() - std::cos (a) * (reach + 2));
        QPainterPath marker;
        marker.moveTo (tip);
        marker.lineTo (back + side);
        marker.lineTo (back - side);
        marker.closeSubpath();
        g.setPen (Qt::NoPen);
        g.setBrush (faded (color ("highlight"), s.elevation > 0 ? 0.55 : 0.95));
        g.drawPath (marker);
    }
}

void AmbiVisualizer::paintReadout (QPainter& g, const QRectF& area)
{
    g.setFont (smaller (font(), 0.8));
    g.setPen (color ("placeholderText"));
    QStringList parts;
    if (order > 0)
    {
        static const char* const ordinal[] { "", "1st", "2nd", "3rd" };
        const auto name = order <= 3 ? QString::fromLatin1 (ordinal[order]) : tr ("%1th").arg (order);
        parts << (order < fileOrderShown ? tr ("%1 order of %2").arg (name).arg (fileOrderShown) : tr ("%1 order").arg (name));

        double az, el;
        if (hover.x() >= 0.0 && directionAt (hover, az, el))
            parts << tr ("here: %1, %2, %3").arg (azimuthText (az), elevationText (el), dbText (levelAt (az, el)));
        else if (hasSignal)
        {
            size_t best = 0;
            for (size_t d = 1; d < shown.size(); ++d)
                if (shown[d] > shown[best])
                    best = d;
            const auto r = (int) best / cols, c = (int) best % cols;
            parts << tr ("loudest: %1, %2, %3").arg (azimuthText (-180.0 + c * gridStep), elevationText (90.0 - r * gridStep), dbText (shown[best]));
            parts << tr ("diffuse %1%").arg (std::lround (shownDiffuseness * 100.0));
        }
        else
            parts << tr ("silent");
    }
    parts << (relativeScale ? tr ("%1 dB below the loudest").arg (std::lround (rangeDb)) : tr ("%1 to 0 dB").arg (std::lround (-rangeDb)));
    g.drawText (area, Qt::AlignLeft | Qt::AlignVCenter, parts.join (QStringLiteral ("  ·  ")));
}

void AmbiVisualizer::paintHint (QPainter& g)
{
    const auto area = QRectF (rect()).adjusted (16, 16, -16, -16);
    auto f = font();
    g.setFont (f);
    const auto text = tr ("Play or select an ambisonic file to see where its sound is.\n"
                          "AmbiX (ACN, SN3D), 1st to 10th order. Spatial > Channel Layout marks a file as Ambisonics; "
                          "files of 25 channels and more (4th order up) are taken as Ambisonics.");
    const auto box = g.boundingRect (area, Qt::AlignCenter | Qt::TextWordWrap, text).adjusted (-12, -8, 12, 8);
    g.setPen (Qt::NoPen);
    auto fill = color ("base");
    fill.setAlphaF (0.85f);
    g.setBrush (fill);
    g.drawRoundedRect (box, 8, 8);
    g.setPen (color ("placeholderText"));
    g.drawText (area, Qt::AlignCenter | Qt::TextWordWrap, text);
}

//==============================================================================
void AmbiVisualizer::turn (double across, double upwards)
{
    if (current == View::sphere || current == View::fourViews)
    {
        cameraAzimuth = wrapAzimuth (cameraAzimuth + across);
        cameraElevation = std::clamp (cameraElevation + upwards, -89.0, 89.0);
    }
    else
    {
        heading = wrapAzimuth (heading - across);
        scopeTrace.fill (Qt::transparent);
    }
    update();
}

void AmbiVisualizer::zoomBy (double factor)
{
    if (current == View::sphere || current == View::fourViews)
        cameraZoom = std::clamp (cameraZoom * factor, 0.6, 4.0);
    else
    {
        // The flat views: depth is the range, in steps.
        static constexpr double steps[] { 12, 18, 24, 36, 48, 60, 72, 96 };
        const auto* at = std::lower_bound (std::begin (steps), std::end (steps), rangeDb - 0.5);
        if (factor > 1.0 && at != std::begin (steps))
            --at;
        else if (factor < 1.0 && at + 1 != std::end (steps))
            ++at;
        rangeDb = *std::min (at, std::end (steps) - 1);
    }
    update();
}

bool AmbiVisualizer::handleKey (QKeyEvent* e)
{
    const auto mods = e->modifiers() & (Qt::ControlModifier | Qt::AltModifier | Qt::ShiftModifier | Qt::MetaModifier);
    const auto amount = mods & Qt::ShiftModifier ? 15.0 : 5.0;
    const bool ctrl = (mods & ~Qt::ShiftModifier) == Qt::ControlModifier;
    const bool plain = (mods & ~Qt::ShiftModifier) == Qt::NoModifier;
    switch (e->key())
    {
        case Qt::Key_Left:  if (! plain) return false; turn (amount, 0.0); break;
        case Qt::Key_Right: if (! plain) return false; turn (-amount, 0.0); break;
        case Qt::Key_Up:
            if (ctrl) zoomBy (1.15);
            else if (plain) turn (0.0, amount);
            else return false;
            break;
        case Qt::Key_Down:
            if (ctrl) zoomBy (1.0 / 1.15);
            else if (plain) turn (0.0, -amount);
            else return false;
            break;
        case Qt::Key_Home:
            if (! plain) return false;
            resetCamera();
            break;
        default:
            return false;
    }
    saveSettings();
    return true;
}

void AmbiVisualizer::mousePressEvent (QMouseEvent* e)
{
    if (e->button() != Qt::LeftButton)
        return QWidget::mousePressEvent (e);
    dragging = true;
    dragFrom = e->position().toPoint();
    setCursor (Qt::ClosedHandCursor);
}

void AmbiVisualizer::mouseMoveEvent (QMouseEvent* e)
{
    hover = e->position();
    if (dragging)
    {
        const auto delta = e->position().toPoint() - dragFrom;
        dragFrom = e->position().toPoint();
        turn (delta.x() * 0.4, delta.y() * 0.4);
    }
    else
        update();
}

void AmbiVisualizer::mouseReleaseEvent (QMouseEvent* e)
{
    if (dragging && e->button() == Qt::LeftButton)
    {
        dragging = false;
        unsetCursor();
        saveSettings();
    }
}

void AmbiVisualizer::mouseDoubleClickEvent (QMouseEvent*)
{
    resetCamera();
}

void AmbiVisualizer::wheelEvent (QWheelEvent* e)
{
    const auto steps = e->angleDelta().y() / 120.0;
    if (std::abs (steps) < 1.0e-9 || ! (current == View::sphere || current == View::fourViews))
        return QWidget::wheelEvent (e);
    zoomBy (std::pow (1.12, steps));
    saveSettings();
    e->accept();
}

void AmbiVisualizer::leaveEvent (QEvent*)
{
    hover = { -1.0, -1.0 };
    update();
}

void AmbiVisualizer::resizeEvent (QResizeEvent* e)
{
    caches.clear();
    scopeTrace = {};
    QWidget::resizeEvent (e);
}

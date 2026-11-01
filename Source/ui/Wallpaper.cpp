// SPDX-License-Identifier: AGPL-3.0-only
#include "Wallpaper.h"
#include "Theme.h"

#include <QEvent>
#include <QImageReader>
#include <QPainter>

#include <algorithm>
#include <cmath>

Wallpaper::Wallpaper (QWidget* window) : QWidget (window)
{
    setAttribute (Qt::WA_OpaquePaintEvent);
    setAttribute (Qt::WA_TransparentForMouseEvents);
    setFocusPolicy (Qt::NoFocus);
    window->installEventFilter (this);
    place();
}

bool Wallpaper::load (const QString& path)
{
    QImageReader reader (path);
    reader.setAutoTransform (true);   // a phone photo stands the right way up
    auto image = reader.read();
    if (image.isNull())
        return false;
    source = path;
    picture = std::move (image);
    scaled = QPixmap();
    roomy = QPixmap();
    update();
    return true;
}

void Wallpaper::setMoving (bool on)
{
    if (moving == on)
        return;
    moving = on;
    level = bass = flash = slowLevel = 0.0;
    heard = false;
    roomy = QPixmap();
    clock.invalidate();
    update();   // back to the still picture, or the first frame of the moving one
}

void Wallpaper::addSamples (const std::vector<float>& left, const std::vector<float>& right, double sampleRate)
{
    if (! moving || left.empty())
        return;
    const auto n = std::min (left.size(), right.empty() ? left.size() : right.size());
    // Bass below about 150 Hz by a one-pole low-pass; the level is the whole signal.
    const double k = 1.0 - std::exp (-2.0 * 3.14159265358979 * 150.0 / std::max (8000.0, sampleRate));
    double sum = 0.0, bassSum = 0.0;
    for (size_t i = 0; i < n; ++i)
    {
        const double x = right.empty() ? left[i] : 0.5 * ((double) left[i] + (double) right[i]);
        bassState += k * (x - bassState);
        sum += x * x;
        bassSum += bassState * bassState;
    }
    const double rms = std::sqrt (sum / (double) n), bassRms = std::sqrt (bassSum / (double) n);

    // Quiet masters move as much as loud ones: each is measured against its own recent peak.
    loudest = std::max ({ rms, loudest * 0.998, 0.02 });
    loudestBass = std::max ({ bassRms, loudestBass * 0.998, 0.02 });
    const double nowLevel = std::clamp (rms / loudest, 0.0, 1.0);
    const double nowBass = std::clamp (bassRms / loudestBass, 0.0, 1.0);

    // Quick to rise, slower to fall, so it pulses rather than flickers.
    level = nowLevel > level ? nowLevel : level * 0.9 + nowLevel * 0.1;
    bass = nowBass > bass ? 0.4 * bass + 0.6 * nowBass : bass * 0.85 + nowBass * 0.15;
    if (nowLevel > slowLevel * 1.35 + 0.08)
        flash = std::max (flash, std::clamp ((nowLevel - slowLevel) * 1.5, 0.0, 1.0));
    slowLevel = slowLevel * 0.95 + nowLevel * 0.05;
    heard = true;
}

void Wallpaper::tick()
{
    if (! moving || ! isVisible())
        return;
    const double dt = clock.isValid() ? std::clamp (clock.restart() / 1000.0, 0.0, 0.1) : 0.0;
    if (! clock.isValid())
        clock.start();

    if (! heard)
    {
        // Silence: settle, then stop repainting, which is what keeps an idle PHI idle.
        level *= 0.9;
        bass *= 0.9;
    }
    flash *= 0.86;
    const bool settled = ! heard && level < 0.002 && bass < 0.002 && flash < 0.002;
    heard = false;
    if (settled)
        return;
    phase += dt * (0.15 + 1.6 * level);
    update();
}

void Wallpaper::paintEvent (QPaintEvent*)
{
    QPainter g (this);
    const auto pixels = size() * devicePixelRatioF();
    if (moving && ! picture.isNull())
    {
        // Larger than the window by a fifth, so the zoom, drift and sway never show an edge.
        constexpr double room = 1.2;
        const QSize big ((int) std::ceil (pixels.width() * room), (int) std::ceil (pixels.height() * room));
        if (roomy.isNull() || roomy.size() != big)
        {
            const auto image = picture.scaled (big, Qt::KeepAspectRatioByExpanding, Qt::SmoothTransformation);
            roomy = QPixmap::fromImage (image.copy ((image.width() - big.width()) / 2, (image.height() - big.height()) / 2, big.width(), big.height()));
            roomy.setDevicePixelRatio (devicePixelRatioF());
        }
        g.fillRect (rect(), Qt::black);
        g.save();
        g.setRenderHint (QPainter::SmoothPixmapTransform);
        const double w = width(), h = height();
        const double zoom = 1.0 + 0.1 * bass + 0.03 * flash;   // breathes with the bass
        const double drift = 0.025 * (0.3 + level);             // a slow figure of eight, wider when loud
        g.translate (w / 2.0 + w * drift * std::sin (phase * 0.7), h / 2.0 + h * drift * std::sin (phase * 1.1));
        g.rotate (1.4 * std::sin (phase * 0.5) * (0.3 + level));   // sways
        g.scale (zoom, zoom);
        const auto logical = QSizeF (roomy.size()) / roomy.devicePixelRatio();
        g.drawPixmap (QPointF (-logical.width() / 2.0, -logical.height() / 2.0), roomy);
        g.restore();
        if (flash > 0.01)
        {
            // A hit lights the picture up for a moment.
            g.setCompositionMode (QPainter::CompositionMode_Screen);
            g.fillRect (rect(), QColor::fromRgbF (1.0f, 1.0f, 1.0f, (float) (0.22 * flash)));
            g.setCompositionMode (QPainter::CompositionMode_SourceOver);
        }
    }
    else if (! picture.isNull() && scaled.size() != pixels)
    {
        // Filling the window, cut to its shape and centred.
        const auto image = picture.scaled (pixels, Qt::KeepAspectRatioByExpanding, Qt::SmoothTransformation);
        scaled = QPixmap::fromImage (image.copy ((image.width() - pixels.width()) / 2, (image.height() - pixels.height()) / 2,
                                                 pixels.width(), pixels.height()));
        scaled.setDevicePixelRatio (devicePixelRatioF());
    }
    if (! moving || picture.isNull())
    {
        if (scaled.isNull())
            g.fillRect (rect(), Qt::black);
        else
            g.drawPixmap (0, 0, scaled);
    }

    // The window's own background, which this covers: the opacity is how much of
    // PHI's colour lies over the picture, as with the Visualizer behind PHI.
    auto tint = palette().color (QPalette::Window);
    tint.setAlphaF ((float) Theme::opacity());
    g.fillRect (rect(), tint);
}

void Wallpaper::place()
{
    if (parentWidget() == nullptr)
        return;
    setGeometry (parentWidget()->rect());
    lower();
}

bool Wallpaper::eventFilter (QObject* watched, QEvent* e)
{
    if (watched == parentWidget() && (e->type() == QEvent::Resize || e->type() == QEvent::ChildAdded))
        place();
    else if (watched == parentWidget() && e->type() == QEvent::PaletteChange)
        update();
    return false;
}

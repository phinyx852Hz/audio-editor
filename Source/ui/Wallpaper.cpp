// SPDX-License-Identifier: AGPL-3.0-only
#include "Wallpaper.h"
#include "Theme.h"

#include <QEvent>
#include <QImageReader>
#include <QPainter>

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
    update();
    return true;
}

void Wallpaper::paintEvent (QPaintEvent*)
{
    QPainter g (this);
    const auto pixels = size() * devicePixelRatioF();
    if (! picture.isNull() && scaled.size() != pixels)
    {
        // Filling the window, cut to its shape and centred.
        const auto image = picture.scaled (pixels, Qt::KeepAspectRatioByExpanding, Qt::SmoothTransformation);
        scaled = QPixmap::fromImage (image.copy ((image.width() - pixels.width()) / 2, (image.height() - pixels.height()) / 2,
                                                 pixels.width(), pixels.height()));
        scaled.setDevicePixelRatio (devicePixelRatioF());
    }
    if (scaled.isNull())
        g.fillRect (rect(), Qt::black);
    else
        g.drawPixmap (0, 0, scaled);

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

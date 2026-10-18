// SPDX-License-Identifier: AGPL-3.0-only
#include "GainHud.h"
#include "Theme.h"

#include <QMouseEvent>
#include <QPainter>

#include <algorithm>
#include <cmath>

GainHud::GainHud (QWidget* parent) : QWidget (parent)
{
    setAccessibleName (tr ("Gain knob"));
    setToolTip (tr ("Drag or turn the wheel to make the selection (or the whole file) louder or quieter. "
                    "Shift: finer. Double-click: type a value. Keys: Ctrl+Shift+Up and Ctrl+Shift+Down."));
    setCursor (Qt::SizeVerCursor);
    setFixedSize (118, 34);
    wheelIdle.setSingleShot (true);
    wheelIdle.setInterval (700);
    connect (&wheelIdle, &QTimer::timeout, this, [this] { Q_EMIT finished (db); });
}

void GainHud::setValue (double newDb)
{
    db = std::clamp (newDb, minDb, maxDb);
    update();
}

void GainHud::set (double newDb)
{
    newDb = std::round (std::clamp (newDb, minDb, maxDb) * 100.0) / 100.0;
    if (std::abs (newDb - db) < 1.0e-9)
        return;
    db = newDb;
    update();
    Q_EMIT changed (db);
}

void GainHud::paintEvent (QPaintEvent*)
{
    QPainter g (this);
    g.setRenderHint (QPainter::Antialiasing);
    const auto& theme = Theme::current();
    const auto active = dragging || hovered || std::abs (db) > 1.0e-9;

    auto panel = theme.color (QStringLiteral ("window"));
    panel.setAlphaF (active ? 0.85f : 0.55f);
    g.setPen (QPen (theme.color (QStringLiteral ("mid")), 1.0));
    g.setBrush (panel);
    g.drawRoundedRect (QRectF (rect()).adjusted (0.5, 0.5, -0.5, -0.5), 6, 6);

    // The knob: its pointer turns from -135 degrees (quietest) to +135 (loudest), straight up at 0 dB.
    const QPointF centre (height() / 2.0 + 2.0, height() / 2.0);
    const auto radius = height() / 2.0 - 5.0;
    g.setPen (QPen (theme.color (QStringLiteral ("windowText")), 1.5));
    g.setBrush (theme.color (QStringLiteral ("button")));
    g.drawEllipse (centre, radius, radius);
    const auto turn = db >= 0.0 ? db / maxDb : -db / minDb;
    const auto angle = (turn * 135.0 - 90.0) * M_PI / 180.0;
    g.setPen (QPen (theme.color (QStringLiteral ("accent")), 2.2, Qt::SolidLine, Qt::RoundCap));
    g.drawLine (centre, centre + QPointF (std::cos (angle), std::sin (angle)) * (radius - 1.5));

    auto text = QStringLiteral ("%1%2 dB").arg (db > 0.0 ? QStringLiteral ("+") : QString()).arg (db, 0, 'f', 1);
    g.setPen (active ? theme.color (QStringLiteral ("windowText")) : theme.color (QStringLiteral ("placeholderText")));
    g.drawText (QRectF (height() + 2.0, 0, width() - height() - 8.0, height()), Qt::AlignRight | Qt::AlignVCenter, text);
}

void GainHud::mousePressEvent (QMouseEvent* e)
{
    if (e->button() != Qt::LeftButton)
        return;
    dragging = true;
    pressAt = e->globalPosition();
    pressDb = db;
    wheelIdle.stop();
    update();
}

void GainHud::mouseMoveEvent (QMouseEvent* e)
{
    if (! dragging)
        return;
    const auto moved = (e->globalPosition().x() - pressAt.x()) - (e->globalPosition().y() - pressAt.y());
    const auto perPixel = (e->modifiers() & Qt::ShiftModifier) ? 0.02 : 0.1;
    set (pressDb + moved * perPixel);
}

void GainHud::mouseReleaseEvent (QMouseEvent* e)
{
    if (e->button() != Qt::LeftButton || ! dragging)
        return;
    dragging = false;
    update();
    Q_EMIT finished (db);
}

void GainHud::mouseDoubleClickEvent (QMouseEvent* e)
{
    if (e->button() == Qt::LeftButton)
        Q_EMIT typeRequested();
}

void GainHud::wheelEvent (QWheelEvent* e)
{
    const auto delta = e->angleDelta().y() != 0 ? e->angleDelta().y() : e->angleDelta().x();   // Shift turns the wheel sideways in Qt
    const auto steps = delta / 120.0;
    set (db + steps * ((e->modifiers() & Qt::ShiftModifier) ? 0.1 : 0.5));
    wheelIdle.start();
    e->accept();
}

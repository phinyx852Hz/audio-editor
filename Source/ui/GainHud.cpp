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
    setToolTip (tr ("Gain knob: drag or turn the wheel to make the selection (or the whole file) louder or quieter. "
                    "Shift: finer. Double-click: type a value. Right-drag or Ctrl+drag: move the knob. "
                    "Keys: Ctrl+Shift+Up and Ctrl+Shift+Down; Ctrl+Alt+K hides it."));
    setCursor (Qt::SizeVerCursor);
    setFixedSize (34, 34);
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
    const auto active = dragging || moving || hovered || std::abs (db) > 1.0e-9;

    // Only the knob, drawn like the Device Chain's: a track arc from -135 to +135
    // degrees, filled from straight up (0 dB) to the value, and a pointer.
    const QPointF centre (width() / 2.0, height() / 2.0);
    const auto radius = std::min (width(), height()) / 2.0 - 3.0;
    const QRectF arcBox (centre.x() - radius, centre.y() - radius, 2 * radius, 2 * radius);
    auto track = theme.color (QStringLiteral ("windowText"));
    track.setAlphaF (active ? 0.55f : 0.35f);
    g.setPen (QPen (track, 2.5, Qt::SolidLine, Qt::RoundCap));
    g.setBrush (Qt::NoBrush);
    g.drawArc (arcBox, -45 * 16, 270 * 16);

    const auto turn = db >= 0.0 ? db / maxDb : -db / minDb;   // -1 to 1
    auto accent = theme.color (QStringLiteral ("accent"));
    if (! active)
        accent.setAlphaF (0.7f);
    if (std::abs (turn) > 1.0e-6)
    {
        g.setPen (QPen (accent, 2.5, Qt::SolidLine, Qt::FlatCap));
        g.drawArc (arcBox, 90 * 16, (int) std::lround (-turn * 135.0 * 16.0));
    }
    const auto angle = (turn * 135.0 - 90.0) * M_PI / 180.0;
    g.setPen (QPen (accent, 2.2, Qt::SolidLine, Qt::RoundCap));
    g.drawLine (centre + QPointF (std::cos (angle), std::sin (angle)) * (radius * 0.25),
                centre + QPointF (std::cos (angle), std::sin (angle)) * (radius - 2.0));
}

void GainHud::mousePressEvent (QMouseEvent* e)
{
    // Right button, or Ctrl with the left, picks the knob up to move it.
    if (e->button() == Qt::RightButton || (e->button() == Qt::LeftButton && (e->modifiers() & Qt::ControlModifier)))
    {
        moving = true;
        grabOffset = e->position().toPoint();
        setCursor (Qt::ClosedHandCursor);
        return;
    }
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
    if (moving && parentWidget() != nullptr)
    {
        const auto inParent = mapToParent (e->position().toPoint()) - grabOffset;
        move (std::clamp (inParent.x(), 0, std::max (0, parentWidget()->width() - width())),
              std::clamp (inParent.y(), 0, std::max (0, parentWidget()->height() - height())));
        return;
    }
    if (! dragging)
        return;
    const auto moved = (e->globalPosition().x() - pressAt.x()) - (e->globalPosition().y() - pressAt.y());
    const auto perPixel = (e->modifiers() & Qt::ShiftModifier) ? 0.02 : 0.1;
    set (pressDb + moved * perPixel);
}

void GainHud::mouseReleaseEvent (QMouseEvent* e)
{
    if (moving)
    {
        moving = false;
        setCursor (Qt::SizeVerCursor);
        Q_EMIT placed (pos());
        return;
    }
    if (e->button() != Qt::LeftButton || ! dragging)
        return;
    dragging = false;
    update();
    Q_EMIT finished (db);
}

void GainHud::mouseDoubleClickEvent (QMouseEvent* e)
{
    if (e->button() == Qt::LeftButton && ! (e->modifiers() & Qt::ControlModifier))
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

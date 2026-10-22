// SPDX-License-Identifier: AGPL-3.0-only
#pragma once

#include <QDial>
#include <QMouseEvent>
#include <QPainter>

#include <algorithm>
#include <cmath>

// A knob with a start and an end: a 270 degree
// track, the value filled in along it, and a line to the current position.
// Dragging up or down turns it (Shift for fine steps) instead of QDial's
// circular drag. Endless encoders would keep QDial's look.
class Potentiometer final : public QDial
{
public:
    using QDial::QDial;

    // The value the fill starts from (0 dB on a gain knob); the minimum unless set.
    void setOrigin (int value) { origin = value; hasOrigin = true; update(); }

protected:
    void paintEvent (QPaintEvent*) override
    {
        QPainter p (this);
        p.setRenderHint (QPainter::Antialiasing);

        const auto side = std::min (width(), height()) - 6.0;
        const QRectF arc ((width() - side) / 2.0, (height() - side) / 2.0, side, side);
        const auto range = std::max (1, maximum() - minimum());
        auto angleOf = [&] (int v) { return 225.0 - 270.0 * (double) (std::clamp (v, minimum(), maximum()) - minimum()) / range; };

        // Qt angles are in 1/16 degree, counter-clockwise from 3 o'clock.
        auto track = QPen (palette().color (QPalette::Mid), 3.0, Qt::SolidLine, Qt::FlatCap);
        p.setPen (track);
        p.drawArc (arc, (int) (225.0 * 16), (int) (-270.0 * 16));

        const auto from = angleOf (hasOrigin ? origin : minimum());
        const auto to = angleOf (value());
        auto fill = QPen (palette().color (isEnabled() ? QPalette::Highlight : QPalette::Mid), 3.0, Qt::SolidLine, Qt::FlatCap);
        p.setPen (fill);
        if (std::abs (to - from) > 0.01)
            p.drawArc (arc, (int) (from * 16), (int) ((to - from) * 16));

        const auto radians = to * M_PI / 180.0;
        const QPointF centre = arc.center();
        const QPointF tip (centre.x() + std::cos (radians) * side / 2.0, centre.y() - std::sin (radians) * side / 2.0);
        p.setPen (QPen (palette().color (QPalette::WindowText), 2.0, Qt::SolidLine, Qt::RoundCap));
        p.drawLine (QPointF (centre.x() + (tip.x() - centre.x()) * 0.25, centre.y() + (tip.y() - centre.y()) * 0.25), tip);
    }

    void mousePressEvent (QMouseEvent* e) override
    {
        if (e->button() != Qt::LeftButton)
            return QDial::mousePressEvent (e);
        dragY = e->position().y();
        dragStart = value();
        setSliderDown (true);
        e->accept();
    }

    void mouseMoveEvent (QMouseEvent* e) override
    {
        if (! isSliderDown())
            return;
        // 200 pixels cover the whole range; Shift is ten times finer.
        const auto pixels = dragY - e->position().y();
        const auto perPixel = (maximum() - minimum()) / 200.0 * ((e->modifiers() & Qt::ShiftModifier) != 0 ? 0.1 : 1.0);
        setValue ((int) std::lround (dragStart + pixels * perPixel));
        e->accept();
    }

    void mouseReleaseEvent (QMouseEvent* e) override
    {
        if (isSliderDown())
            setSliderDown (false);
        e->accept();
    }

private:
    int origin = 0;
    bool hasOrigin = false;
    double dragY = 0.0;
    int dragStart = 0;
};

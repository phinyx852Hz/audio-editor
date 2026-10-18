// SPDX-License-Identifier: AGPL-3.0-only
#pragma once

#include <QTimer>
#include <QWidget>

// The small floating gain knob over the waveform, like Audition's heads-up
// display: drag it (up or right raises, down or left lowers; Shift is finer)
// or turn the mouse wheel over it, and the selection, or the whole file when
// nothing is selected, gets louder or quieter while you listen. Letting go
// (or a moment after the last wheel step) makes it one undoable edit and the
// knob returns to 0 dB. Double-click to type a value. It is just the knob, with
// no panel or number (the status bar shows the value while you adjust), and it
// floats: drag it with the right mouse button, or Ctrl and the left, to put it
// anywhere over the waveform.
class GainHud final : public QWidget
{
    Q_OBJECT

public:
    explicit GainHud (QWidget* parent);

    void setValue (double db);   // shown, without signals (keyboard nudges)
    double value() const { return db; }
    bool isMoving() const { return moving; }

    static constexpr double minDb = -48.0, maxDb = 24.0;

Q_SIGNALS:
    void changed (double db);    // while adjusting
    void finished (double db);   // adjusting is over: apply it
    void typeRequested();
    void placed (QPoint topLeft);   // moved by the user, in the parent's coordinates

protected:
    void paintEvent (QPaintEvent*) override;
    void mousePressEvent (QMouseEvent*) override;
    void mouseMoveEvent (QMouseEvent*) override;
    void mouseReleaseEvent (QMouseEvent*) override;
    void mouseDoubleClickEvent (QMouseEvent*) override;
    void wheelEvent (QWheelEvent*) override;
    void enterEvent (QEnterEvent*) override { hovered = true; update(); }
    void leaveEvent (QEvent*) override      { hovered = false; update(); }

private:
    void set (double newDb);

    double db = 0.0, pressDb = 0.0;
    QPointF pressAt;
    bool dragging = false, moving = false, hovered = false;
    QPoint grabOffset;
    QTimer wheelIdle;
};

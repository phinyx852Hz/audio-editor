// SPDX-License-Identifier: AGPL-3.0-only
#pragma once

#include "../core/AudioData.h"

#include <QWidget>

// The time ruler above the waveform: numbered ticks that follow the zoom,
// from hours and minutes when zoomed out down to microseconds when zoomed in,
// with the cursor and selection marked. Clicking it moves the cursor.
class TimeRuler final : public QWidget
{
    Q_OBJECT

public:
    explicit TimeRuler (QWidget* parent);

    // Called on every UI tick; repaints only when something changed.
    // A length of 0 means nothing is open.
    void setView (double startFrame, double lengthFrames, double sampleRate, Frame fileLength,
                  Frame cursor, Frame selStart, Frame selEnd);

    // The label for a time on a ruler whose ticks are `decimals` places apart
    // ("2 s", "1 min 30 s", "4.250125 s"). Public for the tests.
    static QString label (double seconds, int decimals);

    QSize sizeHint() const override;
    QSize minimumSizeHint() const override { return { 0, sizeHint().height() }; }

Q_SIGNALS:
    void cursorRequested (Frame);

protected:
    void paintEvent (QPaintEvent*) override;
    void mousePressEvent (QMouseEvent*) override;
    void mouseMoveEvent (QMouseEvent*) override;

private:
    double frameToX (double frame) const;

    double start = 0.0, length = 0.0, rate = 48000.0;
    Frame fileLength = 0, cursor = 0, selStart = 0, selEnd = 0;
};

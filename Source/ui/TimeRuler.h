// SPDX-License-Identifier: AGPL-3.0-only
#pragma once

#include "../core/AudioData.h"
#include "../core/BeatGrid.h"

#include <QWidget>

// The time ruler above the waveform: numbered ticks that follow the zoom,
// from hours and minutes when zoomed out down to microseconds when zoomed in,
// with the cursor and selection marked. Clicking it moves the cursor.
// With a beat grid it can count bars and beats (1, 1.2, 1.2.3) instead of
// time, or show both on two rows (bars and beats above, time below).
class TimeRuler final : public QWidget
{
    Q_OBJECT

public:
    explicit TimeRuler (QWidget* parent);

    // Called on every UI tick; repaints only when something changed.
    // A length of 0 means nothing is open.
    void setView (double startFrame, double lengthFrames, double sampleRate, Frame fileLength,
                  Frame cursor, Frame selStart, Frame selEnd);

    enum class Mode { time, beats, both };
    void setGrid (const BeatGrid&, Mode);

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
    void drawTime (QPainter&, double top, double bottom) const;
    void drawBeats (QPainter&, double top, double bottom) const;
    bool showsBeats() const { return mode != Mode::time && grid.isSet(); }
    bool showsTime() const  { return mode != Mode::beats || ! grid.isSet(); }

    double start = 0.0, length = 0.0, rate = 48000.0;
    Frame fileLength = 0, cursor = 0, selStart = 0, selEnd = 0;
    BeatGrid grid;
    Mode mode = Mode::time;
};

// SPDX-License-Identifier: AGPL-3.0-only
#pragma once

#include "../core/AudioData.h"

#include <QImage>
#include <QWidget>

class TimeRuler;

// The whole file at a glance, above the editor (as in WaveLab): its own time
// ruler, the waveform from start to end, and a box showing the part the
// editor is zoomed in on. Click or drag to move the editor there, drag an
// edge of the box to zoom, or turn the mouse wheel to zoom in and out around
// the point under the mouse.
class Overview final : public QWidget
{
    Q_OBJECT

public:
    explicit Overview (QWidget* parent);

    // Called on every UI tick; repaints only when something changed.
    void setState (AudioDataPtr, double viewStart, double viewLength, Frame cursor, Frame selStart, Frame selEnd);

Q_SIGNALS:
    void viewRequested (double start, double length);   // in frames
    void cursorRequested (Frame);

private:
    class Wave;
    TimeRuler* ruler;
    Wave* wave;
};

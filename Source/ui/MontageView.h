// SPDX-License-Identifier: AGPL-3.0-only
#pragma once

#include "../core/Montage.h"

#include <QWidget>
#include <memory>

// The songs of a montage as blocks over the waveform, on the same time scale:
// two lanes, so crossfades show as overlapping blocks, with each song's number
// and name, its fades as slopes, its gain, and the pause before it. The song
// under the cursor is outlined. Clicking a song puts the cursor at its start;
// double-clicking opens its properties.
class MontageView final : public QWidget
{
    Q_OBJECT

public:
    explicit MontageView (QWidget* parent);

    void setMontage (std::shared_ptr<const Montage>);
    // Called on every UI tick with the waveform's view, in frames; repaints only on a change.
    void setView (double startFrame, double lengthFrames, Frame cursor);

    QSize sizeHint() const override;
    QSize minimumSizeHint() const override { return { 0, sizeHint().height() }; }

Q_SIGNALS:
    void cursorRequested (Frame);
    void propertiesRequested();

protected:
    void paintEvent (QPaintEvent*) override;
    void mousePressEvent (QMouseEvent*) override;
    void mouseDoubleClickEvent (QMouseEvent*) override;

private:
    double frameToX (double frame) const;
    int clipAtX (double x, double y) const;

    std::shared_ptr<const Montage> montage;
    double start = 0.0, length = 0.0;
    Frame cursor = 0;
};

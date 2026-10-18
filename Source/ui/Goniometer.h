// SPDX-License-Identifier: AGPL-3.0-only
#pragma once

#include <QImage>
#include <QWidget>
#include <vector>

// Goniometer (stereo vectorscope) with a phase correlation meter under it.
// Mid (mono) draws straight up, side across: a mono sound is a vertical line,
// wide stereo a round cloud, and left or right alone the diagonals. Points glow
// and fade like a phosphor screen. The correlation meter runs from -1 (out of
// phase: cancels in mono) through 0 (unrelated) to +1 (mono).
class Goniometer final : public QWidget
{
public:
    explicit Goniometer (QWidget* parent);

    void addSamples (const std::vector<float>& left, const std::vector<float>& right);  // call regularly
    void reset();

    QSize sizeHint() const override        { return { 260, 290 }; }
    QSize minimumSizeHint() const override { return { 140, 160 }; }

protected:
    void paintEvent (QPaintEvent*) override;
    void resizeEvent (QResizeEvent*) override;
    void changeEvent (QEvent*) override;

private:
    QRectF plotArea() const;
    QRectF correlationArea() const;

    QImage trace;                // the glowing points, faded a little every frame
    double correlation = 0.0;    // smoothed
    bool hasSignal = false;
};

// SPDX-License-Identifier: AGPL-3.0-only
#pragma once

#include <QImage>
#include <QWidget>
#include <array>
#include <vector>

// Stereo imaging display with a phase correlation meter under it. Four views,
// one at a time; a click (or Next Imaging View, Ctrl+Alt+V) moves to the next:
// - Goniometer: mid straight up, side across, in a circle. A mono sound is a
//   vertical line, wide stereo a round cloud, left or right alone a diagonal.
// - Polar Sample: one dot per sample in a half circle, angle by stereo
//   position, distance by level. Dots inside the 45 degree lines are mono-safe.
// - Polar Level: rays from the centre, one per direction, as long as the
//   loudest sample heard there, with an outer skin that slowly contracts.
// - Lissajous: the classic oscilloscope trace in a diamond (full scale on each
//   side). It should stay taller than wide.
// Points glow and fade like a phosphor screen. The correlation meter runs from
// -1 (out of phase: cancels in mono) through 0 (unrelated) to +1 (mono).
class Goniometer final : public QWidget
{
public:
    enum class View { goniometer, polarSample, polarLevel, lissajous };
    static constexpr int numViews = 4;

    explicit Goniometer (QWidget* parent);

    void addSamples (const std::vector<float>& left, const std::vector<float>& right);  // call regularly
    void reset();
    void nextView();
    View view() const { return current; }
    static QString viewName (View);

    QSize sizeHint() const override        { return { 260, 290 }; }
    QSize minimumSizeHint() const override { return { 140, 160 }; }

protected:
    void paintEvent (QPaintEvent*) override;
    void resizeEvent (QResizeEvent*) override;
    void changeEvent (QEvent*) override;
    void mousePressEvent (QMouseEvent*) override;

private:
    static constexpr int numRays = 90;   // 2 degrees each across the half circle

    QRectF plotArea() const;
    QRectF correlationArea() const;
    // Where the polar views' half circle has its centre, and its radius, within the plot.
    QPointF polarOrigin (const QRectF& plot) const { return { plot.center().x(), plot.top() + plot.height() * 0.78 }; }
    double polarRadius (const QRectF& plot) const  { return plot.width() * 0.5 - 2.0; }

    void drawGuides (QPainter&, const QRectF& plot) const;
    void drawRays (QPainter&, const QRectF& plot) const;

    View current = View::goniometer;
    QImage trace;                // the glowing points, faded a little every frame
    std::array<float, numRays> rays {};   // Polar Level: current level per direction
    std::array<float, numRays> skin {};   // and the slowly contracting outline
    double correlation = 0.0;    // smoothed
    bool hasSignal = false;
};

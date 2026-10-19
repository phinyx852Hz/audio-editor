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
// - Binaural: the head seen from above, with where each frequency band seems to
//   come from, judged from the time and level differences between the ears
//   (low bands nearest the head). Front and back sound alike to this, so it
//   shows the front half. Faint, spread dots are diffuse (room) sound.
// Points glow and fade like a phosphor screen. The correlation meter runs from
// -1 (out of phase: cancels in mono) through 0 (unrelated) to +1 (mono).
class Goniometer final : public QWidget
{
public:
    enum class View { goniometer, polarSample, polarLevel, lissajous, binaural };
    static constexpr int numViews = 5;

    explicit Goniometer (QWidget* parent);

    void addSamples (const std::vector<float>& left, const std::vector<float>& right);  // call regularly
    void reset();
    void setSampleRate (double rate) { sampleRate = rate; }   // of the samples given to addSamples
    void nextView();
    View view() const { return current; }
    static QString viewName (View);

    QSize sizeHint() const override        { return { 290, 315 }; }
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
    void analyseBinaural (const std::vector<float>& left, const std::vector<float>& right);
    void drawBinaural (QPainter&, const QRectF& plot) const;

    View current = View::goniometer;
    QImage trace;                // the glowing points, faded a little every frame
    std::array<float, numRays> rays {};   // Polar Level: current level per direction
    std::array<float, numRays> skin {};   // and the slowly contracting outline
    double correlation = 0.0;    // smoothed
    double lissajousPeak = 0.0, lissajousZoom = 1.0;
    bool hasSignal = false;

    // Binaural: per band, the direction (degrees, left negative), level (0 to 1) and coherence.
    static constexpr int numBands = 24;
    struct Band { double angle = 0.0, level = 0.0, coherence = 1.0; };
    std::array<Band, numBands> bands {};
    std::vector<float> recentLeft, recentRight;   // the newest samples, for the analysis
    double sampleRate = 48000.0;
};

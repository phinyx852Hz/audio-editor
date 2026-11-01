// SPDX-License-Identifier: AGPL-3.0-only
#pragma once

#include <QElapsedTimer>
#include <QImage>
#include <QPixmap>
#include <QWidget>

#include <vector>

// A picture as the background of the whole PHI window (Options or Visualizer > Wallpaper),
// seen through the window as far as its opacity allows, like the Visualizer behind PHI.
//
// Still (the default): drawn once for each size and then costs nothing, since nothing
// repaints while PHI sits idle.
// Moving with the music (Visualizer > Wallpaper Moves with the Music): the picture breathes
// with the bass, drifts and sways with the level and flashes on hits, repainting the whole
// window every frame while music plays. That is heavy on the CPU, so it is meant for making
// visuals; it holds still again when the music stops.
class Wallpaper final : public QWidget
{
public:
    explicit Wallpaper (QWidget* window);

    bool load (const QString& path);   // false when it is not a picture Qt can read
    QString path() const { return source; }   // the picture shown, empty for none

    void setMoving (bool);
    bool isMoving() const { return moving; }
    // The audio playing (the scope samples), then a frame: repaints only while it moves.
    void addSamples (const std::vector<float>& left, const std::vector<float>& right, double sampleRate);
    void tick();

protected:
    void paintEvent (QPaintEvent*) override;
    bool eventFilter (QObject*, QEvent*) override;   // follows the window's size

private:
    void place();

    QString source;
    QImage picture;
    QPixmap scaled;   // the picture filling the window, made again when the size changes
    QPixmap roomy;    // moving: the picture a little larger than the window, room to move in

    bool moving = false;
    bool heard = false;      // samples came in since the last frame
    double bassState = 0.0;  // the low-pass filter picking out the bass
    double level = 0.0, bass = 0.0, flash = 0.0;   // 0..1, smoothed for the eye
    double slowLevel = 0.0;  // the recent average, which a hit stands out from
    double loudest = 0.05, loudestBass = 0.05;     // follows the music's own loudness
    double phase = 0.0;      // drift and sway; it advances faster when the music is loud
    QElapsedTimer clock;
};

// SPDX-License-Identifier: AGPL-3.0-only
#pragma once

#include <QImage>
#include <QPixmap>
#include <QWidget>

// A still picture as the background of the whole PHI window (Options > Wallpaper),
// seen through the window as far as its opacity allows, like the Visualizer behind PHI.
// It is drawn once for each size and then costs nothing: unlike a moving background,
// nothing repaints while PHI sits idle.
class Wallpaper final : public QWidget
{
public:
    explicit Wallpaper (QWidget* window);

    bool load (const QString& path);   // false when it is not a picture Qt can read
    QString path() const { return source; }   // the picture shown, empty for none

protected:
    void paintEvent (QPaintEvent*) override;
    bool eventFilter (QObject*, QEvent*) override;   // follows the window's size

private:
    void place();

    QString source;
    QImage picture;
    QPixmap scaled;   // the picture filling the window, made again when the size changes
};

// SPDX-License-Identifier: AGPL-3.0-only
#pragma once

#include "../core/Binaural.h"

#include <QDialog>
#include <QTimer>
#include <QWidget>
#include <functional>

class QCheckBox;
class QComboBox;
class QDoubleSpinBox;
class QSpinBox;

// The listener seen from above (nose up), with the sound placed around the head, and
// from the side for its height. Drag the sound, or with the keyboard: Left/Right turn
// it, Up/Down bring it closer or further, Page Up/Down raise or lower it, and
// Shift+Left/Right narrow or widen a stereo sound.
class BinauralField final : public QWidget
{
    Q_OBJECT

public:
    BinauralField (bool stereo, QWidget* parent);

    void set (const binaural::Settings&);
    QSize sizeHint() const override { return { 460, 260 }; }

Q_SIGNALS:
    void moved (double azimuth, double elevation, double distance, double width);

protected:
    void paintEvent (class QPaintEvent*) override;
    void mousePressEvent (class QMouseEvent*) override;
    void mouseMoveEvent (class QMouseEvent*) override;
    void wheelEvent (class QWheelEvent*) override;
    void keyPressEvent (class QKeyEvent*) override;

private:
    QRectF topArea() const;
    QRectF sideArea() const;
    void dragTo (QPointF, bool top);
    void changed();

    bool stereo;
    double azimuth = 0.0, elevation = 0.0, distance = 1.0, spread = 60.0;   // spread: the stereo width
    int dragging = 0;   // 1: the view from above, 2: the side view
};

// Effect > Binaural: places a mono or stereo sound around the listener for headphones.
class BinauralDialog final : public QDialog
{
    Q_OBJECT

public:
    // `preview` is called with the settings while Preview is on (and after each change), and
    // with nothing when it goes off; it should play the audio rendered with them.
    BinauralDialog (const binaural::Settings& initial, bool stereo, bool wholeFile, const QStringList& hrtfFiles,
                    std::function<void (const binaural::Settings*)> preview, QWidget* parent);
    ~BinauralDialog() override;

    binaural::Settings settings() const;

    void done (int result) override;

private:
    void fromField (double azimuth, double elevation, double distance, double width);
    void changed();

    BinauralField* field;
    QDoubleSpinBox* azimuthBox;
    QDoubleSpinBox* elevationBox;
    QDoubleSpinBox* widthBox = nullptr;
    QDoubleSpinBox* distanceBox;
    QSpinBox* roomBox;
    QDoubleSpinBox* roomSecondsBox;
    QDoubleSpinBox* gainBox;
    QComboBox* hrtfBox;
    QCheckBox* previewBox;
    std::function<void (const binaural::Settings*)> preview;
    QTimer previewTimer;
    bool updating = false;
};

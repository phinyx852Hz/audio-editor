// SPDX-License-Identifier: AGPL-3.0-only
#pragma once

#include "../core/MidSide.h"

#include <QDialog>
#include <QTimer>
#include <QWidget>
#include <functional>
#include <vector>

class QCheckBox;
class QComboBox;
class QDoubleSpinBox;
class QLabel;
class QPushButton;
class QSpinBox;

// The stereo field as dots: mid up, side across (a goniometer), the sound before
// in grey and after in colour, with the left and right axes as diagonals. Drag
// sideways for width and up or down to rotate; with the keyboard Left/Right change
// the width and Up/Down the rotation (Shift: finer), Home resets both.
class MidSideField final : public QWidget
{
    Q_OBJECT

public:
    explicit MidSideField (QWidget* parent);

    void setPoints (std::vector<QPointF> before, std::vector<QPointF> after);
    void setValues (double width, double maxWidth, double rotation);
    QSize sizeHint() const override { return { 320, 320 }; }

Q_SIGNALS:
    void changed (double width, double rotation);

protected:
    void paintEvent (class QPaintEvent*) override;
    void mousePressEvent (class QMouseEvent*) override;
    void mouseMoveEvent (class QMouseEvent*) override;
    void keyPressEvent (class QKeyEvent*) override;

private:
    std::vector<QPointF> before, after;   // (side, mid), scaled to fit
    double amount = 1.0, most = 2.0, rotation = 0.0;   // width, its maximum
    QPointF pressAt;
    double pressAmount = 1.0, pressRotation = 0.0;
};

// Process or Spatial > Mid/Side Width: widens or narrows the stereo image.
class MidSideDialog final : public QDialog
{
    Q_OBJECT

public:
    // `sample` is the stretch heard in the preview and measured for the readouts (up to
    // 15 s, stereo). `play` is called with it rendered while Preview is on, and with
    // nothing when it goes off; `flip` switches between the result and the original
    // while previewing, returning true when the result is heard.
    MidSideDialog (const midside::Settings& initial, bool wholeFile, AudioDataPtr sample,
                   std::function<void (AudioDataPtr)> play, std::function<bool()> flip, QWidget* parent);
    ~MidSideDialog() override;

    midside::Settings settings() const;   // what is applied (listening is always stereo)
    void done (int result) override;

private:
    void load (const midside::Settings&);
    void changed();
    void update();

    MidSideField* field;
    QComboBox* presetBox;
    QSpinBox* widthBox;
    QCheckBox* hugeBox;
    QSpinBox* monoBox;
    QSpinBox* generateBox;
    QDoubleSpinBox* airBox;
    QDoubleSpinBox* rotationBox;
    QSpinBox* liftBox;
    QCheckBox* matchBox;
    QComboBox* listenBox;
    QLabel* readout;
    QCheckBox* previewBox;
    QPushButton* abButton;
    AudioDataPtr sample;
    std::function<void (AudioDataPtr)> play;
    std::function<bool()> flip;
    QTimer timer;
    bool loading = false;
    std::vector<QPointF> beforePoints;
    midside::Analysis beforeAnalysis;
};

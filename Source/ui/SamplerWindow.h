// SPDX-License-Identifier: AGPL-3.0-only
#pragma once

#include "../sampler/SamplerEngine.h"

#include <QTimer>
#include <QWidget>
#include <functional>
#include <vector>

class QCheckBox;
class QComboBox;
class QDoubleSpinBox;
class QLabel;
class QSpinBox;

// The sample with its start and end (white) and loop (shaded, Classic only), and
// a line where each playing voice is. Drag a marker to move it.
class SampleView final : public QWidget
{
    Q_OBJECT

public:
    SampleView (sampler::Engine&, QWidget* parent);
    void setSample (sampler::SamplePtr, double rate);
    void setMarkers (Frame start, Frame end, Frame loopStart, Frame loopEnd, bool showLoop);
    QSize sizeHint() const override { return { 760, 190 }; }
    QSize minimumSizeHint() const override { return { 300, 110 }; }

Q_SIGNALS:
    void markerMoved (int which, Frame position);   // 0 start, 1 end, 2 loop start, 3 loop end

protected:
    void paintEvent (class QPaintEvent*) override;
    void resizeEvent (class QResizeEvent*) override;
    void mousePressEvent (class QMouseEvent*) override;
    void mouseMoveEvent (class QMouseEvent*) override;
    void mouseReleaseEvent (class QMouseEvent*) override;

private:
    double xFor (double frame) const;
    Frame frameAt (double x) const;
    void buildPeaks();

    sampler::Engine& engine;
    sampler::SamplePtr sample;
    double rate = 48000.0;
    std::vector<std::pair<float, float>> peaks;   // per pixel column, both channels together
    Frame markers[4] { 0, 0, 0, 0 };
    bool showLoop = true;
    int dragging = -1;
};

// A piano keyboard: click to play (higher on the key is louder); lit keys sound.
class PianoKeys final : public QWidget
{
    Q_OBJECT

public:
    PianoKeys (sampler::Engine&, QWidget* parent);
    void setLowestNote (int note);
    void setRootNote (int note) { root = note; update(); }
    QSize sizeHint() const override { return { 760, 80 }; }

Q_SIGNALS:
    void played (int note, float velocity);
    void released (int note);

protected:
    void paintEvent (class QPaintEvent*) override;
    void mousePressEvent (class QMouseEvent*) override;
    void mouseMoveEvent (class QMouseEvent*) override;
    void mouseReleaseEvent (class QMouseEvent*) override;

private:
    int noteAt (QPointF, float* velocity = nullptr) const;
    QRectF keyRect (int note) const;
    int whiteCount() const;

    sampler::Engine& engine;
    int lowest = 36, root = 60, pressed = -1;
};

// Sample > Sampler: one sound across the keyboard. Played from MIDI, the piano at the
// bottom or the computer keyboard (A to K as the white keys with W, E, T, Y, U as the
// black ones, as in Live; Z and X octave down and up, C and V velocity down and up).
class SamplerWindow final : public QWidget
{
    Q_OBJECT

public:
    SamplerWindow (sampler::Engine&, QWidget* parent);

    // A new sound, already at the engine's rate. `keepMarkers`: the start, end and loop
    // stay where they were when they still fit (refreshing after an edit).
    void setSample (sampler::SamplePtr, double rate, const QString& name, bool keepMarkers);
    bool followsEditor() const;
    void setMidiStatus (const QString&);

Q_SIGNALS:
    void refreshRequested();
    void midiInputsRequested();
    void activated();

protected:
    void keyPressEvent (class QKeyEvent*) override;
    void keyReleaseEvent (class QKeyEvent*) override;
    void changeEvent (QEvent*) override;
    bool eventFilter (QObject*, QEvent*) override;
    void closeEvent (class QCloseEvent*) override;

private:
    void fromControls();
    void toControls();
    void markerMoved (int which, Frame position);
    void showStatus();
    Frame snap (Frame) const;
    int noteForKey (const class QKeyEvent*) const;

    sampler::Engine& engine;
    sampler::Settings settings;
    double rate = 48000.0;
    QString name;
    SampleView* view;
    PianoKeys* piano;
    QLabel* title;
    QLabel* status;
    QLabel* midiStatus;
    QComboBox* modeBox;
    QCheckBox* loopBox;
    QCheckBox* snapBox;
    QCheckBox* followBox;
    QDoubleSpinBox* gainBox;
    QSpinBox* panBox;
    QSpinBox* transposeBox;
    QSpinBox* detuneBox;
    QComboBox* rootBox;
    QSpinBox* voicesBox;
    QSpinBox* velocityBox;
    QDoubleSpinBox* attackBox;
    QDoubleSpinBox* decayBox;
    QSpinBox* sustainBox;
    QDoubleSpinBox* releaseBox;
    QDoubleSpinBox* fadeInBox;
    QDoubleSpinBox* fadeOutBox;
    QWidget* envelopeRow;
    QWidget* fadeRow;
    QTimer redraw;
    int octave = 3;                       // the A key plays C of this octave (C3 = MIDI 60 as in Live)
    int keyVelocity = 100;
    std::vector<std::pair<int, int>> heldKeys;   // scan code, note
    bool updating = false;
};

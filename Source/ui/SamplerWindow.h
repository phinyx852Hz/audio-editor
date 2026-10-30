// SPDX-License-Identifier: AGPL-3.0-only
#pragma once

#include "../sampler/SamplerEngine.h"

#include <QTimer>
#include <QWidget>
#include <functional>
#include <vector>

class QAbstractButton;
class QButtonGroup;
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
    void setReversed (bool);   // drawn back to front, as it plays
    QSize sizeHint() const override { return { 620, 130 }; }
    QSize minimumSizeHint() const override { return { 300, 90 }; }

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
    bool showLoop = true, reversed = false;
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

// Sample > Sampler: one sound across the keyboard. Played from MIDI or the computer keyboard (A to K as the white keys with W, E, T, Y, U as the
// black ones, as in Live; Z and X octave down and up, C and V velocity down and up). As Live's Simpler, the
// mode picks which controls there are, one row of them shows, and Edit opens the rest.
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
    void closed();
    void fileDropped (const QString& path);   // an audio file dragged here from Find or a file manager

protected:
    void keyPressEvent (class QKeyEvent*) override;
    void keyReleaseEvent (class QKeyEvent*) override;
    bool event (QEvent*) override;
    bool eventFilter (QObject*, QEvent*) override;
    void dragEnterEvent (class QDragEnterEvent*) override;
    void dropEvent (class QDropEvent*) override;
    void resizeEvent (class QResizeEvent*) override;   // side by side when wide (along the bottom), stacked when narrow
    void hideEvent (class QHideEvent*) override;

private:
    void releaseKeys();
    void focusMoved (QWidget* old, QWidget* now);
    void fromControls();
    void toControls();
    void markerMoved (int which, Frame position);
    void reverse (bool);
    void crop();
    void normalize();
    void showStatus();
    Frame snap (Frame) const;
    int noteForKey (const class QKeyEvent*) const;

    sampler::Engine& engine;
    sampler::Settings settings;
    double rate = 48000.0;
    QString name;
    SampleView* view;
    PianoKeys* piano;
    class QBoxLayout* columns = nullptr;
    QLabel* title;
    QLabel* status;
    QLabel* midiStatus;
    QButtonGroup* modeButtons;
    QAbstractButton* editButton;
    QWidget* editPanel;
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
    QComboBox* gateBox;
    QCheckBox* filterBox;
    QSpinBox* cutoffBox;
    QSpinBox* resonanceBox;
    QComboBox* filterTypeBox;
    QCheckBox* steepBox;
    QDoubleSpinBox* driveBox;
    QSpinBox* keyFollowBox;
    QSpinBox* filterVelocityBox;
    QComboBox* loopModeBox;
    QDoubleSpinBox* crossfadeBox;
    QComboBox* qualityBox;
    QCheckBox* reverseBox;
    QCheckBox* fixedBox;
    QDoubleSpinBox* glideBox;
    QCheckBox* fingeredBox;
    QCheckBox* legatoBox;
    QComboBox* keyLowBox;
    QComboBox* keyHighBox;
    // Edit's Envelopes and LFO tabs show one of the two at a time, picked by these.
    QComboBox* envelopeChoice;
    QCheckBox* envOnBox;
    QDoubleSpinBox* envAttackBox;
    QDoubleSpinBox* envDecayBox;
    QSpinBox* envSustainBox;
    QDoubleSpinBox* envReleaseBox;
    QSpinBox* envCurveBox;
    QDoubleSpinBox* envAmountBox;
    QSpinBox* envVelocityBox;
    QComboBox* lfoChoice;
    QComboBox* waveBox;
    QSpinBox* shapeBox;
    QCheckBox* syncBox;
    QDoubleSpinBox* rateBox;
    QComboBox* beatsBox;
    QWidget* rateField;
    QWidget* beatsField;
    QComboBox* retriggerBox;
    QSpinBox* phaseBox;
    QCheckBox* randomBox;
    QSpinBox* modWheelBox;
    QDoubleSpinBox* toPitchBox;
    QDoubleSpinBox* toFilterBox;
    QSpinBox* toVolumeBox;
    QSpinBox* toPanBox;
    QDoubleSpinBox* bendBox;
    QDoubleSpinBox* tempoBox;
    QWidget* envelopeRow;
    QWidget* fadeRow;
    QWidget* loopRow;
    QTimer redraw;
    int octave = 3;                       // the A key plays C of this octave (C3 = MIDI 60 as in Live)
    int keyVelocity = 100;
    std::vector<std::pair<int, int>> heldKeys;   // scan code, note
    bool updating = false;
};

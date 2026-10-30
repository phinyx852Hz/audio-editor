// SPDX-License-Identifier: AGPL-3.0-only
#pragma once

// The Slicer's step sequencer on screen (sampler/StepSequencer.h): a row of steps for each
// pad of the bank showing, as Cubase's Pattern Editor and FL Studio's Channel Rack, with a
// lane under it to draw one setting of the chosen pad's steps (velocity, gate, pitch, chance,
// repeats, ramp, shift), as FL Studio's graph editor.
#include "../sampler/SlicerEngine.h"

#include <QColor>
#include <QTimer>
#include <QWidget>
#include <functional>
#include <memory>

class QButtonGroup;
class QCheckBox;
class QComboBox;
class QLabel;
class QLineEdit;
class QPushButton;
class QSpinBox;

// What a pad is called and its colour (invalid: none), from the Slicer.
struct PadLooks
{
    std::function<QString (int pad)> name;
    std::function<QColor (int pad)> colour;
};

// Sixteen rows, one for each pad of a bank, of up to 64 steps. Click a step to turn it on
// or off, drag along to paint; click a pad's name to choose it. M and S mute and solo the
// pad's lane (its pattern only). Steps past a lane's length are greyed.
class StepGrid final : public QWidget
{
    Q_OBJECT

public:
    StepGrid (slicer::Pattern&, const PadLooks&, QWidget* parent);
    void setBank (int b) { bank = b; update(); }
    void setSelected (int pad) { selected = pad; update(); }
    void setPlayhead (bool playing, double beat);   // beat: from the pattern's start
    int columns() const;       // as many as the bank's longest lane, in fours of four
    static constexpr int header = 132;
    QSize sizeHint() const override { return { 720, rowHeight * slicer::padsPerBank + 2 }; }
    QSize minimumSizeHint() const override { return { 420, rowHeight * slicer::padsPerBank + 2 }; }

Q_SIGNALS:
    void laneEdited (int lane);
    void padChosen (int pad);

protected:
    void paintEvent (class QPaintEvent*) override;
    void mousePressEvent (class QMouseEvent*) override;
    void mouseMoveEvent (class QMouseEvent*) override;
    void mouseReleaseEvent (class QMouseEvent*) override;

private:
    static constexpr int rowHeight = 16;
    QRectF cell (int row, int column) const;
    int columnAt (double x) const;   // -1: in the header
    void paintStep (QPointF at);

    slicer::Pattern& pattern;
    const PadLooks& looks;
    int bank = 0, selected = 0, paintRow = -1;
    bool paintOn = false, playing = false;
    double beat = 0.0;
};

// One setting of a lane's steps as bars: drag across to draw them.
class StepValues final : public QWidget
{
    Q_OBJECT

public:
    enum Which { velocity, gate, pitch, chance, repeats, ramp, shift };
    StepValues (slicer::Pattern&, QWidget* parent);
    void setLane (int l) { lane = l; update(); }
    void setWhich (Which w) { which = w; update(); }
    void setColumns (int n) { shown = n; update(); }
    QSize sizeHint() const override { return { 720, 36 }; }
    QSize minimumSizeHint() const override { return { 420, 30 }; }

Q_SIGNALS:
    void laneEdited (int lane);

protected:
    void paintEvent (class QPaintEvent*) override;
    void mousePressEvent (class QMouseEvent*) override;
    void mouseMoveEvent (class QMouseEvent*) override;
    void leaveEvent (class QEvent*) override;

private:
    void draw (QPointF at);
    QRectF bars() const;

    slicer::Pattern& pattern;
    int lane = 0, shown = 16, hovered = -1;
    Which which = velocity;
};

// The sequencer's controls, the grid and the lane of values. Patterns 1 to 16: choosing
// one edits it and plays it next (from the next bar while playing); a chain plays them in
// turn. Follow Mixer plays in time with the Mixer's transport and tempo instead of its own.
class StepSequencerView final : public QWidget
{
    Q_OBJECT

public:
    StepSequencerView (slicer::Engine&, PadLooks, QWidget* parent);
    void setBank (int);
    void setSelected (int pad);
    void refresh();            // the engine's song again (a kit loaded with its patterns)
    void looksChanged();       // pads renamed or coloured
    // The Mixer's beat and tempo, for Follow Mixer; empty: no Mixer to follow.
    void setMixerClock (std::function<slicer::Engine::Clock()> make);
    void playOrStop();

Q_SIGNALS:
    void padChosen (int pad);

protected:
    void showEvent (class QShowEvent*) override;
    void hideEvent (class QHideEvent*) override;

private:
    void showPattern (int index);
    void laneEdited (int lane);
    void laneToControls();
    void laneFromControls();
    void chainEdited();
    void follow (bool on);
    void tick();
    slicer::Lane& lane() { return shown.lanes[(size_t) selected]; }

    slicer::Engine& engine;
    PadLooks looks;
    slicer::Pattern shown;     // the pattern being edited
    std::unique_ptr<slicer::Pattern> copied;
    std::function<slicer::Engine::Clock()> mixerClock;
    int index = 0, selected = 0, bank = 0, playingWas = -1, queuedWas = -2;
    unsigned recordedWas = 0;
    bool updating = false;

    StepGrid* grid;
    StepValues* values;
    QPushButton* playButton;
    QCheckBox* recordBox;
    QCheckBox* followBox;
    QSpinBox* tempoBox;
    QButtonGroup* patterns;
    QSpinBox* barsBox;
    QSpinBox* swingBox;
    QLineEdit* chainEdit;
    QCheckBox* chainBox;
    QLabel* laneTitle;
    QSpinBox* stepsBox;
    QComboBox* rateBox;
    QComboBox* directionBox;
    QSpinBox* offsetBox;
    QSpinBox* laneSwingBox;
    QComboBox* valuesBox;
    QTimer timer;
};

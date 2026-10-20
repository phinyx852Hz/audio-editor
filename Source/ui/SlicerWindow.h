// SPDX-License-Identifier: AGPL-3.0-only
#pragma once

#include "../core/AudioData.h"
#include "../core/BeatGrid.h"
#include "../sampler/SlicerEngine.h"

#include <QTimer>
#include <QWidget>
#include <functional>
#include <vector>

class QButtonGroup;
class QCheckBox;
class QComboBox;
class QDoubleSpinBox;
class QLabel;
class QSpinBox;
class QStackedWidget;

// The sound with a line where each slice starts. Click a slice to choose and play its
// pad; drag a line to move it, double-click to add one, right-click a line to remove it.
class SliceView final : public QWidget
{
    Q_OBJECT

public:
    SliceView (slicer::Engine&, QWidget* parent);
    void setSample (sampler::SamplePtr);
    void setKit (const slicer::Kit& k) { kit = k; update(); }
    void setSelected (int pad) { selected = pad; update(); }
    QSize sizeHint() const override { return { 620, 120 }; }
    QSize minimumSizeHint() const override { return { 300, 100 }; }

    // Moves the line at `from` towards `to`; gives where it landed.
    std::function<Frame (Frame from, Frame to)> moveCut;

Q_SIGNALS:
    void padPressed (int pad);
    void padReleased (int pad);
    void cutAdded (Frame at);
    void cutRemoved (Frame at);

protected:
    void paintEvent (class QPaintEvent*) override;
    void resizeEvent (class QResizeEvent*) override;
    void mousePressEvent (class QMouseEvent*) override;
    void mouseMoveEvent (class QMouseEvent*) override;
    void mouseReleaseEvent (class QMouseEvent*) override;
    void mouseDoubleClickEvent (class QMouseEvent*) override;

private:
    double xFor (double frame) const;
    Frame frameAt (double x) const;
    Frame cutNear (double x) const;   // -1: none within reach
    int padAt (Frame) const;
    void buildPeaks();

    slicer::Engine& engine;
    sampler::SamplePtr sample;
    slicer::Kit kit;
    std::vector<std::pair<float, float>> peaks;
    int selected = 0, pressedPad = -1;
    Frame dragging = -1;
};

// One bank of 16 pads as on Push: pad 1 at the bottom left. Click to play (higher on
// the pad is louder); lit pads sound, the outlined one is being edited.
class PadGrid final : public QWidget
{
    Q_OBJECT

public:
    PadGrid (slicer::Engine&, QWidget* parent);
    void setBank (int b) { bank = b; update(); }
    void setSelected (int pad) { selected = pad; update(); }
    void setKit (const slicer::Kit& k) { kit = k; update(); }
    QSize sizeHint() const override { return { 270, 270 }; }
    QSize minimumSizeHint() const override { return { 220, 220 }; }
    bool hasHeightForWidth() const override { return true; }
    int heightForWidth (int w) const override { return w; }

Q_SIGNALS:
    void padPressed (int pad, float velocity);
    void padReleased (int pad);

protected:
    void paintEvent (class QPaintEvent*) override;
    void mousePressEvent (class QMouseEvent*) override;
    void mouseReleaseEvent (class QMouseEvent*) override;

private:
    QRectF padRect (int index) const;   // index 0..15 in the bank

    slicer::Engine& engine;
    slicer::Kit kit;
    int bank = 0, selected = 0, pressed = -1;
};

// Sample > Slicer: one sound cut into slices across 64 pads in four banks of 16, laid
// out for Push's 8x8 grid (or chromatically, as Live's drum racks). Played from MIDI,
// by clicking, or from the computer keyboard: Z X C V are pads 1 to 4, A S D F 5 to 8,
// Q W E R 9 to 12 and 1 2 3 4 13 to 16 of the bank showing; 5 6 7 8 choose banks A to D.
class SlicerWindow final : public QWidget
{
    Q_OBJECT

public:
    SlicerWindow (slicer::Engine&, QWidget* parent);

    // A new sound at the engine's rate, with the editor's tempo grid (bar 1 measured
    // from the sound's start) and its markers (in the sound's frames) for slicing.
    // `keepSlices`: the slices stay when they still fit (refreshing after an edit).
    void setSample (sampler::SamplePtr, AudioDataPtr audio, double rate, const QString& name, bool keepSlices,
                    const BeatGrid& grid, const std::vector<Frame>& markers);
    bool followsEditor() const;
    void setMidiStatus (const QString&);

Q_SIGNALS:
    void refreshRequested();
    void midiInputsRequested();
    void activated();
    void closed();

protected:
    void keyPressEvent (class QKeyEvent*) override;
    void keyReleaseEvent (class QKeyEvent*) override;
    void changeEvent (QEvent*) override;
    bool eventFilter (QObject*, QEvent*) override;
    void closeEvent (class QCloseEvent*) override;

private:
    void slice();
    Frame moveCut (Frame from, Frame to);
    void applyCuts (std::vector<Frame> cuts);
    std::vector<Frame> currentCuts() const;
    void setKit (const slicer::Kit&);
    void select (int pad);
    void setBank (int bank);
    void padToControls();
    void padFromControls();
    void methodChanged();
    void showStatus();
    Frame snap (Frame) const;

    slicer::Engine& engine;
    slicer::Kit kit;
    sampler::SamplePtr sample;
    AudioDataPtr audio;
    BeatGrid grid;
    std::vector<Frame> markers;
    double rate = 48000.0;
    QString name;
    int bank = 0, selected = 0, lastSeenHit = -1, found = 0;

    QLabel* title;
    QLabel* status;
    QLabel* midiStatus;
    QLabel* padTitle;
    SliceView* view;
    PadGrid* pads;
    QButtonGroup* banks;
    QComboBox* methodBox;
    QStackedWidget* methodOptions;
    QSpinBox* sensitivityBox;
    QSpinBox* partsBox;
    QComboBox* divisionBox;
    QCheckBox* followBox;
    QCheckBox* snapBox;
    QComboBox* layoutBox;
    QSpinBox* velocityBox;
    QSpinBox* voicesBox;
    QDoubleSpinBox* gainBox;
    QSpinBox* panBox;
    QSpinBox* pitchBox;
    QCheckBox* reverseBox;
    QComboBox* playBox;
    QDoubleSpinBox* attackBox;
    QDoubleSpinBox* releaseBox;
    QSpinBox* chokeBox;
    QTimer redraw;
    std::vector<std::pair<int, int>> heldKeys;   // scan code, pad
    bool updating = false;
};

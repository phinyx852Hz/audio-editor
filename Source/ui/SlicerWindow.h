// SPDX-License-Identifier: AGPL-3.0-only
#pragma once

#include "../core/AudioData.h"
#include "../core/BeatGrid.h"
#include "../sampler/SlicerEngine.h"
#include "StepSequencerView.h"

#include <QTimer>
#include <array>
#include <QWidget>
#include <functional>
#include <optional>
#include <vector>

class QButtonGroup;
class QCheckBox;
class QComboBox;
class QDoubleSpinBox;
class QLabel;
class QPushButton;
class QSpinBox;
class QStackedWidget;

// The sound with a line where each slice starts. Click a slice to choose and play its
// pad; drag a line to move it, double-click to add one, right-click a line to remove it.
// A pad with a sound of its own shows that sound instead, with the part it plays shaded.
class SliceView final : public QWidget
{
    Q_OBJECT

public:
    SliceView (slicer::Engine&, QWidget* parent);
    void setSample (sampler::SamplePtr);
    void setKit (const slicer::Kit& k);
    void setSelected (int pad);
    QSize sizeHint() const override { return { 620, 120 }; }
    QSize minimumSizeHint() const override { return { 160, 100 }; }

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
    const sampler::Sample* shown() const;   // the kit's sample, or the chosen pad's own sound
    bool ownSound() const;

    slicer::Engine& engine;
    sampler::SamplePtr sample;
    const sampler::Sample* peaksOf = nullptr;
    slicer::Kit kit;
    std::vector<std::pair<float, float>> peaks;
    int selected = 0, pressedPad = -1;
    Frame dragging = -1;
};

// One bank of 16 pads as on Push: pad 1 at the bottom left. Click to play (higher on
// the pad is louder); lit pads sound, the outlined one is being edited. An empty pad
// shows only its note, a pad with one sound its name, a layered pad "Multi"; M and S at
// its top left mute and solo it, as on Cubase's Drum Machine. Drag a pad onto another to swap them, drop audio files on a pad to load them (several fill
// upward from it), right-click for the pad's menu.
class PadGrid final : public QWidget
{
    Q_OBJECT

public:
    PadGrid (slicer::Engine&, QWidget* parent);
    void setBank (int b) { bank = b; update(); }
    void setSelected (int pad) { selected = pad; update(); }
    void setKit (const slicer::Kit& k) { kit = k; update(); }
    QSize sizeHint() const override { return { 230, 160 }; }
    QSize minimumSizeHint() const override { return { 230, 140 }; }

Q_SIGNALS:
    void padPressed (int pad, float velocity);
    void padReleased (int pad);
    void padMenu (int pad, QPoint globalPos);
    void padsSwapped (int from, int to);
    void filesDropped (int pad, const QStringList& paths);
    void muteSoloClicked (int pad, bool solo);   // its M or S

protected:
    void paintEvent (class QPaintEvent*) override;
    void mousePressEvent (class QMouseEvent*) override;
    void mouseMoveEvent (class QMouseEvent*) override;
    void mouseReleaseEvent (class QMouseEvent*) override;
    void contextMenuEvent (class QContextMenuEvent*) override;
    void dragEnterEvent (class QDragEnterEvent*) override;
    void dragMoveEvent (class QDragMoveEvent*) override;
    void dropEvent (class QDropEvent*) override;

private:
    QRectF padRect (int index) const;   // index 0..15 in the bank
    QRectF muteRect (int index) const;
    QRectF soloRect (int index) const;
    int padAt (QPointF) const;          // -1: between pads

    slicer::Engine& engine;
    slicer::Kit kit;
    int bank = 0, selected = 0, pressed = -1;
    QPointF pressedAt;
};

// Sample > Slicer: a drum machine of 128 pads in eight banks of 16 (A to H), laid out as
// Cubase's Drum Machine: the banks and pads on the left, the chosen pad's layer slots beside
// its sound in the middle (or the step sequencer, on the Steps tab), the pad's level, pan,
// pitch and settings on the right. Compact, to dock under the editor or the mixer. One sound cut into slices across the pads, and any pad can
// hold sounds of its own instead, up to four layers. Laid out for Push's 8x8 grid (four
// banks at a time) or chromatically. Played from MIDI, by clicking, or from the computer
// keyboard: Z X C V are pads 1 to 4, A S D F 5 to 8, Q W E R 9 to 12 and 1 2 3 4 13 to 16
// of the bank showing; 5 6 7 8 9 0 - = choose banks A to H. Steps shows the bank's pads
// in the step sequencer instead of the pads (Space plays and stops it).
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
    // A track's instrument editor in the mixer: closing it (Ctrl+W) leaves the instrument
    // playing, so notes that are sounding ring on. Otherwise closing silences it at once.
    void keepPlayingWhenClosed (bool keep) { keepsPlaying = keep; }
    // Hot swap: while it is on, a sound sent here (F8 in Find, or dropped on the window)
    // goes onto the chosen pad's layer to try; Keep keeps it, Undo Swap puts back what was there.
    bool hotSwapping() const;
    void hotSwap (const QString& path);
    // The Mixer's beat and tempo, for the step sequencer's Follow Mixer.
    void setMixerClock (std::function<slicer::Engine::Clock()> make);
    // A built-in kit, PHI tabla's (sampler/PhiTabla.h) or PHI frame drum's (sampler/PhiFrameDrum.h):
    // the Kit menu can make this kit, as edited here, the one every new one (`name`) starts from,
    // kept in `defaultFile`, or go back to the built-in one.
    void setBuiltInKit (const QString& name, const QString& defaultFile);
    void setPhiTabla();

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
    void hideEvent (class QHideEvent*) override;

private:
    void releaseKeys();
    void focusMoved (QWidget* old, QWidget* now);
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
    void layerToControls();
    void layerFromControls();
    void padMenu (int pad, QPoint at);
    void loadPadFiles (int pad, const QStringList& paths);
    void loadLayerFile();
    void endHotSwap (bool keep);
    void saveKit();
    void loadKit();
    QMenu* kitMenu = nullptr;
    bool phiTablaActions = false;
    void showPage (int page);   // 0: the pads, 1: the steps
    double soundRate() const;   // the rate pads' own sounds are read at
    void changed();             // the kit was edited here: to the engine and the views

    slicer::Engine& engine;
    slicer::Kit kit;
    sampler::SamplePtr sample;
    AudioDataPtr audio;
    BeatGrid grid;
    std::vector<Frame> markers;
    double rate = 48000.0;
    QString name;
    int bank = 0, selected = 0, layer = 0, lastSeenHit = -1, found = 0;
    std::optional<slicer::Pad> clipboard;
    std::optional<std::pair<int, slicer::Pad>> swapping;   // hot swap: the pad as it was

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
    QComboBox* playBox;
    QDoubleSpinBox* attackBox;
    QDoubleSpinBox* releaseBox;
    QSpinBox* chokeBox;
    QCheckBox* muteBox;
    QCheckBox* soloBox;
    QSpinBox* receiveBox;
    QComboBox* pageBox;
    QWidget* padArea;
    std::array<QPushButton*, slicer::maxLayers> layerPowers {};
    std::array<class Potentiometer*, slicer::maxLayers> layerLevels {};
    std::vector<std::function<void()>> enableFollowers;   // the knobs follow their boxes' enabling
    std::array<uint32_t, slicer::numBanks> bankLooks;      // what each bank button's icon shows
    QButtonGroup* layers;
    QCheckBox* layerOnBox;
    QLabel* layerSound;
    QDoubleSpinBox* layerGainBox;
    QDoubleSpinBox* layerPitchBox;
    QDoubleSpinBox* layerStartBox;
    QDoubleSpinBox* layerEndBox;
    QDoubleSpinBox* fadeInBox;
    QDoubleSpinBox* fadeOutBox;
    QCheckBox* reverseLayerBox;
    QPushButton* layerLoad;
    QPushButton* layerClear;
    QCheckBox* hotSwapBox;
    QPushButton* keepSwap;
    QPushButton* undoSwap;
    QStackedWidget* pages;
    QButtonGroup* pageButtons;
    StepSequencerView* stepsView;
    QTimer redraw;
    std::vector<std::pair<int, int>> heldKeys;   // scan code, pad
    bool updating = false;
    bool keepsPlaying = false;
};

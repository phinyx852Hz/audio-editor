// SPDX-License-Identifier: AGPL-3.0-only
#pragma once

#include "../patchbay/PatchBay.h"

#include <QTimer>
#include <QWidget>

#include <array>
#include <functional>

class AudioEngine;
class QLabel;
class QPushButton;

// The 8x8 grid: 8 sources (rows, outputs) against 8 destinations (columns, inputs).
// A lit jack is a patch; its colour is the source's, its glow what is going through.
// Click a jack to patch or unpatch; the wheel, a vertical drag or + and - set its gain
// (Shift for 0.1 dB), a double click or 0 puts it back to 0 dB. Jacks that would feed a
// signal back into itself are crossed out. Click a row or column name to put something
// else there, or to choose a synth's instrument or an FX bus's effect.
class PatchGrid final : public QWidget
{
    Q_OBJECT

public:
    static constexpr int size = 8;

    explicit PatchGrid (patchbay::PatchBay&, QWidget* parent);

    std::array<int, size> rows {}, columns {};    // nodes shown
    std::array<float, patchbay::numNodes> levels {};   // display levels, 0..1, falling
    std::function<QString (int node)> nameOf, detailOf;
    std::function<bool (int node)> activeOf;

    void tick();   // takes the peaks; repaints when anything moved
    QSize sizeHint() const override { return { 820, 640 }; }
    QSize minimumSizeHint() const override { return { 560, 420 }; }

Q_SIGNALS:
    void headerClicked (bool row, int slot, QPoint globalPos);
    void patched();                          // the graph or a gain changed
    void hovered (const QString& text);      // what is under the mouse (or the focused jack)

protected:
    void paintEvent (class QPaintEvent*) override;
    void mousePressEvent (class QMouseEvent*) override;
    void mouseMoveEvent (class QMouseEvent*) override;
    void mouseReleaseEvent (class QMouseEvent*) override;
    void mouseDoubleClickEvent (class QMouseEvent*) override;
    void wheelEvent (class QWheelEvent*) override;
    void keyPressEvent (class QKeyEvent*) override;
    void leaveEvent (QEvent*) override;
    void contextMenuEvent (class QContextMenuEvent*) override;

private:
    struct Hit { int row = -1, column = -1; bool header = false; };
    Hit hitAt (QPointF) const;
    QRectF cellRect (int row, int column) const;
    QRectF rowHeader (int row) const;
    QRectF columnHeader (int column) const;
    double headerWidth() const;
    double headerHeight() const;
    double cell() const;
    void toggle (int row, int column);
    void nudge (int row, int column, float db);
    void setGain (int row, int column, float db);
    void describe (int row, int column);
    QColor kindColour (int node) const;

    patchbay::PatchBay& bay;
    int hoverRow = -1, hoverColumn = -1;
    int focusRow = 0, focusColumn = 0;
    int dragRow = -1, dragColumn = -1;
    double dragY = 0.0;
    float dragDb = 0.0f;
    bool dragMoved = false;
};

// The patchbay in its own window (Mixer > Patchbay, F4): it fills itself from what is in
// use (the editor's file, the Sampler and Slicer, the mixer's tracks, the synths and FX
// buses) and keeps every patch, shown or not.
class PatchBayWindow final : public QWidget
{
    Q_OBJECT

public:
    struct Hooks
    {
        std::function<bool (int node)> active;          // in use now
        std::function<QString (int node)> detail;       // the file, instrument or effect it carries
        std::function<void (int node)> open;            // its own window: Sampler, Slicer, Mixer
        std::function<void (int synth)> playSynth;      // the MIDI keyboard plays this synth
    };

    PatchBayWindow (patchbay::PatchBay&, AudioEngine&, Hooks, QWidget* parent);

    // Which nodes are shown, and whether they follow what is in use.
    std::array<int, PatchGrid::size> shownRows() const { return grid->rows; }
    std::array<int, PatchGrid::size> shownColumns() const { return grid->columns; }
    void setShown (const std::array<int, PatchGrid::size>& rows, const std::array<int, PatchGrid::size>& columns);
    bool autoFill() const { return autoFilling; }
    void setAutoFill (bool);
    void refill();               // rows and columns from what is in use (with auto-fill on)
    void setMidiStatus (const QString&);

    QString nodeName (int node) const;

Q_SIGNALS:
    void changed();              // something to save
    void activated();
    void closed();

protected:
    void changeEvent (QEvent*) override;
    void closeEvent (class QCloseEvent*) override;

private:
    void headerMenu (bool row, int slot, QPoint at);
    void chooseInstrument (int synth, const std::string& pluginId);   // "" none, "synth" the built-in one
    void chooseEffect (int bus, const std::string& pluginId);         // "" none
    void editNode (int node);
    void refreshInfo();          // what is in use and what each node carries, asked every half second


    patchbay::PatchBay& bay;
    AudioEngine& audio;
    Hooks hooks;
    PatchGrid* grid;
    QPushButton* autoButton;
    QLabel* status;
    QLabel* midiStatus;
    QTimer meters;
    std::array<bool, patchbay::numNodes> inUse {};
    std::array<QString, patchbay::numNodes> details;
    int sinceRefill = 0;
    bool autoFilling = true;
};

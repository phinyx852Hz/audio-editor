// SPDX-License-Identifier: AGPL-3.0-only
#pragma once

// MIDI Map mode (Ctrl+M, docs/control-surfaces.md), as in Live: laid over the mixer, it tints
// every control a MIDI control can be mapped to and writes the mapping on those mapped. Click one,
// then move a knob or press a key or button on any keyboard or controller: it is mapped. Delete
// takes the clicked control's mappings away; Escape (or Ctrl+M again) leaves the mode.
#include "MixerWindow.h"
#include "../control/MidiLearn.h"

#include <QTimer>
#include <QWidget>

#include <functional>
#include <optional>

class MidiMapOverlay final : public QWidget
{
public:
    MidiMapOverlay (MixerWindow& mixer, control::Learn& learn);

    std::function<void()> mappingsChanged;   // Delete took mappings away (to save them)
    std::function<void()> finished;          // Escape

    void learned();   // a control was mapped just now

protected:
    void paintEvent (QPaintEvent*) override;
    void mousePressEvent (QMouseEvent*) override;
    void wheelEvent (QWheelEvent*) override;
    void keyPressEvent (QKeyEvent*) override;
    bool eventFilter (QObject*, QEvent*) override;

private:
    struct Shown
    {
        QRect rect;   // in the overlay
        MixerWindow::Mappable control;
    };
    std::vector<Shown> shown() const;
    static bool same (const control::Command&, const control::Command&);
    static QString label (const control::Command&);   // "Track 2 Volume"
    static QString shortSource (const control::Mapping&);   // "CC 21"

    MixerWindow& mixer;
    control::Learn& learn;
    std::optional<control::Command> chosen;
    QTimer redraw;   // the mixer scrolls and resizes under it
};

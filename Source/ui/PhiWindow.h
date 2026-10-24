// SPDX-License-Identifier: AGPL-3.0-only
#pragma once

#include "../engine/AudioEngine.h"

#include <QDialog>
#include <QDial>
#include <vector>

class QLabel;
class QTimer;
class QToolButton;

// A Φ effect's window, laid out by the golden ratio: a golden rectangle (809 x 500 to
// start), the gold Φ and the effect's name across the top, the effect's picture (its
// DeviceDisplay: the EQ curve, the taps, the cuts it makes...) in the upper 0.618, and
// gold knobs below, large for the main controls and smaller for the rest, with
// switches as pills. Black and gold whatever the theme.
class PhiWindow final : public QDialog
{
public:
    PhiWindow (AudioEngine&, ChainPath, QWidget* parent);

protected:
    void keyPressEvent (class QKeyEvent*) override;

private:
    void sync (bool parameters);
    void buildKnobs();

    AudioEngine& engine;
    ChainPath path;
    std::string pluginId;
    class PhiDisplay* display;
    QWidget* knobArea = nullptr;
    struct Control
    {
        int index = 0;
        int steps = 0;              // the knob's range: 1000 for continuous parameters
        class PhiKnob* knob = nullptr;
        QToolButton* toggle = nullptr;
        QLabel* value = nullptr;
    };
    std::vector<Control> controls;
    int ticks = 0;
};

// The parameter window for any device: a Φ window for the Φ effects, else the list of
// sliders (ParameterDialog). Shown, and deleted when closed.
QDialog* openParameterWindow (AudioEngine&, const ChainPath&, QWidget* parent);

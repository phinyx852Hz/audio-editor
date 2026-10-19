// SPDX-License-Identifier: AGPL-3.0-only
#pragma once

#include "../core/SpectrumView.h"

#include <QDialog>

class QComboBox;
class QDoubleSpinBox;
class QSpinBox;
class WaveformView;

// View > Spectrogram Settings: Sonic Visualiser's spectrogram controls. Every
// change shows at once in the spectral display; nothing to confirm.
class SpectrogramDialog final : public QDialog
{
    Q_OBJECT

public:
    SpectrogramDialog (WaveformView&, QWidget* parent);

    void refresh();   // shows the view's settings and frequency range

Q_SIGNALS:
    void changed();   // the view's settings changed (to be remembered)

private:
    void apply();
    void applyRange();

    WaveformView& view;
    bool filling = false;
    QComboBox* windowSize;
    QComboBox* windowShape;
    QComboBox* overlap;
    QComboBox* oversample;
    QComboBox* scale;
    QComboBox* bins;
    QComboBox* colours;
    QComboBox* normalise;
    QDoubleSpinBox* gain;
    QDoubleSpinBox* range;
    QSpinBox* lowHz;
    QSpinBox* highHz;
};

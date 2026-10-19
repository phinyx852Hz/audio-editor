// SPDX-License-Identifier: AGPL-3.0-only
#pragma once

#include "../core/AudioData.h"
#include "../core/SpectrumView.h"

#include <QColor>
#include <QImage>
#include <QVector>

// A rendered spectrogram: the picture, one lane per channel, and the level in
// dB behind every pixel (for the readout under the mouse).
struct SpectrogramPicture
{
    QImage image;
    std::vector<std::vector<float>> levels;   // per channel, row by row from the top
    int columns = 0, rows = 0;                // per lane

    float levelAt (int channel, int x, int y) const;   // spectrum::silent outside
};

// Renders frames [viewStart, viewStart + viewLength) with the settings over the
// frequency range [low, high] (high 0 = half the sample rate), coloured with the
// settings' colour map or, for the theme's, `themeGradient`. Safe on a worker thread.
SpectrogramPicture renderSpectrogram (const AudioData&, double viewStart, double viewLength, QSize size,
                                      const QVector<QColor>& themeGradient, const spectrum::Settings&,
                                      double low, double high);

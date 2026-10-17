// SPDX-License-Identifier: AGPL-3.0-only
#pragma once

#include "../core/AudioData.h"

#include <QColor>
#include <QImage>
#include <QVector>

// Renders frames [viewStart, viewStart + viewLength) as a spectrogram image,
// one lane per channel, log frequency (20 Hz at the bottom), -120..0 dBFS
// mapped onto `gradient`. Safe to call from a worker thread.
QImage renderSpectrogram (const AudioData&, double viewStart, double viewLength, QSize size, const QVector<QColor>& gradient);

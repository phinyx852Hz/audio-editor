// SPDX-License-Identifier: AGPL-3.0-only
#pragma once

#include "../core/AudioDocument.h"

#include <QFutureWatcher>
#include <QImage>
#include <QWidget>
#include <vector>

// Waveform display with cursor, selection and markers. Keyboard focusable;
// all navigation is driven through commands so every key can be remapped.
// Mouse: click places the cursor, drag selects, Shift+click extends.
class WaveformView final : public QWidget
{
    Q_OBJECT

public:
    explicit WaveformView (QWidget* parent = nullptr);

    // Keeps the current zoom when the length is unchanged, otherwise fits.
    void setData (AudioDataPtr, bool keepView);
    void setMarkers (std::vector<Marker>);
    void clear();
    void setLoading (bool, float progress = 0.0f);

    void setCursorFrame (Frame);
    void setSelection (Frame start, Frame end);  // empty when start == end

    // Visible range in frames, so each editor tab can keep its own zoom.
    std::pair<double, double> view() const { return { viewStart, viewLength }; }
    void setView (double start, double length);

    void zoomBy (double factor);
    void zoomToFit();
    void zoomToSelection();

    // Spectral display (log frequency) instead of the waveform.
    void setSpectral (bool);
    bool isSpectral() const { return spectral; }

Q_SIGNALS:
    void cursorRequested (Frame);
    void selectionRequested (Frame anchor, Frame end);

protected:
    void paintEvent (QPaintEvent*) override;
    void mousePressEvent (QMouseEvent*) override;
    void mouseMoveEvent (QMouseEvent*) override;
    void mouseReleaseEvent (QMouseEvent*) override;
    void wheelEvent (QWheelEvent*) override;
    void focusInEvent (QFocusEvent*) override  { update(); }
    void focusOutEvent (QFocusEvent*) override { update(); }

private:
    Frame length() const { return data ? data->length() : 0; }
    Frame xToFrame (double x) const;
    double frameToX (Frame) const;
    void keepVisible (Frame);
    void clampView();
    void requestSpectrogram();

    struct SpectrogramKey
    {
        const AudioData* data = nullptr;
        double viewStart = 0.0, viewLength = 0.0;
        QSize size;
        QString theme;
        bool operator== (const SpectrogramKey&) const = default;
    };

    SpectrogramKey currentKey() const;

    AudioDataPtr data;
    std::vector<Marker> markers;
    double viewStart = 0.0, viewLength = 0.0;  // in frames
    Frame cursor = 0, selStart = 0, selEnd = 0;
    Frame dragAnchor = -1;
    bool loading = false;
    float loadProgress = 0.0f;

    bool spectral = false;
    QImage spectrogram;
    SpectrogramKey spectrogramKey, pendingKey;
    QFutureWatcher<QImage> spectrogramWatcher;
};

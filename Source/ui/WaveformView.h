// SPDX-License-Identifier: AGPL-3.0-only
#pragma once

#include "../engine/AudioEngine.h"

#include <QWidget>
#include <memory>

// Waveform display with a cursor. Keyboard focusable; all navigation is
// driven through commands so every key can be remapped.
class WaveformView final : public QWidget
{
    Q_OBJECT

public:
    explicit WaveformView (QWidget* parent = nullptr);

    void setPeaks (std::shared_ptr<const WaveformPeaks>);
    void clear();
    void setLoading (bool);

    void setCursorSeconds (double seconds);
    double cursorSeconds() const { return cursor; }

    void zoomBy (double factor);
    void zoomToFit();

Q_SIGNALS:
    void cursorRequested (double seconds);

protected:
    void paintEvent (QPaintEvent*) override;
    void mousePressEvent (QMouseEvent*) override;
    void focusInEvent (QFocusEvent*) override  { update(); }
    void focusOutEvent (QFocusEvent*) override { update(); }

private:
    double lengthSeconds() const;
    double xToTime (double x) const;
    double timeToX (double t) const;
    void keepCursorVisible();

    std::shared_ptr<const WaveformPeaks> peaks;
    double viewStart = 0.0, viewLength = 0.0, cursor = 0.0;
    bool loading = false;
};

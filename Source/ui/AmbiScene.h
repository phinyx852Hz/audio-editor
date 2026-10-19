// SPDX-License-Identifier: AGPL-3.0-only
#pragma once

#include <QWidget>
#include <vector>

// Where the sound is in an ambisonic file, at the playhead: a map of the whole
// sphere (azimuth across, -180 at the left edge, front in the middle, height up),
// brighter where there is more energy. Fed by MainWindow while it is showing.
class AmbiScene final : public QWidget
{
public:
    static constexpr int cols = 72, rows = 36;

    explicit AmbiScene (QWidget* parent);

    // Channels of an ambisonic file around the playhead; none (or not ambisonic) clears it.
    void analyse (const std::vector<std::vector<float>>& channels, bool ambisonic);
    QSize sizeHint() const override { return { 420, 240 }; }

protected:
    void paintEvent (QPaintEvent*) override;

private:
    std::vector<float> map;   // smoothed, 0 to 1
    bool active = false;
};

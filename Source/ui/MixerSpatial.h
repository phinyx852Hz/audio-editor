// SPDX-License-Identifier: AGPL-3.0-only
#pragma once

// Spatial mixing in the mixer (owner, 2026-10-28; docs/spatial-mixing.md): the room seen from
// above, with every source the scene has (the tracks, the Pre-Master, the returns) and the
// speakers; and the Spatial dialog, where the mode is turned on and the outputs are chosen.

#include <QDialog>
#include <QWidget>

#include <vector>

class MixerWindow;
class QCheckBox;
class QComboBox;
class QLabel;
class QSpinBox;
namespace mixer { struct Panner3d; }

// Drag a source to place it (its direction and distance; Shift keeps the distance), the wheel
// over it raises or lowers it, Shift+wheel widens it; double-click puts it back ahead.
class RoomView final : public QWidget
{
public:
    RoomView (MixerWindow&, QWidget* parent);
    QSize sizeHint() const override { return { 260, 260 }; }
    bool hasHeightForWidth() const override { return true; }
    int heightForWidth (int w) const override { return w; }

protected:
    void paintEvent (class QPaintEvent*) override;
    void mousePressEvent (class QMouseEvent*) override;
    void mouseMoveEvent (class QMouseEvent*) override;
    void mouseReleaseEvent (class QMouseEvent*) override;
    void mouseDoubleClickEvent (class QMouseEvent*) override;
    void wheelEvent (class QWheelEvent*) override;
    bool event (QEvent*) override;   // tooltips

private:
    struct Source
    {
        int key;              // a track, or preMasterKey, or returnKey - r
        mixer::Panner3d* panner;
        QString label, name;
        QColor colour;
    };
    static constexpr int preMasterKey = -1, returnKey = -2;
    std::vector<Source> sources() const;
    QPointF centre() const;
    double radius() const;
    QPointF where (double azimuth, double distance) const;
    int sourceAt (QPointF) const;   // index in sources(), or -1
    void moved();

    MixerWindow& window;
    int dragging = -100;  // a source's key while it is dragged; -100: none
    bool dragged = false;
};

// The master's meter in Spatial mode: a bar for each speaker of the array (the subwoofers too),
// with its name and a peak hold, from what the engine fed it since the last look.
class SpeakerMeter final : public QWidget
{
public:
    SpeakerMeter (MixerWindow&, QWidget* parent);
    QSize sizeHint() const override { return { 260, 90 }; }

protected:
    void paintEvent (class QPaintEvent*) override;

private:
    void take();   // the engine's peaks, falling 20 dB a second between them

    MixerWindow& window;
    std::vector<float> level, hold;   // dB
    std::vector<int> held;            // frames each hold has stayed
};

class SpatialDialog final : public QDialog
{
public:
    SpatialDialog (MixerWindow&, QWidget* parent);
    void refresh();   // from the window and the settings

private:
    void fillSpeakers();
    void save();      // the choices into the settings, then the decoder made again

    MixerWindow& window;
    QCheckBox* onBox;
    QComboBox* phonesBox;
    QComboBox* hrtfBox;
    QComboBox* speakersBox;
    QSpinBox* firstBox;
    QSpinBox* yawBox;
    QComboBox* renderBox;
    RoomView* room;
    SpeakerMeter* meter;
    QLabel* status;
    bool updating = false;
};

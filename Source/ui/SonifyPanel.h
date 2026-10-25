// SPDX-License-Identifier: AGPL-3.0-only
#pragma once

#include "../core/Sonify.h"

#include <QWidget>

class QAbstractButton;
class QCheckBox;
class QComboBox;
class QLabel;
class QPushButton;
class QSlider;
class QSpinBox;

// The Sonify bar of the Visualizer window: what is played (the visualizer itself, a
// picture or video file, a camera or the web page behind PHI), how (scan or lanes,
// free pitch to scale notes, tempo, voices, drums) and on what (the built-in Synth or
// whatever the MIDI keyboard plays). MainWindow does the playing (SonifySection.cpp).
class SonifyPanel final : public QWidget
{
    Q_OBJECT

public:
    enum Source { visualizer, file, camera, web };
    enum Target { builtInSynth, keyboard };

    SonifyPanel (QWidget* parent, bool webPage);

    sonify::Settings settings() const;
    void setSettings (const sonify::Settings&);
    int target() const;
    void setTarget (int);
    int source() const;
    void setSource (int);   // without asking for a file or a camera again
    void setPlaying (bool);
    void setNote (const QString&);   // what is playing, or why not

Q_SIGNALS:
    void changed();                    // a setting or the target
    void playToggled (bool);
    void sourceChosen (int source);    // picked in the list (a file or camera is to be chosen)

private:
    QPushButton* play;
    QComboBox *sources, *mode, *scale, *key, *targets;
    QSlider *quantize, *threshold;
    QSpinBox *tempo, *voices, *columns, *low, *high, *bend;
    QCheckBox* drums;
    QAbstractButton* fibonacci;   // a Φ button
    QLabel* note;
    bool quiet = false;
};

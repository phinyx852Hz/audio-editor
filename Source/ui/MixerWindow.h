// SPDX-License-Identifier: AGPL-3.0-only
#pragma once

#include "../core/AudioData.h"
#include "../mixer/MixerEngine.h"

#include <QColor>
#include <QPointer>
#include <QTimer>
#include <QWidget>

#include <array>
#include <functional>
#include <tuple>

class AudioEngine;
class QCheckBox;
class QDoubleSpinBox;
class QLabel;
class QPushButton;
class QHBoxLayout;
class MixerStrip;

// A level meter for a strip: peak and RMS for both sides, with a peak hold and a
// clip light (click to clear it).
class LevelMeter final : public QWidget
{
public:
    explicit LevelMeter (QWidget* parent);
    void setLevels (const mixer::Meter&, double seconds);   // seconds since the last call
    float highestPeak() const { return highest; }
    void clear();
    QSize sizeHint() const override { return { 18, 160 }; }
    QSize minimumSizeHint() const override { return { 14, 60 }; }

protected:
    void paintEvent (class QPaintEvent*) override;
    void mousePressEvent (class QMouseEvent*) override;

private:
    float peak[2] {}, rms[2] {}, hold[2] {};
    double holdAge[2] {};
    bool clipped = false;
    float highest = 0.0f;
};

// The mixer (docs/mixer.md): up to 8 tracks and the master out, in its own window.
// Every track can hold an audio file, a MIDI file and an instrument, together: the
// MIDI file (and the MIDI keyboard, on the armed track) plays the instrument and both
// it and the audio are heard. No MIDI editing. Keyboard-first: 1-8 pick a track,
// 9 the master, Left/Right move, Up/Down move the fader (Shift finer), 0 resets it,
// M mute, S solo, R arms the track for the MIDI keyboard, Space plays and stops,
// Home goes to the start, L loops, B bounces in place and Shift+B to a new track,
// Ctrl+T adds a track and Ctrl+Shift+T removes the last.
class MixerWindow final : public QWidget
{
    Q_OBJECT

public:
    // Where a track's Sampler or Slicer can take its sound from: the Sample menu's own instruments.
    struct Hooks
    {
        std::function<std::pair<sampler::SamplePtr, sampler::Settings>()> samplerSound;
        std::function<std::pair<sampler::SamplePtr, slicer::Kit>()> slicerSound;
    };

    MixerWindow (mixer::Engine&, AudioEngine&, Hooks, QWidget* parent);
    ~MixerWindow() override;

    void setMidiStatus (const QString&);

    // Mix files (.phinyxmix, JSON): the tracks, their files and settings, instruments included.
    void newMix();
    void openMix();
    bool saveMix (bool saveAs);
    bool loadMix (const QString& path, QString& error);
    bool writeMix (const QString& path, QString& error);
    void autosave();      // the mix as it is, kept in the config folder and opened again next time
    void restoreAutosave();
    QString mixName() const;

    void addTrack();
    void removeTrack();
    void togglePlay();
    void toStart();

Q_SIGNALS:
    void mixdownRequested();
    void midiInputsRequested();
    void activated();
    void closed();
    void playStarted();   // so the main window makes sure the mixer is heard

protected:
    void keyPressEvent (class QKeyEvent*) override;
    void changeEvent (QEvent*) override;
    void closeEvent (class QCloseEvent*) override;

private:
    friend class MixerStrip;

    // What the window keeps about a track besides the engine's settings.
    struct TrackInfo
    {
        QString name;
        QColor colour;
        AudioDataPtr sound;          // the track Sampler or Slicer's sound, for its window
        QPointer<QWidget> editor;    // its Sampler, Slicer or Synth window
        int loading = 0;             // a newer load outdates one still running
        bool busy = false;           // an audio file is loading
    };

    void rebuildStrips();
    void select (int index);         // 0..7 a track, 8 the master
    void tick();
    void refreshTransport();
    void loadAudio (int track, const QString& path);
    bool loadMidi (int track, const QString& path, QString& error);
    void chooseAudio (int track);
    void chooseMidi (int track);
    void placeAllMidi();             // again, after a tempo change
    void setInstrumentKind (int track, int choice, const QString& pluginId);
    void editInstrument (int track);
    void loadInstrumentSound (int track, const QString& path);   // into the track's Sampler or Slicer
    void takeInstrumentSound (int track);                         // from the Sample menu's
    void arm (int track);
    // Renders the track (MIDI through its instrument, and its audio) to a WAV: in place
    // it becomes the track's audio, else it goes on a new track and this one is muted.
    void bounce (int track, bool inPlace);
    void changed();                  // the mix differs from its file
    void updateTitle();
    double rate() const;
    QString folder() const;

    mixer::Engine& engine;
    AudioEngine& audio;
    Hooks hooks;
    std::array<TrackInfo, mixer::maxTracks> info;
    std::array<MixerStrip*, mixer::maxTracks> strips {};
    QWidget* stripArea;
    QHBoxLayout* stripRow;
    LevelMeter* masterMeter;
    class QSlider* masterFader;
    QLabel* masterValue;
    QLabel* masterPeak;
    QWidget* masterStrip;
    QPushButton* playButton;
    QPushButton* loopButton;
    QLabel* positionLabel;
    QDoubleSpinBox* tempoBox;
    QCheckBox* followTempoBox;
    QLabel* midiStatus;
    QLabel* status;
    QTimer timer;
    qint64 lastTick = 0;
    int selected = 0;
    QString path;                    // the mix file; empty: not saved yet
    bool modified = false;
    bool restoring = false;
};

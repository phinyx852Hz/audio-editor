// SPDX-License-Identifier: AGPL-3.0-only
#pragma once

#include "../core/AudioData.h"
#include "../mixer/MixerEngine.h"

#include <QColor>
#include <QPointer>
#include <QTimer>
#include <QWidget>

#include <array>
#include <map>
#include <memory>
#include <vector>
#include <functional>
#include <tuple>

class AudioEngine;
class QCheckBox;
class QDoubleSpinBox;
class QLabel;
class QPushButton;
class QHBoxLayout;
class MixerStrip;
class QSplitter;
class TrackLanes;
class FileBrowser;

// A level meter for a strip: peak and RMS for both sides, with a peak hold and a
// clip light (click to clear it).
class LevelMeter final : public QWidget
{
public:
    explicit LevelMeter (QWidget* parent);
    void setLevels (const mixer::Meter&, double seconds);   // seconds since the last call
    float highestPeak() const { return highest; }
    void clear();
    void setScale (bool on) { scale = on; updateGeometry(); update(); }   // dBFS marks to the right
    QSize sizeHint() const override { return { 18 + (scale ? scaleWidth : 0), 160 }; }
    QSize minimumSizeHint() const override { return { 14 + (scale ? scaleWidth : 0), 60 }; }

protected:
    void paintEvent (class QPaintEvent*) override;
    void mousePressEvent (class QMouseEvent*) override;

private:
    float peak[2] {}, rms[2] {}, hold[2] {};
    double holdAge[2] {};
    bool clipped = false;
    bool scale = false;
    float highest = 0.0f;
    static constexpr int scaleWidth = 24;
};

// The mixer (docs/mixer.md): up to 8 tracks and the master out, in its own window.
// Every track is a timeline of clips, each an audio file or a MIDI file, plus an
// instrument: the MIDI clips (and the MIDI keyboard, on the armed track) play it. No
// MIDI editing. Three sections, gone round with F6 or Tab: the file browser (Enter puts
// a file on the selected track at the playhead), the tracks across time (clips dragged,
// trimmed, split), and the strips. Keyboard-first: 1-8 pick a track, 9 the master,
// Left/Right move, Up/Down move the fader (Shift finer), 0 resets it, M mute, S solo,
// R arms the track for the MIDI keyboard, Space plays and stops, Home goes to the start,
// L loops, V shows the tracks across time, Ctrl+B the browser, B bounces in place and
// Shift+B to a new track, Ctrl+Z undoes clip edits, Ctrl+T adds a track and
// Ctrl+Shift+T removes the last.
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

    // A clip opened in the editor (double-click): its sound as it plays, and the edit
    // put back, saved as a WAV beside the mix (the original file is left alone).
    AudioDataPtr clipAudio (int track, uint32_t clip, QString& name) const;
    bool replaceClipAudio (int track, uint32_t clip, const AudioData& edited, const QString& name, QString& error);

Q_SIGNALS:
    void mixdownRequested();
    void midiInputsRequested();
    void activated();
    void closed();
    void playStarted();   // so the main window makes sure the mixer is heard
    void editClipRequested (int track, quint32 clip);

protected:
    void keyPressEvent (class QKeyEvent*) override;
    void changeEvent (QEvent*) override;
    void closeEvent (class QCloseEvent*) override;
    bool focusNextPrevChild (bool next) override;

private:
    friend class MixerStrip;
    friend class TrackLanes;
    friend class FileBrowser;

    // What the window keeps about a track besides the engine's settings.
    struct TrackInfo
    {
        QString name;
        QColor colour;
        AudioDataPtr sound;          // the track Sampler or Slicer's sound, for its window
        QPointer<QWidget> editor;    // its Sampler, Slicer or Synth window
        int generation = 0;          // a cleared track outdates loads still running
        int busy = 0;                // audio files loading
    };

    void rebuildStrips();
    void select (int index);         // 0..7 a track, 8 the master
    void tick();
    void refreshTransport();
    // Files become clips at `place` (start, and the part heard; length 0: the whole file).
    void addFiles (int track, const QStringList& paths, Frame at, bool movePlayhead = false);   // one after another
    void placeFiles (int track, QStringList paths, Frame at, bool movePlayhead);
    void stamp (mixer::Clips&);      // ids for new clips
    void loadAudio (int track, const QString& path, mixer::Clip place, std::function<void (Frame end)> then = {});
    bool loadMidi (int track, const QString& path, QString& error, mixer::Clip place, Frame* end = nullptr);
    void addClip (int track, mixer::Clip);
    void editClips (int track, mixer::Clips);   // no undo step: call remember() first
    using Snapshot = std::array<mixer::Clips, mixer::maxTracks>;
    Snapshot snapshot() const;
    void restore (const Snapshot&);
    void remember();                 // an undo step: the clips as they are, before an edit
    void undo();
    void redo();
    void chooseAudio (int track);
    void chooseMidi (int track);
    void placeAllMidi();             // again, after a tempo change
    void setInstrumentKind (int track, int choice, const QString& pluginId);
    void editInstrument (int track);
    void loadInstrumentSound (int track, const QString& path);   // into the track's Sampler or Slicer
    void takeInstrumentSound (int track);                         // from the Sample menu's
    void arm (int track);
    void toggleLanes();              // the tracks across time, above the strips (V)
    void toggleBrowser();            // the file browser on the left (Ctrl+B)
    void cycleFocus (bool forward);  // F6, Tab: the browser, the tracks, the strips
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
    TrackLanes* lanes = nullptr;
    FileBrowser* browser = nullptr;
    QSplitter* splitter;
    QSplitter* sideSplitter;
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
    std::vector<Snapshot> undoStack, redoStack;
    uint32_t nextClipId = 1;
    std::map<QString, std::weak_ptr<const mixer::AudioClip>> audioCache;
};

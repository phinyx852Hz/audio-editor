// SPDX-License-Identifier: AGPL-3.0-only
#pragma once

#include "../core/AudioData.h"
#include "../mixer/MixerEngine.h"

#include <QColor>
#include <QJsonObject>
#include <QPointer>
#include <QTimer>
#include <QWidget>

#include <array>
#include <map>
#include <set>
#include <memory>
#include <vector>
#include <functional>
#include <tuple>

class AudioEngine;
class QDialog;
class QCheckBox;
class QDoubleSpinBox;
class QLabel;
class QPushButton;
class QHBoxLayout;
class MixerStrip;
class QSplitter;
class TrackLanes;
class FileBrowser;
class ClipGrid;
class LauncherRack;
class QComboBox;
class QSpinBox;

// A level meter for a strip: peak and RMS for both sides, with a peak hold and a
// clip light (click to clear it).
// The built-in Synth's settings (also used by the patchbay's synths), applied as they change.
// `changed` runs after each change. Deletes itself when closed.
QDialog* synthDialog (mixer::InstrumentPtr synth, const QString& title, QWidget* parent, std::function<void()> changed);

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
// The clip launcher (docs/clip-launcher.md, Performance > Clip Launcher) takes the place of
// the tracks across time: a grid of slots, tracks across and scenes down, with the rack of
// the selected track on its left and the strips made compact below.
class MixerWindow final : public QWidget
{
    Q_OBJECT

public:
    // Where a track's Sampler or Slicer can take its sound from: the Sample menu's own instruments.
    struct Hooks
    {
        std::function<std::pair<sampler::SamplePtr, sampler::Settings>()> samplerSound;
        std::function<std::pair<sampler::SamplePtr, slicer::Kit>()> slicerSound;
        // The rest of a project (.phi) besides the mix: the patchbay and the view. What
        // projectState gives is written into the file; setProjectState gets the file's
        // whole object when one is opened; resetProject runs for a new project.
        std::function<QJsonObject()> projectState;
        std::function<void (const QJsonObject&)> setProjectState;
        std::function<void()> resetProject;
    };

    MixerWindow (mixer::Engine&, AudioEngine&, Hooks, QWidget* parent);
    ~MixerWindow() override;

    void setMidiStatus (const QString&);

    // Project files (.phi, JSON, docs/project.md): the mix (the tracks, their files and
    // settings, instruments included) and what the hooks add, the patchbay and the view.
    // A mix file from before (.phinyxmix) is the same without those, and still opens.
    static inline const QString projectSuffix = QStringLiteral ("phi");
    static bool isProjectFile (const QString& path);   // .phi or .phinyxmix
    bool newMix();   // false when the user keeps the mix they have
    // A new mix of 8 tracks named and coloured from the bottom of a Selector set up:
    // the last name and colour on track 1 (DR, BA, PR... for Live Performance).
    void newMixFromSet (const QString& setName, const QStringList& names, const QList<QColor>& colours);
    void openMix();
    bool openMixFile (const QString& path);   // asks first when the mix has unsaved changes
    bool saveMix (bool saveAs);
    bool loadMix (const QString& path, QString& error);
    bool writeMix (const QString& path, QString& error);
    void autosave();      // the mix as it is, kept in the config folder and opened again next time
    void restoreAutosave();
    static QString autosavePath();   // last.phi in the config folder
    void projectChanged() { changed(); }   // the patchbay, say: the project differs from its file
    QString mixName() const;
    QString trackName (int track) const;   // what its strip says (the patchbay shows it)

    void addTrack();
    void removeTrack();

    // The clip launcher in the upper area, or the tracks across time.
    void showLauncher (bool on);
    bool launcherShown() const;
    // A file chosen in the Find panel's Selector: into the first empty slot of the track with
    // that name (else that colour, else the selected track), and the mixer follows it there.
    void sendToLauncher (const QString& path, const QString& trackName, const QColor& colour);
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
    friend class ClipGrid;
    friend class LauncherRack;

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
    // The file read (or shared, when already loaded) at the mixer's rate; null when unreadable.
    void decodeAudio (int track, const QString& path, std::function<void (mixer::AudioClipPtr)> done);
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
    void updateUpperArea();
    void toggleBrowser();            // the file browser on the left (Ctrl+B)
    void cycleFocus (bool forward);  // F6, Tab: the browser, the tracks, the strips
    // Renders the track (MIDI through its instrument, and its audio) to a WAV: in place
    // it becomes the track's audio, else it goes on a new track and this one is muted.
    void bounce (int track, bool inPlace);
    // The launcher.
    // A file into a slot, played as `like` says (its loop and quantise; its clip's offset,
    // length and reversal, a length of 0 for the whole file).
    void loadSlot (int track, int scene, const QString& path, mixer::Slot like = {});
    mixer::MidiClipPtr launcherMidi (midifile::SongPtr, const std::string& path) const;   // whole bars long
    QString sceneName (int scene) const;
    void editSlot (int track, int scene, const std::function<void (mixer::Slot&)>& edit);
    void clearSlot (int track, int scene);
    int firstEmptyScene (int track) const;   // -1: none among the scenes shown
    void setScenes (int count);
    void moveCursor (int track, int scene);
    void launchSlot (int track, int scene);
    void renameScene (int scene);
    void placeLauncherMidi();          // the MIDI slots again, after a tempo change
    bool launcherKey (class QKeyEvent*);   // the grid's keys, while it is shown
    QString slotName (int track, int scene) const;
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
    ClipGrid* grid = nullptr;
    LauncherRack* rack = nullptr;
    QWidget* launcherArea = nullptr;
    QWidget* upperArea = nullptr;      // the tracks across time or the launcher, above the strips
    std::array<std::array<mixer::SlotPtr, mixer::maxScenes>, mixer::maxTracks> slots {};   // as given to the engine
    QPushButton* launcherButton = nullptr;
    QComboBox* quantiseBox = nullptr;
    QSpinBox* scenesBox = nullptr;
    int numScenes = 8;
    int cursorScene = 0;
    std::array<QString, mixer::maxScenes> sceneNames;   // empty: "Scene n"
    std::set<std::pair<int, int>> loadingSlots;        // track and scene of files still loading
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
    bool opening = false;            // inside loadMix: its newMix() keeps the patchbay
    std::vector<Snapshot> undoStack, redoStack;
    uint32_t nextClipId = 1;
    std::map<QString, std::weak_ptr<const mixer::AudioClip>> audioCache;
};

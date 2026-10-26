// SPDX-License-Identifier: AGPL-3.0-only
#pragma once

#include "../core/AudioData.h"
#include "../mixer/MixerEngine.h"

#include <QColor>
#include <QJsonArray>
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
class QSpinBox;
class QAbstractButton;
namespace counterpoint { struct Settings; }
namespace control { struct State; struct Command; }
class StatusLine;
class QScrollArea;
class QDialog;
class QCheckBox;
class QDoubleSpinBox;
class QLabel;
class QPushButton;
class QHBoxLayout;
class QVBoxLayout;
class MixerStrip;
class QSplitter;
class TrackLanes;
class FileBrowser;
class ClipGrid;
class LauncherRack;
class ReturnStripWidget;
class TrackDevices;
class ClipView;
class QStackedWidget;
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
        // The Selector set chosen in the Find panel, top to bottom: its names and colours.
        std::function<void (QStringList& names, QList<QColor>& colours)> selectorSet;
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
    // Names and colours the tracks from the Selector set, bottom up (owner, 2026-10-25: track 1
    // is the set's bottom category, DR in Live Performance). A track renamed or recoloured by
    // the user keeps what it was given.
    void applySelectorSet();
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
    void stripsChanged();   // faders, pans... set on the engine from outside (the Mix Assistant): shown, and the mix marked changed

    // Cuts (Hz, dB) from the Mix Assistant into a Φ EQ in the track's inserts: the one there,
    // else a new one. An error, or empty.
    QString applyEqCuts (int track, const std::vector<std::pair<double, double>>& cuts);

    void addTrack();
    void removeTrack();

    // The clip launcher in the upper area, or the tracks across time.
    void showLauncher (bool on);
    bool launcherShown() const;
    // A file chosen in the Find panel's Selector: into the first empty slot of the track with
    // that name (else that colour, else the selected track), and the mixer follows it there.
    void sendToLauncher (const QString& path, const QString& trackName, const QColor& colour);
    // The Φ random selector's deal: a file and an instrument (a plugin id, loaded on the
    // track first) for each Selector colour (empty: that colour has none), with the
    // colour's name and colour, top colour first. Each goes to the track
    // with that name, else that colour, else the one a mix from the set gives it (tracks
    // are added, named and coloured when the mix has fewer). Into the first scene empty on
    // all those tracks, or onto the tracks after everything on them. `again` deals into
    // the place the last deal went instead, replacing it.
    void phiDeal (const QStringList& files, const QStringList& instruments, const QStringList& names, const QList<QColor>& colours,
                  bool launcher, bool again);
    void togglePlay();
    void toStart();

    // A clip opened in the editor (double-click): its sound as it plays, and the edit
    // put back, saved as a WAV beside the mix (the original file is left alone).
    AudioDataPtr clipAudio (int track, uint32_t clip, QString& name) const;
    bool replaceClipAudio (int track, uint32_t clip, const AudioData& edited, const QString& name, QString& error);

    // The Device Chain along the mixer's bottom (owner, 2026-10-25): the selected track's
    // instrument and inserts, or the clip picked in the launcher or the tracks. Hidden while
    // the Master strip is selected, so the editor's own chain can show.
    bool devicePanelShown() const;
    // What the browser's Instruments and Audio Effects lists drag: "effect\n<id>",
    // "instrument\n<id>", or "synth", "sampler", "slicer".
    static QString pluginMime() { return QStringLiteral ("application/x-phi-plugin"); }

    // The column left of the tracks (owner, 2026-10-25): the Finder until a track is picked,
    // then the Inspector in its place; Ctrl+F brings the Finder back. In the editor's place the
    // Finder is the editor's Find panel (finderWanted shows or hides it); in a window of its
    // own it is the mixer's browser.
    void showSideFinder (bool finder);
    bool sideFinderShown() const { return finderShown; }
    void engageTrack();   // a track clicked or picked: the Inspector takes the Finder's place
    // The parts of the mixer that take the keyboard when pointed at (Focus Follows Mouse).
    QList<QWidget*> focusAreas() const;
    // A sound from the editor into the launcher (owner, 2026-10-25): saved as a WAV beside the
    // mix and put in the slot under the launcher's cursor, or the selected track's first empty one.
    bool sendAudioToLauncher (const AudioData& sound, const QString& name, QString& error);
    // Control surfaces and MIDI Learn (MixerControl.cpp, docs/control-surfaces.md): the mix as
    // their lights show it, and what their pads, knobs and buttons ask for.
    control::State surfaceState() const;
    void surfaceCommand (const control::Command&);

Q_SIGNALS:
    void mixdownRequested();
    void midiInputsRequested();
    void activated();
    void closed();
    void playStarted();   // so the main window makes sure the mixer is heard
    void editClipRequested (int track, quint32 clip);
    void devicePanelChanged();   // shown or hidden
    void finderWanted (bool show);   // in the editor's place: the editor's Find panel shown or hidden

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
    friend class ReturnStripWidget;
    friend class TrackDevices;
    friend class ClipView;

    // What the window keeps about a track besides the engine's settings.
    struct TrackInfo
    {
        QString name;
        QColor colour;
        AudioDataPtr sound;          // the track Sampler or Slicer's sound, for its window
        QPointer<QWidget> editor;    // its Sampler, Slicer or Synth window
        int generation = 0;          // a cleared track outdates loads still running
        int busy = 0;                // audio files loading
        mixer::TuningChoice tuning;  // the PHI synth's, when neither the clip nor the scene has one
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
    // The inserts: effects on a track before its width, pan and fader.
    void chooseInsert (int track, int replace, QPoint at);   // the effects menu: adds one, or replaces insert `replace`
    void addInsert (int track, const std::string& pluginId, int replace = -1);
    void editInsert (int track, int insert);                 // its window, or its knobs for the built-in ones
    void insertMenu (int track, int insert, QPoint at);      // insert -1: the list's empty space
    void setInsertBypassed (int track, int insert, bool);
    void setInsertMix (int track, int insert, float mix);   // 0..1, the rest dry
    void removeInsert (int track, int insert);
    void moveInsert (int track, int insert, int to);
    void reorderInserts (int track, const std::vector<int>& order);   // the engine's indices in their new order
    QJsonArray insertsJson (int track) const;
    void loadInserts (int track, const QJsonArray&, QStringList& problems);
    void stripsMenu (QPoint at);     // which sections the strips show, and how wide they are
    // An instrument or effect dropped on a track (its strip, or the Device Chain): the
    // instrument takes the instrument's place, an effect goes after the inserts.
    bool dropPlugin (int track, const QByteArray& what);
    void showDevices();                          // the selected track's chain in the Device Chain
    void showTimelineClip (int track, uint32_t id);
    void showSlotClip (int track, int scene);
    void updateDevicePanel();                    // shown unless the master is selected
    // The MIDI clip shown (owner, 2026-10-25: "really fast MIDI editing", from the Inspector):
    // R reverses it; Φ, Duplicate, ½, ×2 and a mode or raga make a new MIDI file beside the mix.
    void reverseShownClip();
    void editShownMidi (const std::function<midifile::SongPtr (const midifile::Song&)>& edit, const QString& what);
    void refreshMidiClipBox();
    void phiShownMidi();   // Φ: the notes dealt out again, another order each time
    // Counterpoint (owner, 2026-10-25): new voices against the clip's melody, in a species and a
    // mode, raga or maqam (mixer/Counterpoint.h), each a MIDI file beside the mix and a clip on a
    // new track, where the clip is on the timeline or in the same scene, in the clip's tuning.
    void counterpointShownMidi();
    // Writes the voices against `clip` (in `slot` of `scene`, or on the timeline when null). A
    // live loop's (launchAt >= 0) go on the tracks the last loop's went on, and launch on that
    // frame of the launch clock with it.
    void writeCounterpoint (int track, const mixer::Clip& clip, mixer::SlotPtr slot, int scene, const counterpoint::Settings&,
                            mixer::TuningChoice modeChoice, mixer::TuningChoice tuning, Frame launchAt);
    // Recording (docs/recording.md; owner, 2026-10-25: "play a melody into a clip with a
    // pre-determined length that can be adjusted and the recording immediately stops, loops, and
    // the new melody is generated", and "the main method super easy to learn with the options for
    // the advanced performer"). The transport's Record (Ctrl+R) in the launcher steps the armed
    // track through record, play, overdub and play again; in the tracks across time it records
    // onto the armed track from the playhead until pressed again. A slot's record dot records into
    // that slot. What the take does after (play, stop, record the next slot, add counterpoint),
    // its length, quantise, count-in and the audio input are the transport's Record options.
    QWidget* buildTransport();   // the transport row across the top, for both views
    void recordPressed();
    void recordSlot (int track, int scene);     // a take into the slot (an empty one, or replacing it)
    void recordTimeline (int track);
    void toggleOverdub (int track);
    // A slot holds an audio layer under a MIDI layer (owner, 2026-10-26), each drawn in its half.
    // An empty half records one pass of the loop and plays on with the other.
    void recordLayer (int track, int scene, bool wantAudio);
    bool recordsAudioLayer (int track) const;   // what Record adds to a loop with one layer
    void giveBothLayers (int track);            // an Audio or Inst track plays both once a slot has them
    bool addLayerFile (int track, int scene, const QString& file);   // dropped on a clip of the other kind: false if not
    // Drums to MIDI (owner, 2026-10-26): a timeline clip (id) or a slot's audio as kit notes, the
    // slot's MIDI layer or a MIDI clip beside it.
    void drumsToMidi (int track, uint32_t id, int scene);
    void captureMidi();                         // the last phrase played on the armed track, into its next empty slot
    void keepRecorded();                        // each tick: the takes that ended, saved as files and placed
    bool startTake (mixer::RecordRequest, QString& error);   // asks the engine, the input and the transport
    mixer::RecordRequest recordRequest (int track, int scene) const;
    void refreshRecording();                    // the Record button and the status line
    void recordMenu (QPoint at);                // the advanced options
    bool ensureInput (QString& error);          // the audio input running, when a track records or hears it
    void updateInput();                         // started or stopped as the tracks need it
    QString saveTake (const mixer::Recorded&, QString& error);   // the take's file beside the mix
    int nextSlotFor (int track, int from) const;   // the first empty slot from `from`, a new scene when all are full
    void say (const QString&);                  // on the status line
    void refreshViews();                        // the launcher's grid and the tracks across time drawn again
    void showPosition (Frame, bool launchClock);
    void giveSynth (int track);                 // the Synth, for a track with MIDI and no instrument
    // A loop just recorded gets counterpoint with the Counterpoint dialog's last choices, launched
    // with it on `launchAt` of the launch clock.
    void autoCounterpoint (int track, int scene, mixer::SlotPtr, Frame launchAt);
    QAbstractButton* recordButton = nullptr;
    QComboBox* recordLengthBox = nullptr;
    QComboBox* countInBox = nullptr;
    QAbstractButton* clickButton = nullptr;
    QSpinBox* beatsBox = nullptr;               // the time signature's top
    class PositionDisplay* positionDisplay = nullptr;
    std::unique_ptr<class AudioCapture> capture;
    std::unique_ptr<mixer::Input> captureInput;
    QString inputSource;                        // the PipeWire source recorded from; empty: the first input
    enum class After { play = 0, stop = 1, recordNext = 2, counterpoint = 3 };
    After afterRecording = After::play;
    double recordQuantise = 0.0;                // beats; 0 as played
    int takeNumber = 1;
    std::array<std::vector<uint32_t>, mixer::maxTracks> timelinePasses;   // a timeline take's clips so far, the newest kept
    std::vector<qint64> taps;      // Tap Tempo's last taps, in ms
    struct LayerTake { int scene = -1; bool audio = false; };
    std::array<LayerTake, mixer::maxTracks> layerTakes;   // the half a track is recording, for the grid
    QString editsFolder() const;   // where edits of the mix's clips are saved
    // An audio clip shown: Paulstretch makes it the remembered number of times longer, with the
    // editor's Extreme Stretch settings, saved beside the mix (owner, 2026-10-25).
    void stretchShownAudio();
    mixer::AudioClipPtr saveClipEdit (const AudioData& edited, const QString& name, QString& file, QString& error);
    QWidget* audioClipBox = nullptr;
    QLabel* audioClipTitle = nullptr;
    QDoubleSpinBox* stretchRatioBox = nullptr;
    QWidget* midiClipBox = nullptr;
    QLabel* midiClipTitle = nullptr;
    QComboBox* rootBox = nullptr;
    QComboBox* scaleBox = nullptr;
    QComboBox* hypoBox = nullptr;   // the scale's range: authentic, or a hypo range
    int phiTurn = 0;
    // Q-Link (Cubase): with it on, a change to one of the picked strips goes to all of them;
    // Alt+Shift sends it to every track. `fn` gets each strip, and whether it is the one changed.
    void forLinked (int track, const std::function<void (mixer::Strip&, bool self)>& fn);
    void pick (int track, bool add);  // Ctrl+click: add or take a strip from the picked ones
    // The returns: A Reverb and B Delay.
    void chooseReturnEffect (int index, QPoint at);
    void setReturnEffect (int index, const std::string& pluginId);   // empty: none
    void editReturn (int index);
    void defaultReturns (int only = -1);   // Φ Space on A, Φ Echo on B (a dotted eighth at the tempo); -1 both
    QJsonArray returnsJson() const;
    void loadReturns (const QJsonArray&, QStringList& problems);
    void toggleLanes();              // the tracks across time, above the strips (V)
    void updateUpperArea();
    void toggleBrowser();            // the file browser on the left (Ctrl+B)
    void cycleFocus (bool forward);  // F6: the browser, the tracks, the strips
    // Renders the track (MIDI through its instrument, and its audio) to a WAV: in place
    // it becomes the track's audio, else it goes on a new track and this one is muted.
    void bounce (int track, bool inPlace);
    // The launcher.
    // A file into a slot, played as `like` says (its loop and quantise; its clip's offset,
    // length and reversal, a length of 0 for the whole file).
    void loadSlot (int track, int scene, const QString& path, mixer::Slot like = {}, const QString& midiLayer = {});
    mixer::MidiClipPtr launcherMidi (midifile::SongPtr, const std::string& path) const;   // whole bars long
    QString sceneName (int scene) const;
    void editSlot (int track, int scene, const std::function<void (mixer::Slot&)>& edit);
    void clearSlot (int track, int scene);
    int firstEmptyScene (int track) const;
    int trackFor (const QString& name, const QColor& colour) const;   // by name, else colour; -1: none
    struct LastDeal
    {
        bool valid = false, launcher = false;
        int scene = -1;
        Frame at = 0;
        std::array<mixer::Clips, mixer::maxTracks> before;   // the tracks' clips before it (a Snapshot)
    };
    LastDeal lastDeal;   // -1: none among the scenes shown
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
    std::pair<QString, QColor> defaultTrack (int track) const;   // from the Selector set, else "Track n"
    std::array<std::pair<QString, QColor>, mixer::maxTracks> givenDefaults;   // what applySelectorSet last gave each track
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
    QPushButton* inspectorButton = nullptr;   // the launcher's rack, on the right, hidden until asked for
    QWidget* inspectorArea = nullptr;
    QVBoxLayout* inspectorLayout = nullptr;
    QScrollArea* inspectorScroll = nullptr;
    MixerStrip* inspectorStrip = nullptr;   // the selected track's, rebuilt when the selection moves
    bool inspectorPending = false;
    QStackedWidget* sideStack = nullptr;     // the Finder (mixer's browser) or the Inspector
    bool finderShown = true;
    void toggleDevicePanel();   // Shift+Tab over the bottom panel: the Device Chain or the clip
    QStackedWidget* devicePanel = nullptr;
    TrackDevices* devices = nullptr;
    ClipView* clipView = nullptr;
    bool showingClip = false;
    void refreshInspector();   // soon, once for any number of changes
    QScrollArea* gridScroll = nullptr;
    QComboBox* quantiseBox = nullptr;
    QSpinBox* scenesBox = nullptr;
    int numScenes = 16;
    int cursorScene = 0;
    std::array<QString, mixer::maxScenes> sceneNames;   // empty: "Scene n"
    std::array<mixer::TuningChoice, mixer::maxScenes> sceneTunings;
    // The tuning a track plays in (Tunings.h): its launched clip's, else that scene's, else its own.
    mixer::TuningChoice tuningInForce (int track) const;
    QString tuningText (const mixer::TuningChoice&) const;   // "Rāst on D", or "12-TET"
    void applyTunings();   // tells each instrument whose tuning changed
    struct HeardTuning
    {
        mixer::TuningChoice choice;
        std::weak_ptr<mixer::Instrument> instrument;
    };
    std::array<HeardTuning, mixer::maxTracks> heardTunings;
    std::set<std::pair<int, int>> loadingSlots;        // track and scene of files still loading
    QSplitter* splitter;
    QSplitter* sideSplitter;
    QHBoxLayout* stripRow;
    LevelMeter* masterMeter;
    class QSlider* masterFader;
    QLabel* masterValue;
    QLabel* masterPeak;
    QWidget* masterStrip;
    QAbstractButton* playButton;
    QAbstractButton* loopButton;
    QDoubleSpinBox* tempoBox;
    QCheckBox* followTempoBox;
    QLabel* midiStatus;
    QPushButton* midiButton = nullptr;
    StatusLine* status;
    QTimer timer;
    qint64 lastTick = 0;
    int selected = 0;
    int stripSections = 31;
    std::set<int> picked;            // strips Q-Link joins (the selected one is always among them)
    QPushButton* qLinkButton = nullptr;
    QPushButton* bypassInsertsButton = nullptr;
    QPushButton* bypassSendsButton = nullptr;
    std::array<ReturnStripWidget*, mixer::numReturns> returnStrips {};          // StripSection bits (MixerWindow.cpp) shown on every strip
    bool wideStrips = false;
    QString path;                    // the mix file; empty: not saved yet
    bool modified = false;
    bool restoring = false;
    bool opening = false;            // inside loadMix: its newMix() keeps the patchbay
    std::vector<Snapshot> undoStack, redoStack;
    uint32_t nextClipId = 1;
    std::map<QString, std::weak_ptr<const mixer::AudioClip>> audioCache;
};

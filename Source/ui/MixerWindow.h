// SPDX-License-Identifier: AGPL-3.0-only
#pragma once

#include "../core/AudioData.h"
#include "../mixer/MixerEngine.h"
#include "../control/ControlSurface.h"

#include <QColor>
#include <QJsonArray>
#include <QJsonObject>
#include <QPointer>
#include <QTimer>
#include <QWidget>

#include <algorithm>
#include <array>
#include <map>
#include <set>
#include <memory>
#include <vector>
#include <functional>
#include <tuple>

class AudioEngine;
class QSpinBox;
class QMenu;
class QAbstractButton;
namespace counterpoint { struct Settings; }
namespace minimalize { struct Part; }
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
class PrismDialog;
class ClipView;
class EditorTrack;
class QStackedWidget;
class QComboBox;
class QSpinBox;

// A level meter for a strip: peak and RMS for both sides, with a peak hold and a
// clip light (click to clear it).
// The built-in Synth's settings (also used by the patchbay's synths), applied as they change.
// `changed` runs after each change. Deletes itself when closed.
QDialog* synthDialog (mixer::InstrumentPtr synth, const QString& title, QWidget* parent, std::function<void()> changed);
// PHI rāga's settings, and an effect's knobs (for effects without a window of their own), the same way.
QDialog* ragaDialog (mixer::InstrumentPtr raga, const QString& title, QWidget* parent, std::function<void()> changed);
QDialog* effectDialog (mixer::EffectPtr effect, const QString& title, QWidget* parent, std::function<void()> changed);

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

// The mixer (docs/mixer.md): up to 8 tracks (16 when asked for) and the master out, in its own window.
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
namespace mixer { class MtsEsp; }

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
    // DAWproject (docs/dawproject.md), the open format of Bitwig Studio, Studio One, Cubase and
    // others: one opened becomes a PHI project in a folder beside it ("<name> PHI"), with its
    // audio and its note clips as MIDI files; this one is written as one, its audio in it.
    // What does not carry over is listed when it is done. DawProjectSection.cpp.
    static bool isDawProject (const QString& path);
    bool importDawProject (const QString& path, QString& error);
    void importDawProjectFromMenu();
    void exportDawProject();
    // Unsaved changes offered a save first: Save, Discard or Cancel. False when the user
    // cancels, or cancels the save, and keeps the project.
    bool askToSave (const QString& title, const QString& question);
    // Crash recovery (owner, 2026-10-30): last.phi is written while PHI runs and removed on a
    // clean quit, so PHI starts on a blank project and offers last.phi only after a crash.
    void autosave();      // the mix as it is, kept in the config folder
    void restoreAutosave();
    void dropAutosave();   // quitting: last.phi removed, and not written again
    static void removeAutosave();      // last.phi and what it was saved as, gone
    static QString autosaveName();     // the project last.phi holds: its file's name, or empty when untitled
    bool hasUnsavedChanges() const { return modified; }
    static QString autosavePath();   // last.phi in the config folder
    void projectChanged() { changed(); }   // the patchbay, say: the project differs from its file
    QString mixName() const;
    QString trackName (int track) const;   // what its strip says (the patchbay shows it)
    void stripsChanged();   // faders, pans... set on the engine from outside (the Mix Assistant): shown, and the mix marked changed

    // Cuts (Hz, dB) from the Mix Assistant into a Φ EQ in the track's inserts: the one there,
    // else a new one. An error, or empty.
    QString applyEqCuts (int track, const std::vector<std::pair<double, double>>& cuts);

    void addTrack();      // after the selected track
    void removeTrack();   // the selected track
    // A new track at `at` (0-based), the ones from there on moving along; and track `index`
    // deleted (asked first when something is on it), the ones after it moving up.
    void insertTrack (int at);
    void dragTrack (int from, int to);   // moveTrack, undone by Ctrl+Z
    // A track dragged by its name (strip) or header (launcher): where it would land, shown on
    // both; -1 hides it.
    void showTrackDrag (int from, int to);
    int stripTrackAt (QPoint global) const;   // the track whose strip is nearest that x
    void deleteTrack (int index);

    // The clip launcher in the upper area, or the tracks across time.
    void showLauncher (bool on);
    bool launcherShown() const;
    // Architectonics (owner, 2026-10-29): the clips in each of the launcher's scenes shown,
    // scene 0 first, and a scene's name ("Scene n" until renamed).
    std::vector<int> sceneClips() const;
    QString sceneName (int scene) const;
    // Its Compose a Song: the track for each Selector colour wanted, as the Φ deal gives them
    // (added, named and coloured when missing), with the colour's instrument loaded when it has
    // one; -1 for a colour not wanted. The first of `count` scenes in a row empty on every
    // track (-1: not that many left). And one scene of the song: on each colour's track its
    // MIDI (in `tuning`), else its file, else nothing; the scene named `name`.
    std::vector<int> colourTracks (const QStringList& names, const QList<QColor>& colours, const std::vector<bool>& wanted,
                                   const QStringList& instruments);
    int emptyScenes (int count) const;
    // Its Compose a Score: the score opened in the MIDI editor (the staff view there, once it
    // has one), played on the selected track; kept, it goes on a new track (else the selected
    // one) from the start, the track tuned to `tuning`. `notation`: the notation the editor opens
    // on (mixer/Notation.h's ids, "phi" PHI's microtonal staff), empty the piano roll.
    // `instrument`: what the score plays on while it is edited and on the track it is kept on
    // (PHI tabla for the Bol Processor); none: the selected track's.
    void openScore (const midifile::Song& score, mixer::TuningChoice tuning, const QString& title, const QString& notation = {},
                    mixer::InstrumentPtr instrument = nullptr);
    // PHI tabla (sampler/PhiTabla.h): a Slicer with the owner's own strokes on 32 pads.
    mixer::InstrumentPtr makePhiTabla();
    void composeScene (int scene, const std::vector<int>& tracks, const QStringList& files,
                       const std::vector<midifile::SongPtr>& midi, mixer::TuningChoice tuning, const QString& name);
    // A file chosen in the Find panel's Selector: into the first empty slot of the track with
    // that name (else that colour, else the selected track), and the mixer follows it there.
    void sendToLauncher (const QString& path, const QString& trackName, const QColor& colour);
    // The MIDI editor on the MIDI clip picked (Mixer › Edit MIDI Notes); false when no MIDI clip is picked.
    bool editNotes();
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
    // instrument and inserts, or the clip picked in the launcher or the tracks. Ctrl+Shift+C
    // (the editor's Device Chain key) hides or shows it, remembered (mixer/devices).
    bool devicePanelShown() const;
    // Options > Mixer Preferences: Arm on Select and Focus Follows Mouse.
    void showPreferences();
    // Where Simplest (0) and the Slicer (1) sit while the mixer is in front: in the Device
    // Chain's place under the strips, taking turns with it (owner, 2026-10-29: "toggle between
    // the device chain and the slicer in that space"). Tabs over it pick which shows while one
    // is open; opening one puts it in front. A track's own Sampler or Slicer (Edit on its strip)
    // takes the third place there (2), one track at a time.
    QWidget* instrumentHost (int which) const { return instrumentHosts[(size_t) std::clamp (which, 0, 1)]; }
    void setInstrumentOpen (int which, bool open);
    bool instrumentOpen (int which) const { return instrumentOpens[(size_t) std::clamp (which, 0, 1)]; }
    bool instrumentInFront() const { return lowerFront >= 0; }
    void showDeviceChainInFront();
    bool toggleLowerZone();   // the Device Chain or the instrument in front last; false: none open
    void toggleDevices();
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
    // The column hidden while the editor's research panel takes its place, then given back as it was.
    void hideSideColumn (bool hidden);
    // Off, the left column is closed: no Inspector, and picking a track does not bring it back.
    void showInspector (bool shown);
    bool inspectorShown() const { return ! inspectorClosed && ! finderShown; }
    void setInspectorAction (QAction*);   // the View menu's item, kept in step
    // The parts of the mixer that take the keyboard when pointed at (Focus Follows Mouse).
    QList<QWidget*> focusAreas() const;
    // A sound from the editor into the launcher (owner, 2026-10-25): saved as a WAV beside the
    // mix and put in the slot under the launcher's cursor, or the selected track's first empty one.
    bool sendAudioToLauncher (const AudioData& sound, const QString& name, QString& error);
    // Control surfaces and MIDI Learn (MixerControl.cpp, docs/control-surfaces.md): the mix as
    // their lights show it, and what their pads, knobs and buttons ask for.
    control::State surfaceState() const;
    void surfaceCommand (const control::Command&);
    // MIDI Map mode (Ctrl+M, ControlSection.cpp): every control a MIDI control can be mapped to,
    // where it is (an area of a widget; empty: all of it) and what a mapped control does there.
    struct Mappable
    {
        QWidget* widget = nullptr;
        QRect area;
        control::Command target;
        bool continuous = false;   // a fader or knob; else a button
    };
    std::vector<Mappable> mappable() const;

    // The Mode menu (ModeSection.cpp, owner 2026-10-26): the selected track tuned to a mode
    // (its clips' and scenes' own tunings left as they are), and the MIDI clip shown in the
    // Device Chain moved into the mode's keys, folded into a hypo range `below` keys under
    // the root when that is not 0. False when there is no track, or no MIDI clip shown.
    bool tuneSelectedTrack (mixer::TuningChoice);
    bool modeShownMidi (int root, uint16_t keys, int below, const QString& what);
    bool showsMidiClip() const;
    // MTS-ESP (mixer/MtsEsp.h): synths that follow it play in the live track's tuning in force.
    bool mtsEspOn() const;
    void setMtsEsp (bool on);
    void installLibMts();   // downloads ODDSound's libMTS and installs it, when the user agrees
    void counterpointShown() { counterpointShownMidi(); }
    void fugueShown() { fugueShownMidi(); }
    // The editor's track (owner, 2026-10-26): one track of the mix, Audio only, MIDI only or
    // Audio + MIDI, under the editor's waveform, with the mixer's launcher slots (left to right),
    // its clips across time, recording and the MIDI tools. It is the track last selected here or
    // in the mixer. Made once, for `parent`.
    QWidget* editorTrackView (QWidget* parent);
    int editorTrack() const;
    void refreshEditorTrack();
    // A sound from the editor onto the editor's track at the playhead, saved as a WAV beside the mix.
    bool sendAudioToTimeline (const AudioData& sound, const QString& name, QString& error);
    // Between the launcher and the timeline (owner, 2026-10-26): a copy of a slot's clip at the
    // playhead, or of a timeline clip into the track's next empty slot.
    void slotToTimeline (int track, int scene);
    void clipToLauncher (int track, uint32_t id);
    void stochasticizeShown() { stochasticizeShownMidi(); }
    // cubify (CubifySection.cpp, owner 2026-10-26): the MIDI clip shown, else the cursor's scene,
    // arranged by Xenakis's cubes (mixer/Cubify.h) onto the tracks' timelines.
    void cubify();
    // Minimalize on the launcher's scene at the cursor (the Compose menu's Minimalize a Scene).
    void minimalizeCursorScene() { minimalizeScene (cursorScene); }
    // Twelve-tone serial music (SerialSection.cpp, owner 2026-10-27; mixer/Serial.h): a piece's
    // voices saved as MIDI files beside the mix, on the selected track and the ones after it,
    // then on new tracks. In the launcher they fill one scene and loop together, so cubify and
    // minimalize can take the scene; else they go on the timeline at the playhead. Each clip
    // plays in 12 equal steps, so counterpoint and the fugue open on every key. `then` 1 or 2:
    // the first voice shown in the Device Chain and counterpoint or a fugue asked for on it.
    // False, with `error` set, when there is no track or no room.
    bool addSerial (const std::vector<midifile::SongPtr>& voices, const QStringList& names, const QString& stem,
                    bool launcher, int then, QString& error);
    // The notes of the MIDI clip shown in the Device Chain as they are heard, drums left out
    // (a row can be taken from them). Empty when no MIDI clip is shown.
    std::vector<int> shownMidiPitches() const;
    double mixTempo() const;
    // A rhythm from the Mode menu's rhythmic modes (RhythmSection.cpp): saved as a MIDI file
    // with the mix's edits, `stem` naming it, and placed on the selected track at the playhead,
    // the Synth's drum kit playing it when the track has no instrument. False with no track.
    bool addRhythm (const midifile::Song&, const QString& stem);

Q_SIGNALS:
    void mixdownRequested();
    void midiInputsRequested();
    void activated();
    void closed();
    void playStarted();   // so the main window makes sure the mixer is heard
    void editClipRequested (int track, quint32 clip);
    void devicePanelChanged();   // shown or hidden
    void finderWanted (bool show);   // in the editor's place: the editor's Find panel shown or hidden
    void trackToEditorRequested (int track);   // the editor's track rendered into a tab of the editor
    void message (const QString&);   // what the status line says, for the editor's track to show too

protected:
    void keyPressEvent (class QKeyEvent*) override;
    bool event (QEvent*) override;   // = and - (Ctrl+= and Ctrl+-) are the mixer's while it has the keyboard
    void changeEvent (QEvent*) override;
    void closeEvent (class QCloseEvent*) override;
    bool focusNextPrevChild (bool next) override;
    bool eventFilter (QObject*, QEvent*) override;

private:
    friend class MixerStrip;
    friend class TrackLanes;
    friend class FileBrowser;
    friend class ClipGrid;
    friend class LauncherRack;
    friend class ReturnStripWidget;
    friend class TrackDevices;
    friend class PrismDialog;
    friend class ClipView;
    friend class EditorTrack;
    friend class BusStripWidget;

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

    void copyTrack (int from, int to);
    void clearTrack (int track);
    bool readyToMoveTracks (int from);
    bool openTrack (int at);         // a new track there, the ones after it moved up; no undo step
    void moveDuckKeys (const std::function<int (int)>& moved);
    void rebuildStrips();
    void select (int index);         // 0..15 a track, mixer::masterIndex the master
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
    // The effects menu (Built-in first, then by format); `pick` gets the chosen plugin's id.
    void effectsMenu (QMenu&, std::function<void (const std::string& id)> pick);
    static QJsonArray effectsJson (const mixer::Inserts&);
    mixer::Inserts effectsFromJson (const QJsonArray&, const QString& owner, QStringList& problems);
    // Instrument Prisms (MixerPrism.cpp, docs/prism.md): a track's instrument made of rays, each an
    // instrument and then its effects, played together. Choosing one wraps the track's instrument.
    mixer::InstrumentPtr makePrism (int track);
    // A ray's instrument: "synth", "raga" or "instrument\n<plugin id>"; null with `error`.
    mixer::InstrumentPtr makeRayInstrument (const QByteArray& what, QString& error);
    static bool canBeRay (const mixer::Instrument&);   // not a Sampler, Slicer or Prism (yet)
    void openPrism (int track);   // its rays, in a window of their own
    void prismChanged (int track);   // its rays were edited: the strip and the Device Chain follow
    QJsonObject prismJson (const mixer::Instrument& prism) const;
    mixer::InstrumentPtr prismFromJson (const QJsonObject&, int track, QStringList& problems);
    QString insertOwner (int track) const;   // "track 2, “Bass”", or "the Pre-Master" (mixer::busTrack)
    int insertLimit (int track) const;
    void refreshInsertsOf (int track);
    // The Pre-Master (owner, 2026-10-28): its strip under the launcher's scenes, its settings in the project.
    class BusStripWidget* busStrip = nullptr;
    QJsonObject busJson() const;
    void loadBus (const QJsonObject&, QStringList& problems);
    // Spatial mixing (owner, 2026-10-28; MixerSpatial.cpp, docs/spatial-mixing.md): the project's
    // Spatial mode, the decoder it is heard through (headphones and speakers, kept in the
    // settings: they are the studio's, not the project's), and the room view of where it all is.
    friend class RoomView;
    friend class SpatialDialog;
    friend class SpeakerMeter;
    bool spatialMode() const;
    void setSpatialMode (bool on);
    void applySpatial();      // the decoder made again from the settings, for the engine and the device
    void spatialMoved();      // a panner changed: the strips, the Inspector and the room views follow
    void showSpatialDialog();
    QJsonObject spatialJson() const;
public:
    // The channel layout a Mix Down in Spatial mode has ("Ambisonics (3rd order)", "5.1.4"...;
    // empty: the usual one for its channel count).
    std::string mixdownLayout() const;
private:
    void loadSpatial (const QJsonObject&);
    class QPushButton* spatialButton = nullptr;
    class RoomView* roomView = nullptr;            // in the Inspector, in Spatial mode
    QPointer<class SpatialDialog> spatialDialog;
    QString spatialStatus;                         // what the decoder is, or why it could not be made
    std::vector<ambi::Speaker> spatialSpeakers;    // the speakers' layout, for the room views
    double sceneYaw = 0.0;
    // The strips line up with the launcher's columns: the Pre-Master's as wide as the scenes',
    // each track's as wide as its clips' (laneWidth, shared with the grid).
    static constexpr int laneMargin = 4, sceneColumn = 130, minLane = 100, maxLane = 150;
    static constexpr int stripGap = 4, stripMargin = laneMargin + stripGap / 2;
    class QScrollArea* stripScroll = nullptr;
    int laneWidth() const;
    void applyStripWidths();
    // Strip zoom (owner, 2026-10-30, as Cubase's MixConsole): the strips and the launcher's
    // columns as wide as one of zoomLanes, from a strip of little more than its fader to a wide
    // one; 0 fits them to the window as before. = and - (or Ctrl+wheel over the strips or the
    // launcher) step it, the strip under the mouse (or the selected one) kept where it is.
    static constexpr std::array<int, 8> zoomLanes { 44, 60, 84, 104, 128, 150, 180, 240 };
    static constexpr int slimLane = 116, minimalLane = 80;   // narrower than these: fewer parts, then the fader alone
    int stripZoom = 0;
    void zoomStrips (int steps, int anchorX = -1);   // anchorX: in the strips' viewport, -1 the selected strip
    void setStripZoom (int width, int anchorX = -1);
    bool wideLanes() const { return laneWidth() >= 180; }
    bool slimLanes() const { return laneWidth() < slimLane; }
    bool minimalLanes() const { return laneWidth() < minimalLane; }
    void syncLauncherScroll();
    static int zoomKey (const class QKeyEvent*);   // 1 for = (or +), -1 for -, else 0; Ctrl for the timeline's
    void zoomTimeline (int steps);
    bool syncingScroll = false;
    int wheelSteps = 0;              // Ctrl+wheel's eighths of a degree towards the next zoom step
    // 8 tracks (the default, as the Selector sets) or 16 (owner, 2026-10-30), saved with the mix.
    void setSixteenTracks (bool on);
    // Room for `needed` more tracks; at 8 with room at 16, asks to go to 16 first.
    bool roomForTracks (int needed, const QString& title);
    // Pointing at a strip selects its track (Strips > Focus Follows Mouse, off at first).
    bool focusFollowsMouse = false;
    void hoverSelect (int track);
    // Selecting a track that plays an instrument arms it for the MIDI keyboard and recording
    // (Mixer Preferences, on at first, as Ableton's); a hover never arms.
    bool autoArm = true;
    bool autoArmed = false;          // the arm is where selecting put it, so the next selection moves it
    bool keepArm = false;            // select() leaves the arm be: a hover, a project opening
    void routeToBus (int track, bool on);   // the track (and the Q-Linked ones) to the Pre-Master, or back to the master
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
    void splitShownClip();   // the clip's file, a track for each channel
    // Each channel of `file` on a new track, as a mono file beside the mix, placed as `like` is. False: nothing done.
    bool splitToTracks (const QString& file, mixer::Clip like);
    void editShownMidi (const std::function<midifile::SongPtr (const midifile::Song&)>& edit, const QString& what);
    void refreshMidiClipBox();
    void phiShownMidi();   // Φ: the notes dealt out again, another order each time
    // Counterpoint (owner, 2026-10-25): new voices against the clip's melody, in a species and a
    // mode, raga or maqam (mixer/Counterpoint.h), each a MIDI file beside the mix and a clip on a
    // new track, where the clip is on the timeline or in the same scene, in the clip's tuning.
    void counterpointShownMidi();
    void noteEditorShownMidi();   // the MIDI editor on the clip shown
    // Writes the voices against `clip` (in `slot` of `scene`, or on the timeline when null). A
    // live loop's (launchAt >= 0) go on the tracks the last loop's went on, and launch on that
    // frame of the launch clock with it.
    void writeCounterpoint (int track, const mixer::Clip& clip, mixer::SlotPtr slot, int scene, const counterpoint::Settings&,
                            mixer::TuningChoice modeChoice, mixer::TuningChoice tuning, Frame launchAt);
    // Fugue (owner, 2026-10-26; MixerFugue.cpp): a fugue of two to four voices on the clip's
    // melody as its subject, in any mode, raga, maqam or equal temperament (mixer/Fugue.h), each
    // voice a MIDI file beside the mix and a clip on a new track, where the clip is.
    void fugueShownMidi();
    bool writeFugue (int track, const mixer::Clip& clip, mixer::SlotPtr slot, int scene);   // true if it made any
    // A generated MIDI file (saved as `file`) as a clip where `clip` is: on the timeline at its
    // start, or in the same scene of the launcher. On track `target`, or on a new track named
    // `trackName` with `track`'s fader and colour, in `tuning`, when that is -1 (the caller makes
    // sure there is room). With launchAt >= 0 it launches on that frame.
    void placeGenerated (int track, const mixer::Clip&, mixer::SlotPtr, int scene, midifile::SongPtr written, const QString& file,
                         const QString& trackName, mixer::TuningChoice tuning, int target, Frame launchAt);
    void generatedPlaced();   // the strips, the launcher and the rack shown again, and the mix marked changed
    // The MIDI clip shown in the Device Chain: its track, its scene (or -1), the clip, its slot
    // (null on the timeline), and the tuning it plays in. False when none is shown.
    bool shownMidiClip (int& track, int& scene, mixer::Clip&, mixer::SlotPtr&, mixer::TuningChoice& tuning) const;
    // Stochasticize (StochasticSection.cpp, owner 2026-10-26): a new line by Xenakis's methods
    // (mixer/Stochastic.h), ST clouds or Markov screens, its pitches and rhythm through sieves
    // that can come from the mode, a rhythmic mode or the clip itself; on a new track, or in
    // place of the clip.
    void stochasticizeShownMidi();
    // cubify: a scene's clips (or the clip shown) as 24 sections by Xenakis's cubes, onto their
    // tracks after everything already on them; MIDI tracks get a MIDI file each, audio loops
    // play in the sections their corner of the grand cube turns into the upper tetrahedron.
    void cubifyShownMidi();
    void cubifyScene (int scene);
    void cubifyTracks (const std::vector<std::pair<int, mixer::Clip>>& clips, const QString& name);
    // Minimalize (owner, 2026-10-26; mixer/Minimalize.h): an arrangement by Reich's and Glass's
    // processes, from a scene's MIDI slots or from the timeline at `from` on (cubify's
    // arrangement, a Φ deal), placed after everything on the tracks, all starting together;
    // new voices (phase copies, canons, a pulse) on the free tracks.
    void minimalizeScene (int scene);
    void minimalizeTimeline (Frame from);
    // `parts` (one cell each: a slot's loop, or the whole stretch of the timeline, which the
    // dialog cuts into sections) come from `tracks`, at `bpm`.
    void arrangeMinimal (const std::vector<int>& tracks, std::vector<minimalize::Part> parts, double bpm, bool timeline, const QString& what);
    // Recording (docs/recording.md; owner, 2026-10-25: "play a melody into a clip with a
    // pre-determined length that can be adjusted and the recording immediately stops, loops, and
    // the new melody is generated", and "the main method super easy to learn with the options for
    // the advanced performer"). The transport's Record (Ctrl+R) in the launcher steps the armed
    // track through record, play, overdub and play again; in the tracks across time it records
    // onto the armed track from the playhead until pressed again. A slot's record dot records into
    // that slot. What the take does after (play, stop, record the next slot, add counterpoint),
    // its length, quantise, count-in and the audio input are the transport's Record options.
    QWidget* buildTransport();   // the transport row across the top, for both views
    void recordPressed (int intoLauncher = -1);   // -1 as the mixer's view is; the editor's track says which
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
    // An audio file dropped on the editor's MIDI-only track: as MIDI (Drums to MIDI) or as audio
    // (the track then Audio + MIDI), as asked. False when the drop goes on as usual.
    bool audioIntoMidiTrack (int track, const QString& file, int scene, Frame at);
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
    // The returns, A and B: Φ Space and Φ Echo by default, but any effect (built-in, stock or a plug-in).
    static QString returnName (int index);   // "Return A"
    void returnEffectsMenu (int index, QMenu&);
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
    void showStatus (const QString&);   // the line under the strips, for a few seconds
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
    int dragFrom = -1, dragTarget = -1;   // showTrackDrag
    class QFrame* dropMarker = nullptr;
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
    bool inspectorClosed = false;
    QPointer<QAction> inspectorAction;
    void toggleDevicePanel();   // Shift+Tab over the bottom panel: the Device Chain or the clip
    QStackedWidget* devicePanel = nullptr;
    std::array<QWidget*, 3> instrumentHosts {};
    std::array<bool, 3> instrumentOpens {};
    QPointer<QWidget> trackEditor;       // in instrumentHosts[2]
    QString trackEditorName;
    void dockTrackEditor (QWidget* editor, const QString& name);
    int lowerFront = -1;                 // the instrument in front; -1: the Device Chain
    int lastFront = 1;                   // the instrument in front last
    class QTabBar* lowerTabs = nullptr;  // the Device Chain and the open instruments
    void updateLowerZone();
    TrackDevices* devices = nullptr;
    ClipView* clipView = nullptr;
    bool showingClip = false;
    bool devicesHidden = false;   // by Ctrl+Shift+C
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
    std::unique_ptr<mixer::MtsEsp> mtsEsp;   // running while it is on
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
    int idleTicks = 0;
    int selected = 0;
    int shownTrack = 0;              // the editor's track: the last track selected
    EditorTrack* editorView = nullptr;
    int stripSections = 31;
    std::set<int> picked;            // strips Q-Link joins (the selected one is always among them)
    QPushButton* qLinkButton = nullptr;
    QPushButton* bypassInsertsButton = nullptr;
    QPushButton* bypassSendsButton = nullptr;
    std::array<ReturnStripWidget*, mixer::numReturns> returnStrips {};          // StripSection bits (MixerWindow.cpp) shown on every strip
    QString path;                    // the mix file; empty: not saved yet
    bool modified = false;
    bool restoring = false;
    bool opening = false;            // inside loadMix: its newMix() keeps the patchbay
    bool quitting = false;           // after dropAutosave: nothing more written to last.phi
    // A deleted track, all that copyTrack moves of it, kept for undo to bring it back where it was.
    struct TrackCopy
    {
        int index = 0;
        std::unique_ptr<mixer::Strip> strip;   // its settings and sends
        mixer::Clips clips;
        mixer::InstrumentPtr instrument;
        mixer::Inserts inserts;
        std::array<mixer::SlotPtr, mixer::maxScenes> slots {};
        QString name;
        QColor colour;
        AudioDataPtr sound;
        mixer::TuningChoice tuning;
        std::pair<QString, QColor> givenDefault;
        HeardTuning heardTuning;
        std::vector<uint32_t> timelinePasses;
        LayerTake layerTake;
        std::vector<int> duckedBy;           // the tracks its sound ducked
        bool live = false;
        std::vector<mixer::Clips> history;   // its clips in the steps below this one's, the nearest first
    };
    // An undo (or redo) step: the clips before an edit, or a track to take away or bring back.
    struct Step
    {
        Snapshot clips;
        int removes = -1;                      // this track goes (undoing its adding)
        std::shared_ptr<const TrackCopy> restores;   // this track comes back (undoing its deleting)
        int moveFrom = -1, moveTo = -1;        // this track moves back (undoing its dragging)
    };
    void copyOut (int index, TrackCopy&);          // all of track `index` that copyTrack moves
    void copyIn (const TrackCopy&, int at);
    // Track `from` dragged to `to`, the tracks between moving over by one; false when it cannot.
    bool moveTrack (int from, int to);
    // Track `index` taken out, the tracks after it moved down; null when it cannot be. Its
    // clips in the steps of `stack` (where the step bringing it back goes) are kept with it.
    std::shared_ptr<const TrackCopy> takeTrack (int index, const std::vector<Step>& stack);
    bool putTrack (const TrackCopy&, std::vector<Step>& stack);   // stack: the one its step came from
    // A step done, and the one undoing it made; false (nothing done) when it cannot be.
    bool apply (const Step&, std::vector<Step>& from, const std::vector<Step>& to, Step& inverse);
    void pushUndo (Step);            // a new edit's: there is nothing to redo after it
    std::vector<Step> undoStack, redoStack;
    uint32_t nextClipId = 1;
    std::map<QString, std::weak_ptr<const mixer::AudioClip>> audioCache;
};

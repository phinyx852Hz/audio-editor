// SPDX-License-Identifier: AGPL-3.0-only
#pragma once
#include <atomic>
#include <QPointer>

#include "NyquistUi.h"

#include "../core/AudioDocument.h"
#include "../core/Repair.h"
#include "../core/Dsp.h"
#include "../engine/AudioCapture.h"
#include "../engine/AudioEngine.h"
#include "CommandRegistry.h"
#include "Macros.h"
#include "Recovery.h"

#include <mutex>
#include "../engine/Export.h"
#include "WaveformView.h"

#include <QDialog>
#include <QElapsedTimer>
#include <QFutureWatcher>
#include <QHash>
#include <QMainWindow>
#include <QTimer>
#include <atomic>
#include <memory>

class ChainRow;
class FindPanel;
struct FindItem;
class DockablePanel;
class MarkerList;
class MasterPanel;
class MeterPanel;
class Album;
class AlbumView;
class QDragEnterEvent;
class QDropEvent;
class QLabel;
class QListWidget;
class QSlider;
namespace vamphost { struct Result; }

namespace timestretch { class SegmentCache; }

namespace sampler { class Engine; }
namespace slicer { class Engine; }
namespace mixer { class Engine; }
namespace control { class Learn; struct Command; struct State; }
namespace patchbay { class PatchBay; }
namespace perf { class Recorder; class Camera; class VideoFile; }
namespace sonify { class Sonifier; struct Message; struct Picture; }

class MainWindow final : public QMainWindow
{
    Q_OBJECT

public:
    explicit MainWindow (AudioEngine&);
    ~MainWindow() override;

    void openFile (const QString& path);  // in a new tab when the current one holds a file

protected:
    void closeEvent (QCloseEvent*) override;
    void changeEvent (QEvent*) override;
    void dragEnterEvent (QDragEnterEvent*) override;   // files dropped on the editor open
    void dropEvent (QDropEvent*) override;

private:
    void sendThemeToPluginFrames();
    struct LoadResult
    {
        QString path;
        AudioDataPtr data;
        std::vector<Marker> markers;
        Tags tags;
        std::string error;
    };

    void registerCommands();
    void buildMenus();
    void buildPanels();
    void restoreLayout();
    void saveLayout();
    void restoreDockSizes();   // each docked panel back to the size it closed at
    DockablePanel* focusedOrLastPanel() const;
    QList<FindItem> findItems();   // processes, effects and plugins for the Find panel
    void updateStatus();
    void updateCommandStates();
    void updateTitle();
    void refreshFromDocument (bool keepView);
    void loadFinished();

    // File
    void showOpenDialog();
    bool save();
    bool saveAs();
    bool saveTo (const QString& path, bool askOptions);
    bool confirmDiscard();
    void closeFile();

    // Editing. Each edit replaces [start, end) of the current data with
    // `inserted` frames and records one undo step.
    void commit (const QString& description, AudioDataPtr newData, Frame start, Frame end, Frame inserted);
    void undo();
    void redo();
    void jumpToHistory (int index);
    void copy();
    void copyToNew();  // selection (or the whole file) into a new untitled tab
    void warnAboutDesktopShortcuts();
    void record();              // Record dialog: desktop audio, an input or one app
    void recoverRecordings();   // offers takes left behind by a crash
    void recoverDocuments();    // offers unsaved files left behind by a crash (see Recovery)
    void autosave();
    void cut();
    void paste();
    void deleteSelection();
    void trimToSelection();
    void insertSilence();
    void generateSignal (bool noise);   // Effect > Generate > Tone or Noise
    void correctPitch();                // Repair > Correct Pitch (PitchSection.cpp)
    void binauralRender();              // Spatial > Binaural (SpatialSection.cpp)
    void chooseChannelLayout();         // Spatial > Channel Layout
    void ambiEncode();                  // Spatial > Encode to Ambisonics
    void ambiRotate();                  // Spatial > Rotate / Mirror
    void ambiDecode();                  // Spatial > Decode Ambisonics
    void speakerArrays();               // Spatial > Speaker Arrays
    void ambiStudio();                  // Spatial > Ambisonic Studio (E4L's devices, in a floating panel)
    void ambiConvertFuma (bool toFuma);
    int ambisonicOrderOrHint (const QString& what);   // the file's order, or 0 with a hint
    void feedAmbiScene();
    void openInterfaceMixer();          // Options > Audio Interface Mixer
    void chooseInterfaceMixer();

    // Tools (ToolsSection.cpp).
    void managePlugins();
    void focusPanel (int direction);   // F6 / Shift+F6: the next or previous of the waveform and every panel
    int panelStop = 0;                         // where F6 last went, when the focus cannot tell
    DockablePanel* openedByCycle = nullptr;    // a closed panel F6 opened; it closes again when F6 moves on
    QPointer<QWidget> cycleFrom;               // the keyboard focus when F6 last moved on

    // Panel navigation (PanelNavigation.cpp): Ctrl+W or Super+W closes only what has the
    // keyboard, F6 held with an arrow key moves to the panel that way, Super+F full screen.
    struct PanelStop { QWidget* widget; DockablePanel* panel; };
    std::vector<PanelStop> panelStops() const;   // the waveform (or the mixer in its place), then every panel
    int stopWithFocus (const std::vector<PanelStop>&) const;   // -1 when the keyboard is elsewhere
    void focusStop (const PanelStop&);
    void focusDirection (int arrowKey);
    void closeFocused();
    void toggleFullScreen (QWidget* window);
    bool isPanelWindow (const QWidget*) const;   // a panel popped out into a window of its own
    bool navigationKey (QObject*, QEvent*);     // the application-wide filter for the keys above
    void buildNavigation();
    void openDropped (const QStringList& paths);
    bool panelKeyHeld = false, panelKeyMoved = false;   // the Next Panel key (F6) held, and an arrow pressed with it
    int panelKey = 0;
    bool altArmed = false, altMoved = false;   // Alt down with nothing else yet, and an arrow pressed with it
    void resetConfiguration();
public:
    // After the window is gone: clears the settings if Reset Configuration asked to; true if it did (then restart).
    static bool finishReset();
private:
    struct Reset { bool requested = false, keys = false, plugins = false; };
    static Reset pendingReset;

    // Macros (MacroSection.cpp, Macros.h).
    void registerMacroCommands();
    void toggleMacroRecording();
    void runMacro (const macro::Macro&);
    MacroRecorder* macroRecorder = nullptr;
    QLabel* macroStatus = nullptr;
    QString lastMacro;

    // Processing applies to the selection, or to the whole file when nothing is selected.
    std::pair<Frame, Frame> processRange() const;
    void gain();
    void normalize();

    // Live edits: the gain knob, its keys and the fade handles change the audio
    // you hear and see while you adjust, and become one undo step when done.
    void buildLiveEdits();
    void placeGainHud();          // shows, hides and centres the knob over the waveform
    void previewLive (const QString& description, Frame start, Frame end, std::function<AudioDataPtr (const AudioData&)> make);
    void finishLive();
    void cancelLive();
    void nudgeGain (double db);   // keyboard: Ctrl+Shift+Up/Down, merged into one step after a pause
    AudioDataPtr liveBase;        // the audio before the live edit; null when none is going on
    AudioDataPtr livePreview;
    QString liveDescription;
    Frame liveStart = 0, liveEnd = 0;
    std::function<AudioDataPtr (const AudioData&)> liveMake;
    QTimer liveTimer;             // limits how often a preview is computed
    QTimer nudgeTimer;            // a pause after keyboard nudges finishes them
    double nudgeDb = 0.0;
    class GainHud* gainHud = nullptr;

    // Nyquist effects (Effect, Tools and Analyze menus): see NyquistUi.h.
    void registerNyquistCommands();
    void fillNyquistMenus();
    void rescanNyquist();
    void runNyquistCommand (const QString& id, bool repeat = false);
    void nyquistPrompt();
    void runNyquist (nyquist::Script, const QString& title, bool debug);
    void stereoize();
    void midSideWidth();
    void convertSampleType();
    void editMetadata();
    void autoHeal();
    void learnNoise();
    void noiseReduction();
    void declick();
    void declip();
    void dehum();
    void deess();
    // Runs a restoration on a worker thread with a progress bar; null when cancelled.
    AudioDataPtr runRepair (const QString& title, std::function<AudioDataPtr (repair::Progress)> work);
    void commitRepair (const QString& description, AudioDataPtr result, Frame start, Frame end);
    void spectralGain();
    void chooseFrequencyRange();
    void setBand (double lowHz, double highHz);
    bool hasBand() const { return bandHigh > bandLow || shape != nullptr; }
    // The selection as a spectral region: [s, e), limited to the band or the drawn shape.
    repair::SpectralRegion region (Frame s, Frame e) const { return { s, e, bandLow, bandHigh, shape }; }
    QString bandText() const;   // "200 Hz to 4 kHz", or the drawn shape
    // A shape drawn with the lasso or a brush becomes the selection; the Spot Healing Brush also heals it.
    void setShape (std::shared_ptr<const repair::SpectralMask>, bool heal);
    void chooseTool (WaveformView::Tool);
    void addHarmonics();
    void findSimilar();

    // Diagnostics panel
    void scanDiagnostics();
    void showProblem (Frame start, Frame end);
    void repairProblems (std::vector<int> rows);
    void markProblems (std::vector<int> rows);
    void deleteSilence (std::vector<int> rows);
    void fade (bool in);
    void simpleProcess (const QString& description, AudioDataPtr (*fn) (const AudioData&, Frame, Frame));

    // Markers
    void commitMarkers (const QString& description, std::vector<Marker>);
    void addMarker();
    void addRegion();
    void jumpToMarker (int id);
    void jumpToAdjacentMarker (bool forward);
    void renameMarker (int id);
    void deleteMarker (int id);

    void showLoudnessStatistics();

    // Album (album assembly, see Album.h and AlbumEditor.cpp). An album
    // is a tab whose document holds the album; its audio is the mixdown.
    void registerAlbumCommands();
    void updateAlbumCommandStates();
    const Album* album() const;
    int currentSong() const;                 // under the cursor, else the next one, else the last; -1 when empty
    bool refuseAlbumEdit();                // true, with a hint, when the active tab is an album
    void newAlbum();
    void openAlbum (const QString& path);
    bool saveAlbum (const QString& path);
    bool saveAlbumAs();
    void addSongs();
    void addSongFiles (const QStringList& paths);
    void commitAlbum (const QString& description, Album, Frame newCursor = -1);
    void jumpToSong (int direction);
    void moveSong (int direction);
    void changePause (double seconds);
    void songProperties();
    void albumProperties();
    void removeSong();
    void openSong();
    void exportAlbum();
    void refreshAlbumSources();            // picks up edits made to its songs in other tabs
    // Runs `work` on a worker thread behind a progress dialog; false when cancelled.
    bool runWithProgress (const QString& title, const std::function<void (std::atomic<float>&, const std::atomic<bool>&)>& work);

    // Analysis with Vamp plugins (Analysis.cpp): markers from beats, onsets or
    // any plugin, tempo and key into tags, and slices cut at the markers.
    void registerAnalysisCommands();
    // The Warp menu (WarpSection.cpp): the beat grid and stretching to a tempo.
    void registerWarpCommands();
    void updateWarpCommandStates();
    void refreshGrid();
    void applyGrid (const QString& description, BeatGrid);
    void detectBeatGrid();
    void autoDetectGrid();   // on opening a loop with no tempo yet, in the background
    void setTempoDialog();
    void stretchToTempo();
    void stretchBy (double speed);
    // Paulstretch on the selection or the whole file (WarpSection.cpp): the dialog, or
    // `again` with the settings used last, at once.
    void extremeStretch (bool again);
    // Live-style clip editing (WarpSection.cpp): the selection copied right after itself, or
    // dragged elsewhere, overwriting what is there; a moved selection leaves silence behind.
    void duplicateSelection();
    void splitAtCursor();
    void showGridStep();
    void exportLoop();
    // Repeated Double or Half Speed on the same audio renders once from the audio before the
    // first one (one stretch at the total ratio, not a stretch of a stretch).
    struct SpeedChain { AudioDataPtr base, result; Frame start = 0, baseEnd = 0; double speed = 1.0; double baseBpm = 0.0; };
    SpeedChain speedChain;
    void moveSelection (Frame from, Frame to, Frame start, bool copy);   // 2: double speed, 0.5: half speed
    // Replaces [s, e) with it stretched to `target` frames; on the whole file the grid takes `newBpm` (when > 0).
    void applyStretch (const QString& title, Frame s, Frame e, Frame target, int mode, double newBpm);
    // Replaces [s, e) with `stretched`, moving the markers along (one undo step named `description`).
    void putStretched (const QString& description, Frame s, Frame e, AudioDataPtr stretched, double newBpm);
    // Warp markers: shown and edited while warpEditing; the first move makes the document's warp.
    std::shared_ptr<const WarpEdit> currentWarp() const;
    void setWarpEditing (bool);
    void applyWarp (const QString& description, std::shared_ptr<WarpEdit>);
    void warpMove (Frame source, Frame target, int marker, bool reanchor);
    void warpToggle (Frame source);
    void stepTransient (bool forward);
    void nudgeWarpMarker (int direction, bool fine);
    // The Quantize panel (QuantizeSection.cpp, QuantizePanel.h): hitpoints, quantizing
    // with grooves, and the warp algorithm, docked or popped out.
    void buildQuantizePanel();
    void registerQuantizeCommands();
    void updateQuantizePanel();
    std::vector<Frame> findHits (const AudioData&) const;   // with the panel's detection and filter
    void detectHitpoints();
    void quantizeNow (bool replaceLast);
    void warpMarkersFromHits();
    void sliceAtHits();
    void grooveFromHits();
    void resetWarp();
    void setWarpMode (int);
    std::vector<std::pair<Frame, Frame>> hitsInRange();    // (source, where it is now) of the hits in the selection or file
    class QuantizePanel* quantizeWidget = nullptr;
    DockablePanel* quantizePanel = nullptr;
    QTimer* detectTimer = nullptr;
    QTimer* requantizeTimer = nullptr;
    struct { AudioDataPtr result; int position = -1; } lastQuantize;   // what Auto Apply replaces
    bool warpEditing = false;
    std::shared_ptr<const WarpEdit> warpDraft;               // hits found, no marker placed yet
    std::shared_ptr<timestretch::SegmentCache> warpCache;
    // The Visualizer (VisualizerSection.cpp).
    void registerVisualizerCommands();

    // The Tracker (TrackerSection.cpp): MilkyTracker's module player.
    void registerTrackerCommands();
    class TrackerWindow* trackerWindow();
    void openModule();
    void trackerPlay (bool patternOnly);
    void trackerStop();

    // Auditioning files from the Find panel (Audition.cpp): played live, not opened.
    struct AuditionVoice;
    void auditionFile (const QString& path);
    void stopAudition();
    void auditionTick();
    std::shared_ptr<AuditionVoice> auditionVoice;
    int auditionRequest = 0;   // a newer request outdates a file still loading
    QTimer auditionWatch;
    void trackerRecord (bool multiChannel);
    void trackerNew();
    bool trackerSave (bool saveAs);
    void trackerLoadSample (int instrument);
    void trackerPreview (int channel, int note, int instrument);
    // Linked instruments: the Tracker playing the Retro Sampler (Ami), slot by slot.
    void trackerLink (int instrument, int slot);
    int trackerAmi (bool load, QString* why = nullptr);   // Ami's patchbay synth, or -1
    void trackerLiveSource();
    void trackerPatchRendered();   // audio thread
    void trackerConnectLinks();
    void trackerSaveLinks (const QString& path);
    void trackerLoadLinks (const QString& path);
    std::shared_ptr<class TrackerLink> trackerLinks;
    bool trackerConfirmDiscard();   // false when the user cancels
    std::unique_ptr<class TrackerEngine> tracker;
    void openFurnace();   // Tracker2
    qint64 furnacePid = 0;

    // The Performance menu (PerformanceSection.cpp, Source/performance): records PHI's
    // window, cameras and sound to MP4 and streams them live.
    void registerPerformanceCommands();
    void toggleVideoRecording();
    void toggleStreaming();
    void performanceSettings();
    void capturePerformanceFrame();
    void startPerformanceCapture();
    void stopPerformanceCaptureIfIdle();
    void updatePerformanceStatus();
    QWidget* performanceTarget (bool follow);
    std::unique_ptr<perf::Recorder> performance;
    QTimer performanceTimer;
    QElapsedTimer performanceStatusClock;
    class QLabel* performanceBadge = nullptr;
    QPointer<QWidget> performanceWindow;
    bool performanceFollow = true;

    // The Sampler (SamplerSection.cpp): its engine plays through engine.setInstrument, its
    // window pops out, and MIDI inputs play it. It remembers which tab and range it came
    // from, so edits there can follow it.
    void registerSamplerCommands();
    void sendToSampler (bool keepMarkers);
    void showSampler();
    void samplerFollow();
    void loadSamplerFile (const QString& path);   // a file dropped on Simplest
    void loadSlicerFile (const QString& path);    // a file dropped on the Slicer
    QString findFile() const;                     // the file highlighted in Find, when Find has the keyboard
    void keepFindFocus (QWidget* had);            // after F7 or F8 from Find
    void midiInputsDialog();
    void openMidiInputs();
    void updateMidiStatus();
    std::unique_ptr<sampler::Engine> samplerEngine;
    class SamplerWindow* samplerWin = nullptr;
    DockablePanel* samplerPanel = nullptr;
    DockablePanel* slicerPanel = nullptr;
    std::unique_ptr<class MidiInputs> midiInputs;
    int samplerTab = -1;
    std::string samplerPath;
    Frame samplerFrom = 0, samplerTo = 0;
    bool samplerWhole = true;
    AudioDataPtr samplerSource;
    QTimer samplerFollowTimer;
    QFutureWatcher<void> samplerLoad;

    // The Slicer likewise (sampler/SlicerEngine.h); MIDI notes play whichever of the two
    // windows was used last (0 Sampler, 1 Slicer, -1 neither is open).
    void sendToSlicer (bool keepSlices);
    void showSlicer();
    void slicerFollow();
    void playInstruments();
    void instrumentClosed();
    void playMidi (int status, int channel, int a, int b);   // as a MIDI keyboard would; any thread
    std::unique_ptr<slicer::Engine> slicerEngine;
    class SlicerWindow* slicerWin = nullptr;
    int slicerTab = -1;
    std::string slicerPath;
    Frame slicerFrom = 0, slicerTo = 0;
    bool slicerWhole = true;
    AudioDataPtr slicerSource;
    QTimer slicerFollowTimer;
    QFutureWatcher<void> slicerLoad;
    std::atomic<int> midiTarget { -1 };   // 2: the Mixer's armed track, 3: a patchbay synth

    // The Mixer (MixerSection.cpp, docs/mixer.md): 8 tracks, each with an audio file,
    // a MIDI file and an instrument; heard through engine.setInstrument with the Sampler.
    void registerMixerCommands();
    // The Mode menu (ModeSection.cpp, docs/mixer.md): one mode, from every tradition PHI
    // has, for the MIDI clip, counterpoint, the fugue and the PHI synth's tuning together.
    void registerModeCommands();
    void fillModeMenu (QMenu*);
    // Its rhythmic modes (RhythmSection.cpp, docs/rhythms.md): a rhythm put on the selected
    // mixer track, written into a new Tracker pattern, or saved as a MIDI file.
    void fillRhythmMenu (QMenu*);
    void useRhythm (int index);
    void rhythmToTracker (int index, int cycles, double bpm);
    // Control surfaces and MIDI Learn (ControlSection.cpp, docs/control-surfaces.md): Launchpads,
    // Launchkeys and Push play the mixer's launcher, strips and transport, lit in the tracks'
    // colours; any other controller's knobs and buttons are mapped by MIDI Learn.
    void registerControlCommands();
    void controlSurfacesDialog();
    void midiLearnDialog();
    void controlTick();                                // their commands done, their lights sent
    void controlCommand (const control::Command&);
    void saveMidiMappings();
    void drawPushDisplay (const control::State&);      // Push 2 or 3's screen, over USB
    std::unique_ptr<class ControlSurfaces> surfaces;
    std::unique_ptr<class PushDisplay> pushDisplay;
    std::unique_ptr<control::Learn> midiLearn;
    QTimer controlTimer;
    int controlTicks = 0;
    QPointer<QDialog> learnDialog;
    std::function<void()> learnDialogRefresh;          // the open MIDI Learn dialog's list, again
    class MixerWindow* mixerWindow();
    void showMixer();
    void editMixerClip (int track, quint32 clip);
    void returnMixerEdits();
    void mixDownToEditor (int track = -1);   // the whole mix, or one track as it sounds, into a new tab
    std::unique_ptr<mixer::Engine> mixerEngine;
    class MixerWindow* mixerWin = nullptr;

    // The mixer takes the editor's place (centralStack) unless it was undocked
    // (mixer/undocked; a project file can set it with placeMixer before showMixer).
public:
    bool mixerDocked() const;
    void placeMixer (bool docked);   // in the editor's place or in a window of its own, showing
private:
    void showEditorPage();            // the editor back in front; a docked mixer steps aside
    // A docked mixer in front shows the selected track's own Device Chain, so the editor's
    // (the master's) steps aside until the Master strip is picked or the editor comes back.
    void updateChainDock();
    bool chainHiddenForMixer = false;
    // The Find panel is the mixer's Finder in the editor's place: it and the Inspector take
    // turns (owner, 2026-10-25). Hidden for the Inspector, it comes back with the editor.
    void mixerFinder (bool show);
    void updateFindDock();
    bool findHiddenForMixer = false;
    bool hidingFindForMixer = false;
    void sendToLauncher();   // the editor's sound (or its selection) into the mixer's launcher
    void sendToTimeline();   // the same onto the editor's track at the playhead
    // The editor's track (owner, 2026-10-26, MixerWindow::editorTrackView): under the waveform,
    // in a split the user sizes (F4 shows or hides it). Made with the mixer, the first time shown.
    void showEditorTrack (bool on);
    class QSplitter* editorSplit = nullptr;
    QWidget* trackHolder = nullptr;
    class QStackedWidget* centralStack = nullptr;
    QWidget* editorPage = nullptr;
    bool mixerFullFromDock = false;   // Super+F took the docked mixer full screen; again puts it back

    // The patchbay (PatchBaySection.cpp, docs/patchbay.md): one routing graph for the
    // editor's playback, the Sampler, the Slicer, the mixer's tracks, its synths and FX
    // buses, heard through engine.setInstrument in place of the plain sum. F4 pops it out.
    // Declared after the mixer's engine, so it lets go of the mixer first.
    void registerPatchBayCommands();
    // The stock instruments (InstrumentsSection.cpp): Sample and Synth menus, Find's Instruments.
    void registerInstrumentCommands();
    void openInstrument (const char* key);
    int instrumentSynth (const char* key, bool load, QString* why = nullptr, bool* failed = nullptr);
    class PatchBayWindow* patchBayWindow();
    void showPatchBay();
    void restorePatchBay();
    void savePatchBay();
    bool patchBayInUse() const;   // its window is open, or it holds patches, synths or effects of its own
    // The patchbay in a project file (.phi): its patches, synths and effects.
    class QJsonObject patchBayJson() const;
    void setPatchBayJson (const class QJsonObject&);
    void resetPatchBay();         // no synths, no effects, every source where it is heard without a patchbay

    // Projects (.phi, docs/project.md): the mixer's mix, the patchbay and the view, kept
    // in the file and in last.phi in the config folder, so the next start carries on.
    void openProject (const QString& path);
    void restoreProject();        // at start: the project open when PHI was last closed
    bool mixerInView() const;     // the Mixer is what is shown, rather than the editor
    bool projectPatchBay = false; // the patchbay came from a project, not settings.ini
    class QJsonObject projectState() const;               // what a project keeps besides the mix
    void setProjectState (const class QJsonObject&);      // from a project being opened
    std::unique_ptr<patchbay::PatchBay> patchBay;
    class PatchBayWindow* patchBayWin = nullptr;
    std::atomic<int> patchSynth { 0 };   // the synth the MIDI keyboard plays (midiTarget 3)
    QTimer patchBaySave;
    class TrackerWindow* trackerWin = nullptr;
    class AnalysisWindow* analysisWindow();   // Analysis Meters, made when first opened
    class AnalysisWindow* analysisWin = nullptr;
    class PluginAnalyzer* pluginAnalyzer();   // Plugin Analyzer, made when first opened
    class PluginAnalyzer* pluginAnalyzerWin = nullptr;
    void analyzeSlot (int slot);              // opens it on a Master Section slot (-1: the whole section)
    void feedAnalysisAmbisonics();
    const class AudioData* ambiFedData = nullptr;   // what the Ambisonics card last got
    Frame ambiFedAt = -1;
    int ambiFedOrder = 0;
    class VisualizerWindow* visualizerWindow();
    void setVisualMode (int index, bool openIfHidden);
    void toggleVisualizerBehind();
    void feedVisualizers();
    bool visualizerShowing() const;
    void addVisualizerSubmenu (QMenu* menu, const QString& which);   // artistic, science, milkdrop, rain
    void syncVisualModeActions();
    class QActionGroup* visualModeActions = nullptr;   // the window or the one behind PHI
    void showMilkDropPreset (const QString& path);
    void setRainTempo (double bpm);   // Digital Rain, saved and shown
    double fileTempo() const;         // the open file's BPM tag, or 0
    class VisualizerView* showMilkDrop();
    void chooseMilkDropPreset();
    void downloadMilkDropPresets();
    class VisualizerWindow* visualizer = nullptr;
    class VisualizerView* visualBackdrop = nullptr;
    // A web page behind PHI instead (VisualizerSection.cpp, WebBackdrop.h); only with Qt WebEngine.
    void toggleWebBackdrop();
    bool chooseWebAddress (bool reload);   // false when cancelled
    class WebBackdrop* webBackdrop = nullptr;
    int visualMode = 0;
    double opacityBeforeBackdrop = 0.0;

    // Sonifying the Visualizer (SonifySection.cpp): what it shows, played as notes.
    void registerSonifyCommands();
    void addSonifyPanel();
    void setSonifying (bool on);
    void sonifyTick();
    bool chooseSonifySource (int source);   // false when cancelled
    bool openSonifyFile (const QString& path);
    bool startSonifyCamera();
    void stopSonifyMotion();   // the camera or video
    void showSonifyPicture (const class QImage&);
    void sendSonified (const std::vector<sonify::Message>&);
    int sonifySynth();         // the patchbay slot of the built-in Synth it plays, made when needed; -1: none free
    void saveSonifySettings();
    class SonifyPanel* sonifyPanel = nullptr;
    std::unique_ptr<sonify::Sonifier> sonifier;
    std::unique_ptr<sonify::Picture> sonifyPicture;   // the newest, small
    std::unique_ptr<perf::Camera> sonifyCamera;
    std::unique_ptr<perf::VideoFile> sonifyVideo;
    uint64_t sonifySerial = 0;     // the camera's or video's last picture shown
    bool sonifying = false;
    int sonifySource = 0;          // SonifyPanel::Source
    int sonifySlot = -1;           // the patchbay synth it last played
    int sonifyTarget = 0;          // SonifyPanel::Target, as last played
    QElapsedTimer sonifyClock;
    qint64 sonifyLast = 0, sonifyAsked = 0;
    void updateAnalysisCommandStates();
    bool runAnalysis (const QString& title, const AudioData&, Frame start, Frame end, const QString& outputId, vamphost::Result&);
    void markFeatures (const QString& outputId, const QString& title);
    void detectTempoAndKey();
    void chooseVampPlugin();
    void runVampOutput (const QString& outputId);
    void exportSlices();
    void exportLabels();

    // A/B Reference section (ReferenceSection.cpp, ReferencePanel.h).
    struct ReferenceTrack
    {
        QString path;
        AudioDataPtr original;   // as loaded; null until first heard
        AudioDataPtr atRate;     // converted to your audio's sample rate
        double lufs = -200.0;    // integrated loudness of the whole reference
        double offsetDb = 0.0;   // your own adjustment on top of the loudness match
    };
    void buildReferences();
    void registerReferenceCommands();
    void addReferences();
    void addReferenceFiles (const QStringList& paths);
    void removeReference (int row);
    bool loadReference (int row);
    bool matchLoudness();                    // measures your audio as heard, once per version of it
    float referenceGain (int row) const;
    void hearReference (int row);
    void setHearingReference (bool);
    void syncReference();
    void placeReferenceBadge();
    void updateReferencePanel();
    void saveReferences();
    std::vector<ReferenceTrack> references;
    int currentReference = -1;
    bool referenceClean = true;
    const AudioData* matchedFor = nullptr;   // the audio yourLufs was measured on
    AudioDataPtr matchedData;                // keeps it alive so the pointer stays unique
    double yourLufs = -200.0;
    bool yourThroughMaster = false;
    class ReferencePanel* referencePanel = nullptr;
    DockablePanel* referencesPanel = nullptr;
    QLabel* referenceBadge = nullptr;

    // Window opacity (see Theme::setOpacity)
    void changeOpacity (double value, bool announce);
    QSlider* makeOpacitySlider (QWidget* parent);

    // Master Section
    bool masterWouldChange (const QString& title);   // false (with a note) when rendering would change nothing
    void renderInPlace();     // bounce the selection or file through the Master Section
    void masterAssistant();
    void mixAssistant();      // sets the mixer's faders and pans by role (MixAssistant.h)   // tunes the Master Section's knobs towards a target (MasterAssistant.h)
    void renderToNewTab();    // the same into a new untitled tab

    // Export (ExportSection.cpp): every output of the Export dialog, and its codec preview.
    void exportAudio();
    void previewExport (const exporter::Output&, bool difference, bool throughMaster);
    void stopExportPreview();
    QString previewName;                       // shown on the badge while a preview is heard
    std::pair<Frame, Frame> previewRange { 0, 0 };
    AudioDataPtr renderWithProgress (Frame start, Frame end, std::string& error, bool withDither = true);

    // Cursor and selection
    bool hasSelection() const { return selEnd > selStart; }
    bool requireSelection();  // shows a hint when nothing is selected
    void setCursor (Frame);
    void moveCursorBy (double seconds);
    void selectRange (Frame anchor, Frame head);
    void extendSelectionBy (double seconds);
    void extendSelectionTo (Frame);
    Frame snapToZero (Frame) const;   // the nearest zero crossing when Snap to Zero Crossings is on
    void togglePlayback();
    void openMenuBar();

    AudioDataPtr data() const { return doc.isOpen() ? doc.current().data : nullptr; }

    AudioEngine& engine;
    CommandRegistry commands;
    AudioCapture capture;
    AudioDocument doc;
    WaveformView* waveform;
    class InfoStrip* infoStrip;
    QListWidget* historyList = nullptr;
    MarkerList* markerList = nullptr;
    class DiagnosticsPanel* diagnostics = nullptr;
    const AudioData* diagnosed = nullptr;   // the audio the diagnostics list belongs to
    MeterPanel* meterPanel = nullptr;
    MasterPanel* masterWidget = nullptr;
    DockablePanel* masterPanel = nullptr;
    ChainRow* chainRow = nullptr;
    DockablePanel* chainPanel = nullptr;
    DockablePanel* historyPanel = nullptr;
    DockablePanel* markerPanel = nullptr;
    DockablePanel* diagnosticsPanel = nullptr;
    DockablePanel* metersPanel = nullptr;
    FindPanel* findWidget = nullptr;
    class Goniometer* goniometer = nullptr;
    class AmbiScene* ambiSceneView = nullptr;
    class AmbiStudio* ambiStudioWindow = nullptr;
    DockablePanel* ambiScenePanel = nullptr;
    std::vector<float> scopeLeft, scopeRight;   // goniometer samples, reused every frame
    DockablePanel* findPanel = nullptr;
    QList<DockablePanel*> panels;
    QList<QSlider*> opacitySliders;
    class FocusFollowsMouse* focusFollowsMouse = nullptr;
    DockablePanel* lastPanel = nullptr;
    QByteArray defaultLayout;
    static constexpr int layoutVersion = 3;  // 2: device chain row at the bottom, 3: Simplest above it, the Slicer left

    Frame cursor = 0, selStart = 0, selEnd = 0, selAnchor = 0;
    Frame playStart = 0;        // where the last Play began; Stop returns here
    bool followingPlayback = false;   // the cursor is following playback (see the UI timer)
    bool looping = false;
    class TransportBar* transportBar = nullptr;
    class TimeRuler* timeRuler = nullptr;
    AlbumView* albumView = nullptr;
    class Overview* overview = nullptr;
    std::vector<nyquistui::Entry> nyquistEntries;
    QMap<QString, QAction*> nyquistAnchors;       // where each menu's Nyquist items go
    QMenu* generateMenu = nullptr;
    QList<QAction*> nyquistMenuItems;
    QString lastEffectId;                         // for Repeat Last Effect
    double bandLow = 0.0, bandHigh = 0.0;   // the selection's frequency band in Hz, none when high <= low
    std::shared_ptr<const repair::SpectralMask> shape;   // a drawn selection, in place of the band
    double spectralGainDb = -12.0;
    repair::NoiseProfile noiseProfile;   // learned with Learn Noise Profile, for this session
    AudioDataPtr clipboard;
    dsp::FadeShape fadeShape = dsp::FadeShape::equalPower;
    SaveOptions saveOptions;
    QString optionsConfirmedFor;  // path whose save options the user has already chosen

    QFutureWatcher<LoadResult> loadWatcher;
    std::shared_ptr<std::atomic<float>> loadProgress;
    bool loading = false;

    // Editor tabs, one file each. The active tab's document, cursor and
    // selection live in the members above while it is active; the others are
    // parked here and swapped in when their tab is chosen.
    struct TabState
    {
        AudioDocument doc;
        Frame cursor = 0, selStart = 0, selEnd = 0, selAnchor = 0;
        double bandLow = 0.0, bandHigh = 0.0;
        std::shared_ptr<const repair::SpectralMask> shape;
        SaveOptions saveOptions;
        QString optionsConfirmedFor;
        std::pair<double, double> view { 0.0, 0.0 };
        // Opened from the mixer (a clip double-clicked): F3 puts it back on that clip.
        int mixerTrack = -1;
        quint32 mixerClip = 0;
        QString mixerName;
    };

    void newTab();
    void switchToTab (int index);
    void parkActiveTab();
    void unparkTab (int index);
    void updateTabBar();
    bool closeTab();                 // the active tab; asks about unsaved changes
    QString tabName (const AudioDocument&) const;

    class QTabBar* tabBar = nullptr;
    std::vector<TabState> tabs { TabState() };
    int activeTab = 0;

    // The Batch Processor (BatchWindow.h), made when first opened.
    void showBatch();
    class BatchWindow* batchWindow = nullptr;
    class SpectrogramDialog* spectrogramDialog = nullptr;
    std::mutex masterRenderLock;   // batch workers render through the Master Section one at a time

    Recovery recovery;
    QTimer autosaveTimer;

    // The CPU meter in the status bar: PHI's own processor use and the audio thread's load.
    void updateCpuMeter();
    class QLabel* cpuLabel = nullptr;
    QTimer cpuTimer;
    QElapsedTimer cpuWatch;
    long long cpuLastTicks = 0;
    QHash<QString, QString> recoveringFrom;   // recovery file being opened -> the file it was an unsaved copy of
};

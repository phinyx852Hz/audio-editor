// SPDX-License-Identifier: AGPL-3.0-only
#pragma once
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
class QLabel;
class QListWidget;
class QSlider;
namespace vamphost { struct Result; }

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
    // The Visualizer (VisualizerSection.cpp).
    void registerVisualizerCommands();

    // The Tracker (TrackerSection.cpp): MilkyTracker's module player.
    void registerTrackerCommands();
    class TrackerWindow* trackerWindow();
    void openModule();
    void trackerPlay (bool patternOnly);
    void trackerStop();
    void trackerRecord (bool multiChannel);
    void trackerNew();
    bool trackerSave (bool saveAs);
    void trackerLoadSample (int instrument);
    void trackerPreview (int channel, int note, int instrument);
    bool trackerConfirmDiscard();   // false when the user cancels
    std::unique_ptr<class TrackerEngine> tracker;
    class TrackerWindow* trackerWin = nullptr;
    class VisualizerWindow* visualizerWindow();
    void setVisualMode (int index, bool openIfHidden);
    void toggleVisualizerBehind();
    void feedVisualizers();
    bool visualizerShowing() const;
    void addVisualizerSubmenu (QMenu* menu, const QString& which);   // artistic, science, milkdrop, rain
    void syncVisualModeActions();
    class QActionGroup* visualModeActions = nullptr;   // the window or the one behind PHINYX
    void showMilkDropPreset (const QString& path);
    void setRainTempo (double bpm);   // Digital Rain, saved and shown
    double fileTempo() const;         // the open file's BPM tag, or 0
    class VisualizerView* showMilkDrop();
    void chooseMilkDropPreset();
    void downloadMilkDropPresets();
    class VisualizerWindow* visualizer = nullptr;
    class VisualizerView* visualBackdrop = nullptr;
    int visualMode = 0;
    double opacityBeforeBackdrop = 0.0;
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
    DockablePanel* ambiScenePanel = nullptr;
    std::vector<float> scopeLeft, scopeRight;   // goniometer samples, reused every frame
    DockablePanel* findPanel = nullptr;
    QList<DockablePanel*> panels;
    QList<QSlider*> opacitySliders;
    class FocusFollowsMouse* focusFollowsMouse = nullptr;
    DockablePanel* lastPanel = nullptr;
    QByteArray defaultLayout;
    static constexpr int layoutVersion = 2;  // 2: device chain row at the bottom

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
    QHash<QString, QString> recoveringFrom;   // recovery file being opened -> the file it was an unsaved copy of
};

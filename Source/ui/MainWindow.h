// SPDX-License-Identifier: AGPL-3.0-only
#pragma once

#include "../core/AudioDocument.h"
#include "../core/Repair.h"
#include "../core/Dsp.h"
#include "../engine/AudioCapture.h"
#include "../engine/AudioEngine.h"
#include "CommandRegistry.h"
#include "WaveformView.h"

#include <QFutureWatcher>
#include <QMainWindow>
#include <atomic>
#include <memory>

class ChainRow;
class DockablePanel;
class MarkerList;
class MasterPanel;
class MeterPanel;
class QLabel;
class QListWidget;
class QSlider;

class MainWindow final : public QMainWindow
{
    Q_OBJECT

public:
    explicit MainWindow (AudioEngine&);
    ~MainWindow() override;

    void openFile (const QString& path);  // in a new tab when the current one holds a file

protected:
    void closeEvent (QCloseEvent*) override;

private:
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
    void cut();
    void paste();
    void deleteSelection();
    void trimToSelection();
    void insertSilence();

    // Processing applies to the selection, or to the whole file when nothing is selected.
    std::pair<Frame, Frame> processRange() const;
    void gain();
    void normalize();
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

    // Window opacity (see Theme::setOpacity)
    void changeOpacity (double value, bool announce);
    QSlider* makeOpacitySlider (QWidget* parent);

    // Master Section
    void renderMaster();
    AudioDataPtr renderWithProgress (Frame start, Frame end, std::string& error);

    // Cursor and selection
    bool hasSelection() const { return selEnd > selStart; }
    bool requireSelection();  // shows a hint when nothing is selected
    void setCursor (Frame);
    void moveCursorBy (double seconds);
    void selectRange (Frame anchor, Frame head);
    void extendSelectionBy (double seconds);
    void extendSelectionTo (Frame);
    void togglePlayback();
    void openMenuBar();

    AudioDataPtr data() const { return doc.isOpen() ? doc.current().data : nullptr; }

    AudioEngine& engine;
    CommandRegistry commands;
    AudioCapture capture;
    AudioDocument doc;
    WaveformView* waveform;
    QLabel* statusLabel;
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
    QList<DockablePanel*> panels;
    QList<QSlider*> opacitySliders;
    class FocusFollowsMouse* focusFollowsMouse = nullptr;
    DockablePanel* lastPanel = nullptr;
    QByteArray defaultLayout;
    static constexpr int layoutVersion = 2;  // 2: device chain row at the bottom

    Frame cursor = 0, selStart = 0, selEnd = 0, selAnchor = 0;
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
};

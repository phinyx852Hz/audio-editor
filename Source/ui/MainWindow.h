// SPDX-License-Identifier: AGPL-3.0-only
#pragma once

#include "../core/AudioDocument.h"
#include "../core/Dsp.h"
#include "../engine/AudioEngine.h"
#include "CommandRegistry.h"

#include <QFutureWatcher>
#include <QMainWindow>
#include <atomic>
#include <memory>

class DockablePanel;
class MarkerList;
class MeterPanel;
class QLabel;
class QListWidget;
class WaveformView;

class MainWindow final : public QMainWindow
{
    Q_OBJECT

public:
    explicit MainWindow (AudioEngine&);
    ~MainWindow() override;

    void openFile (const QString& path);

protected:
    void closeEvent (QCloseEvent*) override;

private:
    struct LoadResult
    {
        QString path;
        AudioDataPtr data;
        std::vector<Marker> markers;
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
    void cut();
    void paste();
    void deleteSelection();
    void trimToSelection();
    void insertSilence();

    // Processing applies to the selection, or to the whole file when nothing is selected.
    std::pair<Frame, Frame> processRange() const;
    void gain();
    void normalize();
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

    // Cursor and selection
    bool hasSelection() const { return selEnd > selStart; }
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
    AudioDocument doc;
    WaveformView* waveform;
    QLabel* statusLabel;
    QListWidget* historyList = nullptr;
    MarkerList* markerList = nullptr;
    MeterPanel* meterPanel = nullptr;
    DockablePanel* historyPanel = nullptr;
    DockablePanel* markerPanel = nullptr;
    DockablePanel* metersPanel = nullptr;
    QList<DockablePanel*> panels;
    DockablePanel* lastPanel = nullptr;

    Frame cursor = 0, selStart = 0, selEnd = 0, selAnchor = 0;
    AudioDataPtr clipboard;
    dsp::FadeShape fadeShape = dsp::FadeShape::equalPower;
    SaveOptions saveOptions;
    QString optionsConfirmedFor;  // path whose save options the user has already chosen

    QFutureWatcher<LoadResult> loadWatcher;
    std::shared_ptr<std::atomic<float>> loadProgress;
    bool loading = false;
};

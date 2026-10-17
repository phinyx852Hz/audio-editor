// SPDX-License-Identifier: AGPL-3.0-only
#include "MainWindow.h"
#include "ChainRow.h"
#include "DesktopShortcuts.h"
#include "Dialogs.h"
#include "../core/Repair.h"
#include "MasterPanel.h"
#include "Panels.h"
#include "RecordDialog.h"
#include "Theme.h"
#include "WaveformView.h"

#include <QActionGroup>
#include <QApplication>
#include <QCloseEvent>
#include <QDateTime>
#include <QDir>
#include <QDockWidget>
#include <QSettings>
#include <QSlider>
#include <QWidgetAction>
#include <QStandardPaths>
#include <QFileDialog>
#include <QFileInfo>
#include <QHBoxLayout>
#include <QInputDialog>
#include <QKeyEvent>
#include <QLabel>
#include <QLineEdit>
#include <QListWidget>
#include <QMenuBar>
#include <QMessageBox>
#include <QProgressDialog>
#include <QPushButton>
#include <QEventLoop>
#include <QStatusBar>
#include <QTabBar>
#include <QTimer>
#include <QVBoxLayout>
#include <QtConcurrent/QtConcurrentRun>

#include <algorithm>
#include <cmath>

namespace
{
    constexpr double smallStepSeconds = 0.1;
    constexpr double largeStepSeconds = 1.0;

    QString formatTime (double seconds)
    {
        const auto ms = (long long) (seconds * 1000.0 + 0.5);
        return QString::asprintf ("%02lld:%02lld.%03lld", ms / 60000, (ms / 1000) % 60, ms % 1000);
    }

    QString formatHz (double hz)
    {
        return hz >= 1000.0 ? QObject::tr ("%1 kHz").arg (hz / 1000.0, 0, 'f', hz >= 10000.0 ? 1 : 2)
                            : QObject::tr ("%1 Hz").arg ((int) std::lround (hz));
    }

    namespace Category
    {
        const QString file      = QObject::tr ("File");
        const QString edit      = QObject::tr ("Edit");
        const QString select    = QObject::tr ("Selection");
        const QString process   = QObject::tr ("Process");
        const QString repair    = QObject::tr ("Repair");
        const QString markers   = QObject::tr ("Markers");
        const QString analyze   = QObject::tr ("Analyze");
        const QString master    = QObject::tr ("Master Section");
        const QString transport = QObject::tr ("Transport");
        const QString view      = QObject::tr ("View");
        const QString options   = QObject::tr ("Options");
        const QString help      = QObject::tr ("Help");
    }
}

MainWindow::MainWindow (AudioEngine& e)
    : engine (e), waveform (new WaveformView (this)), statusLabel (new QLabel (this))
{
    setWindowTitle (QStringLiteral (APP_DISPLAY_NAME));
    resize (1100, 620);
    Theme::makeTranslucent (this);
    tabBar = new QTabBar (this);
    tabBar->setDocumentMode (true);
    tabBar->setTabsClosable (true);
    tabBar->setExpanding (false);
    tabBar->setFocusPolicy (Qt::NoFocus);  // Ctrl+Tab and Ctrl+Shift+Tab switch tabs
    tabBar->setAccessibleName (tr ("Open files"));
    tabBar->addTab (QString());
    connect (tabBar, &QTabBar::currentChanged, this, [this] (int index) { switchToTab (index); });
    connect (tabBar, &QTabBar::tabCloseRequested, this, [this] (int index)
    {
        switchToTab (index);
        closeTab();
    });

    auto* editor = new QWidget (this);
    auto* editorLayout = new QVBoxLayout (editor);
    editorLayout->setContentsMargins (0, 0, 0, 0);
    editorLayout->setSpacing (0);
    editorLayout->addWidget (tabBar);
    editorLayout->addWidget (waveform, 1);
    setCentralWidget (editor);
    statusBar()->addWidget (statusLabel, 1);
    statusLabel->setAccessibleName (tr ("Status"));
    statusBar()->addPermanentWidget (new QLabel (tr ("Opacity"), this));
    statusBar()->addPermanentWidget (makeOpacitySlider (this));

    buildPanels();
    registerCommands();
    commands.load();
    buildMenus();
    restoreLayout();

    connect (waveform, &WaveformView::cursorRequested, this, &MainWindow::setCursor);
    connect (waveform, &WaveformView::selectionRequested, this, &MainWindow::selectRange);
    connect (waveform, &WaveformView::bandRequested, this, &MainWindow::setBand);
    connect (&loadWatcher, &QFutureWatcher<LoadResult>::finished, this, &MainWindow::loadFinished);

    auto* uiTimer = new QTimer (this);
    connect (uiTimer, &QTimer::timeout, this, [this]
    {
        engine.dispatchMessages();

        if (engine.isPlaying())
        {
            cursor = engine.positionFrames();
            waveform->setCursorFrame (cursor);
        }

        if (loading && loadProgress)
            waveform->setLoading (true, loadProgress->load());

        meterPanel->addBlocks (engine.takeMeterBlocks());

        for (auto* p : panels)
            if (p->containsFocus())
                lastPanel = p;

        updateStatus();
    });
    uiTimer->start (30);

    updateTitle();
    updateCommandStates();
    waveform->setFocus();
}

MainWindow::~MainWindow()
{
    loadWatcher.waitForFinished();
}

//==============================================================================
void MainWindow::registerCommands()
{
    using KS = QKeySequence;
    auto add = [this] (const char* id, const QString& cat, const QString& text,
                       QList<QKeySequence> keys, std::function<void()> fn)
    {
        auto* a = commands.add (QString::fromLatin1 (id), cat, text, keys, std::move (fn));
        addAction (a);
        return a;
    };

    add ("file.open", Category::file, tr ("&Open..."), { KS::Open }, [this] { showOpenDialog(); });
    add ("file.record", Category::file, tr ("&Record..."), { KS (Qt::CTRL | Qt::Key_R) }, [this] { record(); });
    add ("file.save", Category::file, tr ("&Save"), { KS (Qt::CTRL | Qt::Key_S) }, [this] { save(); });
    add ("file.saveAs", Category::file, tr ("Save &As..."), { KS (Qt::CTRL | Qt::SHIFT | Qt::Key_S) }, [this] { saveAs(); });
    add ("file.close", Category::file, tr ("&Close"), { KS (Qt::CTRL | Qt::Key_W) }, [this] { closeFile(); });
    add ("file.nextTab", Category::file, tr ("Ne&xt Tab"), { KS (Qt::CTRL | Qt::Key_Tab), KS (Qt::CTRL | Qt::Key_PageDown) },
         [this] { if (tabs.size() > 1) tabBar->setCurrentIndex ((activeTab + 1) % (int) tabs.size()); });
    add ("file.previousTab", Category::file, tr ("Pre&vious Tab"),
         { KS (Qt::CTRL | Qt::SHIFT | Qt::Key_Backtab), KS (Qt::CTRL | Qt::SHIFT | Qt::Key_Tab), KS (Qt::CTRL | Qt::Key_PageUp) },
         [this] { if (tabs.size() > 1) tabBar->setCurrentIndex ((activeTab + (int) tabs.size() - 1) % (int) tabs.size()); });
    add ("file.quit", Category::file, tr ("&Quit"), { KS (Qt::CTRL | Qt::Key_Q) }, [this] { close(); });

    add ("edit.undo", Category::edit, tr ("&Undo"), { KS (Qt::CTRL | Qt::Key_Z) }, [this] { undo(); });
    add ("edit.redo", Category::edit, tr ("&Redo"), { KS (Qt::CTRL | Qt::SHIFT | Qt::Key_Z), KS (Qt::CTRL | Qt::Key_Y) },
         [this] { redo(); });
    add ("edit.cut", Category::edit, tr ("Cu&t"), { KS (Qt::CTRL | Qt::Key_X) }, [this] { cut(); });
    add ("edit.copy", Category::edit, tr ("&Copy"), { KS (Qt::CTRL | Qt::Key_C) }, [this] { copy(); });
    add ("edit.copyToNew", Category::edit, tr ("Copy to Ne&w"), { KS (Qt::SHIFT | Qt::Key_C) }, [this] { copyToNew(); });
    add ("edit.paste", Category::edit, tr ("&Paste"), { KS (Qt::CTRL | Qt::Key_V) }, [this] { paste(); });
    add ("edit.delete", Category::edit, tr ("&Delete"), { KS (Qt::Key_Delete), KS (Qt::Key_Backspace) }, [this] { deleteSelection(); });
    add ("edit.trim", Category::edit, tr ("T&rim to Selection"), { KS (Qt::CTRL | Qt::Key_T) }, [this] { trimToSelection(); });
    add ("edit.insertSilence", Category::edit, tr ("Insert &Silence..."), { KS (Qt::CTRL | Qt::ALT | Qt::Key_S) },
         [this] { insertSilence(); });

    add ("select.all", Category::select, tr ("Select &All"), { KS (Qt::CTRL | Qt::Key_A) },
         [this] { if (auto d = data()) selectRange (0, d->length()); });
    add ("select.none", Category::select, tr ("Select &None"), { KS (Qt::Key_Escape) }, [this] { setCursor (cursor); });
    add ("select.back", Category::select, tr ("Extend Selection &Back"), { KS (Qt::SHIFT | Qt::Key_Left) },
         [this] { extendSelectionBy (-smallStepSeconds); });
    add ("select.forward", Category::select, tr ("Extend Selection &Forward"), { KS (Qt::SHIFT | Qt::Key_Right) },
         [this] { extendSelectionBy (smallStepSeconds); });
    add ("select.backLarge", Category::select, tr ("Extend Selection Back 1 s"),
         { KS (Qt::CTRL | Qt::SHIFT | Qt::Key_Left), KS (Qt::SHIFT | Qt::Key_J) }, [this] { extendSelectionBy (-largeStepSeconds); });
    add ("select.forwardLarge", Category::select, tr ("Extend Selection Forward 1 s"),
         { KS (Qt::CTRL | Qt::SHIFT | Qt::Key_Right), KS (Qt::SHIFT | Qt::Key_L) }, [this] { extendSelectionBy (largeStepSeconds); });
    add ("select.toStart", Category::select, tr ("Extend Selection to S&tart"), { KS (Qt::SHIFT | Qt::Key_Home) },
         [this] { extendSelectionTo (0); });
    add ("select.toEnd", Category::select, tr ("Extend Selection to &End"), { KS (Qt::SHIFT | Qt::Key_End) },
         [this] { if (auto d = data()) extendSelectionTo (d->length()); });

    add ("process.gain", Category::process, tr ("&Gain..."), { KS (Qt::CTRL | Qt::Key_G) }, [this] { gain(); });
    add ("process.normalize", Category::process, tr ("&Normalize..."), { KS (Qt::CTRL | Qt::SHIFT | Qt::Key_N) },
         [this] { normalize(); });
    add ("process.fadeIn", Category::process, tr ("Fade &In"), { KS (Qt::CTRL | Qt::SHIFT | Qt::Key_I) }, [this] { fade (true); });
    add ("process.fadeOut", Category::process, tr ("Fade &Out"), { KS (Qt::CTRL | Qt::SHIFT | Qt::Key_O) }, [this] { fade (false); });
    add ("process.reverse", Category::process, tr ("&Reverse"), { KS (Qt::CTRL | Qt::SHIFT | Qt::Key_R) },
         [this] { simpleProcess (tr ("Reverse"), &dsp::reverse); });
    add ("process.invert", Category::process, tr ("In&vert Polarity"), { KS (Qt::CTRL | Qt::ALT | Qt::Key_I) },
         [this] { simpleProcess (tr ("Invert"), &dsp::invert); });
    add ("process.stereoize", Category::process, tr ("&Stereoize..."), { KS (Qt::CTRL | Qt::ALT | Qt::Key_W) }, [this] { stereoize(); });
    add ("repair.autoHeal", Category::repair, tr ("&Auto Heal"), { KS (Qt::CTRL | Qt::Key_U) }, [this] { autoHeal(); });
    add ("repair.spectralGain", Category::repair, tr ("Spectral &Gain..."), { KS (Qt::CTRL | Qt::ALT | Qt::Key_G) },
         [this] { spectralGain(); });
    add ("repair.learnNoise", Category::repair, tr ("&Learn Noise Profile"), { KS (Qt::CTRL | Qt::ALT | Qt::Key_N) },
         [this] { learnNoise(); });
    add ("repair.noiseReduction", Category::repair, tr ("&Noise Reduction..."), { KS (Qt::CTRL | Qt::ALT | Qt::SHIFT | Qt::Key_N) },
         [this] { noiseReduction(); });
    add ("repair.declick", Category::repair, tr ("De&Click..."), {}, [this] { declick(); });
    add ("repair.declip", Category::repair, tr ("DeCli&p..."), {}, [this] { declip(); });
    add ("repair.dehum", Category::repair, tr ("De&Hum..."), {}, [this] { dehum(); });
    add ("repair.deess", Category::repair, tr ("De&Ess..."), {}, [this] { deess(); });
    add ("repair.frequencyRange", Category::repair, tr ("&Frequency Range..."), { KS (Qt::CTRL | Qt::ALT | Qt::Key_F) },
         [this] { chooseFrequencyRange(); });
    add ("repair.allFrequencies", Category::repair, tr ("A&ll Frequencies"), { KS (Qt::CTRL | Qt::ALT | Qt::SHIFT | Qt::Key_F) },
         [this] { setBand (0.0, 0.0); });
    add ("process.dcOffset", Category::process, tr ("Remove &DC Offset"), { KS (Qt::CTRL | Qt::ALT | Qt::Key_D) },
         [this] { simpleProcess (tr ("Remove DC Offset"), &dsp::removeDcOffset); });

    auto* shapes = new QActionGroup (this);
    const std::pair<const char*, std::pair<QString, dsp::FadeShape>> shapeList[] {
        { "process.fadeShape.linear",      { tr ("Fade Shape: &Linear"), dsp::FadeShape::linear } },
        { "process.fadeShape.equalPower",  { tr ("Fade Shape: &Equal Power"), dsp::FadeShape::equalPower } },
        { "process.fadeShape.exponential", { tr ("Fade Shape: E&xponential"), dsp::FadeShape::exponential } },
        { "process.fadeShape.sCurve",      { tr ("Fade Shape: &S-Curve"), dsp::FadeShape::sCurve } },
    };

    for (const auto& [id, info] : shapeList)
    {
        const auto shape = info.second;
        auto* a = add (id, Category::process, info.first, {}, [this, shape] { fadeShape = shape; });
        a->setCheckable (true);
        a->setChecked (shape == fadeShape);
        shapes->addAction (a);
    }

    add ("marker.add", Category::markers, tr ("&Add Marker"), { KS (Qt::Key_M) }, [this] { addMarker(); });
    add ("marker.addRegion", Category::markers, tr ("Add &Region from Selection"), { KS (Qt::SHIFT | Qt::Key_M) },
         [this] { addRegion(); });
    add ("marker.next", Category::markers, tr ("Go to &Next Marker"), { KS (Qt::Key_Apostrophe) }, [this] { jumpToAdjacentMarker (true); });
    add ("marker.previous", Category::markers, tr ("Go to &Previous Marker"), { KS (Qt::Key_Semicolon) },
         [this] { jumpToAdjacentMarker (false); });

    add ("analyze.loudness", Category::analyze, tr ("&Loudness Statistics..."), { KS (Qt::CTRL | Qt::SHIFT | Qt::Key_A) },
         [this] { showLoudnessStatistics(); });
    add ("analyze.resetMeters", Category::analyze, tr ("&Reset Meters"), { KS (Qt::CTRL | Qt::ALT | Qt::Key_R) },
         [this] { meterPanel->reset(); });

    add ("view.master", Category::master, tr ("&Master Section"), { KS (Qt::CTRL | Qt::SHIFT | Qt::Key_E) },
         [this] { masterPanel->toggle(); lastPanel = masterPanel; });
    add ("master.addPlugin", Category::master, tr ("&Add Plugin..."), { KS (Qt::CTRL | Qt::ALT | Qt::Key_E) }, [this]
    {
        masterPanel->show (true);
        lastPanel = masterPanel;
        masterWidget->choosePlugin (-1);
    });
    add ("master.bypass", Category::master, tr ("&Bypass Master Section"), { KS (Qt::CTRL | Qt::SHIFT | Qt::Key_B) }, [this]
    {
        engine.setMasterBypassed (! engine.isMasterBypassed());
        masterWidget->refresh();
        commands.action (QStringLiteral ("master.bypass"))->setChecked (engine.isMasterBypassed());
        statusBar()->showMessage (engine.isMasterBypassed() ? tr ("Master Section bypassed") : tr ("Master Section on"), 3000);
    })->setCheckable (true);
    add ("master.render", Category::master, tr ("&Render..."), { KS (Qt::CTRL | Qt::SHIFT | Qt::Key_Return) }, [this] { renderMaster(); });
    add ("master.scan", Category::master, tr ("&Scan Plugins"), { KS (Qt::CTRL | Qt::ALT | Qt::SHIFT | Qt::Key_S) },
         [this] { masterWidget->scanPlugins(); });

    add ("transport.playStop", Category::transport, tr ("&Play / Stop"), { KS (Qt::Key_Space) }, [this] { togglePlayback(); });
    add ("transport.stop", Category::transport, tr ("&Stop"), { KS (Qt::Key_K) }, [this] { engine.stop(); });
    add ("transport.start", Category::transport, tr ("Go to S&tart"), { KS (Qt::Key_Home) }, [this] { setCursor (0); });
    add ("transport.end", Category::transport, tr ("Go to &End"), { KS (Qt::Key_End) },
         [this] { if (auto d = data()) setCursor (d->length()); });
    add ("cursor.back", Category::transport, tr ("Cursor &Back"), { KS (Qt::Key_Left) }, [this] { moveCursorBy (-smallStepSeconds); });
    add ("cursor.forward", Category::transport, tr ("Cursor &Forward"), { KS (Qt::Key_Right) }, [this] { moveCursorBy (smallStepSeconds); });
    add ("cursor.backLarge", Category::transport, tr ("Cursor Back 1 s"),
         { KS (Qt::CTRL | Qt::Key_Left), KS (Qt::Key_J) }, [this] { moveCursorBy (-largeStepSeconds); });
    add ("cursor.forwardLarge", Category::transport, tr ("Cursor Forward 1 s"),
         { KS (Qt::CTRL | Qt::Key_Right), KS (Qt::Key_L) }, [this] { moveCursorBy (largeStepSeconds); });

    add ("view.zoomIn", Category::view, tr ("Zoom &In"), { KS (Qt::CTRL | Qt::Key_Equal), KS (Qt::Key_Plus), KS (Qt::Key_Equal) },
         [this] { waveform->zoomBy (0.5); });
    add ("view.zoomOut", Category::view, tr ("Zoom &Out"), { KS (Qt::CTRL | Qt::Key_Minus), KS (Qt::Key_Minus) },
         [this] { waveform->zoomBy (2.0); });
    add ("view.zoomFit", Category::view, tr ("Zoom to &Fit"), { KS (Qt::CTRL | Qt::Key_0) }, [this] { waveform->zoomToFit(); });
    add ("view.zoomSelection", Category::view, tr ("Zoom to &Selection"), { KS (Qt::CTRL | Qt::Key_E) },
         [this] { if (requireSelection()) waveform->zoomToSelection(); });
    add ("view.chain", Category::master, tr ("Device &Chain"), { KS (Qt::CTRL | Qt::SHIFT | Qt::Key_C) },
         [this] { chainPanel->toggle(); });
    add ("view.history", Category::view, tr ("&History Panel"), { KS (Qt::CTRL | Qt::Key_H) },
         [this] { historyPanel->toggle(); lastPanel = historyPanel; });
    add ("view.markers", Category::view, tr ("&Markers Panel"), { KS (Qt::CTRL | Qt::SHIFT | Qt::Key_M) },
         [this] { markerPanel->toggle(); lastPanel = markerPanel; });
    add ("view.meters", Category::view, tr ("&Level Meters"), { KS (Qt::CTRL | Qt::SHIFT | Qt::Key_L) },
         [this] { metersPanel->toggle(); lastPanel = metersPanel; });
    add ("view.popOut", Category::view, tr ("&Pop Out / Dock Panel"), { KS (Qt::CTRL | Qt::ALT | Qt::Key_P) }, [this]
    {
        if (auto* p = focusedOrLastPanel())
            p->setPoppedOut (! p->isPoppedOut());
        else
            statusBar()->showMessage (tr ("Open a panel first, for example the meters with Ctrl+Shift+L"), 5000);
    });
    add ("view.resetLayout", Category::view, tr ("&Reset Panel Layout"), { KS (Qt::CTRL | Qt::ALT | Qt::Key_0) }, [this]
    {
        for (auto* p : panels)
            p->hide();
        restoreState (defaultLayout, layoutVersion);
        for (auto* p : panels)
            p->hide();
        activateWindow();
        waveform->setFocus();
    });
    add ("view.spectral", Category::view, tr ("&Spectral Display"), { KS (Qt::SHIFT | Qt::Key_D) }, [this]
    {
        waveform->setSpectral (! waveform->isSpectral());
        if (! waveform->isSpectral())
            setBand (0.0, 0.0);   // a frequency box only makes sense where it can be seen
        commands.action (QStringLiteral ("view.spectral"))->setChecked (waveform->isSpectral());
    })->setCheckable (true);
    add ("view.opacityUp", Category::view, tr ("More &Opaque"),
         { KS (Qt::CTRL | Qt::ALT | Qt::Key_Equal), KS (Qt::CTRL | Qt::ALT | Qt::Key_Plus) },
         [this] { changeOpacity (Theme::opacity() + 0.05, true); });
    add ("view.opacityDown", Category::view, tr ("More &Transparent"), { KS (Qt::CTRL | Qt::ALT | Qt::Key_Minus) },
         [this] { changeOpacity (Theme::opacity() - 0.05, true); });
    add ("view.opacityFull", Category::view, tr ("&Fully Opaque"), {}, [this] { changeOpacity (1.0, true); });

    add ("view.focusFollowsMouse", Category::view, tr ("Focus Follows &Mouse"), {}, [this]
    {
        const bool on = commands.action (QStringLiteral ("view.focusFollowsMouse"))->isChecked();
        focusFollowsMouse->setEnabled (on);
        statusBar()->showMessage (on ? tr ("Focus follows the mouse: point at the waveform, device chain or a panel to type there")
                                     : tr ("Focus follows clicks"), 5000);
    });
    commands.action (QStringLiteral ("view.focusFollowsMouse"))->setCheckable (true);

    add ("view.focusWaveform", Category::view, tr ("Focus &Waveform"), { KS (Qt::Key_F6) }, [this]
    {
        activateWindow();
        waveform->setFocus();
    });

    add ("options.separateOutputs", Category::options, tr ("Play Surround on &Separate Outputs"), {}, [this]
    {
        const bool on = commands.action (QStringLiteral ("options.separateOutputs"))->isChecked();
        engine.setMonitorMode (on ? AudioEngine::MonitorMode::separateOutputs : AudioEngine::MonitorMode::stereoDownmix);
        statusBar()->showMessage (on ? tr ("Files with more than two channels play each channel on its own output when the device has enough")
                                     : tr ("Files with more than two channels are heard folded down to stereo (the file is not changed)"), 6000);
    });
    commands.action (QStringLiteral ("options.separateOutputs"))->setCheckable (true);

    add ("options.audio", Category::options, tr ("&Audio Settings..."), { KS (Qt::CTRL | Qt::Key_Comma) },
         [this] { AudioSettingsDialog (engine, this).exec(); });
    add ("options.keymap", Category::options, tr ("&Keyboard Shortcuts..."), { KS (Qt::CTRL | Qt::SHIFT | Qt::Key_K) },
         [this] { KeymapDialog (commands, this).exec(); });
    add ("options.waveformColors", Category::options, tr ("&Waveform Colors..."), { KS (Qt::CTRL | Qt::SHIFT | Qt::Key_W) },
         [this] { WaveformColorsDialog (this).exec(); });
    add ("options.theme", Category::options, tr ("&Theme..."), { KS (Qt::CTRL | Qt::SHIFT | Qt::Key_T) },
         [this] { ThemeDialog (this).exec(); });

    add ("help.palette", Category::help, tr ("&Command Palette..."), { KS (Qt::CTRL | Qt::SHIFT | Qt::Key_P), KS (Qt::Key_F1) },
         [this] { CommandPalette (commands, this).exec(); });
    add ("help.menuBar", Category::help, tr ("Open &Menu Bar"), { KS (Qt::Key_F10) }, [this] { openMenuBar(); });
    add ("help.about", Category::help, tr ("&About " APP_DISPLAY_NAME), {}, [this]
    {
        QMessageBox::about (this, tr ("About " APP_DISPLAY_NAME),
            tr ("<b>" APP_DISPLAY_NAME " " APP_VERSION "</b><p>Audio editor and mastering suite for Linux.</p>"
                "<p>Free software under the GNU Affero General Public License v3.</p>"
                "<p>Press Ctrl+Shift+P or F1 to search every command and shortcut.</p>"));
    });
}

void MainWindow::buildMenus()
{
    // Menu titles carry & mnemonics, so Alt+F, Alt+E, Alt+P, Alt+M, Alt+A... open them.
    const QList<QPair<QString, QStringList>> layout {
        { tr ("&File"),      { "file.open", "file.record", "-", "file.save", "file.saveAs", "-", "file.close", "file.nextTab", "file.previousTab", "-", "file.quit" } },
        { tr ("&Edit"),      { "edit.undo", "edit.redo", "-", "edit.cut", "edit.copy", "edit.copyToNew", "edit.paste", "edit.delete", "-",
                               "edit.trim", "edit.insertSilence", "-", "select.all", "select.none", "-",
                               "select.back", "select.forward", "select.backLarge", "select.forwardLarge",
                               "select.toStart", "select.toEnd" } },
        { tr ("&Process"),   { "process.gain", "process.normalize", "-", "process.fadeIn", "process.fadeOut", "-",
                               "process.fadeShape.linear", "process.fadeShape.equalPower", "process.fadeShape.exponential",
                               "process.fadeShape.sCurve", "-", "process.reverse", "process.invert", "process.dcOffset", "-", "process.stereoize" } },
        { tr ("&Repair"),    { "repair.autoHeal", "repair.spectralGain", "-", "repair.learnNoise", "repair.noiseReduction", "-",
                               "repair.declick", "repair.declip", "repair.dehum", "repair.deess", "-",
                               "repair.frequencyRange", "repair.allFrequencies", "-", "view.spectral" } },
        { tr ("&Markers"),   { "marker.add", "marker.addRegion", "-", "marker.next", "marker.previous", "-", "view.markers" } },
        { tr ("&Analyze"),   { "analyze.loudness", "-", "view.meters", "analyze.resetMeters", "-", "view.spectral" } },
        { tr ("Ma&ster"),    { "view.master", "view.chain", "master.addPlugin", "master.bypass", "-", "master.render", "-", "master.scan" } },
        { tr ("&Transport"), { "transport.playStop", "transport.stop", "-", "transport.start", "transport.end",
                               "cursor.back", "cursor.forward", "cursor.backLarge", "cursor.forwardLarge" } },
        { tr ("&View"),      { "view.zoomIn", "view.zoomOut", "view.zoomFit", "view.zoomSelection", "-",
                               "view.spectral", "-", "view.master", "view.chain", "view.history", "view.markers", "view.meters", "view.popOut", "view.resetLayout", "-",
                               "opacity", "-", "view.focusWaveform", "view.focusFollowsMouse" } },
        { tr ("&Options"),   { "options.audio", "options.separateOutputs", "-", "options.keymap", "options.theme", "options.waveformColors" } },
        { tr ("&Help"),      { "help.palette", "help.menuBar", "-", "help.about" } },
    };

    for (const auto& [title, ids] : layout)
    {
        auto* menu = menuBar()->addMenu (title);
        for (const auto& id : ids)
        {
            if (id == QLatin1String ("-"))
                menu->addSeparator();
            else if (id == QLatin1String ("opacity"))
            {
                auto* sub = menu->addMenu (tr ("Window Opaci&ty"));
                auto* box = new QWidget (sub);
                auto* row = new QHBoxLayout (box);
                row->setContentsMargins (24, 6, 12, 6);
                row->addWidget (new QLabel (tr ("Opacity"), box));
                row->addWidget (makeOpacitySlider (box));
                auto* sliderAction = new QWidgetAction (sub);
                sliderAction->setDefaultWidget (box);
                sub->addAction (sliderAction);
                sub->addSeparator();
                for (const auto* a : { "view.opacityUp", "view.opacityDown", "view.opacityFull" })
                    sub->addAction (commands.action (QString::fromLatin1 (a)));
            }
            else
                menu->addAction (commands.action (id));
        }
    }
}

QSlider* MainWindow::makeOpacitySlider (QWidget* parent)
{
    auto* slider = new QSlider (Qt::Horizontal, parent);
    slider->setRange ((int) std::lround (Theme::minOpacity * 100.0), 100);
    slider->setValue ((int) std::lround (Theme::opacity() * 100.0));
    slider->setFixedWidth (120);
    slider->setFocusPolicy (Qt::NoFocus);  // keys stay with the editor; Ctrl+Alt+- and Ctrl+Alt+= change it
    slider->setAccessibleName (tr ("Window opacity"));
    slider->setToolTip (tr ("Window opacity (Ctrl+Alt+- and Ctrl+Alt+=)"));
    connect (slider, &QSlider::valueChanged, this, [this] (int v) { changeOpacity (v / 100.0, false); });
    opacitySliders.push_back (slider);
    return slider;
}

void MainWindow::changeOpacity (double value, bool announce)
{
    Theme::setOpacity (value);
    const auto percent = (int) std::lround (Theme::opacity() * 100.0);

    for (auto* s : opacitySliders)
    {
        const QSignalBlocker block (s);
        s->setValue (percent);
    }

    if (announce)
        statusBar()->showMessage (tr ("Window opacity %1%").arg (percent), 2000);
}

void MainWindow::buildPanels()
{
    setDockOptions (QMainWindow::AnimatedDocks | QMainWindow::AllowTabbedDocks);

    historyList = new QListWidget (this);
    historyList->setAccessibleName (tr ("Undo history"));
    historyList->installEventFilter (new NavigationKeyGuard (waveform, {}, historyList));
    connect (historyList, &QListWidget::itemActivated, this, [this] (QListWidgetItem* item)
    {
        jumpToHistory (historyList->row (item));
    });

    markerList = new MarkerList (this);
    markerList->installEventFilter (new NavigationKeyGuard (waveform, { Qt::Key_Delete, Qt::Key_Backspace, Qt::Key_F2 }, markerList));
    connect (markerList, &MarkerList::jumpRequested, this, &MainWindow::jumpToMarker);
    connect (markerList, &MarkerList::renameRequested, this, &MainWindow::renameMarker);
    connect (markerList, &MarkerList::deleteRequested, this, &MainWindow::deleteMarker);

    meterPanel = new MeterPanel (this);

    masterWidget = new MasterPanel (engine, waveform, this);
    connect (masterWidget, &MasterPanel::renderRequested, this, &MainWindow::renderMaster);
    connect (masterWidget, &MasterPanel::message, this, [this] (const QString& text)
    {
        if (! text.isEmpty())
            statusBar()->showMessage (text, 6000);
    });

    metersPanel = new DockablePanel (*this, QStringLiteral ("metersPanel"), tr ("Meters"), meterPanel, Qt::RightDockWidgetArea);
    markerPanel = new DockablePanel (*this, QStringLiteral ("markersPanel"), tr ("Markers"), markerList, Qt::BottomDockWidgetArea);
    historyPanel = new DockablePanel (*this, QStringLiteral ("historyPanel"), tr ("History"), historyList, Qt::RightDockWidgetArea);
    masterPanel = new DockablePanel (*this, QStringLiteral ("masterPanel"), tr ("Master Section"), masterWidget, Qt::RightDockWidgetArea);
    chainRow = new ChainRow (engine, waveform, this);
    chainRow->choosePlugin = [this] (int slot)
    {
        masterWidget->choosePlugin (slot);
        return ! engine.masterSlot (slot).empty;
    };
    connect (chainRow, &ChainRow::chainChanged, this, [this]
    {
        masterWidget->refresh();
        commands.action (QStringLiteral ("master.bypass"))->setChecked (engine.isMasterBypassed());
    });
    connect (chainRow, &ChainRow::message, this, [this] (const QString& text) { statusBar()->showMessage (text, 6000); });

    // The chain row spans the whole width along the bottom, under the side panels.
    setCorner (Qt::BottomLeftCorner, Qt::BottomDockWidgetArea);
    setCorner (Qt::BottomRightCorner, Qt::BottomDockWidgetArea);
    chainPanel = new DockablePanel (*this, QStringLiteral ("chainPanel"), tr ("Device Chain"), chainRow, Qt::BottomDockWidgetArea);
    chainPanel->dock()->show();

    panels = { masterPanel, chainPanel, metersPanel, markerPanel, historyPanel };

    focusFollowsMouse = new FocusFollowsMouse (this);
    focusFollowsMouse->setAreas ({ waveform, chainRow, masterWidget, markerList, historyList });
    qApp->installEventFilter (focusFollowsMouse);
}

DockablePanel* MainWindow::focusedOrLastPanel() const
{
    for (auto* p : panels)
        if (p->containsFocus())
            return p;

    return lastPanel != nullptr && lastPanel->isShown() ? lastPanel : nullptr;
}

namespace
{
    QSettings appSettings()
    {
        return QSettings (QStandardPaths::writableLocation (QStandardPaths::GenericConfigLocation)
                              + QStringLiteral ("/" APP_ID "/settings.ini"), QSettings::IniFormat);
    }
}

void MainWindow::restoreLayout()
{
    auto settings = appSettings();
    restoreGeometry (settings.value (QStringLiteral ("window/geometry")).toByteArray());

    const bool followMouse = settings.value (QStringLiteral ("view/focusFollowsMouse"), true).toBool();
    focusFollowsMouse->setEnabled (followMouse);
    commands.action (QStringLiteral ("view.focusFollowsMouse"))->setChecked (followMouse);

    const bool separate = settings.value (QStringLiteral ("audio/separateOutputs"), false).toBool();
    engine.setMonitorMode (separate ? AudioEngine::MonitorMode::separateOutputs : AudioEngine::MonitorMode::stereoDownmix);
    commands.action (QStringLiteral ("options.separateOutputs"))->setChecked (separate);

    const bool spectral = settings.value (QStringLiteral ("view/spectral"), false).toBool();
    waveform->setSpectral (spectral);
    commands.action (QStringLiteral ("view.spectral"))->setChecked (spectral);

    // Load plugins and pop panels out again once the main window is on screen, so it opens first.
    QTimer::singleShot (0, this, [this]
    {
        // Dock layouts restore reliably only once the window exists on screen.
        defaultLayout = saveState (layoutVersion);
        restoreState (appSettings().value (QStringLiteral ("window/state")).toByteArray(), layoutVersion);

        const auto problems = QString::fromStdString (engine.restoreMasterSection());
        masterWidget->refresh();
        commands.action (QStringLiteral ("master.bypass"))->setChecked (engine.isMasterBypassed());
        if (! problems.isEmpty())
            QMessageBox::warning (this, tr ("Master Section"), tr ("Some plugins could not be loaded:\n%1").arg (problems));

        auto s = appSettings();
        for (auto* p : panels)
            if (s.value (QStringLiteral ("panels/%1/poppedOut").arg (p->id()), false).toBool())
                p->setPoppedOut (true);

        activateWindow();
        waveform->setFocus();
        QTimer::singleShot (1500, this, &MainWindow::warnAboutDesktopShortcuts);
        QTimer::singleShot (0, this, &MainWindow::recoverRecordings);
    });
}

void MainWindow::saveLayout()
{
    auto settings = appSettings();

    for (auto* p : panels)
    {
        const bool popped = p->isPoppedOut() && p->isShown();
        settings.setValue (QStringLiteral ("panels/%1/poppedOut").arg (p->id()), popped);
        if (p->isPoppedOut())
            p->hide();  // docks it back so the app can exit
    }

    settings.setValue (QStringLiteral ("window/geometry"), saveGeometry());
    settings.setValue (QStringLiteral ("window/state"), saveState (layoutVersion));
    settings.setValue (QStringLiteral ("view/spectral"), waveform->isSpectral());
    settings.setValue (QStringLiteral ("view/focusFollowsMouse"), focusFollowsMouse->isEnabled());
    settings.setValue (QStringLiteral ("audio/separateOutputs"), engine.monitorMode() == AudioEngine::MonitorMode::separateOutputs);
    engine.saveMasterSection();
}

void MainWindow::openMenuBar()
{
    if (auto first = menuBar()->actions(); ! first.isEmpty())
    {
        menuBar()->setFocus (Qt::ShortcutFocusReason);
        menuBar()->setActiveAction (first.first());
    }
}

//==============================================================================
void MainWindow::updateCommandStates()
{
    const bool open = doc.isOpen() && ! loading;
    const bool sel = open && hasSelection();

    for (const auto* id : { "file.save", "file.saveAs", "file.close", "edit.insertSilence", "edit.copyToNew", "select.all", "select.none",
                            "select.back", "select.forward", "select.backLarge", "select.forwardLarge",
                            "select.toStart", "select.toEnd", "process.gain", "process.normalize",
                            "process.reverse", "process.invert", "process.dcOffset", "process.stereoize",
                            "repair.spectralGain", "repair.frequencyRange", "repair.allFrequencies", "repair.learnNoise",
                            "repair.noiseReduction", "repair.declick", "repair.declip", "repair.dehum", "repair.deess",
                            "transport.playStop", "transport.stop", "transport.start", "transport.end",
                            "cursor.back", "cursor.forward", "cursor.backLarge", "cursor.forwardLarge",
                            "view.zoomIn", "view.zoomOut", "view.zoomFit" })
        commands.action (QString::fromLatin1 (id))->setEnabled (open);

    // Selection commands stay enabled so pressing their key without a selection
    // explains what is missing instead of silently doing nothing.
    for (const auto* id : { "edit.cut", "edit.copy", "edit.delete", "edit.trim", "process.fadeIn", "process.fadeOut", "repair.autoHeal",
                            "view.zoomSelection" })
        commands.action (QString::fromLatin1 (id))->setEnabled (open);
    Q_UNUSED (sel);

    commands.action (QStringLiteral ("edit.paste"))->setEnabled (open && clipboard != nullptr);
    commands.action (QStringLiteral ("marker.addRegion"))->setEnabled (open);
    commands.action (QStringLiteral ("master.render"))->setEnabled (open);

    for (const auto* id : { "marker.add", "marker.next", "marker.previous", "analyze.loudness" })
        commands.action (QString::fromLatin1 (id))->setEnabled (open);

    auto* undoAction = commands.action (QStringLiteral ("edit.undo"));
    auto* redoAction = commands.action (QStringLiteral ("edit.redo"));
    undoAction->setEnabled (open && doc.canUndo());
    redoAction->setEnabled (open && doc.canRedo());
    undoAction->setText (open && doc.canUndo() ? tr ("&Undo %1").arg (QString::fromStdString (doc.undoDescription())) : tr ("&Undo"));
    redoAction->setText (open && doc.canRedo() ? tr ("&Redo %1").arg (QString::fromStdString (doc.redoDescription())) : tr ("&Redo"));
}

void MainWindow::updateTitle()
{
    updateTabBar();

    if (! doc.isOpen())
    {
        setWindowTitle (QStringLiteral (APP_DISPLAY_NAME));
        return;
    }

    const auto name = doc.path().empty() ? tr ("Untitled") : QFileInfo (QString::fromStdString (doc.path())).fileName();
    setWindowTitle ((doc.isModified() ? QStringLiteral ("*") : QString()) + name + QStringLiteral (" - " APP_DISPLAY_NAME));
}

//==============================================================================
QString MainWindow::tabName (const AudioDocument& d) const
{
    if (! d.isOpen())
        return tr ("(empty)");

    const auto name = d.path().empty() ? tr ("Untitled") : QFileInfo (QString::fromStdString (d.path())).fileName();
    return d.isModified() ? name + QStringLiteral (" *") : name;
}

void MainWindow::updateTabBar()
{
    if (tabBar == nullptr)
        return;

    for (int i = 0; i < (int) tabs.size(); ++i)
    {
        const auto& d = i == activeTab ? doc : tabs[(size_t) i].doc;
        tabBar->setTabText (i, tabName (d));
        tabBar->setTabToolTip (i, QString::fromStdString (d.path()));
    }

    tabBar->setVisible (tabs.size() > 1 || doc.isOpen());
}

void MainWindow::parkActiveTab()
{
    auto& t = tabs[(size_t) activeTab];
    t.doc = std::move (doc);
    t.cursor = cursor;
    t.selStart = selStart;
    t.selEnd = selEnd;
    t.selAnchor = selAnchor;
    t.bandLow = bandLow;
    t.bandHigh = bandHigh;
    t.saveOptions = saveOptions;
    t.optionsConfirmedFor = optionsConfirmedFor;
    t.view = waveform->view();
    doc = AudioDocument();
}

void MainWindow::unparkTab (int index)
{
    activeTab = index;
    auto& t = tabs[(size_t) index];
    doc = std::move (t.doc);
    t.doc = AudioDocument();
    cursor = t.cursor;
    selStart = t.selStart;
    selEnd = t.selEnd;
    selAnchor = t.selAnchor;
    bandLow = t.bandLow;
    bandHigh = t.bandHigh;
    waveform->setBand (bandLow, bandHigh);
    saveOptions = t.saveOptions;
    optionsConfirmedFor = t.optionsConfirmedFor;
}

void MainWindow::switchToTab (int index)
{
    if (index == activeTab || index < 0 || index >= (int) tabs.size())
        return;

    if (loading)
    {
        // A file is still being read into the current tab; stay on it.
        const QSignalBlocker block (tabBar);
        tabBar->setCurrentIndex (activeTab);
        return;
    }

    engine.stop();
    parkActiveTab();
    unparkTab (index);

    const auto view = tabs[(size_t) index].view;
    refreshFromDocument (false);
    waveform->setView (view.first, view.second);
    {
        const QSignalBlocker block (tabBar);
        tabBar->setCurrentIndex (index);
    }
    waveform->setFocus();
}

void MainWindow::newTab()
{
    engine.stop();
    parkActiveTab();
    tabs.push_back (TabState());
    activeTab = (int) tabs.size() - 1;
    cursor = selStart = selEnd = selAnchor = 0;
    optionsConfirmedFor.clear();
    {
        const QSignalBlocker block (tabBar);
        tabBar->addTab (QString());
        tabBar->setCurrentIndex (activeTab);
    }
    waveform->clear();
    refreshFromDocument (false);
}

bool MainWindow::closeTab()
{
    if (loading || ! confirmDiscard())
        return false;

    engine.stop();

    if (tabs.size() == 1)
    {
        doc = AudioDocument();
        cursor = selStart = selEnd = selAnchor = 0;
        waveform->clear();
        refreshFromDocument (false);
        return true;
    }

    // Drop this tab and show its neighbour.
    const auto closing = activeTab;
    tabs.erase (tabs.begin() + closing);
    {
        const QSignalBlocker block (tabBar);
        tabBar->removeTab (closing);
    }
    const auto next = std::min (closing, (int) tabs.size() - 1);
    const auto view = tabs[(size_t) next].view;
    unparkTab (next);
    {
        const QSignalBlocker block (tabBar);
        tabBar->setCurrentIndex (next);
    }
    refreshFromDocument (false);
    waveform->setView (view.first, view.second);
    waveform->setFocus();
    return true;
}

void MainWindow::updateStatus()
{
    QString text;

    if (loading)
        text = tr ("Loading...");
    else if (auto d = data())
    {
        const auto rate = d->sampleRate();
        text = QStringLiteral ("%1 / %2").arg (formatTime ((double) cursor / rate), formatTime (d->lengthSeconds()));

        if (hasSelection())
            text += tr ("   Selection %1 to %2 (%3)")
                        .arg (formatTime ((double) selStart / rate), formatTime ((double) selEnd / rate),
                              formatTime ((double) (selEnd - selStart) / rate));
        if (hasSelection() && hasBand())
            text += tr (", %1 to %2").arg (formatHz (bandLow), formatHz (bandHigh));

        const auto layout = QString::fromStdString (dsp::speakerLayout (d->numChannels()).name);
        text += QStringLiteral ("   %1 Hz, %2").arg ((int) rate).arg (layout);
        if (d->numChannels() > 2 && engine.isPlaying() && engine.isDownmixing())
            text += tr (" (heard folded to stereo)");
        text += QStringLiteral ("   ") + (engine.isPlaying() ? tr ("Playing") : tr ("Stopped"));
    }
    else
        text = tr ("No file");

    text += QStringLiteral ("   |   ") + QString::fromStdString (engine.deviceDescription());

    if (statusLabel->text() != text)
        statusLabel->setText (text);
}

void MainWindow::refreshFromDocument (bool keepView)
{
    const auto d = data();
    engine.setData (d);
    waveform->setData (d, keepView);
    waveform->setMarkers (d ? doc.current().markers : std::vector<Marker>());

    const auto len = d ? d->length() : 0;
    cursor = std::clamp<Frame> (cursor, 0, len);
    selStart = std::clamp<Frame> (selStart, 0, len);
    selEnd = std::clamp<Frame> (selEnd, 0, len);
    waveform->setCursorFrame (cursor);
    waveform->setSelection (selStart, selEnd);
    if (! engine.isPlaying())
        engine.setPositionFrames (cursor);

    markerList->setMarkers (d ? doc.current().markers : std::vector<Marker>(), d ? d->sampleRate() : 48000.0);

    historyList->clear();
    if (doc.isOpen())
    {
        for (const auto& desc : doc.historyDescriptions())
            historyList->addItem (QString::fromStdString (desc));

        historyList->setCurrentRow (doc.historyPosition());
    }

    updateTitle();
    updateCommandStates();
}

//==============================================================================
void MainWindow::showOpenDialog()
{
    QStringList patterns;
    for (const auto& ext : AudioEngine::readableExtensions())
        patterns << QStringLiteral ("*.") + QString::fromStdString (ext);

    const auto start = doc.path().empty() ? QString() : QFileInfo (QString::fromStdString (doc.path())).absolutePath();
    const auto path = QFileDialog::getOpenFileName (this, tr ("Open Audio File"), start,
                                                    tr ("Audio files (%1);;All files (*)").arg (patterns.join (QLatin1Char (' '))));
    if (! path.isEmpty())
        openFile (path);
}

void MainWindow::openFile (const QString& path)
{
    if (loading)
        return;

    if (doc.isOpen())
        newTab();

    engine.stop();
    loading = true;
    loadProgress = std::make_shared<std::atomic<float>> (0.0f);
    waveform->clear();
    waveform->setLoading (true);
    updateCommandStates();

    loadWatcher.setFuture (QtConcurrent::run ([path, progress = loadProgress]
    {
        LoadResult r;
        r.path = path;
        r.data = AudioEngine::loadFile (path.toStdString(), r.markers, r.error, progress.get());
        return r;
    }));
}

void MainWindow::loadFinished()
{
    loading = false;
    auto r = loadWatcher.result();

    if (r.data == nullptr)
    {
        waveform->setLoading (false);
        if (tabs.size() > 1)
            closeTab();  // the empty tab opened for this file
        else
            refreshFromDocument (false);
        QMessageBox::warning (this, tr ("Open failed"),
                              tr ("Could not open %1:\n%2").arg (r.path, QString::fromStdString (r.error)));
        return;
    }

    doc.reset (r.data, std::move (r.markers), r.path.toStdString());
    cursor = selStart = selEnd = selAnchor = 0;
    optionsConfirmedFor.clear();
    refreshFromDocument (false);
    waveform->setFocus();
}

bool MainWindow::confirmDiscard()
{
    if (! doc.isOpen() || ! doc.isModified())
        return true;

    const auto name = doc.path().empty() ? tr ("Untitled") : QFileInfo (QString::fromStdString (doc.path())).fileName();
    const auto answer = QMessageBox::question (this, tr ("Unsaved changes"),
                                               tr ("Save changes to %1?").arg (name),
                                               QMessageBox::Save | QMessageBox::Discard | QMessageBox::Cancel,
                                               QMessageBox::Save);
    if (answer == QMessageBox::Save)
        return save();

    return answer == QMessageBox::Discard;
}

void MainWindow::closeFile()
{
    closeTab();
}

bool MainWindow::save()
{
    if (! doc.isOpen())
        return false;

    const auto path = QString::fromStdString (doc.path());
    const auto ext = QFileInfo (path).suffix().toLower().toStdString();
    const auto writable = AudioEngine::writableExtensions();

    if (path.isEmpty() || std::find (writable.begin(), writable.end(), ext) == writable.end())
        return saveAs();

    return saveTo (path, optionsConfirmedFor != path);
}

bool MainWindow::saveAs()
{
    if (! doc.isOpen())
        return false;

    QStringList filters;
    for (const auto& ext : AudioEngine::writableExtensions())
    {
        const auto e = QString::fromStdString (ext);
        filters << tr ("%1 (*.%2)").arg (e.toUpper(), e);
    }

    QFileInfo current (QString::fromStdString (doc.path()));
    auto suggested = current.absolutePath() + QLatin1Char ('/') + current.completeBaseName() + QStringLiteral (".wav");
    QString selectedFilter = filters.first();

    auto path = QFileDialog::getSaveFileName (this, tr ("Save Audio File As"), suggested, filters.join (QStringLiteral (";;")),
                                              &selectedFilter);
    if (path.isEmpty())
        return false;

    // Add the extension from the chosen filter when the name has none we can write.
    const auto writable = AudioEngine::writableExtensions();
    const auto ext = QFileInfo (path).suffix().toLower().toStdString();
    if (std::find (writable.begin(), writable.end(), ext) == writable.end())
    {
        const auto start = selectedFilter.indexOf (QStringLiteral ("*.")) + 2;
        path += QLatin1Char ('.') + selectedFilter.mid (start, selectedFilter.indexOf (QLatin1Char (')')) - start);
    }

    return saveTo (path, true);
}

bool MainWindow::saveTo (const QString& path, bool askOptions)
{
    if (askOptions)
    {
        SaveOptionsDialog dialog (QFileInfo (path).suffix(), saveOptions, this);
        if (dialog.exec() != QDialog::Accepted)
            return false;
    }

    QApplication::setOverrideCursor (Qt::WaitCursor);
    std::string error;
    const bool ok = AudioEngine::saveFile (path.toStdString(), *data(), doc.current().markers, saveOptions, error);
    QApplication::restoreOverrideCursor();

    if (! ok)
    {
        QMessageBox::warning (this, tr ("Save failed"), tr ("Could not save %1:\n%2").arg (path, QString::fromStdString (error)));
        return false;
    }

    doc.setPath (path.toStdString());
    doc.markSaved();
    optionsConfirmedFor = path;
    updateTitle();
    statusBar()->showMessage (tr ("Saved %1").arg (QFileInfo (path).fileName()), 4000);
    return true;
}

//==============================================================================
void MainWindow::commit (const QString& description, AudioDataPtr newData, Frame start, Frame end, Frame inserted)
{
    if (newData == nullptr)
        return;

    DocumentState state { std::move (newData), doc.current().markers };
    adjustMarkersForEdit (state.markers, start, end, inserted);
    doc.apply (description.toStdString(), std::move (state));
    refreshFromDocument (true);
}

void MainWindow::undo()
{
    if (! doc.canUndo())
        return;

    doc.undo();
    refreshFromDocument (true);
}

void MainWindow::redo()
{
    if (! doc.canRedo())
        return;

    doc.redo();
    refreshFromDocument (true);
}

void MainWindow::jumpToHistory (int index)
{
    if (! doc.isOpen() || index == doc.historyPosition())
        return;

    doc.jumpTo (index);
    refreshFromDocument (true);
    historyList->setFocus();
}

void MainWindow::copy()
{
    if (! requireSelection())
        return;

    clipboard = data()->slice (selStart, selEnd);
    updateCommandStates();
}

void MainWindow::copyToNew()
{
    if (data() == nullptr || loading)
        return;

    const bool whole = ! hasSelection();
    const auto [start, end] = whole ? std::pair (Frame (0), data()->length()) : std::pair (selStart, selEnd);
    auto piece = data()->slice (start, end);

    // Markers inside the copied range come along, moved to the new start.
    std::vector<Marker> markers;
    for (auto mk : doc.current().markers)
    {
        if (mk.start < start || mk.start >= end)
            continue;
        mk.start -= start;
        mk.end = std::min (mk.end, end) - start;
        markers.push_back (mk);
    }

    const auto options = saveOptions;
    newTab();
    doc.reset (std::move (piece), std::move (markers), {});
    doc.markUnsaved();
    saveOptions = options;
    refreshFromDocument (false);
    waveform->setFocus();
    statusBar()->showMessage (whole ? tr ("Copied the whole file to a new tab") : tr ("Copied the selection to a new tab"), 6000);
}

void MainWindow::record()
{
    if (loading)
        return;

    if (! AudioCapture::isAvailable())
    {
        QMessageBox::information (this, tr ("Record"), tr ("This build of %1 was made without PipeWire, so it cannot record.").arg (QStringLiteral (APP_DISPLAY_NAME)));
        return;
    }

    RecordDialog dialog (capture, data(), doc.isOpen() ? tabName (doc).remove (QStringLiteral (" *")) : QString(), this);
    if (dialog.exec() != QDialog::Accepted)
        return;

    auto take = dialog.take();
    if (take == nullptr || take->length() == 0)
        return;

    const auto seconds = (double) take->length() / take->sampleRate();
    if (dialog.insertAtCursor() && data() != nullptr)
    {
        const auto start = cursor;
        const auto len = take->length();
        auto result = data()->replaced (start, start, take.get());
        selStart = selAnchor = start;
        selEnd = cursor = start + len;
        commit (tr ("Record"), std::move (result), start, start, len);
    }
    else
    {
        if (doc.isOpen())
            newTab();
        doc.reset (std::move (take), {}, {});
        doc.markUnsaved();
        cursor = selStart = selEnd = selAnchor = 0;
        refreshFromDocument (false);
    }

    waveform->setFocus();
    statusBar()->showMessage (tr ("Recorded %1 s").arg (seconds, 0, 'f', 1), 6000);
}

void MainWindow::recoverRecordings()
{
    const auto count = RecordingFile::countLeftBehind();
    if (count == 0)
        return;

    const auto app = QStringLiteral (APP_DISPLAY_NAME);
    const auto question = count == 1
        ? tr ("%1 closed while recording. Recover the recording? It will be saved in your Music folder and opened.").arg (app)
        : tr ("%1 closed while recording. Recover the %2 recordings? They will be saved in your Music folder and opened.").arg (app).arg (count);
    const auto answer = QMessageBox::question (this, tr ("Recover Recordings"), question,
        QMessageBox::Yes | QMessageBox::No, QMessageBox::Yes);
    if (answer != QMessageBox::Yes)
        return;

    const auto paths = RecordingFile::recoverAll();
    if (paths.isEmpty())
        return;

    statusBar()->showMessage (tr ("Recovered to %1").arg (QFileInfo (paths.first()).absolutePath()), 10000);
    // Files load one at a time; open the next when the previous has finished.
    auto* opener = new QTimer (this);
    connect (opener, &QTimer::timeout, this, [this, opener, queue = paths] () mutable
    {
        if (loading)
            return;
        if (queue.isEmpty())
            return opener->deleteLater();
        openFile (queue.takeFirst());
    });
    opener->start (100);
}

void MainWindow::warnAboutDesktopShortcuts()
{
    QStringList clashes;
    for (const auto& c : commands.all())
        for (const auto& key : c.action->shortcuts())
            if (! DesktopShortcuts::owner (key).isEmpty())
                clashes << QStringLiteral ("%1 (%2)").arg (key.toString (QKeySequence::NativeText), c.name());

    if (! clashes.isEmpty())
        statusBar()->showMessage (tr ("Your desktop already uses %n PHINYX shortcut(s): %1. Change them in Options > Keyboard Shortcuts.",
                                      nullptr, (int) clashes.size()).arg (clashes.join (QStringLiteral (", "))), 15000);
}

void MainWindow::cut()
{
    copy();
    deleteSelection();
}

void MainWindow::paste()
{
    const auto d = data();
    if (d == nullptr || clipboard == nullptr)
        return;

    const auto start = hasSelection() ? selStart : cursor;
    const auto end = hasSelection() ? selEnd : cursor;
    auto result = d->replaced (start, end, clipboard.get());

    if (std::abs (clipboard->sampleRate() - d->sampleRate()) > 0.5)
        statusBar()->showMessage (tr ("Pasted audio was recorded at %1 Hz and plays at %2 Hz")
                                      .arg ((int) clipboard->sampleRate()).arg ((int) d->sampleRate()), 6000);

    if (clipboard->numChannels() != d->numChannels() && clipboard->numChannels() > 1)
        statusBar()->showMessage (d->numChannels() == 1
                                      ? tr ("Pasted %1 audio into a mono file: left and right were mixed").arg (QString::fromStdString (dsp::speakerLayout (clipboard->numChannels()).name))
                                      : tr ("Pasted %1 audio into a %2 file: channels went to the same positions, the rest are silent")
                                            .arg (QString::fromStdString (dsp::speakerLayout (clipboard->numChannels()).name),
                                                  QString::fromStdString (dsp::speakerLayout (d->numChannels()).name)), 8000);

    // Select what was pasted so it can be processed or undone at a glance.
    selStart = selAnchor = start;
    selEnd = cursor = start + clipboard->length();
    commit (tr ("Paste"), std::move (result), start, end, clipboard->length());
}

void MainWindow::deleteSelection()
{
    if (! requireSelection())
        return;

    if (hasBand())
    {
        // Delete in a frequency box removes those frequencies; the length stays.
        QApplication::setOverrideCursor (Qt::WaitCursor);
        auto result = repair::spectralGain (*data(), { selStart, selEnd, bandLow, bandHigh }, repair::removeDb);
        QApplication::restoreOverrideCursor();
        commit (tr ("Delete %1 to %2").arg (formatHz (bandLow), formatHz (bandHigh)), std::move (result), selStart, selEnd, selEnd - selStart);
        return;
    }

    const auto start = selStart, end = selEnd;
    auto result = data()->replaced (start, end, nullptr);
    cursor = selStart = selEnd = selAnchor = start;
    commit (tr ("Delete"), std::move (result), start, end, 0);
}

void MainWindow::trimToSelection()
{
    if (! requireSelection())
        return;

    const auto d = data();
    const auto start = selStart, end = selEnd;

    DocumentState state { d->slice (start, end), doc.current().markers };
    adjustMarkersForEdit (state.markers, end, d->length(), 0);
    adjustMarkersForEdit (state.markers, 0, start, 0);

    cursor = selStart = selAnchor = 0;
    selEnd = end - start;
    doc.apply (tr ("Trim").toStdString(), std::move (state));
    refreshFromDocument (false);
}

void MainWindow::insertSilence()
{
    const auto d = data();
    if (d == nullptr)
        return;

    bool ok = false;
    const auto seconds = QInputDialog::getDouble (this, tr ("Insert Silence"), tr ("&Duration in seconds:"),
                                                  1.0, 0.001, 3600.0, 3, &ok);
    if (! ok)
        return;

    const auto frames = (Frame) std::llround (seconds * d->sampleRate());
    const auto silence = dsp::silence (d->numChannels(), d->sampleRate(), frames);
    const auto at = cursor;
    auto result = d->replaced (at, at, silence.get());

    selStart = selAnchor = at;
    selEnd = cursor = at + frames;
    commit (tr ("Insert Silence"), std::move (result), at, at, frames);
}

//==============================================================================
std::pair<Frame, Frame> MainWindow::processRange() const
{
    if (hasSelection())
        return { selStart, selEnd };

    return { 0, data() ? data()->length() : 0 };
}

void MainWindow::gain()
{
    if (data() == nullptr)
        return;

    bool ok = false;
    const auto db = QInputDialog::getDouble (this, tr ("Gain"), tr ("&Gain in dB:"), 0.0, -96.0, 48.0, 2, &ok);
    if (! ok || std::abs (db) < 1.0e-9)
        return;

    const auto [s, e] = processRange();
    QApplication::setOverrideCursor (Qt::WaitCursor);
    auto result = dsp::applyGain (*data(), s, e, db);
    QApplication::restoreOverrideCursor();
    commit (tr ("Gain %1 dB").arg (db, 0, 'f', 1), std::move (result), s, e, e - s);
}

void MainWindow::stereoize()
{
    const auto d = data();
    if (d == nullptr)
        return;

    if (d->numChannels() > 2)
    {
        statusBar()->showMessage (tr ("Stereoize works on mono and stereo files"), 6000);
        return;
    }

    auto settings = appSettings();
    dsp::StereoizeSettings initial;
    initial.width = settings.value (QStringLiteral ("stereoize/width"), initial.width).toDouble();
    initial.delayMs = settings.value (QStringLiteral ("stereoize/delayMs"), initial.delayMs).toDouble();
    initial.bassHz = settings.value (QStringLiteral ("stereoize/bassHz"), initial.bassHz).toDouble();

    StereoizeDialog dialog (initial, ! hasSelection(), d->numChannels(), this);
    if (dialog.exec() != QDialog::Accepted)
        return;

    const auto chosen = dialog.settings();
    settings.setValue (QStringLiteral ("stereoize/width"), chosen.width);
    settings.setValue (QStringLiteral ("stereoize/delayMs"), chosen.delayMs);
    settings.setValue (QStringLiteral ("stereoize/bassHz"), chosen.bassHz);

    const auto [s, e] = processRange();
    QApplication::setOverrideCursor (Qt::WaitCursor);
    auto result = dsp::stereoize (*d, s, e, chosen);
    QApplication::restoreOverrideCursor();

    // The whole file is replaced (a mono file gains a channel); its length and markers stay the same.
    doc.apply (tr ("Stereoize").toStdString(), { std::move (result), doc.current().markers });
    refreshFromDocument (true);
    waveform->setFocus();
}

void MainWindow::setBand (double lowHz, double highHz)
{
    if (highHz <= lowHz)
        lowHz = highHz = 0.0;
    if (std::abs (lowHz - bandLow) < 1.0e-9 && std::abs (highHz - bandHigh) < 1.0e-9)
        return;

    bandLow = lowHz;
    bandHigh = highHz;
    waveform->setBand (bandLow, bandHigh);
    updateStatus();
}

void MainWindow::chooseFrequencyRange()
{
    const auto d = data();
    if (d == nullptr)
        return;

    FrequencyRangeDialog dialog (bandLow, bandHigh, d->sampleRate() / 2.0, this);
    if (dialog.exec() != QDialog::Accepted)
        return;

    setBand (dialog.lowHz(), dialog.highHz());
    waveform->setFocus();
    if (hasBand() && ! waveform->isSpectral())
        commands.action (QStringLiteral ("view.spectral"))->trigger();   // show the box
    if (hasBand() && ! hasSelection())
        statusBar()->showMessage (tr ("Now select the time range; edits apply to %1 to %2 inside it.")
                                      .arg (formatHz (bandLow), formatHz (bandHigh)), 6000);
}

void MainWindow::autoHeal()
{
    const auto d = data();
    if (d == nullptr || ! requireSelection())
        return;

    std::string error;
    QApplication::setOverrideCursor (Qt::WaitCursor);
    auto result = repair::autoHeal (*d, { selStart, selEnd, bandLow, bandHigh }, error);
    QApplication::restoreOverrideCursor();
    if (result == nullptr)
    {
        QMessageBox::information (this, tr ("Auto Heal"), QString::fromStdString (error));
        return;
    }

    commit (tr ("Auto Heal"), std::move (result), selStart, selEnd, selEnd - selStart);
    waveform->setFocus();
}

AudioDataPtr MainWindow::runRepair (const QString& title, std::function<AudioDataPtr (repair::Progress)> work)
{
    auto progress = std::make_shared<std::atomic<float>> (0.0f);
    auto cancel = std::make_shared<std::atomic<bool>> (false);
    engine.stop();

    QProgressDialog dialog (title + QStringLiteral ("..."), tr ("Cancel"), 0, 1000, this);
    dialog.setWindowTitle (title);
    dialog.setWindowModality (Qt::WindowModal);
    dialog.setMinimumDuration (400);
    connect (&dialog, &QProgressDialog::canceled, this, [cancel] { cancel->store (true); });

    QFutureWatcher<AudioDataPtr> watcher;
    QEventLoop loop;
    connect (&watcher, &QFutureWatcher<AudioDataPtr>::finished, &loop, &QEventLoop::quit);
    QTimer poll;
    connect (&poll, &QTimer::timeout, this, [&] { dialog.setValue ((int) (progress->load() * 1000.0f)); });
    poll.start (50);

    watcher.setFuture (QtConcurrent::run ([work, progress, cancel] { return work ({ progress.get(), cancel.get() }); }));
    loop.exec();
    poll.stop();
    dialog.reset();
    return cancel->load() ? nullptr : watcher.result();
}

void MainWindow::commitRepair (const QString& description, AudioDataPtr result, Frame start, Frame end)
{
    if (result == nullptr)
        return;
    commit (description, std::move (result), start, end, end - start);
    waveform->setFocus();
}

void MainWindow::learnNoise()
{
    const auto d = data();
    if (d == nullptr || ! requireSelection())
        return;

    auto profile = repair::learnNoise (*d, selStart, selEnd);
    if (! profile.isValid())
    {
        statusBar()->showMessage (tr ("Select at least a quarter of a second of noise on its own, then learn it again"), 8000);
        return;
    }
    noiseProfile = std::move (profile);
    statusBar()->showMessage (tr ("Learned the noise from %1 s. Now select what to clean (or nothing for the whole file) and press %2")
                                  .arg ((double) noiseProfile.learnedFrom / d->sampleRate(), 0, 'f', 2)
                                  .arg (commands.action (QStringLiteral ("repair.noiseReduction"))->shortcut().toString (QKeySequence::NativeText)),
                              10000);
}

void MainWindow::noiseReduction()
{
    const auto d = data();
    if (d == nullptr)
        return;
    if (! noiseProfile.isValid() || std::abs (noiseProfile.sampleRate - d->sampleRate()) > 0.5)
    {
        QMessageBox::information (this, tr ("Noise Reduction"),
                                  tr ("First teach PHINYX what the noise sounds like:\n\n"
                                      "1. Select a part with only the noise (hiss, hum, room tone), at least a quarter of a second.\n"
                                      "2. Press %1 (Repair > Learn Noise Profile).\n"
                                      "3. Select what to clean, or nothing for the whole file, and open Noise Reduction again.")
                                      .arg (commands.action (QStringLiteral ("repair.learnNoise"))->shortcut().toString (QKeySequence::NativeText)));
        return;
    }

    ValuesDialog dialog (tr ("Noise Reduction"),
                         tr ("Turns down everything that sounds like the learned noise, %1. Start gentle: a little noise left sounds "
                             "more natural than a watery, over-processed result.").arg (hasSelection() ? tr ("in the selection") : tr ("in the whole file")),
                         {
                             { QStringLiteral ("repair/noise/reduction"), tr ("&Reduce noise by:"), 12.0, 3.0, 40.0, 1.0, 0, tr (" dB"),
                               tr ("How far the noise is turned down"), {} },
                             { QStringLiteral ("repair/noise/sensitivity"), tr ("&Sensitivity:"), 6.0, 0.0, 24.0, 1.0, 0, tr (" dB"),
                               tr ("How far above the noise a sound must be to be kept. Higher removes more but can dull quiet sounds"), {} },
                             { QStringLiteral ("repair/noise/release"), tr ("Re&lease:"), 100.0, 20.0, 500.0, 10.0, 0, tr (" ms"),
                               tr ("How long the reduction takes to come back after a sound; longer is smoother"), {} },
                         }, this);
    if (dialog.exec() != QDialog::Accepted)
        return;

    repair::NoiseReductionSettings settings { dialog.value (0), dialog.value (1), dialog.value (2) };
    const auto [s, e] = processRange();
    const auto profile = noiseProfile;
    auto result = runRepair (tr ("Noise Reduction"), [d, s, e, profile, settings] (repair::Progress p)
    {
        return repair::reduceNoise (*d, s, e, profile, settings, p);
    });
    commitRepair (tr ("Noise Reduction %1 dB").arg (settings.reductionDb, 0, 'f', 0), std::move (result), s, e);
}

void MainWindow::declick()
{
    const auto d = data();
    if (d == nullptr)
        return;

    ValuesDialog dialog (tr ("DeClick"),
                         tr ("Finds clicks and crackle %1 and rebuilds each one from the sound around it.")
                             .arg (hasSelection() ? tr ("in the selection") : tr ("in the whole file")),
                         { { QStringLiteral ("repair/declick/sensitivity"), tr ("&Sensitivity:"), 1.0, 0, 0, 1, 0, {},
                             tr ("High finds quieter clicks but may touch sharp drum hits"),
                             { tr ("Low (loud clicks only)"), tr ("Medium"), tr ("High (vinyl crackle)") } } },
                         this);
    if (dialog.exec() != QDialog::Accepted)
        return;

    const double thresholds[] { 9.0, 6.5, 4.5 };
    const auto threshold = thresholds[std::clamp ((int) dialog.value (0), 0, 2)];
    const auto [s, e] = processRange();
    auto report = std::make_shared<repair::Report>();
    auto result = runRepair (tr ("DeClick"), [d, s, e, threshold, report] (repair::Progress p)
    {
        return repair::declick (*d, s, e, threshold, *report, p);
    });
    if (result == nullptr)
        return;
    if (report->fixed == 0)
    {
        statusBar()->showMessage (tr ("No clicks found. Try a higher sensitivity, or Auto Heal (Ctrl+U) on a selected click"), 8000);
        return;
    }
    commitRepair (tr ("DeClick (%n click(s))", nullptr, report->fixed), std::move (result), s, e);
    statusBar()->showMessage (tr ("Fixed %n click(s)", nullptr, report->fixed)
                                  + (report->skipped > 0 ? tr ("; %n longer one(s) left for Auto Heal", nullptr, report->skipped) : QString()), 8000);
}

void MainWindow::declip()
{
    const auto d = data();
    if (d == nullptr)
        return;

    ValuesDialog dialog (tr ("DeClip"),
                         tr ("Rebuilds the tops of waves that were cut off by recording too loud, %1. "
                             "The rebuilt peaks can go above 0 dB, so Normalize afterwards.")
                             .arg (hasSelection() ? tr ("in the selection") : tr ("in the whole file")),
                         { { QStringLiteral ("repair/declip/level"), tr ("&Clipped at or above:"), 99.0, 80.0, 100.0, 0.5, 1, tr (" % of the peak"),
                             tr ("Samples at least this loud, in runs of two or more, count as clipped"), {} } },
                         this);
    if (dialog.exec() != QDialog::Accepted)
        return;

    const auto level = dialog.value (0);
    const auto [s, e] = processRange();
    auto report = std::make_shared<repair::Report>();
    auto result = runRepair (tr ("DeClip"), [d, s, e, level, report] (repair::Progress p)
    {
        return repair::declip (*d, s, e, level, *report, p);
    });
    if (result == nullptr)
        return;
    if (report->fixed == 0)
    {
        statusBar()->showMessage (tr ("No clipping found"), 6000);
        return;
    }
    commitRepair (tr ("DeClip"), result, s, e);
    const auto peak = result->peakAbs (s, e);
    statusBar()->showMessage (tr ("Rebuilt %n clipped peak(s)", nullptr, report->fixed)
                                  + (peak > 1.0f ? tr ("; they now reach %1 dB, so Normalize (Ctrl+Shift+N) before saving")
                                                       .arg (20.0 * std::log10 (peak), 0, 'f', 1)
                                                 : QString()), 10000);
}

void MainWindow::dehum()
{
    const auto d = data();
    if (d == nullptr)
        return;

    ValuesDialog dialog (tr ("DeHum"),
                         tr ("Removes mains hum and its overtones with very narrow notches, %1.")
                             .arg (hasSelection() ? tr ("in the selection") : tr ("in the whole file")),
                         {
                             { QStringLiteral ("repair/dehum/frequency"), tr ("&Hum at:"), 0.0, 0, 0, 1, 0, {},
                               tr ("Find looks for 50 or 60 Hz hum (and its exact pitch) by itself"),
                               { tr ("Find it"), tr ("50 Hz (Europe, Asia, Africa, Australia)"), tr ("60 Hz (Americas, parts of Asia)"), {} } },
                             { QStringLiteral ("repair/dehum/harmonics"), tr ("&Overtones:"), 8.0, 1.0, 30.0, 1.0, 0, {},
                               tr ("How many multiples of the hum to remove; buzzy hum needs more"), {} },
                             { QStringLiteral ("repair/dehum/width"), tr ("&Width:"), 1.5, 0.3, 10.0, 0.1, 1, tr (" Hz"),
                               tr ("Narrower touches less of the music; wider catches hum that drifts"), {} },
                         }, this);
    if (dialog.exec() != QDialog::Accepted)
        return;

    const auto [s, e] = processRange();
    repair::HumSettings settings;
    settings.harmonics = (int) dialog.value (1);
    settings.widthHz = dialog.value (2);
    const auto choice = (int) dialog.value (0);
    if (choice == 0)
    {
        QApplication::setOverrideCursor (Qt::WaitCursor);
        settings.frequency = repair::findHum (*d, s, e);
        QApplication::restoreOverrideCursor();
        if (settings.frequency <= 0.0)
        {
            QMessageBox::information (this, tr ("DeHum"), tr ("No clear 50 or 60 Hz hum was found here. If you can hear one, choose 50 or 60 Hz yourself."));
            return;
        }
    }
    else
        settings.frequency = choice == 1 ? 50.0 : 60.0;

    auto result = runRepair (tr ("DeHum"), [d, s, e, settings] (repair::Progress p) { return repair::dehum (*d, s, e, settings, p); });
    commitRepair (tr ("DeHum %1 Hz").arg (settings.frequency, 0, 'f', 2), std::move (result), s, e);
    statusBar()->showMessage (tr ("Removed hum at %1 Hz and %n overtone(s)", nullptr, settings.harmonics - 1).arg (settings.frequency, 0, 'f', 2), 8000);
}

void MainWindow::deess()
{
    const auto d = data();
    if (d == nullptr)
        return;

    ValuesDialog dialog (tr ("DeEss"),
                         tr ("Turns down sharp \"s\" and \"sh\" sounds in a voice, %1, only at the moments they get loud.")
                             .arg (hasSelection() ? tr ("in the selection") : tr ("in the whole file")),
                         {
                             { QStringLiteral ("repair/deess/threshold"), tr ("&Threshold:"), -30.0, -60.0, 0.0, 1.0, 0, tr (" dB"),
                               tr ("Esses louder than this are turned down; lower catches more"), {} },
                             { QStringLiteral ("repair/deess/from"), tr ("&From:"), 5000.0, 2000.0, 12000.0, 250.0, 0, tr (" Hz"),
                               tr ("Where the esses start; around 4 to 6 kHz for most voices"), {} },
                             { QStringLiteral ("repair/deess/max"), tr ("&At most:"), 10.0, 1.0, 24.0, 1.0, 0, tr (" dB"),
                               tr ("The most an ess is turned down"), {} },
                         }, this);
    if (dialog.exec() != QDialog::Accepted)
        return;

    repair::DeEssSettings settings { dialog.value (0), dialog.value (1), dialog.value (2) };
    const auto [s, e] = processRange();
    auto result = runRepair (tr ("DeEss"), [d, s, e, settings] (repair::Progress p) { return repair::deess (*d, s, e, settings, p); });
    commitRepair (tr ("DeEss"), std::move (result), s, e);
}

void MainWindow::spectralGain()
{
    const auto d = data();
    if (d == nullptr)
        return;

    const auto [s, e] = processRange();
    const auto what = hasBand() ? tr ("%1 to %2 in the selection").arg (formatHz (bandLow), formatHz (bandHigh))
                                : hasSelection() ? tr ("the selection") : tr ("the whole file");
    SpectralGainDialog dialog (spectralGainDb, what, this);
    if (dialog.exec() != QDialog::Accepted)
        return;

    spectralGainDb = std::max (-60.0, dialog.gainDb());
    QApplication::setOverrideCursor (Qt::WaitCursor);
    auto result = repair::spectralGain (*d, { s, e, bandLow, bandHigh }, dialog.gainDb());
    QApplication::restoreOverrideCursor();
    const auto name = dialog.gainDb() <= repair::removeDb ? tr ("Spectral Remove") : tr ("Spectral Gain %1 dB").arg (dialog.gainDb(), 0, 'f', 1);
    commit (name, std::move (result), s, e, e - s);
    waveform->setFocus();
}

void MainWindow::normalize()
{
    const auto d = data();
    if (d == nullptr)
        return;

    NormalizeDialog dialog (this);
    if (dialog.exec() != QDialog::Accepted)
        return;

    const auto [s, e] = processRange();
    QApplication::setOverrideCursor (Qt::WaitCursor);

    double changeDb = 0.0;
    QString problem;

    if (dialog.mode() == NormalizeDialog::Mode::peak)
    {
        const auto peak = d->peakAbs (s, e);
        if (peak <= 0.0f)
            problem = tr ("The audio is silent.");
        else
            changeDb = dialog.target() - dsp::gainToDb (peak);
    }
    else
    {
        const auto report = dsp::measureLoudness (*d, s, e);
        if (report.integratedLufs <= -70.0)
            problem = tr ("The audio is too quiet or too short to measure its loudness.");
        else
        {
            changeDb = dialog.target() - report.integratedLufs;
            const auto newTruePeak = report.truePeakDb + changeDb;
            if (newTruePeak > 0.0)
                statusBar()->showMessage (tr ("Warning: true peak will be %1 dBTP and may clip").arg (newTruePeak, 0, 'f', 1), 8000);
        }
    }

    if (! problem.isEmpty())
    {
        QApplication::restoreOverrideCursor();
        QMessageBox::information (this, tr ("Normalize"), problem);
        return;
    }

    auto result = dsp::applyGain (*d, s, e, changeDb);
    QApplication::restoreOverrideCursor();

    const auto unit = dialog.mode() == NormalizeDialog::Mode::peak ? QStringLiteral ("dBFS") : QStringLiteral ("LUFS");
    commit (tr ("Normalize to %1 %2").arg (dialog.target(), 0, 'f', 1).arg (unit), std::move (result), s, e, e - s);
}

void MainWindow::fade (bool in)
{
    if (! requireSelection())
        return;

    const auto s = selStart, e = selEnd;
    auto result = in ? dsp::fadeIn (*data(), s, e, fadeShape) : dsp::fadeOut (*data(), s, e, fadeShape);
    commit (in ? tr ("Fade In") : tr ("Fade Out"), std::move (result), s, e, e - s);
}

void MainWindow::simpleProcess (const QString& description, AudioDataPtr (*fn) (const AudioData&, Frame, Frame))
{
    if (data() == nullptr)
        return;

    const auto [s, e] = processRange();
    QApplication::setOverrideCursor (Qt::WaitCursor);
    auto result = fn (*data(), s, e);
    QApplication::restoreOverrideCursor();
    commit (description, std::move (result), s, e, e - s);
}

//==============================================================================
void MainWindow::commitMarkers (const QString& description, std::vector<Marker> markers)
{
    doc.apply (description.toStdString(), { doc.current().data, std::move (markers) });
    refreshFromDocument (true);
}

void MainWindow::addMarker()
{
    if (data() == nullptr)
        return;

    auto markers = doc.current().markers;
    Marker m;
    m.id = doc.nextMarkerId();
    m.start = m.end = cursor;
    m.name = tr ("Marker %1").arg (m.id).toStdString();
    markers.push_back (m);
    commitMarkers (tr ("Add Marker"), std::move (markers));
    statusBar()->showMessage (tr ("Added %1").arg (QString::fromStdString (m.name)), 3000);
}

void MainWindow::addRegion()
{
    if (! requireSelection())
        return;

    auto markers = doc.current().markers;
    Marker m;
    m.id = doc.nextMarkerId();
    m.type = Marker::Type::region;
    m.start = selStart;
    m.end = selEnd;
    m.name = tr ("Region %1").arg (m.id).toStdString();
    markers.push_back (m);
    commitMarkers (tr ("Add Region"), std::move (markers));
    statusBar()->showMessage (tr ("Added %1").arg (QString::fromStdString (m.name)), 3000);
}

void MainWindow::jumpToMarker (int id)
{
    for (const auto& m : doc.current().markers)
        if (m.id == id)
        {
            if (m.isRegion())
                selectRange (m.start, m.end);
            else
                setCursor (m.start);
            return;
        }
}

void MainWindow::jumpToAdjacentMarker (bool forward)
{
    if (data() == nullptr)
        return;

    // Region ends count as stops too, so ; and ' walk through every boundary.
    std::vector<Frame> stops;
    for (const auto& m : doc.current().markers)
    {
        stops.push_back (m.start);
        if (m.isRegion())
            stops.push_back (m.end);
    }

    std::sort (stops.begin(), stops.end());
    if (forward)
    {
        const auto it = std::upper_bound (stops.begin(), stops.end(), cursor);
        if (it != stops.end())
            setCursor (*it);
    }
    else
    {
        const auto it = std::lower_bound (stops.begin(), stops.end(), cursor);
        if (it != stops.begin())
            setCursor (*std::prev (it));
    }
}

void MainWindow::renameMarker (int id)
{
    auto markers = doc.current().markers;
    for (auto& m : markers)
        if (m.id == id)
        {
            bool ok = false;
            const auto name = QInputDialog::getText (this, tr ("Rename Marker"), tr ("&Name:"), QLineEdit::Normal,
                                                     QString::fromStdString (m.name), &ok);
            if (! ok || name.toStdString() == m.name)
                return;

            m.name = name.toStdString();
            commitMarkers (tr ("Rename Marker"), std::move (markers));
            markerList->setFocus();
            return;
        }
}

void MainWindow::deleteMarker (int id)
{
    auto markers = doc.current().markers;
    const auto before = markers.size();
    markers.erase (std::remove_if (markers.begin(), markers.end(), [id] (const Marker& m) { return m.id == id; }), markers.end());

    if (markers.size() != before)
    {
        commitMarkers (tr ("Delete Marker"), std::move (markers));
        markerList->setFocus();
    }
}

void MainWindow::showLoudnessStatistics()
{
    const auto d = data();
    if (d == nullptr)
        return;

    const auto [s, e] = processRange();
    QApplication::setOverrideCursor (Qt::WaitCursor);
    const auto r = dsp::measureLoudness (*d, s, e);
    QApplication::restoreOverrideCursor();

    auto fmt = [] (double v, const char* unit)
    {
        return v <= -199.0 ? QStringLiteral ("-inf %1").arg (QLatin1String (unit))
                           : QStringLiteral ("%1 %2").arg (v, 0, 'f', 1).arg (QLatin1String (unit));
    };

    const auto what = hasSelection() ? tr ("Selection") : tr ("Whole file");
    QMessageBox box (QMessageBox::Information, tr ("Loudness Statistics"),
                     tr ("<b>%1</b> (%2)<table cellspacing=6>"
                         "<tr><td>Integrated loudness</td><td align=right>%3</td></tr>"
                         "<tr><td>Loudness range</td><td align=right>%4</td></tr>"
                         "<tr><td>True peak</td><td align=right>%5</td></tr>"
                         "<tr><td>Sample peak</td><td align=right>%6</td></tr></table>")
                         .arg (what, formatTime ((double) (e - s) / d->sampleRate()), fmt (r.integratedLufs, "LUFS"),
                               QStringLiteral ("%1 LU").arg (r.loudnessRange, 0, 'f', 1), fmt (r.truePeakDb, "dBTP"),
                               fmt (r.samplePeakDb, "dBFS")),
                     QMessageBox::Ok, this);
    box.exec();
}

//==============================================================================
AudioDataPtr MainWindow::renderWithProgress (Frame start, Frame end, std::string& error)
{
    auto progress = std::make_shared<std::atomic<float>> (0.0f);
    auto cancel = std::make_shared<std::atomic<bool>> (false);
    auto source = data();
    engine.stop();

    QProgressDialog dialog (tr ("Rendering through the Master Section..."), tr ("Cancel"), 0, 1000, this);
    dialog.setWindowTitle (tr ("Render"));
    dialog.setWindowModality (Qt::WindowModal);
    dialog.setMinimumDuration (300);
    connect (&dialog, &QProgressDialog::canceled, this, [cancel] { cancel->store (true); });

    struct Result { AudioDataPtr data; std::string error; };
    QFutureWatcher<Result> watcher;
    QEventLoop loop;
    connect (&watcher, &QFutureWatcher<Result>::finished, &loop, &QEventLoop::quit);

    QTimer poll;
    connect (&poll, &QTimer::timeout, this, [&] { dialog.setValue ((int) (progress->load() * 1000.0f)); });
    poll.start (50);

    watcher.setFuture (QtConcurrent::run ([this, source, start, end, progress, cancel]
    {
        Result r;
        r.data = engine.renderThroughMaster (*source, start, end, r.error, progress.get(), cancel.get());
        return r;
    }));

    loop.exec();
    poll.stop();
    dialog.reset();

    auto result = watcher.result();
    error = result.error;
    return result.data;
}

void MainWindow::renderMaster()
{
    const auto d = data();
    if (d == nullptr)
        return;

    if (! engine.masterIsActive())
    {
        QMessageBox::information (this, tr ("Render"),
                                  tr ("The Master Section is empty or bypassed, so rendering would not change anything.\n"
                                      "Add a plugin with Ctrl+Alt+E or change the master gain first."));
        return;
    }

    QMessageBox ask (QMessageBox::Question, tr ("Render"),
                     tr ("Render the audio through the Master Section.\n\n"
                         "Apply to Audio processes the selection (or the whole file) as an undoable edit, then bypasses "
                         "the Master Section so the effect is not heard twice.\n"
                         "Save to New File writes the whole file through the Master Section to a new file and leaves "
                         "this one unchanged."),
                     QMessageBox::Cancel, this);
    auto* apply = ask.addButton (tr ("&Apply to Audio"), QMessageBox::AcceptRole);
    auto* toFile = ask.addButton (tr ("Save to &New File..."), QMessageBox::AcceptRole);
    ask.setDefaultButton (apply);
    ask.exec();

    std::string error;

    if (ask.clickedButton() == apply)
    {
        const auto [s, e] = processRange();
        auto result = renderWithProgress (s, e, error);
        if (result == nullptr)
        {
            if (error != "Cancelled")
                QMessageBox::warning (this, tr ("Render"), QString::fromStdString (error));
            return;
        }

        // The render covers [s, e) only; put it back into the file. A mono file that
        // the chain widened (Stereoize, a stereo reverb...) becomes stereo throughout.
        auto base = result->numChannels() == d->numChannels() ? d : dsp::toStereo (d);
        commit (tr ("Master Section"), base->replaced (s, e, result.get()), s, e, e - s);
        engine.setMasterBypassed (true);
        masterWidget->refresh();
        commands.action (QStringLiteral ("master.bypass"))->setChecked (true);
        statusBar()->showMessage (tr ("Rendered. The Master Section is now bypassed; Ctrl+Shift+B turns it back on."), 8000);
        return;
    }

    if (ask.clickedButton() != toFile)
        return;

    QStringList filters;
    for (const auto& ext : AudioEngine::writableExtensions())
    {
        const auto x = QString::fromStdString (ext);
        filters << tr ("%1 (*.%2)").arg (x.toUpper(), x);
    }

    QFileInfo current (QString::fromStdString (doc.path()));
    QString selectedFilter = filters.first();
    auto path = QFileDialog::getSaveFileName (this, tr ("Render to New File"),
                                              current.absolutePath() + QLatin1Char ('/') + current.completeBaseName()
                                                  + tr (" (mastered)") + QStringLiteral (".wav"),
                                              filters.join (QStringLiteral (";;")), &selectedFilter);
    if (path.isEmpty())
        return;

    const auto writable = AudioEngine::writableExtensions();
    if (std::find (writable.begin(), writable.end(), QFileInfo (path).suffix().toLower().toStdString()) == writable.end())
    {
        const auto from = selectedFilter.indexOf (QStringLiteral ("*.")) + 2;
        path += QLatin1Char ('.') + selectedFilter.mid (from, selectedFilter.indexOf (QLatin1Char (')')) - from);
    }

    auto options = saveOptions;
    SaveOptionsDialog optionsDialog (QFileInfo (path).suffix(), options, this);
    if (optionsDialog.exec() != QDialog::Accepted)
        return;

    auto rendered = renderWithProgress (0, d->length(), error);
    if (rendered == nullptr)
    {
        if (error != "Cancelled")
            QMessageBox::warning (this, tr ("Render"), QString::fromStdString (error));
        return;
    }

    QApplication::setOverrideCursor (Qt::WaitCursor);
    const bool ok = AudioEngine::saveFile (path.toStdString(), *rendered, doc.current().markers, options, error);
    QApplication::restoreOverrideCursor();

    if (! ok)
        QMessageBox::warning (this, tr ("Render"), tr ("Could not save %1:\n%2").arg (path, QString::fromStdString (error)));
    else
        statusBar()->showMessage (tr ("Rendered to %1").arg (QFileInfo (path).fileName()), 6000);
}

bool MainWindow::requireSelection()
{
    if (hasSelection())
        return true;

    if (data() != nullptr)
        statusBar()->showMessage (tr ("Select some audio first: drag across the waveform, or hold Shift and press "
                                      "the arrow keys. Ctrl+A selects everything."), 6000);
    return false;
}

//==============================================================================
void MainWindow::setCursor (Frame f)
{
    const auto d = data();
    if (d == nullptr)
        return;

    cursor = selAnchor = std::clamp<Frame> (f, 0, d->length());
    selStart = selEnd = cursor;
    setBand (0.0, 0.0);
    engine.setPositionFrames (cursor);
    waveform->setCursorFrame (cursor);
    waveform->setSelection (selStart, selEnd);
    updateCommandStates();
}

void MainWindow::moveCursorBy (double seconds)
{
    if (auto d = data())
        setCursor (cursor + (Frame) std::llround (seconds * d->sampleRate()));
}

void MainWindow::selectRange (Frame anchor, Frame head)
{
    const auto d = data();
    if (d == nullptr)
        return;

    anchor = std::clamp<Frame> (anchor, 0, d->length());
    head = std::clamp<Frame> (head, 0, d->length());
    selAnchor = anchor;
    selStart = std::min (anchor, head);
    selEnd = std::max (anchor, head);
    cursor = head;

    if (! engine.isPlaying())
        engine.setPositionFrames (cursor);

    waveform->setCursorFrame (cursor);
    waveform->setSelection (selStart, selEnd);
    updateCommandStates();
}

void MainWindow::extendSelectionBy (double seconds)
{
    if (auto d = data())
        extendSelectionTo (cursor + (Frame) std::llround (seconds * d->sampleRate()));
}

void MainWindow::extendSelectionTo (Frame head)
{
    // With no selection the anchor is the cursor, so Shift+arrows grow from there.
    selectRange (hasSelection() ? selAnchor : cursor, head);
}

void MainWindow::togglePlayback()
{
    if (engine.isPlaying())
    {
        engine.stop();
        return;
    }

    engine.setPositionFrames (hasSelection() ? selStart : cursor);
    engine.play();
}

void MainWindow::closeEvent (QCloseEvent* e)
{
    if (loading)
    {
        e->ignore();
        return;
    }

    // Ask about each tab with unsaved changes, showing it while asking.
    for (int i = 0; i < (int) tabs.size(); ++i)
    {
        const bool modified = i == activeTab ? doc.isOpen() && doc.isModified()
                                             : tabs[(size_t) i].doc.isOpen() && tabs[(size_t) i].doc.isModified();
        if (! modified)
            continue;

        switchToTab (i);
        if (! confirmDiscard())
        {
            e->ignore();
            return;
        }
    }

    engine.stop();
    commands.save();
    saveLayout();
    e->accept();
}

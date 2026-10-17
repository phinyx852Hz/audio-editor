// SPDX-License-Identifier: AGPL-3.0-only
#include "MainWindow.h"
#include "Dialogs.h"
#include "Panels.h"
#include "Theme.h"
#include "WaveformView.h"

#include <QActionGroup>
#include <QApplication>
#include <QCloseEvent>
#include <QDockWidget>
#include <QSettings>
#include <QStandardPaths>
#include <QFileDialog>
#include <QFileInfo>
#include <QInputDialog>
#include <QKeyEvent>
#include <QLabel>
#include <QLineEdit>
#include <QListWidget>
#include <QMenuBar>
#include <QMessageBox>
#include <QStatusBar>
#include <QTimer>
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

    namespace Category
    {
        const QString file      = QObject::tr ("File");
        const QString edit      = QObject::tr ("Edit");
        const QString select    = QObject::tr ("Selection");
        const QString process   = QObject::tr ("Process");
        const QString markers   = QObject::tr ("Markers");
        const QString analyze   = QObject::tr ("Analyze");
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
    setCentralWidget (waveform);
    statusBar()->addWidget (statusLabel, 1);
    statusLabel->setAccessibleName (tr ("Status"));

    buildPanels();
    registerCommands();
    commands.load();
    buildMenus();
    restoreLayout();

    connect (waveform, &WaveformView::cursorRequested, this, &MainWindow::setCursor);
    connect (waveform, &WaveformView::selectionRequested, this, &MainWindow::selectRange);
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
    add ("file.save", Category::file, tr ("&Save"), { KS (Qt::CTRL | Qt::Key_S) }, [this] { save(); });
    add ("file.saveAs", Category::file, tr ("Save &As..."), { KS (Qt::CTRL | Qt::SHIFT | Qt::Key_S) }, [this] { saveAs(); });
    add ("file.close", Category::file, tr ("&Close"), { KS (Qt::CTRL | Qt::Key_W) }, [this] { closeFile(); });
    add ("file.quit", Category::file, tr ("&Quit"), { KS (Qt::CTRL | Qt::Key_Q) }, [this] { close(); });

    add ("edit.undo", Category::edit, tr ("&Undo"), { KS (Qt::CTRL | Qt::Key_Z) }, [this] { undo(); });
    add ("edit.redo", Category::edit, tr ("&Redo"), { KS (Qt::CTRL | Qt::SHIFT | Qt::Key_Z), KS (Qt::CTRL | Qt::Key_Y) },
         [this] { redo(); });
    add ("edit.cut", Category::edit, tr ("Cu&t"), { KS (Qt::CTRL | Qt::Key_X) }, [this] { cut(); });
    add ("edit.copy", Category::edit, tr ("&Copy"), { KS (Qt::CTRL | Qt::Key_C) }, [this] { copy(); });
    add ("edit.paste", Category::edit, tr ("&Paste"), { KS (Qt::CTRL | Qt::Key_V) }, [this] { paste(); });
    add ("edit.delete", Category::edit, tr ("&Delete"), { KS (Qt::Key_Delete) }, [this] { deleteSelection(); });
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
         [this] { waveform->zoomToSelection(); });
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
    add ("view.spectral", Category::view, tr ("&Spectral Display"), { KS (Qt::SHIFT | Qt::Key_D) }, [this]
    {
        waveform->setSpectral (! waveform->isSpectral());
        commands.action (QStringLiteral ("view.spectral"))->setChecked (waveform->isSpectral());
    })->setCheckable (true);
    add ("view.focusWaveform", Category::view, tr ("Focus &Waveform"), { KS (Qt::Key_F6) }, [this]
    {
        activateWindow();
        waveform->setFocus();
    });

    add ("options.audio", Category::options, tr ("&Audio Settings..."), { KS (Qt::CTRL | Qt::Key_Comma) },
         [this] { AudioSettingsDialog (engine, this).exec(); });
    add ("options.keymap", Category::options, tr ("&Keyboard Shortcuts..."), { KS (Qt::CTRL | Qt::SHIFT | Qt::Key_K) },
         [this] { KeymapDialog (commands, this).exec(); });
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
        { tr ("&File"),      { "file.open", "file.save", "file.saveAs", "-", "file.close", "-", "file.quit" } },
        { tr ("&Edit"),      { "edit.undo", "edit.redo", "-", "edit.cut", "edit.copy", "edit.paste", "edit.delete", "-",
                               "edit.trim", "edit.insertSilence", "-", "select.all", "select.none", "-",
                               "select.back", "select.forward", "select.backLarge", "select.forwardLarge",
                               "select.toStart", "select.toEnd" } },
        { tr ("&Process"),   { "process.gain", "process.normalize", "-", "process.fadeIn", "process.fadeOut", "-",
                               "process.fadeShape.linear", "process.fadeShape.equalPower", "process.fadeShape.exponential",
                               "process.fadeShape.sCurve", "-", "process.reverse", "process.invert", "process.dcOffset" } },
        { tr ("&Markers"),   { "marker.add", "marker.addRegion", "-", "marker.next", "marker.previous", "-", "view.markers" } },
        { tr ("&Analyze"),   { "analyze.loudness", "-", "view.meters", "analyze.resetMeters", "-", "view.spectral" } },
        { tr ("&Transport"), { "transport.playStop", "transport.stop", "-", "transport.start", "transport.end",
                               "cursor.back", "cursor.forward", "cursor.backLarge", "cursor.forwardLarge" } },
        { tr ("&View"),      { "view.zoomIn", "view.zoomOut", "view.zoomFit", "view.zoomSelection", "-",
                               "view.spectral", "-", "view.history", "view.markers", "view.meters", "view.popOut", "-",
                               "view.focusWaveform" } },
        { tr ("&Options"),   { "options.audio", "options.keymap", "options.theme" } },
        { tr ("&Help"),      { "help.palette", "help.menuBar", "-", "help.about" } },
    };

    for (const auto& [title, ids] : layout)
    {
        auto* menu = menuBar()->addMenu (title);
        for (const auto& id : ids)
        {
            if (id == QLatin1String ("-"))
                menu->addSeparator();
            else
                menu->addAction (commands.action (id));
        }
    }
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

    metersPanel = new DockablePanel (*this, QStringLiteral ("metersPanel"), tr ("Meters"), meterPanel, Qt::RightDockWidgetArea);
    markerPanel = new DockablePanel (*this, QStringLiteral ("markersPanel"), tr ("Markers"), markerList, Qt::BottomDockWidgetArea);
    historyPanel = new DockablePanel (*this, QStringLiteral ("historyPanel"), tr ("History"), historyList, Qt::RightDockWidgetArea);
    panels = { metersPanel, markerPanel, historyPanel };
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
    restoreState (settings.value (QStringLiteral ("window/state")).toByteArray());

    const bool spectral = settings.value (QStringLiteral ("view/spectral"), false).toBool();
    waveform->setSpectral (spectral);
    commands.action (QStringLiteral ("view.spectral"))->setChecked (spectral);

    // Pop panels out again once the main window is on screen, so it opens first.
    QTimer::singleShot (0, this, [this]
    {
        auto s = appSettings();
        for (auto* p : panels)
            if (s.value (QStringLiteral ("panels/%1/poppedOut").arg (p->id()), false).toBool())
                p->setPoppedOut (true);

        activateWindow();
        waveform->setFocus();
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
    settings.setValue (QStringLiteral ("window/state"), saveState());
    settings.setValue (QStringLiteral ("view/spectral"), waveform->isSpectral());
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

    for (const auto* id : { "file.save", "file.saveAs", "file.close", "edit.insertSilence", "select.all", "select.none",
                            "select.back", "select.forward", "select.backLarge", "select.forwardLarge",
                            "select.toStart", "select.toEnd", "process.gain", "process.normalize",
                            "process.reverse", "process.invert", "process.dcOffset",
                            "transport.playStop", "transport.stop", "transport.start", "transport.end",
                            "cursor.back", "cursor.forward", "cursor.backLarge", "cursor.forwardLarge",
                            "view.zoomIn", "view.zoomOut", "view.zoomFit" })
        commands.action (QString::fromLatin1 (id))->setEnabled (open);

    for (const auto* id : { "edit.cut", "edit.copy", "edit.delete", "edit.trim", "process.fadeIn", "process.fadeOut",
                            "view.zoomSelection" })
        commands.action (QString::fromLatin1 (id))->setEnabled (sel);

    commands.action (QStringLiteral ("edit.paste"))->setEnabled (open && clipboard != nullptr);
    commands.action (QStringLiteral ("marker.addRegion"))->setEnabled (sel);

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
    if (! doc.isOpen())
    {
        setWindowTitle (QStringLiteral (APP_DISPLAY_NAME));
        return;
    }

    const auto name = doc.path().empty() ? tr ("Untitled") : QFileInfo (QString::fromStdString (doc.path())).fileName();
    setWindowTitle ((doc.isModified() ? QStringLiteral ("*") : QString()) + name + QStringLiteral (" - " APP_DISPLAY_NAME));
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

        text += QStringLiteral ("   %1 Hz, %2 ch   %3").arg ((int) rate).arg (d->numChannels())
                    .arg (engine.isPlaying() ? tr ("Playing") : tr ("Stopped"));
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
    if (loading || ! confirmDiscard())
        return;

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
    if (loading || ! confirmDiscard())
        return;

    engine.stop();
    doc = AudioDocument();
    cursor = selStart = selEnd = selAnchor = 0;
    waveform->clear();
    refreshFromDocument (false);
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
    if (! hasSelection())
        return;

    clipboard = data()->slice (selStart, selEnd);
    updateCommandStates();
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

    // Select what was pasted so it can be processed or undone at a glance.
    selStart = selAnchor = start;
    selEnd = cursor = start + clipboard->length();
    commit (tr ("Paste"), std::move (result), start, end, clipboard->length());
}

void MainWindow::deleteSelection()
{
    if (! hasSelection())
        return;

    const auto start = selStart, end = selEnd;
    auto result = data()->replaced (start, end, nullptr);
    cursor = selStart = selEnd = selAnchor = start;
    commit (tr ("Delete"), std::move (result), start, end, 0);
}

void MainWindow::trimToSelection()
{
    if (! hasSelection())
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
    if (! hasSelection())
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
    if (! hasSelection())
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
void MainWindow::setCursor (Frame f)
{
    const auto d = data();
    if (d == nullptr)
        return;

    cursor = selAnchor = std::clamp<Frame> (f, 0, d->length());
    selStart = selEnd = cursor;
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
    if (loading || ! confirmDiscard())
    {
        e->ignore();
        return;
    }

    engine.stop();
    commands.save();
    saveLayout();
    e->accept();
}

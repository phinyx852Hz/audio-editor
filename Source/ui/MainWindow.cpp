// SPDX-License-Identifier: AGPL-3.0-only
#include "MainWindow.h"
#include "MixerWindow.h"
#include "PatchBayWindow.h"
#include "../patchbay/PatchBay.h"
#include "SamplerWindow.h"
#include "SlicerWindow.h"
#include "../engine/MidiInputs.h"
#include "../engine/ControlSurfaces.h"
#include "../engine/PushDisplay.h"
#include "../control/MidiLearn.h"
#include "../sampler/SamplerEngine.h"
#include "../sampler/SlicerEngine.h"
#include "Brand.h"
#include "PhiMark.h"
#include "Settings.h"
#include "FindPanel.h"
#include "Visualizer.h"
#include "../core/Sonify.h"
#include "../performance/Cameras.h"
#include "../performance/VideoFile.h"
#include "TrackerWindow.h"
#include "../tracker/TrackerEngine.h"
#include "../performance/Recorder.h"
#if PHI_HAVE_PROJECTM
 #include "MilkDrop.h"
#endif
#include "AnalysisWindow.h"
#include "PluginAnalyzer.h"
#include "AmbiVisualizer.h"
#include "Goniometer.h"
#include "AmbiScene.h"
#include "../core/Ambisonics.h"
#include "AlbumFile.h"
#include "AlbumView.h"
#include "../core/Album.h"
#include "InfoStrip.h"
#include "Overview.h"
#include "TimeRuler.h"
#include "TransportBar.h"
#include "ChainRow.h"
#include "DesktopShortcuts.h"
#include "BatchWindow.h"
#include "SpectrogramDialog.h"
#include "Dialogs.h"
#include "MetadataEditor.h"
#include "../core/Repair.h"
#include "../engine/BuiltinEffects.h"
#include "../engine/PluginWindow.h"
#include "../engine/StockPlugins.h"
#include "../engine/VampHost.h"
#include "MasterAssistant.h"
#include "MasterPanel.h"
#include "Panels.h"
#include "QuantizePanel.h"
#include "RecordDialog.h"
#include "ReferencePanel.h"
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
#include <QStackedWidget>
#include <QFileDialog>
#include <QDirIterator>
#include <QFileInfo>
#include <QHBoxLayout>
#include <QInputDialog>
#include <QKeyEvent>
#include <QFile>
#include <QLabel>
#include <QLineEdit>
#include <QListWidget>
#include <QMenuBar>
#include <QDesktopServices>
#include <QUrl>
#include <QMessageBox>
#include <QProgressDialog>
#include <QProxyStyle>
#include <QPushButton>
#include <QRegularExpression>
#include <QEventLoop>
#include <QStatusBar>
#include <QStyleFactory>
#include <QTabBar>
#include <QTimer>
#include <QSplitter>
#include <QVBoxLayout>
#include <QtConcurrent/QtConcurrentRun>

#include <algorithm>
#include <cmath>
#include <unistd.h>

namespace
{
    constexpr double smallStepSeconds = 0.1;
    constexpr double largeStepSeconds = 1.0;

    QString formatTime (double seconds)
    {
        const auto ms = (long long) (seconds * 1000.0 + 0.5);
        return QString::asprintf ("%02lld:%02lld:%02lld.%03lld", ms / 3600000, (ms / 60000) % 60, (ms / 1000) % 60, ms % 1000);
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
        const QString effect    = QObject::tr ("Effect");
        const QString generate  = QObject::tr ("Generate");
        const QString spatial   = QObject::tr ("Spatial");
        const QString tools     = QObject::tr ("Tools");
        const QString master    = QObject::tr ("Master Section");
        const QString transport = QObject::tr ("Transport");
        const QString view      = QObject::tr ("View");
        const QString options   = QObject::tr ("Options");
        const QString help      = QObject::tr ("Help");
    }
}

MainWindow::MainWindow (AudioEngine& e)
    : engine (e), waveform (new WaveformView (this)), infoStrip (new InfoStrip (this))
{
    setWindowTitle (QStringLiteral (APP_BRAND_NAME));
    resize (1100, 620);
    Theme::makeTranslucent (this);
    sendThemeToPluginFrames();
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
    overview = new Overview (editor);
    overview->setVisible (appSettings().value (QStringLiteral ("view/overview"), true).toBool());
    editorLayout->addWidget (overview);
    timeRuler = new TimeRuler (editor);
    editorLayout->addWidget (timeRuler);
    albumView = new AlbumView (editor);
    albumView->hide();
    editorLayout->addWidget (albumView);
    // The waveform above, the editor's track (one track of the project, MixerSection.cpp) below.
    editorSplit = new QSplitter (Qt::Vertical, editor);
    editorSplit->setChildrenCollapsible (false);
    editorSplit->addWidget (waveform);
    trackHolder = new QWidget (editorSplit);
    auto* holderLayout = new QVBoxLayout (trackHolder);
    holderLayout->setContentsMargins (0, 0, 0, 0);
    trackHolder->hide();
    editorSplit->addWidget (trackHolder);
    editorSplit->setStretchFactor (0, 3);
    editorSplit->setStretchFactor (1, 2);
    editorLayout->addWidget (editorSplit, 1);
    editorLayout->addWidget (infoStrip);
    // The editor shares its place with the mixer (MixerSection.cpp).
    editorPage = editor;
    centralStack = new QStackedWidget (this);
    centralStack->addWidget (editor);
    setCentralWidget (centralStack);
    statusBar()->addWidget (new QWidget (this), 1);   // messages show on the left; the details are under the editor
    cpuLabel = new QLabel (this);
    cpuLabel->setContentsMargins (0, 0, 12, 0);
    cpuLabel->setToolTip (tr ("CPU: what PHI uses of the processor, all its threads together (100% is one core, as top shows it).\n"
                              "Audio: how much of each audio block's time the sound takes to make; near 100% it drops out."));
    statusBar()->addPermanentWidget (cpuLabel);
    connect (&cpuTimer, &QTimer::timeout, this, &MainWindow::updateCpuMeter);
    statusBar()->addPermanentWidget (new QLabel (tr ("Opacity"), this));
    statusBar()->addPermanentWidget (makeOpacitySlider (this));

    buildPanels();
    registerCommands();
    commands.load();
    buildMenus();
    fillNyquistMenus();
    transportBar = new TransportBar (commands, editor);
    editorLayout->insertWidget (0, transportBar);
    connect (transportBar, &TransportBar::tempoEdited, this, [this] (double bpm)
    {
        if (data() == nullptr || refuseAlbumEdit())
            return;
        auto grid = doc.current().grid;
        grid.bpm = bpm;
        grid.normalise();
        applyGrid (tr ("Set Tempo"), grid);
        waveform->setFocus();
    });
    restoreLayout();
    buildNavigation();

    connect (waveform, &WaveformView::cursorRequested, this, [this] (Frame f) { setCursor (waveform->snapToGrid (f)); });
    connect (waveform, &WaveformView::selectionMoved, this, &MainWindow::moveSelection);
    connect (timeRuler, &TimeRuler::cursorRequested, this, &MainWindow::setCursor);
    connect (albumView, &AlbumView::cursorRequested, this, &MainWindow::setCursor);
    connect (albumView, &AlbumView::propertiesRequested, this, &MainWindow::songProperties);
    buildLiveEdits();
    connect (overview, &Overview::cursorRequested, this, &MainWindow::setCursor);
    connect (overview, &Overview::viewRequested, this, [this] (double start, double length) { waveform->setView (start, length); });
    connect (waveform, &WaveformView::selectionRequested, this, [this] (Frame anchor, Frame head)
    {
        selectRange (snapToZero (anchor), snapToZero (head));
    });
    connect (waveform, &WaveformView::rangeRequested, this, [this] (Frame from, Frame to)
    {
        selectRange (from, to);
        if (const auto d = data(); d != nullptr && to > from)
            statusBar()->showMessage (tr ("Selected %1 between the markers").arg (formatTime ((double) (to - from) / d->sampleRate())), 3000);
    });
    connect (waveform, &WaveformView::bandRequested, this, &MainWindow::setBand);
    connect (waveform, &WaveformView::shapeDrawn, this, &MainWindow::setShape);
    connect (&loadWatcher, &QFutureWatcher<LoadResult>::finished, this, &MainWindow::loadFinished);

    auto* uiTimer = new QTimer (this);
    connect (uiTimer, &QTimer::timeout, this, [this]
    {
        engine.dispatchMessages();

        if (engine.isPlaying())
        {
            cursor = engine.heardPositionFrames();
            waveform->setCursorFrame (cursor);
            followingPlayback = true;
        }
        else if (followingPlayback)
        {
            // Playback ran to the end of the file: the cursor lagged by the delay, so finish the trip.
            followingPlayback = false;
            if (const auto d = data(); d != nullptr && engine.positionFrames() >= d->length())
            {
                cursor = d->length();
                waveform->setCursorFrame (cursor);
            }
        }

        if (loading && loadProgress)
            waveform->setLoading (true, loadProgress->load());

        meterPanel->addBlocks (engine.takeMeterBlocks());
        scopeLeft.clear();
        scopeRight.clear();
        engine.takeScopeSamples (scopeLeft, scopeRight);
        if (goniometer->isVisible())
        {
            goniometer->setSampleRate (engine.deviceSampleRate());
            goniometer->addSamples (scopeLeft, scopeRight);
        }
        if (analysisWin != nullptr && analysisWin->isVisible())
        {
            analysisWin->addSamples (scopeLeft, scopeRight, engine.deviceSampleRate());
            feedAnalysisAmbisonics();
            analysisWin->tick();
        }
        feedAmbiScene();
        feedVisualizers();
        if (trackerWin != nullptr && trackerWin->isVisible())
            trackerWin->refresh();

        // Looping plays the selection, or the whole file when nothing is selected.
        const auto d = data();
        if (! previewName.isEmpty())
            engine.setLoop (previewRange.first, previewRange.second);
        else if (looping && d != nullptr)
            engine.setLoop (hasSelection() ? selStart : 0, hasSelection() ? selEnd : d->length());
        else
            engine.setLoop (0, 0);
        transportBar->setState (cursor, d != nullptr ? d->sampleRate() : 48000.0, engine.isPlaying(), looping);
        const auto [viewStart, viewLength] = waveform->view();
        timeRuler->setView (viewStart, d != nullptr ? viewLength : 0.0, d != nullptr ? d->sampleRate() : 48000.0,
                            d != nullptr ? d->length() : 0, cursor, selStart, selEnd);
        if (overview->isVisible())
            overview->setState (d, viewStart, viewLength, cursor, selStart, selEnd);
        placeGainHud();
        placeReferenceBadge();
        if (albumView->isVisible())
            albumView->setView (viewStart, d != nullptr ? viewLength : 0.0, cursor);

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
    performanceTimer.stop();
    performance = nullptr;       // closes a recording or stream while the engine still plays
    loadWatcher.waitForFinished();
    engine.setLiveSource ({});   // the Tracker goes before the engine
    engine.setInstrument ({});   // and the Sampler, Slicer and Mixer
    if (tracker != nullptr)
        tracker->setLinkSink (nullptr);   // its linked notes go to the patchbay
    delete patchBayWin;
    patchBayWin = nullptr;
    patchBay = nullptr;          // its synths and effects, and it lets go of the mixer
    delete mixerWin;             // before the mixer's engine and its instruments
    mixerWin = nullptr;
    controlTimer.stop();
    pushDisplay = nullptr;
    surfaces = nullptr;          // dark, and back to their own modes
    midiInputs = nullptr;
    delete batchWindow;   // stops its workers while the engine they render through still exists
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
    add ("file.export", Category::file, tr ("&Export..."), { KS (Qt::CTRL | Qt::ALT | Qt::Key_X) }, [this] { exportAudio(); });
    add ("file.metadata", Category::file, tr ("&Metadata..."), { KS (Qt::CTRL | Qt::ALT | Qt::Key_M) }, [this] { editMetadata(); });
    // Closes what has the keyboard: a panel, the mixer, or else the file.
    add ("file.close", Category::file, tr ("&Close"), { KS (Qt::CTRL | Qt::Key_W), KS (Qt::META | Qt::Key_W) }, [this] { closeFocused(); });
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
    add ("process.gainUp", Category::process, tr ("Gain &Up 0.5 dB"), { KS (Qt::CTRL | Qt::SHIFT | Qt::Key_Up) }, [this] { nudgeGain (0.5); });
    add ("process.gainDown", Category::process, tr ("Gain D&own 0.5 dB"), { KS (Qt::CTRL | Qt::SHIFT | Qt::Key_Down) }, [this] { nudgeGain (-0.5); });
    add ("process.normalize", Category::process, tr ("&Normalize..."), { KS (Qt::CTRL | Qt::SHIFT | Qt::Key_N) },
         [this] { normalize(); });
    add ("process.fadeIn", Category::process, tr ("Fade &In"), { KS (Qt::CTRL | Qt::SHIFT | Qt::Key_I) }, [this] { fade (true); });
    add ("process.fadeOut", Category::process, tr ("Fade &Out"), { KS (Qt::CTRL | Qt::SHIFT | Qt::Key_O) }, [this] { fade (false); });
    add ("process.reverse", Category::process, tr ("&Reverse"), { KS (Qt::Key_R), KS (Qt::CTRL | Qt::SHIFT | Qt::Key_R) },
         [this] { simpleProcess (tr ("Reverse"), &dsp::reverse); });
    add ("process.invert", Category::process, tr ("In&vert Polarity"), { KS (Qt::CTRL | Qt::ALT | Qt::Key_I) },
         [this] { simpleProcess (tr ("Invert"), &dsp::invert); });
    add ("process.stereoize", Category::process, tr ("&Stereoize..."), { KS (Qt::CTRL | Qt::ALT | Qt::Key_W) }, [this] { stereoize(); });
    add ("process.midSide", Category::process, tr ("&Mid/Side Width..."), { KS (Qt::ALT | Qt::SHIFT | Qt::Key_W) }, [this] { midSideWidth(); });
    commands.action (QStringLiteral ("process.midSide"))->setStatusTip (tr ("Widen or narrow the stereo image: M/S widener, mono bass, side air, rotation, stereo width"));
    add ("spatial.binaural", Category::spatial, tr ("&Binaural..."), { KS (Qt::CTRL | Qt::ALT | Qt::SHIFT | Qt::Key_B) }, [this] { binauralRender(); });
    commands.action (QStringLiteral ("spatial.binaural"))->setStatusTip (tr ("Place the sound around your head for headphones: HRTF, 3D, spatial audio, immersive"));
    add ("spatial.layout", Category::spatial, tr ("Channel &Layout..."), { KS (Qt::CTRL | Qt::ALT | Qt::SHIFT | Qt::Key_L) }, [this] { chooseChannelLayout(); });
    commands.action (QStringLiteral ("spatial.layout"))->setStatusTip (tr ("What the channels are: 5.1, 5.2 two subwoofers, 7.2.4, 9.1.6, quad, ambisonics, surround, Atmos-style beds"));
    add ("spatial.studio", Category::spatial, tr ("Ambisonic &Studio..."), { KS (Qt::CTRL | Qt::ALT | Qt::SHIFT | Qt::Key_H) }, [this] { ambiStudio(); });
    commands.action (QStringLiteral ("spatial.studio"))->setStatusTip (tr ("Envelop for Live (E4L) style rack: source, stereo, quad, multi panners, spinner, spatial delays, AuraVerb, B-format convolution reverb, HOA transform, slicer, master bus decoder"));
    add ("spatial.speakerArrays", Category::spatial, tr ("Spea&ker Arrays..."), {}, [this] { speakerArrays(); });
    commands.action (QStringLiteral ("spatial.speakerArrays"))->setStatusTip (tr ("Your own speaker array, up to 16 channels, for the ambisonic decoders: ring, dome, studio, custom layout"));
    add ("spatial.encode", Category::spatial, tr ("&Encode to Ambisonics..."), { KS (Qt::CTRL | Qt::ALT | Qt::SHIFT | Qt::Key_E) }, [this] { ambiEncode(); });
    commands.action (QStringLiteral ("spatial.encode"))->setStatusTip (tr ("Place a mono or stereo sound in an ambisonic scene: AmbiX, B-format, 360, VR, spatial audio"));
    add ("spatial.rotate", Category::spatial, tr ("&Rotate / Mirror Scene..."), { KS (Qt::CTRL | Qt::ALT | Qt::SHIFT | Qt::Key_Y) }, [this] { ambiRotate(); });
    commands.action (QStringLiteral ("spatial.rotate"))->setStatusTip (tr ("Turn or flip an ambisonic scene: yaw, pitch, roll, head tracking, 360 video"));
    add ("spatial.decode", Category::spatial, tr ("&Decode Ambisonics..."), { KS (Qt::CTRL | Qt::ALT | Qt::SHIFT | Qt::Key_D) }, [this] { ambiDecode(); });
    commands.action (QStringLiteral ("spatial.decode"))->setStatusTip (tr ("Render an ambisonic scene to headphones (binaural) or speakers: stereo, 5.1, 7.1.4, 9.1.6"));
    add ("spatial.fumaToAmbix", Category::spatial, tr ("&FuMa to AmbiX"), {}, [this] { ambiConvertFuma (false); });
    commands.action (QStringLiteral ("spatial.fumaToAmbix"))->setStatusTip (tr ("Convert an old-style (FuMa, WXYZ) ambisonic file to AmbiX"));
    add ("spatial.ambixToFuma", Category::spatial, tr ("AmbiX to F&uMa"), {}, [this] { ambiConvertFuma (true); });
    commands.action (QStringLiteral ("spatial.ambixToFuma"))->setStatusTip (tr ("Convert an AmbiX file to old-style FuMa (WXYZ) for older tools"));
    add ("view.ambiScene", Category::spatial, tr ("Ambisonic &Scene"), { KS (Qt::CTRL | Qt::ALT | Qt::SHIFT | Qt::Key_A) },
         [this] { ambiScenePanel->toggle(); lastPanel = ambiScenePanel; });
    commands.action (QStringLiteral ("view.ambiScene"))->setStatusTip (tr ("Where the sound is in an ambisonic file: sphere, energy map, direction"));
    add ("process.convert", Category::process, tr ("&Convert Sample Type..."), { KS (Qt::CTRL | Qt::ALT | Qt::Key_T) },
         [this] { convertSampleType(); });
    add ("repair.autoHeal", Category::repair, tr ("&Auto Heal"), { KS (Qt::CTRL | Qt::Key_U) }, [this] { autoHeal(); });
    add ("repair.spectralGain", Category::repair, tr ("Spectral &Gain..."), { KS (Qt::CTRL | Qt::ALT | Qt::Key_G) },
         [this] { spectralGain(); });
    add ("repair.learnNoise", Category::repair, tr ("&Learn Noise Profile"), { KS (Qt::CTRL | Qt::ALT | Qt::Key_N) },
         [this] { learnNoise(); });
    add ("repair.noiseReduction", Category::repair, tr ("&Noise Reduction..."), { KS (Qt::CTRL | Qt::ALT | Qt::SHIFT | Qt::Key_N) },
         [this] { noiseReduction(); });
    add ("repair.correctPitch", Category::repair, tr ("Correct &Pitch..."), { KS (Qt::CTRL | Qt::ALT | Qt::SHIFT | Qt::Key_P) },
         [this] { correctPitch(); });
    add ("repair.declick", Category::repair, tr ("De&Click..."), {}, [this] { declick(); });
    add ("repair.declip", Category::repair, tr ("DeCli&p..."), {}, [this] { declip(); });
    add ("repair.dehum", Category::repair, tr ("De&Hum..."), {}, [this] { dehum(); });
    add ("repair.deess", Category::repair, tr ("De&Ess..."), {}, [this] { deess(); });
    add ("repair.frequencyRange", Category::repair, tr ("&Frequency Range..."), { KS (Qt::CTRL | Qt::ALT | Qt::Key_F) },
         [this] { chooseFrequencyRange(); });
    add ("repair.allFrequencies", Category::repair, tr ("A&ll Frequencies"), { KS (Qt::CTRL | Qt::ALT | Qt::SHIFT | Qt::Key_F) },
         [this] { setBand (0.0, 0.0); });
    add ("repair.toolBox", Category::repair, tr ("&Box Selection Tool"), { KS (Qt::Key_V) },
         [this] { chooseTool (WaveformView::Tool::box); })->setCheckable (true);
    add ("repair.toolLasso", Category::repair, tr ("Lass&o Tool"), { KS (Qt::Key_O) },
         [this] { chooseTool (WaveformView::Tool::lasso); })->setCheckable (true);
    add ("repair.toolBrush", Category::repair, tr ("B&rush Tool"), { KS (Qt::Key_B) },
         [this] { chooseTool (WaveformView::Tool::brush); })->setCheckable (true);
    add ("repair.toolSpotHeal", Category::repair, tr ("Spot &Healing Brush"), { KS (Qt::Key_H) },
         [this] { chooseTool (WaveformView::Tool::spotHeal); })->setCheckable (true);
    auto* tools = new QActionGroup (this);
    for (auto id : { "repair.toolBox", "repair.toolLasso", "repair.toolBrush", "repair.toolSpotHeal" })
        tools->addAction (commands.action (QString::fromLatin1 (id)));
    commands.action (QStringLiteral ("repair.toolBox"))->setChecked (true);
    add ("repair.brushSmaller", Category::repair, tr ("Smaller Brush"), { KS (Qt::Key_BracketLeft) },
         [this] { waveform->setBrushSize ((int) std::lround (waveform->brushSize() / 1.25)); });
    add ("repair.brushLarger", Category::repair, tr ("Larger Brush"), { KS (Qt::Key_BracketRight) },
         [this] { waveform->setBrushSize ((int) std::lround (waveform->brushSize() * 1.25) + 1); });
    add ("repair.addHarmonics", Category::repair, tr ("Add Har&monics"), { KS (Qt::CTRL | Qt::ALT | Qt::Key_H) },
         [this] { addHarmonics(); });
    add ("view.diagnostics", Category::repair, tr ("&Diagnostics Panel"), { KS (Qt::CTRL | Qt::SHIFT | Qt::Key_D) },
         [this] { diagnosticsPanel->toggle(); lastPanel = diagnosticsPanel; });
    add ("repair.findSimilar", Category::repair, tr ("F&ind Similar Sounds..."), { KS (Qt::CTRL | Qt::SHIFT | Qt::Key_F) },
         [this] { findSimilar(); });
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
        const auto kind = info.second;
        auto* a = add (id, Category::process, info.first, {}, [this, kind] { fadeShape = kind; });
        a->setCheckable (true);
        a->setChecked (kind == fadeShape);
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
    add ("analyze.imagingView", Category::analyze, tr ("Next &Imaging View"), { KS (Qt::CTRL | Qt::ALT | Qt::Key_V) }, [this]
    {
        if (! metersPanel->isShown())
            metersPanel->show (false);
        goniometer->nextView();
        statusBar()->showMessage (tr ("Imaging: %1").arg (Goniometer::viewName (goniometer->view())), 3000);
    });
    add ("analyze.resetMeters", Category::analyze, tr ("&Reset Meters"), { KS (Qt::CTRL | Qt::ALT | Qt::Key_R) },
         [this]
    {
        meterPanel->reset();
        goniometer->reset();
        if (analysisWin != nullptr)
            analysisWin->reset();
    });
    add ("analyze.window", Category::analyze, tr ("Analysis &Meters Window"), { KS (Qt::CTRL | Qt::ALT | Qt::Key_A) }, [this]
    {
        auto* w = analysisWindow();
        if (w->isVisible() && w->isActiveWindow())
            w->close();
        else
        {
            w->show();
            w->raise();
            w->activateWindow();
        }
    })->setStatusTip (tr ("Every meter in one window: loudness and its history, levels, spectrum, spectrogram, imaging, phase and amplitude statistics"));

    add ("analyze.plugin", Category::analyze, tr ("Plugin &Analyzer..."), { KS (Qt::CTRL | Qt::ALT | Qt::Key_U) }, [this]
    {
        auto* w = pluginAnalyzer();
        w->show();
        w->raise();
        w->activateWindow();
    })->setStatusTip (tr ("What a plugin does to sound: frequency and phase, impulse, harmonics, aliasing, dynamics, modulation, and Forensic \u03a6"));

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
    add ("master.render", Category::master, tr ("&Render in Place"), { KS (Qt::CTRL | Qt::SHIFT | Qt::Key_Return) }, [this] { renderInPlace(); });
    add ("master.renderNew", Category::master, tr ("Render to &New Tab"), { KS (Qt::CTRL | Qt::ALT | Qt::SHIFT | Qt::Key_Return) }, [this] { renderToNewTab(); });
    add ("master.assistant", Category::master, tr ("Master &Assistant..."), { KS (Qt::ALT | Qt::SHIFT | Qt::Key_M) }, [this] { masterAssistant(); })
        ->setStatusTip (tr ("Tunes the plugins in the Master Section towards a reference track or a loudness target"));
    add ("master.scan", Category::master, tr ("&Scan Plugins"), { KS (Qt::CTRL | Qt::ALT | Qt::SHIFT | Qt::Key_S) },
         [this] { masterWidget->scanPlugins(); });

    add ("generate.tone", Category::effect, tr ("&Tone..."), { KS (Qt::CTRL | Qt::SHIFT | Qt::Key_G) }, [this] { generateSignal (false); });
    add ("generate.noise", Category::effect, tr ("&Noise..."), { KS (Qt::CTRL | Qt::ALT | Qt::SHIFT | Qt::Key_G) }, [this] { generateSignal (true); });
    add ("effect.repeat", Category::effect, tr ("&Repeat Last Effect"), { KS (Qt::CTRL | Qt::SHIFT | Qt::Key_Y) }, [this]
    {
        if (lastEffectId.isEmpty())
            statusBar()->showMessage (tr ("No Nyquist effect has run yet"), 4000);
        else
            runNyquistCommand (lastEffectId, true);
    });
    add ("tools.batch", Category::tools, tr ("&Batch Processor..."), { KS (Qt::CTRL | Qt::Key_B) }, [this] { showBatch(); });
    add ("tools.nyquistPrompt", Category::tools, tr ("&Nyquist Prompt..."), { KS (Qt::CTRL | Qt::ALT | Qt::Key_Y) }, [this] { nyquistPrompt(); });
    add ("tools.nyquistFolder", Category::tools, tr ("Open Nyquist Plug-ins &Folder"), {}, [this]
    {
        QDesktopServices::openUrl (QUrl::fromLocalFile (nyquistui::userFolder()));
        statusBar()->showMessage (tr ("Put .ny files here, then choose Rescan Nyquist Plug-ins"), 8000);
    });
    add ("tools.nyquistRescan", Category::tools, tr ("&Rescan Nyquist Plug-ins"), {}, [this] { rescanNyquist(); });
    registerNyquistCommands();
    registerAlbumCommands();
    registerAnalysisCommands();
    registerWarpCommands();
    registerVisualizerCommands();
    registerSonifyCommands();
    registerTrackerCommands();
    registerPerformanceCommands();
    registerArchitectonicsCommands();
    registerSamplerCommands();
    registerInstrumentCommands();
    registerMixerCommands();
    registerModeCommands();
    registerControlCommands();
    registerPatchBayCommands();
    registerReferenceCommands();
    registerMacroCommands();
    add ("tools.managePlugins", Category::tools, tr ("Manage &Plug-ins..."), {}, [this] { managePlugins(); });
    add ("tools.resetConfig", Category::tools, tr ("Reset &Configuration..."), {}, [this] { resetConfiguration(); });

    add ("transport.playStop", Category::transport, tr ("&Play / Stop"), { KS (Qt::Key_Space) }, [this] { togglePlayback(); });
    add ("transport.stop", Category::transport, tr ("&Stop"), { KS (Qt::Key_K) }, [this]
    {
        // Space pauses where it is; Stop goes back to where playback started.
        const bool wasPlaying = engine.isPlaying();
        engine.stop();
        if (wasPlaying)
            setCursor (playStart);
    });
    add ("transport.loop", Category::transport, tr ("&Loop Playback"), { KS (Qt::CTRL | Qt::Key_L) }, [this]
    {
        looping = commands.action (QStringLiteral ("transport.loop"))->isChecked();
        statusBar()->showMessage (looping ? tr ("Loop on: playing the selection (or the whole file) over and over") : tr ("Loop off"), 3000);
    })->setCheckable (true);
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
    add ("view.overview", Category::view, tr ("Whole-File &Overview"), { KS (Qt::CTRL | Qt::ALT | Qt::Key_O) }, [this]
    {
        const bool on = commands.action (QStringLiteral ("view.overview"))->isChecked();
        overview->setVisible (on);
        appSettings().setValue (QStringLiteral ("view/overview"), on);
    });
    commands.action (QStringLiteral ("view.overview"))->setCheckable (true);
    commands.action (QStringLiteral ("view.overview"))->setChecked (! overview->isHidden());
    add ("view.gainKnob", Category::view, tr ("&Gain Knob"), { KS (Qt::CTRL | Qt::ALT | Qt::Key_K) }, [this]
    {
        appSettings().setValue (QStringLiteral ("view/gainKnob"), commands.action (QStringLiteral ("view.gainKnob"))->isChecked());
        placeGainHud();
    });
    commands.action (QStringLiteral ("view.gainKnob"))->setCheckable (true);
    commands.action (QStringLiteral ("view.gainKnob"))->setChecked (appSettings().value (QStringLiteral ("view/gainKnob"), true).toBool());
    add ("view.cpuMeter", Category::view, tr ("&CPU Meter"), {}, [this]
    {
        const auto on = commands.action (QStringLiteral ("view.cpuMeter"))->isChecked();
        appSettings().setValue (QStringLiteral ("view/cpuMeter"), on);
        cpuLabel->setVisible (on);
        if (on)
        {
            cpuLastTicks = -1;
            updateCpuMeter();
            cpuTimer.start (1000);
        }
        else
            cpuTimer.stop();
    });
    commands.action (QStringLiteral ("view.cpuMeter"))->setCheckable (true);
    commands.action (QStringLiteral ("view.cpuMeter"))->setStatusTip (tr ("Show PHI's processor use and the audio load in the status bar"));
    commands.action (QStringLiteral ("view.cpuMeter"))->setChecked (! appSettings().value (QStringLiteral ("view/cpuMeter"), true).toBool());
    commands.action (QStringLiteral ("view.cpuMeter"))->trigger();   // shows it (or not) as last time
    add ("view.levelColors", Category::view, tr ("Colour Waveform by &Level"), { KS (Qt::CTRL | Qt::ALT | Qt::SHIFT | Qt::Key_W) }, [this]
    {
        const auto on = commands.action (QStringLiteral ("view.levelColors"))->isChecked();
        appSettings().setValue (QStringLiteral ("view/levelColors"), on);
        waveform->setLevelColours (on);
        statusBar()->showMessage (on ? tr ("Waveform coloured by level: blue when quiet, through green and yellow, to red at 0 dB. "
                                           "Change the colours in Options > Waveform and Meter Colors.")
                                     : tr ("Waveform in one colour (Options > Waveform and Meter Colors)"), 6000);
    });
    commands.action (QStringLiteral ("view.levelColors"))->setCheckable (true);
    commands.action (QStringLiteral ("view.levelColors"))->setChecked (appSettings().value (QStringLiteral ("view/levelColors"), false).toBool());
    waveform->setLevelColours (commands.action (QStringLiteral ("view.levelColors"))->isChecked());
    add ("edit.snapZero", Category::edit, tr ("Snap to &Zero Crossings"), { KS (Qt::CTRL | Qt::ALT | Qt::Key_Z) }, [this]
    {
        const auto on = commands.action (QStringLiteral ("edit.snapZero"))->isChecked();
        appSettings().setValue (QStringLiteral ("edit/snapZero"), on);
        statusBar()->showMessage (on ? tr ("Selection edges snap to the nearest zero crossing, so cuts and pastes do not click")
                                     : tr ("Selection edges go exactly where you put them"), 5000);
    });
    commands.action (QStringLiteral ("edit.snapZero"))->setCheckable (true);
    commands.action (QStringLiteral ("edit.snapZero"))->setChecked (appSettings().value (QStringLiteral ("edit/snapZero"), false).toBool());
    add ("edit.snapGrid", Category::edit, tr ("Snap to &Grid"), { KS (Qt::CTRL | Qt::Key_4) }, [this]
    {
        const auto on = commands.action (QStringLiteral ("edit.snapGrid"))->isChecked();
        appSettings().setValue (QStringLiteral ("edit/snapGrid"), on);
        waveform->setSnapToGrid (on);
        statusBar()->showMessage (on ? tr ("Clicks, selections and dragged selections snap to the beat grid (when the file has a tempo)")
                                     : tr ("Snap to Grid off"), 4000);
    });
    commands.action (QStringLiteral ("edit.snapGrid"))->setCheckable (true);
    commands.action (QStringLiteral ("edit.snapGrid"))->setChecked (appSettings().value (QStringLiteral ("edit/snapGrid"), true).toBool());
    waveform->setSnapToGrid (commands.action (QStringLiteral ("edit.snapGrid"))->isChecked());
    add ("edit.split", Category::edit, tr ("S&plit"), { KS (Qt::CTRL | Qt::Key_E) }, [this] { splitAtCursor(); });
    commands.action (QStringLiteral ("edit.split"))->setStatusTip (tr ("Put a slice marker at the cursor, or at both ends of the selection; double-click between them selects a slice"));
    add ("edit.narrowGrid", Category::edit, tr ("&Narrow Grid"), { KS (Qt::CTRL | Qt::Key_1) }, [this] { waveform->changeGridDensity (-1); showGridStep(); });
    add ("edit.widenGrid", Category::edit, tr ("&Widen Grid"), { KS (Qt::CTRL | Qt::Key_2) }, [this] { waveform->changeGridDensity (1); showGridStep(); });
    add ("edit.duplicate", Category::edit, tr ("D&uplicate"), { KS (Qt::CTRL | Qt::Key_D) }, [this] { duplicateSelection(); });
    commands.action (QStringLiteral ("edit.duplicate"))->setStatusTip (tr ("Copy the selection to right after it, over what is there, and select the copy"));
    add ("view.zoomFit", Category::view, tr ("Zoom to &Fit"), { KS (Qt::CTRL | Qt::Key_0) }, [this] { waveform->zoomToFit(); });
    add ("view.zoomSelection", Category::view, tr ("Zoom to &Selection"), { KS (Qt::Key_Z) },
         [this] { if (requireSelection()) waveform->zoomToSelection(); });
    add ("view.chain", Category::master, tr ("Device &Chain"), { KS (Qt::CTRL | Qt::SHIFT | Qt::Key_C) },
         [this] { chainPanel->toggle(); });
    add ("view.find", Category::view, tr ("&Find..."), { KS (Qt::CTRL | Qt::Key_F) }, [this]
    {
        // The mixer in the editor's place: the Finder and the Inspector take turns.
        if (mixerDocked() && centralStack->currentWidget() == mixerWin)
        {
            if (mixerWin->sideFinderShown() && findPanel->isShown() && findWidget->searchIsReady())
            {
                mixerWin->showSideFinder (false);
                mixerWin->setFocus (Qt::ShortcutFocusReason);
                return;
            }
            mixerWin->showSideFinder (true);
            findWidget->focusSearch();
            QTimer::singleShot (0, findWidget, [this] { findWidget->focusSearch(); });
            lastPanel = findPanel;
            return;
        }
        // Ctrl+F gets the search ready to type over; pressed again it hides the panel.
        if (findWidget->searchIsReady())
        {
            findPanel->hide();
            waveform->setFocus (Qt::ShortcutFocusReason);
            return;
        }
        findPanel->show (true);
        findWidget->focusSearch();
        // A dock that was hidden takes the keyboard only once it is laid out.
        QTimer::singleShot (0, findWidget, [this] { findWidget->focusSearch(); });
        lastPanel = findPanel;
    });
    add ("view.history", Category::view, tr ("&History Panel"), { KS (Qt::CTRL | Qt::Key_H) },
         [this] { historyPanel->toggle(); lastPanel = historyPanel; });
    add ("view.markers", Category::view, tr ("&Markers Panel"), { KS (Qt::CTRL | Qt::SHIFT | Qt::Key_M) },
         [this] { markerPanel->toggle(); lastPanel = markerPanel; });
    add ("view.meters", Category::view, tr ("&Level Meters"), { KS (Qt::CTRL | Qt::SHIFT | Qt::Key_L) },
         [this] { metersPanel->toggle(); lastPanel = metersPanel; });
    add ("view.popOut", Category::view, tr ("&Pop Out / Dock Panel"), { KS (Qt::CTRL | Qt::ALT | Qt::Key_P) }, [this]
    {
        if (auto* f = QApplication::focusWidget(); mixerWin != nullptr && f != nullptr && (f == mixerWin || mixerWin->isAncestorOf (f)))
            placeMixer (! mixerDocked());
        else if (auto* p = focusedOrLastPanel())
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
    // The blend between the waveform and the spectrogram, a tenth at a time.
    const auto blendBy = [this] (double step)
    {
        const bool wasSpectral = waveform->isSpectral();
        waveform->setBlend (waveform->blend() + step);
        if (wasSpectral && ! waveform->isSpectral())
            setBand (0.0, 0.0);
        commands.action (QStringLiteral ("view.spectral"))->setChecked (waveform->isSpectral());
        const auto percent = (int) std::lround (waveform->blend() * 100.0);
        statusBar()->showMessage (percent == 0 ? tr ("Waveform") : percent == 100 ? tr ("Spectrogram")
                                               : tr ("Blend: %1% spectrogram, %2% waveform").arg (percent).arg (100 - percent), 2500);
    };
    add ("view.blendSpectral", Category::view, tr ("Blend Towards Spectro&gram"), { KS (Qt::ALT | Qt::SHIFT | Qt::Key_Up) },
         [blendBy] { blendBy (0.1); });
    add ("view.blendWaveform", Category::view, tr ("Blend Towards &Waveform"), { KS (Qt::ALT | Qt::SHIFT | Qt::Key_Down) },
         [blendBy] { blendBy (-0.1); });
    add ("view.pitchLine", Category::view, tr ("&Pitch Line"), { KS (Qt::SHIFT | Qt::Key_P) }, [this]
    {
        waveform->setPitchLine (! waveform->pitchLine());
        if (waveform->pitchLine() && ! waveform->isSpectral())
            commands.action (QStringLiteral ("view.spectral"))->trigger();   // the line is drawn over the spectrogram
        commands.action (QStringLiteral ("view.pitchLine"))->setChecked (waveform->pitchLine());
    })->setCheckable (true);
    add ("view.spectrogramSettings", Category::view, tr ("Spectrogram Se&ttings..."), { KS (Qt::ALT | Qt::SHIFT | Qt::Key_D) }, [this]
    {
        if (! waveform->isSpectral())
            commands.action (QStringLiteral ("view.spectral"))->trigger();
        if (spectrogramDialog == nullptr)
        {
            spectrogramDialog = new SpectrogramDialog (*waveform, this);
            connect (spectrogramDialog, &SpectrogramDialog::changed, this, [this]
            {
                appSettings().setValue (QStringLiteral ("view/spectrum"), QString::fromStdString (spectrum::toString (waveform->spectrumSettings())));
            });
        }
        spectrogramDialog->refresh();
        spectrogramDialog->show();
        spectrogramDialog->raise();
        spectrogramDialog->activateWindow();
    });
    // Vertical zoom: the waveform's height (amplitude), or the spectrogram's frequencies.
    add ("view.verticalZoomIn", Category::view, tr ("Zoom Vertically I&n"), { KS (Qt::ALT | Qt::Key_Equal), KS (Qt::ALT | Qt::Key_Plus) },
         [this] { waveform->zoomVertically (1.5); });
    add ("view.verticalZoomOut", Category::view, tr ("Zoom Vertically O&ut"), { KS (Qt::ALT | Qt::Key_Minus) },
         [this] { waveform->zoomVertically (1.0 / 1.5); });
    add ("view.verticalZoomReset", Category::view, tr ("Reset Vertical Zoo&m"), { KS (Qt::ALT | Qt::Key_0) },
         [this] { waveform->resetVerticalZoom(); });
    add ("view.opacityUp", Category::view, tr ("More &Opaque"),
         { KS (Qt::CTRL | Qt::ALT | Qt::Key_Equal), KS (Qt::CTRL | Qt::ALT | Qt::Key_Plus), KS (Qt::CTRL | Qt::ALT | Qt::KeypadModifier | Qt::Key_Plus) },
         [this] { changeOpacity (Theme::opacity() + 0.05, true); });
    add ("view.opacityDown", Category::view, tr ("More &Transparent"), { KS (Qt::CTRL | Qt::ALT | Qt::Key_Minus), KS (Qt::CTRL | Qt::ALT | Qt::Key_Underscore),
                                                                               KS (Qt::CTRL | Qt::ALT | Qt::KeypadModifier | Qt::Key_Minus) },
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

    add ("view.focusWaveform", Category::view, tr ("Focus &Waveform"), { KS (Qt::CTRL | Qt::Key_F6) }, [this]
    {
        showEditorPage();
        activateWindow();
        waveform->setFocus();
    });
    // F6 and Shift+F6 (and Alt tapped) go round the waveform and every panel that is showing.
    add ("view.nextPanel", Category::view, tr ("&Next Panel"), { KS (Qt::Key_F6) }, [this] { focusPanel (1); });
    add ("view.previousPanel", Category::view, tr ("Pre&vious Panel"), { KS (Qt::SHIFT | Qt::Key_F6) }, [this] { focusPanel (-1); });
    commands.action (QStringLiteral ("view.nextPanel"))->setStatusTip (tr ("Or tap Alt. Hold Alt (or this key) and press an arrow key to go to the panel that way"));
    add ("view.fullScreen", Category::view, tr ("F&ull Screen"), { KS (Qt::META | Qt::Key_F) }, [this]
    {
        auto* f = QApplication::focusWidget();
        toggleFullScreen (f != nullptr ? f->window() : this);
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
    add ("options.interfaceMixer", Category::options, tr ("Audio &Interface Mixer"), { KS (Qt::CTRL | Qt::ALT | Qt::SHIFT | Qt::Key_I) },
         [this] { openInterfaceMixer(); });
    commands.action (QStringLiteral ("options.interfaceMixer"))->setStatusTip (tr ("Open your interface's routing and mixer: alsa-scarlett-gui, Focusrite Scarlett, Clarett"));
    add ("options.interfaceMixerProgram", Category::options, tr ("Audio Interface Mixer &Program..."), {}, [this] { chooseInterfaceMixer(); });
    add ("options.keymap", Category::options, tr ("&Keyboard Shortcuts..."), { KS (Qt::CTRL | Qt::SHIFT | Qt::Key_K) },
         [this] { KeymapDialog (commands, this).exec(); });
    add ("options.waveformColors", Category::options, tr ("&Waveform and Meter Colors..."), { KS (Qt::CTRL | Qt::SHIFT | Qt::Key_W) },
         [this] { WaveformColorsDialog (this).exec(); });
    add ("options.theme", Category::options, tr ("&Theme..."), { KS (Qt::CTRL | Qt::SHIFT | Qt::Key_T) },
         [this] { ThemeDialog (this).exec(); });

    add ("help.palette", Category::help, tr ("&Command Palette..."), { KS (Qt::CTRL | Qt::SHIFT | Qt::Key_P), KS (Qt::Key_F1) },
         [this] { CommandPalette (commands, this).exec(); });
    add ("help.menuBar", Category::help, tr ("Open &Menu Bar"), { KS (Qt::Key_F10) }, [this] { openMenuBar(); });
    add ("help.about", Category::help, tr ("&About " APP_DISPLAY_NAME), {}, [this]
    {
        QMessageBox about (QMessageBox::NoIcon, tr ("About " APP_DISPLAY_NAME),
            tr ("<b>" APP_BRAND_NAME " " APP_VERSION "</b><p>Audio editor and mastering suite for Linux.</p>"
                "<p>Free software under the GNU Affero General Public License v3.</p>"
                "<p>The Φ mark is PHI's own, drawn from the golden ratio.</p>"
                "<p>Press Ctrl+Shift+P or F1 to search every command and shortcut.</p>"),
            QMessageBox::Ok, this);
        about.setIconPixmap (phimark::pixmap (96, phimark::Style::goldOnBlack, devicePixelRatioF()));
        about.exec();
    });
}

void MainWindow::buildMenus()
{
    // Menu titles carry & mnemonics: Alt + the first letter where it is free, otherwise the next
    // clearest one. File, Edit and Process lead, then the rest A to Z (but Spatial before Sample
    // and Tracker before Tools), with Help last (owner, 2026-10-27). A "#name" entry is one of
    // the submenus below; each is the first item of its menu.
    const QMap<QString, QPair<QString, QStringList>> submenus {
        { QStringLiteral ("#album"), { tr ("A&lbum"),   { "album.new", "album.addSongs", "-", "album.previousSong", "album.nextSong", "-",
                               "album.moveEarlier", "album.moveLater", "album.longerPause", "album.shorterPause", "-",
                               "album.songProperties", "album.openSong", "album.removeSong", "-",
                               "album.properties", "album.twoTracks", "album.export" } } },
        { QStringLiteral ("#effect"), { tr ("&Effect"),    { "effect.repeat", "-", "@nyquist.generate", "-", "@nyquist.effect" } } },
        { QStringLiteral ("#markers"), { tr ("Mar&kers"),   { "marker.add", "marker.addRegion", "-", "marker.next", "marker.previous", "-", "marker.exportSlices", "warp.exportLoop", "marker.exportLabels", "-", "view.markers" } } },
        { QStringLiteral ("#transport"), { tr ("Tr&ansport"), { "transport.playStop", "transport.stop", "transport.loop", "-", "transport.start", "transport.end",
                               "cursor.back", "cursor.forward", "cursor.backLarge", "cursor.forwardLarge" } } },
    };

    const QList<QPair<QString, QStringList>> layout {
        { tr ("&File"),      { "file.open", "album.new", "file.record", "-", "file.save", "file.saveAs", "file.export", "-", "file.metadata", "-", "file.close", "file.nextTab", "file.previousTab", "-", "file.quit" } },
        { tr ("&Edit"),      { "edit.undo", "edit.redo", "-", "view.find", "-", "edit.cut", "edit.copy", "edit.copyToNew", "edit.paste", "edit.delete", "-",
                               "edit.trim", "edit.insertSilence", "edit.split", "edit.duplicate", "-", "edit.snapZero", "edit.snapGrid", "edit.narrowGrid", "edit.widenGrid", "-", "select.all", "select.none", "-",
                               "select.back", "select.forward", "select.backLarge", "select.forwardLarge",
                               "select.toStart", "select.toEnd" } },
        { tr ("&Process"),   { "#effect", "-", "process.gain", "process.gainUp", "process.gainDown", "process.normalize", "-", "process.fadeIn", "process.fadeOut", "-",
                               "process.fadeShape.linear", "process.fadeShape.equalPower", "process.fadeShape.exponential",
                               "process.fadeShape.sCurve", "-", "process.reverse", "process.invert", "process.dcOffset", "-", "process.stereoize", "process.midSide", "process.convert" } },
        { tr ("&Analyze"),   { "analyze.loudness", "-", "analyze.beats", "analyze.onsets", "analyze.tempoKey", "analyze.vamp", "-", "analyze.window", "view.meters", "analyze.imagingView", "analyze.resetMeters", "-", "analyze.plugin", "-", "view.spectral", "-", "@nyquist.analyze" } },
        { tr ("Architecto&nics"), { "architectonics.compose", "architectonics.score", "architectonics.design", "-", "architectonics.play", "architectonics.deal", "architectonics.endless" } },
        { tr ("&Master"),    { "#album", "-", "view.master", "view.chain", "master.addPlugin", "master.bypass", "analyze.plugin", "-", "master.assistant", "-", "master.render", "master.renderNew", "-", "reference.toggle", "reference.next", "reference.add", "reference.match", "view.references", "-", "master.scan" } },
        { tr ("Mi&xer"),     { "mixer.window", "mixer.dock", "mixer.patchbay", "-", "mixer.new", "mixer.newFromSet", "mixer.open", "mixer.save", "mixer.saveAs", "-", "mixer.addTrack", "mixer.removeTrack", "-",
                               "mixer.playStop", "mixer.mixdown", "mixer.sendToLauncher", "mixer.sendToTimeline", "-", "mixer.editorTrack", "mixer.trackToEditor", "-", "mixer.assistant" } },
        { tr ("Mo&de"),      { "@mode" } },
        { tr ("&Options"),   { "options.audio", "options.separateOutputs", "options.interfaceMixer", "options.interfaceMixerProgram", "-", "options.keymap", "options.theme", "options.waveformColors" } },
        { tr ("Performan&ce"), { "performance.launcher", "-", "control.surfaces", "control.learn", "-", "performance.record", "performance.stream", "-", "performance.settings", "performance.folder" } },
        { tr ("&Repair"),    { "view.diagnostics", "-", "repair.autoHeal", "repair.spectralGain", "repair.findSimilar", "repair.correctPitch", "-", "repair.learnNoise", "repair.noiseReduction", "-",
                               "repair.declick", "repair.declip", "repair.dehum", "repair.deess", "-",
                               "repair.toolBox", "repair.toolLasso", "repair.toolBrush", "repair.toolSpotHeal",
                               "repair.brushSmaller", "repair.brushLarger", "repair.addHarmonics", "-",
                               "repair.frequencyRange", "repair.allFrequencies", "-", "view.spectral" } },
        { tr ("Spat&ial"),   { "spatial.binaural", "process.midSide", "process.stereoize", "-", "spatial.layout", "-", "spatial.studio", "-", "spatial.encode", "spatial.rotate", "spatial.decode", "spatial.speakerArrays", "spatial.fumaToAmbix", "spatial.ambixToFuma", "-", "view.ambiScene", "analyze.imagingView" } },
        { tr ("&Sample"),    { "sample.send", "sample.window", "sample.refresh", "-", "slice.send", "slice.window", "slice.refresh", "-",
                               "instrument.granular", "instrument.retroSampler", "-", "sample.midiInputs", "control.surfaces", "control.learn", "sample.panic" } },
        { tr ("S&ynth"),     { "instrument.wavetable", "instrument.modular", "instrument.additive", "-",
                               "instrument.subtractive1", "instrument.subtractive2", "instrument.fm", "-",
                               "sample.midiInputs", "sample.panic" } },
        { tr ("Trac&ker"),   { "tracker.window", "tracker.furnace", "-", "tracker.new", "tracker.open", "tracker.save", "tracker.saveAs", "tracker.loadSample", "-", "tracker.playSong", "tracker.playPattern", "tracker.stop", "-",
                               "tracker.recordStereo", "tracker.recordMulti", "-", "tracker.resetTracks", "tracker.about" } },
        { tr ("&Tools"),     { "#markers", "-", "tools.macroRecord", "tools.macroApply", "tools.macros", "-", "tools.batch", "-", "tools.managePlugins", "-", "tools.nyquistPrompt", "-", "@nyquist.tools", "-", "tools.nyquistFolder", "tools.nyquistRescan", "-", "tools.resetConfig" } },
        { tr ("&View"),      { "#transport", "-", "view.zoomIn", "view.zoomOut", "view.zoomFit", "view.zoomSelection", "-",
                               "view.overview", "view.rulerTime", "view.rulerBeats", "view.rulerBoth", "view.spectral", "view.blendSpectral", "view.blendWaveform", "view.pitchLine", "view.spectrogramSettings", "view.verticalZoomIn", "view.verticalZoomOut", "view.verticalZoomReset", "view.levelColors", "view.gainKnob", "view.cpuMeter", "-", "view.nextPanel", "view.previousPanel", "view.find", "view.master", "view.chain", "view.history", "view.markers", "view.meters", "analyze.window", "view.diagnostics", "view.references", "view.ambiScene", "view.popOut", "view.resetLayout", "view.fullScreen", "-",
                               "opacity", "-", "view.focusWaveform", "view.focusFollowsMouse" } },
        { tr ("Visuali&zer"), { "view.visualizer", "view.visualizerFullScreen", "view.visualizerBehind", "-",
                               "view.visualizerNext", "view.visualizerPrevious", "view.visualizerChoose", "-",
                               "@visual.artistic", "@visual.science", "-", "@visual.milkdrop", "@visual.rain", "@visual.web", "@visual.sonify", "-",
                               "view.opacityUp", "view.opacityDown", "view.opacityFull" } },
        { tr ("&Warp"),      { "warp.showGrid", "view.rulerTime", "view.rulerBeats", "view.rulerBoth", "-", "warp.detectGrid", "warp.autoDetect", "warp.setTempo", "warp.downbeat", "warp.halve", "warp.double", "-",
                               "warp.edit", "warp.previousHit", "warp.nextHit", "warp.toggleMarker", "warp.nudgeEarlier", "warp.nudgeLater", "-",
                               "warp.quantizePanel", "warp.quantize", "warp.markersFromHits", "warp.sliceAtHits", "warp.resetWarp", "-", "warp.stretch", "warp.halfSpeed", "warp.doubleSpeed", "warp.extremeStretch", "warp.extremeAgain", "-", "warp.exportLoop" } },
        { tr ("&Help"),      { "help.palette", "help.menuBar", "-", "help.about" } },
    };

    // 20 menus (Architectonics added 2026-10-29: the owner found room on a 13" screen; 23 before Album, Effect, Markers and Transport became submenus; owner,
    // 2026-10-26: "what happens when we load PHI on a small screen"): Fusion puts 7 px between titles, and at 1280 px wide with a 10 pt font the last ones went behind
    // the » button. 2 px keeps them all in view there (1204 px at 10 pt DejaVu Sans).
    struct TightMenuBar : QProxyStyle
    {
        using QProxyStyle::QProxyStyle;
        int pixelMetric (PixelMetric metric, const QStyleOption* option, const QWidget* widget) const override
        {
            return metric == PM_MenuBarItemSpacing ? 2 : QProxyStyle::pixelMetric (metric, option, widget);
        }
    };
    auto* tight = new TightMenuBar (QStyleFactory::create (QStringLiteral ("Fusion")));
    tight->setParent (menuBar());
    menuBar()->setStyle (tight);

    std::function<void (QMenu*, const QStringList&)> fill = [&] (QMenu* menu, const QStringList& ids)
    {
        for (const auto& id : ids)
        {
            if (id == QLatin1String ("-"))
                menu->addSeparator();
            else if (id.startsWith (QLatin1Char ('#')))
            {
                const auto [subTitle, subIds] = submenus.value (id);
                fill (menu->addMenu (subTitle), subIds);
            }
            else if (id.startsWith (QLatin1String ("@visual.")))
                addVisualizerSubmenu (menu, id.mid (8));
            else if (id == QLatin1String ("@mode"))
            {
                // Built as it opens, so its ticks follow the mode chosen and the mixer's state.
                connect (menu, &QMenu::aboutToShow, this, [this, menu] { fillModeMenu (menu); });
                fillModeMenu (menu);
            }
            else if (id == QLatin1String ("@nyquist.generate"))
                generateMenu = menu->addMenu (tr ("&Generate"));
            else if (id.startsWith (QLatin1Char ('@')))
            {
                // Nyquist plug-ins are listed before this hidden marker (fillNyquistMenus).
                auto* anchor = menu->addAction (QString());
                anchor->setVisible (false);
                nyquistAnchors[id] = anchor;
            }
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
    };

    for (const auto& [title, ids] : layout)
        fill (menuBar()->addMenu (title), ids);
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
    PluginWindow::setOpacityForAll (Theme::opacity());   // plugin windows open at the saved opacity
    return slider;
}

void MainWindow::focusPanel (int direction)
{
    // The waveform, then every panel in the order they usually sit: below, right, left. A closed
    // panel is opened while F6 is on it and closed again when F6 moves on.
    const auto stops = panelStops();
    const int count = (int) stops.size();

    // Where the keyboard is now; if it is somewhere F6 cannot tell (a panel that takes no keys), where F6 last went.
    // If the focus has not moved since F6 last moved on (the panel takes no keys, or was still opening), F6 goes by where it last went.
    auto* focus = QApplication::focusWidget();
    int at = -1;
    if (focus == nullptr || focus != cycleFrom)
        at = stopWithFocus (stops);
    if (at < 0)
        at = std::clamp (panelStop, 0, count - 1);
    cycleFrom = focus;

    panelStop = (at + direction + count) % count;
    focusStop (stops[(size_t) panelStop]);
}

void MainWindow::changeOpacity (double value, bool announce)
{
    Theme::setOpacity (value);
    PluginWindow::setOpacityForAll (Theme::opacity());
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

    diagnostics = new DiagnosticsPanel (this);
    diagnostics->listWidget()->installEventFilter (new NavigationKeyGuard (waveform, {}, diagnostics->listWidget()));
    connect (diagnostics, &DiagnosticsPanel::scanRequested, this, &MainWindow::scanDiagnostics);
    connect (diagnostics, &DiagnosticsPanel::showRequested, this, &MainWindow::showProblem);
    connect (diagnostics, &DiagnosticsPanel::repairRequested, this, &MainWindow::repairProblems);
    connect (diagnostics, &DiagnosticsPanel::markRequested, this, &MainWindow::markProblems);
    connect (diagnostics, &DiagnosticsPanel::deleteRequested, this, &MainWindow::deleteSilence);

    meterPanel = new MeterPanel (this);

    masterWidget = new MasterPanel (engine, waveform, this);
    connect (masterWidget, &MasterPanel::renderRequested, this, &MainWindow::renderInPlace);
    connect (masterWidget, &MasterPanel::analyzeRequested, this, &MainWindow::analyzeSlot);
    connect (masterWidget, &MasterPanel::message, this, [this] (const QString& text)
    {
        if (! text.isEmpty())
            statusBar()->showMessage (text, 6000);
    });

    // The goniometer sits above the level meters.
    goniometer = new Goniometer (this);
    auto* meters = new QWidget (this);
    meters->setFocusPolicy (Qt::StrongFocus);   // so it can be pointed at or tabbed to like the other panels
    meters->setAccessibleName (tr ("Meters"));
    auto* metersLayout = new QVBoxLayout (meters);
    metersLayout->setContentsMargins (0, 0, 0, 0);
    metersLayout->setSpacing (0);
    metersLayout->addWidget (goniometer, 6);
    metersLayout->addWidget (meterPanel, 5);
    metersPanel = new DockablePanel (*this, QStringLiteral ("metersPanel"), tr ("Meters"), meters, Qt::RightDockWidgetArea);
    markerPanel = new DockablePanel (*this, QStringLiteral ("markersPanel"), tr ("Markers"), markerList, Qt::BottomDockWidgetArea);
    historyPanel = new DockablePanel (*this, QStringLiteral ("historyPanel"), tr ("History"), historyList, Qt::RightDockWidgetArea);
    diagnosticsPanel = new DockablePanel (*this, QStringLiteral ("diagnosticsPanel"), tr ("Diagnostics"), diagnostics, Qt::RightDockWidgetArea);
    masterPanel = new DockablePanel (*this, QStringLiteral ("masterPanel"), tr ("Master Section"), masterWidget, Qt::RightDockWidgetArea);
    chainRow = new ChainRow (engine, waveform, this);
    chainRow->choosePlugin = [this] (int slot)
    {
        masterWidget->choosePlugin (slot);
        return ! engine.masterSlot (slot).empty;
    };
    chainRow->pickPlugin = [this]
    {
        PluginBrowser browser (engine.knownPlugins(), this);
        browser.setWindowTitle (tr ("Choose a Plugin for the Prism"));
        return browser.exec() == QDialog::Accepted ? browser.selectedId() : std::string();
    };
    connect (chainRow, &ChainRow::chainChanged, this, [this]
    {
        masterWidget->refresh();
        commands.action (QStringLiteral ("master.bypass"))->setChecked (engine.isMasterBypassed());
    });
    connect (chainRow, &ChainRow::analyzeRequested, this, &MainWindow::analyzeSlot);
    connect (chainRow, &ChainRow::message, this, [this] (const QString& text) { statusBar()->showMessage (text, 6000); });

    // The chain row spans the whole width along the bottom, under the side panels.
    setCorner (Qt::BottomLeftCorner, Qt::BottomDockWidgetArea);
    setCorner (Qt::BottomRightCorner, Qt::BottomDockWidgetArea);
    chainPanel = new DockablePanel (*this, QStringLiteral ("chainPanel"), tr ("Device Chain"), chainRow, Qt::BottomDockWidgetArea);
    chainPanel->dock()->show();
    // No title bar: the cards say what it is (owner, 2026-10-25). View still shows, hides and pops it out.
    chainPanel->dock()->setTitleBarWidget (new QWidget (chainPanel->dock()));

    findWidget = new FindPanel (waveform, this);
    findWidget->collect = [this] { return findItems(); };
    findWidget->audition = [this] (const QString& path) { auditionFile (path); };
    findWidget->stopAudition = [this] { stopAudition(); };
    connect (&auditionWatch, &QTimer::timeout, this, &MainWindow::auditionTick);
    // With the clip launcher in front, a file chosen in the Selector goes into the next empty
    // slot of the track named and coloured like the Selector colour it was found under.
    findWidget->sendFile = [this] (const QString& path, int colour)
    {
        if (mixerWin == nullptr || ! mixerWin->isVisible() || ! mixerWin->launcherShown())
            return false;
        const auto set = findWidget->currentSet();
        mixerWin->sendToLauncher (path, colour >= 0 ? findWidget->markName (set, colour) : QString(),
                                  colour >= 0 ? findWidget->markColour (set, colour) : QColor());
        return true;
    };
    // The Φ random selector: into a new scene when the clip launcher is shown, else onto the
    // mixer's tracks (the mixer opens for it).
    findWidget->phiDeal = [this] (const QStringList& files, const QStringList& instruments, const QStringList& names,
                                  const QList<QColor>& colours, FindPanel::DealTo to, bool again)
    {
        const bool launcherUp = mixerWin != nullptr && mixerWin->isVisible() && mixerWin->launcherShown();
        const auto launcher = to == FindPanel::DealTo::launcher || (to == FindPanel::DealTo::automatic && launcherUp);
        showMixer();
        if (launcher && ! mixerWin->launcherShown())
            mixerWin->showLauncher (true);
        if (! launcher && mixerWin->launcherShown())
            mixerWin->showLauncher (false);
        mixerWin->phiDeal (files, instruments, names, colours, launcher, again);
    };
    findWidget->phiCompose = [this] { composeSongDialog (false, true); };
    // A colour's favourite instruments: instrument plugins, and the stock instruments of the
    // Sample and Synth menus (found among them by their file).
    findWidget->instrumentOf = [this] (const QString& key) -> QString
    {
        const auto known = engine.knownInstruments();
        if (key.startsWith (QStringLiteral ("plugin:")))
        {
            const auto id = key.mid (7).toStdString();
            for (const auto& p : known)
                if (p.id == id)
                    return QString::fromStdString (p.id);
            return {};
        }
        if (! key.startsWith (QStringLiteral ("command:instrument.")))
            return {};
        const auto* info = stock::instrument (key.mid (19).toLatin1().constData());
        if (info == nullptr || info->file == nullptr)
            return {};
        for (const auto& p : known)
            if (QString::fromStdString (p.file).contains (QString::fromUtf8 (info->file)))
                return QString::fromStdString (p.id);
        return {};
    };
    // A Prism of effects: one of each colour's favourite effects on a ray of its own, named
    // for the colour, in parallel and each turned down so together they are as loud as one.
    findWidget->phiPrism = [this] (const QStringList& effects, const QStringList& names, const QList<QColor>&)
    {
        int slot = -1;
        for (int i = 0; i < AudioEngine::numMasterSlots && slot < 0; ++i)
            if (engine.masterSlot (i).empty)
                slot = i;
        std::vector<std::pair<QString, QString>> rays;   // name, effect
        for (int c = 0; c < (int) std::min (effects.size(), names.size()); ++c)
            if (! effects[c].isEmpty())
                rays.emplace_back (names[c], effects[c]);
        if (rays.empty())
        {
            statusBar()->showMessage (tr ("Φ: no effects to deal. Mark effects with the Selector's colours first (1 to 8 on a plugin in the Find panel)."), 8000);
            return;
        }
        if (slot < 0)
        {
            statusBar()->showMessage (tr ("All %1 Master Section slots are in use: remove a plugin first").arg (AudioEngine::numMasterSlots), 6000);
            return;
        }

        QApplication::setOverrideCursor (Qt::WaitCursor);
        auto error = engine.loadMasterPlugin (slot, "phinyx:prism");
        const ChainPath prism { slot, {} };
        QStringList failed;
        if (error.empty())
        {
            engine.prismRename (prism, QStringLiteral ("Φ Prism").toStdString());
            const auto gainDb = -20.0 * std::log10 ((double) rays.size());
            for (int r = 0; r < (int) rays.size(); ++r)
            {
                if (r >= (int) engine.prismInfo (prism).rays.size() && engine.prismAddRay (prism) < 0)
                    break;
                engine.prismRenameRay (prism, r, rays[(size_t) r].first.toStdString());
                engine.prismSetRayGain (prism, r, gainDb);
                if (const auto why = engine.prismAddDevice (prism, r, 0, rays[(size_t) r].second.toStdString()); ! why.empty())
                    failed << QString::fromStdString (why);
            }
        }
        QApplication::restoreOverrideCursor();
        masterWidget->refresh();
        chainRow->rebuild();
        if (! error.empty())
            QMessageBox::warning (this, tr ("Φ Prism"), QString::fromStdString (error));
        else
            statusBar()->showMessage (failed.isEmpty() ? tr ("Φ dealt a Prism of %n effect(s) into slot %1", nullptr, (int) rays.size()).arg (slot + 1)
                                                       : failed.join (QStringLiteral ("; ")), 8000);
    };
    connect (findWidget, &FindPanel::openRequested, this, [this] (const QString& path)
    {
        // With an album in front, a file chosen in the Find panel joins it as a song.
        if (album() != nullptr && ! albumfile::isAlbumFile (path))
            addSongFiles ({ path });
        else
            openFile (path);
    });
    findPanel = new DockablePanel (*this, QStringLiteral ("findPanel"), tr ("Find"), findWidget, Qt::LeftDockWidgetArea);

    buildReferences();
    ambiSceneView = new AmbiScene (this);
    ambiSceneView->setFocusPolicy (Qt::StrongFocus);
    ambiScenePanel = new DockablePanel (*this, QStringLiteral ("ambiScenePanel"), tr ("Ambisonic Scene"), ambiSceneView, Qt::RightDockWidgetArea);
    panels = { masterPanel, chainPanel, metersPanel, markerPanel, historyPanel, diagnosticsPanel, findPanel, referencesPanel, ambiScenePanel };
    buildQuantizePanel();

    new DockCornerDrag (*this);   // drag where three or four panels meet to resize both ways
    focusFollowsMouse = new FocusFollowsMouse (this);
    focusFollowsMouse->setAreas ({ waveform, chainRow, masterWidget, markerList, historyList, diagnostics, findWidget, referencePanel, ambiSceneView,
                                   metersPanel->content(), (QWidget*) quantizeWidget });
    qApp->installEventFilter (focusFollowsMouse);
}

QList<FindItem> MainWindow::findItems()
{
    QList<FindItem> list;

    // Every command, with its menu and shortcut; Nyquist ones are the Effects.
    for (const auto& c : commands.all())
    {
        if (c.id == QStringLiteral ("view.find") || c.action == nullptr)
            continue;
        FindItem item;
        item.kind = c.id.startsWith (QStringLiteral ("nyquist.")) ? FindItem::Kind::effect
                  : c.id.startsWith (QStringLiteral ("instrument.")) ? FindItem::Kind::instrument : FindItem::Kind::process;
        item.key = QStringLiteral ("command:") + c.id;
        item.name = c.name();
        item.detail = c.shortcutText();
        item.group = c.category;
        item.searchText = c.action->statusTip();
        if (c.id.startsWith (QStringLiteral ("visualizer.")))
        {
            // The visualizations, browsable: moving onto one shows it if a visualizer is open.
            const int mode = VisualizerView::modeIndex (c.id.mid (11));
            item.kind = FindItem::Kind::visual;
            item.name = VisualizerView::tr (VisualizerView::modes()[(size_t) std::max (0, mode)].name);
            item.group = VisualizerView::modes()[(size_t) std::max (0, mode)].science ? tr ("Science") : tr ("Artistic");
            item.detail = item.group;
            item.searchText += tr (" visualizer visualization");
            item.preview = [this, mode] { if (visualizerShowing()) setVisualMode (mode, false); };
        }
        if (item.kind == FindItem::Kind::instrument)
        {
            // Named by what it does; found by its own name and maker too.
            const auto* info = stock::instrument (c.id.mid (11).toLatin1().constData());
            item.name = info != nullptr ? tr (info->function) : c.name();
            item.detail = info != nullptr ? QString::fromUtf8 (info->original) : QString();
            item.group = c.category == tr ("Sample") ? tr ("Samplers") : tr ("Synths");
            item.searchText += tr (" instrument");
        }
        item.enabled = c.action->isEnabled();
        item.activate = [this, id = c.id]
        {
            auto* a = commands.action (id);
            if (a != nullptr && a->isEnabled())
                a->trigger();
            else
                statusBar()->showMessage (tr ("Not available right now (open a file or make a selection first)"), 5000);
        };
        list.push_back (item);
    }

    // Plugins load into the first empty Master Section slot.
    for (const auto& p : engine.knownPlugins())
    {
        FindItem item;
        item.kind = FindItem::Kind::plugin;
        item.key = QStringLiteral ("plugin:") + QString::fromStdString (p.id);
        item.name = QString::fromStdString (p.name);
        const auto builtIn = p.format == builtin::formatName;
        const auto maker = QString::fromStdString (p.manufacturer);
        item.group = builtIn ? QStringLiteral ("Built-in") : maker.isEmpty() ? tr ("Unknown maker") : maker;
        item.detail = QString::fromStdString (p.format);
        item.searchText = maker + QLatin1Char (' ') + QString::fromStdString (p.category);
        item.activate = [this, id = p.id, name = item.name]
        {
            int slot = -1;
            for (int i = 0; i < AudioEngine::numMasterSlots && slot < 0; ++i)
                if (engine.masterSlot (i).empty)
                    slot = i;
            if (slot < 0)
            {
                statusBar()->showMessage (tr ("All %1 Master Section slots are in use: remove a plugin first").arg (AudioEngine::numMasterSlots), 6000);
                return;
            }

            QApplication::setOverrideCursor (Qt::WaitCursor);
            const auto error = engine.loadMasterPlugin (slot, id);
            QApplication::restoreOverrideCursor();
            masterWidget->refresh();
            chainRow->rebuild();
            if (! error.empty())
                QMessageBox::warning (this, tr ("Plugin"), QString::fromStdString (error));
            else
                statusBar()->showMessage (tr ("Loaded %1 in slot %2").arg (name).arg (slot + 1), 6000);
        };
        list.push_back (item);
    }

#if PHI_HAVE_PROJECTM
    // MilkDrop presets, by category, browsable like the views.
    for (const auto& folder : MilkDrop::presetFolders())
    {
        QDirIterator it (folder, { QStringLiteral ("*.milk") }, QDir::Files, QDirIterator::Subdirectories);
        while (it.hasNext())
        {
            const auto path = it.next();
            const auto category = MilkDrop::categoryOf (path);
            FindItem item;
            item.kind = FindItem::Kind::visual;
            item.key = QStringLiteral ("milkdrop:") + path;
            item.name = QFileInfo (path).completeBaseName();
            item.group = category.isEmpty() ? tr ("MilkDrop") : tr ("MilkDrop: %1").arg (category);
            item.detail = tr ("MilkDrop");
            item.searchText = category + tr (" milkdrop preset visualizer visualization");
            item.activate = [this, path] { showMilkDropPreset (path); };
            item.preview = [this, path] { if (visualizerShowing()) showMilkDropPreset (path); };
            list.push_back (item);
        }
    }
#endif

    // Vamp analysis plugins: running one adds markers or regions (Analysis.cpp).
    for (const auto& o : vamphost::outputs())
    {
        FindItem item;
        item.kind = FindItem::Kind::effect;
        item.key = QStringLiteral ("vamp:") + QString::fromStdString (o.id());
        item.name = QStringLiteral ("%1: %2").arg (QString::fromStdString (o.pluginName), QString::fromStdString (o.outputName));
        item.group = tr ("Vamp analysis");
        item.detail = QString::fromStdString (o.maker);
        item.searchText = QString::fromStdString (o.category + " " + o.description) + tr (" analyze analysis vamp");
        item.enabled = doc.isOpen() && ! doc.isAlbum() && ! loading;
        item.activate = [this, id = QString::fromStdString (o.id())]
        {
            if (data() == nullptr || doc.isAlbum())
                statusBar()->showMessage (tr ("Open a file first: analysis runs on the selection or the whole file"), 5000);
            else
                runVampOutput (id);
        };
        list.push_back (item);
    }

    return list;
}

DockablePanel* MainWindow::focusedOrLastPanel() const
{
    for (auto* p : panels)
        if (p->containsFocus())
            return p;

    return lastPanel != nullptr && lastPanel->isShown() ? lastPanel : nullptr;
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
    waveform->setBlend (settings.value (QStringLiteral ("view/blend"), spectral ? 1.0 : 0.0).toDouble());
    waveform->setSpectrumSettings (spectrum::fromString (settings.value (QStringLiteral ("view/spectrum")).toString().toStdString()));
    commands.action (QStringLiteral ("view.spectral"))->setChecked (waveform->isSpectral());
    waveform->setPitchLine (settings.value (QStringLiteral ("view/pitchLine"), false).toBool());
    commands.action (QStringLiteral ("view.pitchLine"))->setChecked (waveform->pitchLine());

    // Load plugins and pop panels out again once the main window is on screen, so it opens first.
    QTimer::singleShot (0, this, [this]
    {
        // Dock layouts restore reliably only once the window exists on screen.
        defaultLayout = saveState (layoutVersion);
        restoreState (appSettings().value (QStringLiteral ("window/state")).toByteArray(), layoutVersion);
        // A tiling window manager (Hyprland and the like) sizes the window after it maps, and Qt
        // squeezes the docks to match; put each one back to the size it had when the app closed.
        restoreDockSizes();
        QTimer::singleShot (500, this, &MainWindow::restoreDockSizes);

        const auto problems = QString::fromStdString (engine.restoreMasterSection());
        masterWidget->refresh();
        commands.action (QStringLiteral ("master.bypass"))->setChecked (engine.isMasterBypassed());
        if (! problems.isEmpty())
            QMessageBox::warning (this, tr ("Master Section"), tr ("Some plugins could not be loaded:\n%1").arg (problems));

        auto s = appSettings();
        for (auto* p : panels)
        {
            // Simplest and the Slicer want more room than a dock leaves them: their own window to start with.
            const bool window = p == samplerPanel || p == slicerPanel;
            p->setOpensInWindow (s.value (QStringLiteral ("panels/%1/window").arg (p->id()), window).toBool());
            // (one left showing in its dock by an older layout moves to its window too)
            if (s.value (QStringLiteral ("panels/%1/poppedOut").arg (p->id()), false).toBool()
                || (p->opensInWindow() && p->dock()->isVisible()))
                p->setPoppedOut (true);
        }

        activateWindow();
        waveform->setFocus();
        QTimer::singleShot (1500, this, &MainWindow::warnAboutDesktopShortcuts);
        QTimer::singleShot (0, this, &MainWindow::recoverRecordings);
        QTimer::singleShot (0, this, &MainWindow::recoverDocuments);
    });

    // Unsaved files are copied to the recovery folder every minute
    // (recovery/autosaveSeconds in settings.ini; 0 turns it off).
    const auto seconds = settings.value (QStringLiteral ("recovery/autosaveSeconds"), 60).toInt();
    connect (&autosaveTimer, &QTimer::timeout, this, &MainWindow::autosave);
    if (seconds > 0)
        autosaveTimer.start (std::max (5, seconds) * 1000);
}

void MainWindow::restoreDockSizes()
{
    auto settings = appSettings();
    for (auto* p : panels)
    {
        auto* d = p->dock();
        if (p->isPoppedOut() || d->isFloating() || ! d->isVisible())
            continue;
        // With nothing saved yet, a dock much narrower than its content wants gets that width.
        auto saved = settings.value (QStringLiteral ("panels/%1/size").arg (p->id())).toSize();
        if (! saved.isValid())
        {
            const auto hint = p->content()->sizeHint().expandedTo (QSize (220, 180));
            saved = QSize (std::max (d->width(), d->width() < hint.width() * 3 / 4 ? hint.width() : 0),
                           std::max (d->height(), d->height() < 80 ? hint.height() : 0));
        }
        const auto area = dockWidgetArea (d);
        if (area == Qt::LeftDockWidgetArea || area == Qt::RightDockWidgetArea)
        {
            if (d->width() != saved.width())
                resizeDocks ({ d }, { saved.width() }, Qt::Horizontal);
        }
        else if (area == Qt::TopDockWidgetArea || area == Qt::BottomDockWidgetArea)
        {
            if (d->height() != saved.height())
                resizeDocks ({ d }, { saved.height() }, Qt::Vertical);
        }
    }
}

void MainWindow::saveLayout()
{
    auto settings = appSettings();
    if (chainHiddenForMixer)
        chainPanel->dock()->show();   // saved as the user left it, not as the mixer hid it

    for (auto* p : panels)
    {
        const bool popped = p->isPoppedOut() && p->isShown();
        settings.setValue (QStringLiteral ("panels/%1/poppedOut").arg (p->id()), popped);
        settings.setValue (QStringLiteral ("panels/%1/window").arg (p->id()), p->opensInWindow());
        if (p->isPoppedOut())
            p->hide();  // docks it back so the app can exit
        else if (p->dock()->isVisible() && ! p->dock()->isFloating())
            settings.setValue (QStringLiteral ("panels/%1/size").arg (p->id()), p->dock()->size());
        else
            settings.remove (QStringLiteral ("panels/%1/size").arg (p->id()));
    }

    settings.setValue (QStringLiteral ("window/geometry"), saveGeometry());
    settings.setValue (QStringLiteral ("window/state"), saveState (layoutVersion));
    settings.setValue (QStringLiteral ("view/spectral"), waveform->isSpectral());
    settings.setValue (QStringLiteral ("view/blend"), waveform->blend());
    settings.setValue (QStringLiteral ("view/pitchLine"), waveform->pitchLine());
    settings.setValue (QStringLiteral ("view/focusFollowsMouse"), focusFollowsMouse->isEnabled());
    settings.setValue (QStringLiteral ("audio/separateOutputs"), engine.monitorMode() == AudioEngine::MonitorMode::separateOutputs);
    engine.saveMasterSection();
}

void MainWindow::updateCpuMeter()
{
    // The process's CPU time (every thread) against the time passed, from /proc like top.
    long long ticks = -1;
    if (QFile stat (QStringLiteral ("/proc/self/stat")); stat.open (QIODevice::ReadOnly))
    {
        const auto line = QString::fromLatin1 (stat.readAll());
        const auto fields = line.mid (line.lastIndexOf (QLatin1Char (')')) + 2).split (QLatin1Char (' '));
        if (fields.size() > 12)
            ticks = fields[11].toLongLong() + fields[12].toLongLong();   // utime + stime
    }
    QString cpu = QStringLiteral ("–");
    if (ticks >= 0 && cpuLastTicks >= 0 && cpuWatch.isValid() && cpuWatch.elapsed() > 0)
    {
        static const auto perSecond = (double) std::max (1L, sysconf (_SC_CLK_TCK));
        const auto percent = 100.0 * (double) (ticks - cpuLastTicks) / perSecond / ((double) cpuWatch.elapsed() / 1000.0);
        cpu = QStringLiteral ("%1%").arg (std::max (0.0, percent), 0, 'f', 0);
    }
    cpuLastTicks = ticks;
    cpuWatch.restart();

    const auto load = engine.audioLoad();
    const auto audio = load < 0.0 ? QStringLiteral ("–") : QStringLiteral ("%1%").arg (100.0 * load, 0, 'f', 0);
    const bool busy = load >= 0.8;
    cpuLabel->setText (busy ? tr ("CPU %1 · <span style=\"color:%3\"><b>Audio %2</b></span>").arg (cpu, audio, Theme::current().color (QStringLiteral ("meterHigh")).name())
                            : tr ("CPU %1 · Audio %2").arg (cpu, audio));
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

    for (const auto* id : { "file.save", "file.saveAs", "file.export", "file.metadata", "edit.insertSilence", "edit.copyToNew", "select.all", "select.none",
                            "select.back", "select.forward", "select.backLarge", "select.forwardLarge",
                            "select.toStart", "select.toEnd", "process.gain", "process.normalize",
                            "process.reverse", "process.invert", "process.dcOffset", "process.stereoize", "process.midSide", "process.convert", "spatial.binaural", "spatial.layout", "spatial.studio", "spatial.encode", "spatial.rotate", "spatial.decode", "spatial.fumaToAmbix", "spatial.ambixToFuma",
                            "repair.findSimilar", "repair.addHarmonics",
                            "repair.spectralGain", "repair.frequencyRange", "repair.allFrequencies", "repair.learnNoise",
                            "repair.noiseReduction", "repair.declick", "repair.declip", "repair.dehum", "repair.deess",
                            "transport.playStop", "transport.stop", "transport.start", "transport.end",
                            "cursor.back", "cursor.forward", "cursor.backLarge", "cursor.forwardLarge",
                            "view.zoomIn", "view.zoomOut", "view.zoomFit" })
        commands.action (QString::fromLatin1 (id))->setEnabled (open);

    // Selection commands stay enabled so pressing their key without a selection
    // explains what is missing instead of silently doing nothing.
    for (const auto* id : { "edit.cut", "edit.copy", "edit.delete", "edit.trim", "process.fadeIn", "process.fadeOut", "repair.autoHeal",
                            "repair.correctPitch", "view.zoomSelection" })
        commands.action (QString::fromLatin1 (id))->setEnabled (open);
    Q_UNUSED (sel);

    commands.action (QStringLiteral ("edit.paste"))->setEnabled (open && clipboard != nullptr);
    updateAlbumCommandStates();
    updateAnalysisCommandStates();
    updateWarpCommandStates();
    commands.action (QStringLiteral ("marker.addRegion"))->setEnabled (open);
    commands.action (QStringLiteral ("master.render"))->setEnabled (open);
    commands.action (QStringLiteral ("master.renderNew"))->setEnabled (open);
    commands.action (QStringLiteral ("master.assistant"))->setEnabled (open);

    for (const auto* id : { "marker.add", "marker.next", "marker.previous", "analyze.loudness" })
        commands.action (QString::fromLatin1 (id))->setEnabled (open);

    auto* undoAction = commands.action (QStringLiteral ("edit.undo"));
    auto* redoAction = commands.action (QStringLiteral ("edit.redo"));
    undoAction->setEnabled (open && doc.canUndo());
    redoAction->setEnabled (open && doc.canRedo());
    undoAction->setText (open && doc.canUndo() ? tr ("&Undo %1").arg (QString::fromStdString (doc.undoDescription())) : tr ("&Undo"));
    redoAction->setText (open && doc.canRedo() ? tr ("&Redo %1").arg (QString::fromStdString (doc.redoDescription())) : tr ("&Redo"));

    // An album's audio is its mixdown: it is changed through its songs, not edited.
    if (doc.isAlbum())
        for (const auto* id : { "edit.cut", "edit.paste", "edit.delete", "edit.trim", "edit.insertSilence", "file.metadata",
                                "process.gain", "process.normalize", "process.fadeIn", "process.fadeOut", "process.reverse",
                                "process.invert", "process.dcOffset", "process.stereoize", "process.midSide", "process.convert", "spatial.binaural", "spatial.layout", "spatial.studio", "spatial.encode", "spatial.rotate", "spatial.decode", "spatial.fumaToAmbix", "spatial.ambixToFuma",
                                "repair.autoHeal", "repair.spectralGain", "repair.noiseReduction", "repair.declick",
                                "repair.declip", "repair.dehum", "repair.deess", "repair.addHarmonics", "repair.correctPitch",
                                "marker.add", "marker.addRegion" })
            commands.action (QString::fromLatin1 (id))->setEnabled (false);
}

void MainWindow::updateTitle()
{
    updateTabBar();

    if (! doc.isOpen())
    {
        setWindowTitle (QStringLiteral (APP_BRAND_NAME));
        return;
    }

    const auto name = doc.path().empty() ? (doc.isAlbum() ? tr ("Untitled Album") : tr ("Untitled"))
                                         : QFileInfo (QString::fromStdString (doc.path())).fileName();
    setWindowTitle ((doc.isModified() ? QStringLiteral ("*") : QString()) + name + QStringLiteral (" - " APP_DISPLAY_NAME));
}

//==============================================================================
QString MainWindow::tabName (const AudioDocument& d) const
{
    if (! d.isOpen())
        return tr ("(empty)");

    const auto name = d.path().empty() ? (d.isAlbum() ? tr ("Untitled Album") : tr ("Untitled"))
                                       : QFileInfo (QString::fromStdString (d.path())).fileName();
    return d.isModified() ? name + QStringLiteral (" *") : name;
}

void MainWindow::updateTabBar()
{
    if (tabBar == nullptr)
        return;

    for (int i = 0; i < (int) tabs.size(); ++i)
    {
        const auto& d = i == activeTab ? doc : tabs[(size_t) i].doc;
        const auto& state = tabs[(size_t) i];
        auto text = tabName (d);
        if (state.mixerClip != 0 && d.isOpen() && d.path().empty())
            text = state.mixerName + (d.isModified() ? QStringLiteral (" *") : QString());
        tabBar->setTabText (i, text);
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
    t.shape = shape;
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
    shape = t.shape;
    waveform->setShape (shape);
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
    refreshAlbumSources();
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
    // Fields stay in this order, an empty one keeping its space, so nothing moves.
    QStringList fields;

    if (loading)
        fields << tr ("Loading...");
    else if (auto d = data())
    {
        const auto rate = d->sampleRate();
        fields << QStringLiteral ("%1 / %2").arg (formatTime ((double) cursor / rate), formatTime (d->lengthSeconds()));
        fields << (hasSelection() ? tr ("Selection %1 to %2 (%3)")
                                        .arg (formatTime ((double) selStart / rate), formatTime ((double) selEnd / rate),
                                              formatTime ((double) (selEnd - selStart) / rate))
                                  : QString());

        auto format = QStringLiteral ("%1 Hz, %2").arg ((int) rate).arg (QString::fromStdString (dsp::speakerLayout (d->numChannels(), doc.current().layout).name));
        if (d->numChannels() > 2 && engine.isPlaying() && engine.isDownmixing())
            format += tr (" (heard folded to stereo)");
        fields << format;
        fields << (engine.isPlaying() ? tr ("Playing") : tr ("Stopped"));
        if (hasSelection() && hasBand())
            fields << bandText();
    }
    else
        fields << tr ("No file");

    infoStrip->setFields (fields, QString::fromStdString (engine.deviceDescription()));
}

void MainWindow::refreshFromDocument (bool keepView)
{
    const auto d = data();
    engine.setData (d);
    const auto layoutName = d != nullptr ? doc.current().layout : std::string();
    engine.setLayoutName (layoutName);
    waveform->setData (d, keepView);
    waveform->setLayoutName (layoutName);
    meterPanel->setLayoutName (layoutName);
    waveform->setMarkers (d ? doc.current().markers : std::vector<Marker>());
    refreshGrid();
    albumView->setAlbum (doc.isAlbum() ? doc.current().album : nullptr);
    albumView->setVisible (doc.isAlbum());
    samplerFollow();
    slicerFollow();
    waveform->setFadeHandles (! doc.isAlbum());

    const auto len = d ? d->length() : 0;
    cursor = std::clamp<Frame> (cursor, 0, len);
    selStart = std::clamp<Frame> (selStart, 0, len);
    selEnd = std::clamp<Frame> (selEnd, 0, len);
    waveform->setCursorFrame (cursor);
    waveform->setSelection (selStart, selEnd);
    if (! engine.isPlaying())
        engine.setPositionFrames (cursor);

    markerList->setMarkers (d ? doc.current().markers : std::vector<Marker>(), d ? d->sampleRate() : 48000.0);
    if (diagnosed != nullptr && d.get() != diagnosed)
    {
        diagnosed = nullptr;
        diagnostics->clearProblems (tr ("The audio changed. Scan again."));
    }

    historyList->clear();
    if (doc.isOpen())
    {
        for (const auto& desc : doc.historyDescriptions())
            historyList->addItem (QString::fromStdString (desc));

        historyList->setCurrentRow (doc.historyPosition());
    }

    updateTitle();
    updateCommandStates();
    syncReference();
}

//==============================================================================
void MainWindow::showOpenDialog()
{
    QStringList patterns;
    for (const auto& ext : AudioEngine::readableExtensions())
        patterns << QStringLiteral ("*.") + QString::fromStdString (ext);

    const auto start = doc.path().empty() ? QString() : QFileInfo (QString::fromStdString (doc.path())).absolutePath();
    patterns << QStringLiteral ("*.") + albumfile::extension << QStringLiteral ("*.") + albumfile::oldExtension
             << QStringLiteral ("*.") + MixerWindow::projectSuffix;
    const auto path = QFileDialog::getOpenFileName (this, tr ("Open Audio File"), start,
                                                    tr ("Audio files, albums and projects (%1);;All files (*)").arg (patterns.join (QLatin1Char (' '))));
    if (! path.isEmpty())
        openFile (path);
}

void MainWindow::openFile (const QString& path)
{
    if (loading)
        return;

    if (albumfile::isAlbumFile (path))
    {
        openAlbum (path);
        return;
    }

    if (MixerWindow::isProjectFile (path))
    {
        openProject (path);
        return;
    }

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
        r.data = AudioEngine::loadFile (path.toStdString(), r.markers, r.tags, r.error, progress.get());
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

    doc.reset (r.data, std::move (r.markers), r.path.toStdString(), std::move (r.tags));
    cursor = selStart = selEnd = selAnchor = 0;
    // A file named as ambisonic (AmbiX, FOA, HOA, B-format) with a fitting channel count opens as one.
    {
        const auto name = QFileInfo (r.path).completeBaseName().toLower();
        const int order = ambi::orderFor (r.data->numChannels());
        static const QRegularExpression ambisonicName (QStringLiteral ("ambix|ambisonic|b-?format|(^|[^a-z])(foa|hoa|[123]oa)([^a-z]|$)"));
        if (order >= 1 && order <= 3 && ambisonicName.match (name).hasMatch())
        {
            static const char* names[] { "", "Ambisonics (1st order)", "Ambisonics (2nd order)", "Ambisonics (3rd order)" };
            doc.setLayout (names[order]);
        }
    }
    optionsConfirmedFor.clear();
    if (recoveringFrom.contains (r.path))
    {
        // A recovered copy: it stands for the original file, with unsaved changes.
        doc.setPath (recoveringFrom.take (r.path).toStdString());
        doc.markUnsaved();
        Recovery::remove ({ r.path, r.path.chopped (4) + QStringLiteral (".ini"), {}, {} });
    }
    refreshFromDocument (false);
    waveform->setFocus();
    autoDetectGrid();
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

    // A clip opened from the mixer's tracks: Ctrl+S puts the edit back on its clip and
    // returns to the tracks (owner, 2026-10-25).
    if (tabs[(size_t) activeTab].mixerClip != 0 && mixerWin != nullptr)
    {
        showMixer();   // returns the edits and closes their tabs
        mixerWin->showLauncher (false);
        return true;
    }

    if (doc.isAlbum())
    {
        const auto path = QString::fromStdString (doc.path());
        return QFileInfo (path).suffix().toLower() == albumfile::extension ? saveAlbum (path) : saveAlbumAs();
    }

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
    if (doc.isAlbum())
        return saveAlbumAs();

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
    const auto extension = QFileInfo (path).suffix().toLower();
    saveOptions.metadata = MetadataEditor::writeOptionsFor (extension);

    // A lossy file whose audio is unchanged gets only its tags rewritten, so it
    // is not encoded again (each encoding loses a little more).
    const auto* codecFormat = AudioEngine::codecFormatFor (extension.toStdString());
    const bool lossy = (codecFormat != nullptr && ! codecFormat->lossless) || extension == QLatin1String ("ogg");
    const bool tagsOnly = lossy && doc.audioIsSaved() && QFileInfo (path) == QFileInfo (QString::fromStdString (doc.path()))
                          && QFileInfo::exists (path);

    if (askOptions && ! tagsOnly)
    {
        SaveOptionsDialog dialog (QFileInfo (path).suffix(), saveOptions, this);
        if (dialog.exec() != QDialog::Accepted)
            return false;
    }

    QApplication::setOverrideCursor (Qt::WaitCursor);
    std::string error;
    auto metadata = saveOptions.metadata;
    if (metadata.software.empty())
        metadata.software = APP_DISPLAY_NAME;
    const bool ok = tagsOnly ? AudioEngine::writeTags (path.toStdString(), doc.current().markers, data()->sampleRate(), doc.current().tags, metadata, error)
                             : AudioEngine::saveFile (path.toStdString(), *data(), doc.current().markers, doc.current().tags, saveOptions, error);
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
    statusBar()->showMessage ((tagsOnly ? tr ("Saved the tags of %1 (the audio was not encoded again)") : tr ("Saved %1")).arg (QFileInfo (path).fileName()), 4000);
    return true;
}

//==============================================================================
void MainWindow::commit (const QString& description, AudioDataPtr newData, Frame start, Frame end, Frame inserted)
{
    if (refuseAlbumEdit())
        return;

    if (newData == nullptr)
        return;

    auto state = doc.current();
    state.data = std::move (newData);
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

void MainWindow::recoverDocuments()
{
    const auto found = Recovery::leftBehind();
    if (found.empty())
        return;

    QStringList names;
    for (const auto& f : found)
        names << QStringLiteral ("  ") + (f.name.isEmpty() ? tr ("Untitled") : f.name);

    QMessageBox box (QMessageBox::Question, tr ("Recover Unsaved Files"),
                     tr ("%1 closed without saving these files:\n%2\n\nOpen the copies kept from just before? "
                         "Each opens as unsaved, so you can check it and save.")
                         .arg (QStringLiteral (APP_DISPLAY_NAME), names.join (QLatin1Char ('\n'))),
                     QMessageBox::NoButton, this);
    auto* recover = box.addButton (tr ("&Recover"), QMessageBox::AcceptRole);
    auto* discard = box.addButton (tr ("&Discard"), QMessageBox::DestructiveRole);
    box.addButton (tr ("&Not Now"), QMessageBox::RejectRole);
    box.setDefaultButton (recover);
    box.exec();

    if (box.clickedButton() == discard)
    {
        for (const auto& f : found)
            Recovery::remove (f);
        return;
    }
    if (box.clickedButton() != recover)
        return;

    QStringList queue;
    for (const auto& f : found)
    {
        recoveringFrom.insert (f.audioFile, f.originalPath);
        queue << f.audioFile;
    }

    // Files load one at a time; open the next when the previous has finished.
    auto* opener = new QTimer (this);
    connect (opener, &QTimer::timeout, this, [this, opener, queue] () mutable
    {
        if (loading)
            return;
        if (queue.isEmpty())
            return opener->deleteLater();
        openFile (queue.takeFirst());
    });
    opener->start (100);
}

void MainWindow::autosave()
{
    std::vector<Recovery::Item> items;
    for (int i = 0; i < (int) tabs.size(); ++i)
    {
        const auto& d = i == activeTab ? doc : tabs[(size_t) i].doc;
        if (d.isOpen() && d.isModified() && ! d.isAlbum())   // an album's mixdown is not worth keeping: its songs are on disk
            items.push_back ({ d.id(), d.current(), QString::fromStdString (d.path()), tabName (d).remove (QStringLiteral (" *")) });
    }
    recovery.update (items);

    // The Device Chain too, so a crash does not lose the plugins and their settings.
    engine.saveMasterSection();
    // And the project, so it comes back as it was.
    if (mixerWin != nullptr)
        mixerWin->autosave();
}

void MainWindow::warnAboutDesktopShortcuts()
{
    QStringList clashes;
    for (const auto& c : commands.all())
        for (const auto& key : c.action->shortcuts())
            if (! DesktopShortcuts::owner (key).isEmpty())
                clashes << QStringLiteral ("%1 (%2)").arg (key.toString (QKeySequence::NativeText), c.name());

    if (! clashes.isEmpty())
        statusBar()->showMessage (tr ("Your desktop already uses %n PHI shortcut(s): %1. Change them in Options > Keyboard Shortcuts.",
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
        auto result = repair::spectralGain (*data(), region (selStart, selEnd), repair::removeDb);
        QApplication::restoreOverrideCursor();
        commit (tr ("Delete %1").arg (bandText()), std::move (result), selStart, selEnd, selEnd - selStart);
        return;
    }

    const auto start = selStart, end = selEnd;
    auto result = data()->replaced (start, end, nullptr);
    cursor = selStart = selEnd = selAnchor = start;
    commit (tr ("Delete"), std::move (result), start, end, 0);
}

void MainWindow::trimToSelection()
{
    if (refuseAlbumEdit())
        return;

    if (! requireSelection())
        return;

    const auto d = data();
    const auto start = selStart, end = selEnd;

    auto state = doc.current();
    state.data = d->slice (start, end);
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
    if (refuseAlbumEdit())
        return;

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
    initial.golden = settings.value (QStringLiteral ("stereoize/golden"), initial.golden).toBool();

    StereoizeDialog dialog (initial, ! hasSelection(), d->numChannels(), this);
    if (dialog.exec() != QDialog::Accepted)
        return;

    const auto chosen = dialog.settings();
    settings.setValue (QStringLiteral ("stereoize/width"), chosen.width);
    settings.setValue (QStringLiteral ("stereoize/delayMs"), chosen.delayMs);
    settings.setValue (QStringLiteral ("stereoize/bassHz"), chosen.bassHz);
    settings.setValue (QStringLiteral ("stereoize/golden"), chosen.golden);

    const auto [s, e] = processRange();
    QApplication::setOverrideCursor (Qt::WaitCursor);
    auto result = dsp::stereoize (*d, s, e, chosen);
    QApplication::restoreOverrideCursor();

    // The whole file is replaced (a mono file gains a channel); its length and markers stay the same.
    auto state = doc.current();
    state.data = std::move (result);
    doc.apply (tr ("Stereoize").toStdString(), std::move (state));
    refreshFromDocument (true);
    waveform->setFocus();
}

void MainWindow::editMetadata()
{
    if (refuseAlbumEdit())
        return;

    const auto d = data();
    if (d == nullptr)
        return;

    const auto extension = QFileInfo (QString::fromStdString (doc.path())).suffix();
    MetadataEditor dialog (doc.current().tags, QString::fromStdString (doc.path()), d->sampleRate(), this);
    const auto accepted = dialog.exec() == QDialog::Accepted;
    waveform->setFocus();
    if (! accepted || dialog.tags() == doc.current().tags)
        return;

    auto state = doc.current();
    state.tags = dialog.tags();
    doc.apply (tr ("Edit Metadata").toStdString(), std::move (state));
    refreshFromDocument (true);
    statusBar()->showMessage (tr ("Metadata changed; it is written when you save."), 5000);
}

void MainWindow::convertSampleType()
{
    if (refuseAlbumEdit())
        return;

    const auto d = data();
    if (d == nullptr)
        return;

    const double rates[] { 0, 8000, 11025, 16000, 22050, 32000, 44100, 48000, 88200, 96000, 176400, 192000 };
    QStringList rateNames { tr ("Keep %1 Hz").arg ((int) d->sampleRate()) };
    for (size_t i = 1; i < std::size (rates); ++i)
        rateNames << tr ("%1 Hz").arg ((int) rates[i]);
    const auto channelsNow = QString::fromStdString (dsp::speakerLayout (d->numChannels()).name);
    const int depths[] { 0, 24, 16, 8 };

    // The choices start at "keep" each time rather than at what was picked last.
    for (auto key : { "convert/rate", "convert/channels", "convert/bits" })
        appSettings().remove (QString::fromLatin1 (key));
    ValuesDialog dialog (tr ("Convert Sample Type"),
                         tr ("Changes the whole file's sample rate, channels or bit depth. The rate change uses a high-quality "
                             "filter; a lower bit depth is applied now, so you hear exactly what will be saved."),
                         {
                             { QStringLiteral ("convert/rate"), tr ("Sample &rate:"), 0, 0, 0, 1, 0, {}, {}, rateNames },
                             { QStringLiteral ("convert/channels"), tr ("&Channels:"), 0, 0, 0, 1, 0, {}, {},
                               { tr ("Keep %1").arg (channelsNow), tr ("Mono (mix all channels)"), tr ("Stereo") } },
                             { QStringLiteral ("convert/bits"), tr ("&Bit depth:"), 0, 0, 0, 1, 0, {}, {},
                               { tr ("Keep (32-bit float while editing)"), tr ("24-bit"), tr ("16-bit"), tr ("8-bit") } },
                             { QStringLiteral ("convert/dither"), tr ("&Dither:"), 2, 0, 0, 1, 0, {},
                               tr ("Dither hides the rounding of a lower bit depth as faint hiss; noise shaping moves that hiss "
                                   "where it is hardest to hear"),
                               { tr ("None"), tr ("TPDF"), tr ("TPDF with noise shaping") } },
                         }, this);
    if (dialog.exec() != QDialog::Accepted)
        return;
    waveform->setFocus();

    const auto newRate = rates[std::clamp ((int) dialog.value (0), 0, (int) std::size (rates) - 1)];
    const auto channels = (int) dialog.value (1);
    const auto bits = depths[std::clamp ((int) dialog.value (2), 0, 3)];
    const auto dither = (dsp::Dither) std::clamp ((int) dialog.value (3), 0, 2);
    const auto changeRate = newRate > 0.0 && std::abs (newRate - d->sampleRate()) > 0.5;
    const auto changeChannels = (channels == 1 && d->numChannels() != 1) || (channels == 2 && d->numChannels() != 2);
    if (! changeRate && ! changeChannels && bits == 0)
        return;

    auto result = runRepair (tr ("Convert Sample Type"), [=] (repair::Progress p)
    {
        auto current = d;
        if (changeChannels)
            current = channels == 1 ? dsp::toMono (*current) : dsp::toStereo (dsp::toMono (*current));
        // Each step reports a third of the way.
        auto step = [&p] (int n) { return [&p, n] (float f) { p.report ((n + f) / 3.0f); return ! p.cancelled(); }; };
        if (current != nullptr && changeRate)
            current = dsp::resample (*current, newRate, step (1));
        if (current != nullptr && bits > 0)
            current = dsp::quantize (*current, bits, dither, step (2));
        return current;
    });
    if (result == nullptr)
        return;

    // Markers keep their place in time.
    auto markers = doc.current().markers;
    const auto scale = result->sampleRate() / d->sampleRate();
    for (auto& m : markers)
    {
        m.start = std::llround ((double) m.start * scale);
        m.end = std::llround ((double) m.end * scale);
    }
    cursor = selStart = selEnd = selAnchor = std::llround ((double) cursor * scale);

    QStringList what;
    if (changeRate)
        what << tr ("%1 Hz").arg ((int) newRate);
    if (changeChannels)
        what << (channels == 1 ? tr ("mono") : tr ("stereo"));
    if (bits > 0)
    {
        what << tr ("%1-bit").arg (bits);
        saveOptions.bitDepth = std::max (16, bits);   // saved at that depth (8-bit is kept in 16-bit files)
    }
    auto state = doc.current();
    state.data = std::move (result);
    state.markers = std::move (markers);
    doc.apply (tr ("Convert to %1").arg (what.join (QStringLiteral (", "))).toStdString(), std::move (state));
    refreshFromDocument (false);
    statusBar()->showMessage (tr ("Converted to %1.").arg (what.join (QStringLiteral (", "))), 5000);
}

void MainWindow::setBand (double lowHz, double highHz)
{
    if (shape != nullptr)
    {
        shape.reset();
        waveform->setShape (nullptr);
        updateStatus();
    }
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

QString MainWindow::bandText() const
{
    if (shape != nullptr)
        return tr ("drawn shape, %1 to %2").arg (formatHz (shape->lowestHzSelected()), formatHz (shape->highestHzSelected()));
    return tr ("%1 to %2").arg (formatHz (bandLow), formatHz (bandHigh));
}

void MainWindow::setShape (std::shared_ptr<const repair::SpectralMask> drawn, bool heal)
{
    const auto d = data();
    if (d == nullptr || drawn == nullptr || drawn->empty())
        return;

    setBand (0.0, 0.0);
    shape = std::move (drawn);
    waveform->setShape (shape);
    selAnchor = selStart = cursor = std::clamp<Frame> (shape->start(), 0, d->length());
    selEnd = std::clamp<Frame> (shape->end(), selStart, d->length());
    if (! engine.isPlaying())
        engine.setPositionFrames (cursor);
    waveform->setCursorFrame (cursor);
    waveform->setSelection (selStart, selEnd);
    updateStatus();
    updateCommandStates();

    if (heal)
    {
        // The Spot Healing Brush heals as soon as the stroke ends, then lets go of it.
        autoHeal();
        setCursor (selStart);
    }
}

void MainWindow::chooseTool (WaveformView::Tool tool)
{
    if (tool != WaveformView::Tool::box && ! waveform->isSpectral())
        commands.action (QStringLiteral ("view.spectral"))->trigger();   // the tools draw on the spectral view
    waveform->setTool (tool);
    const char* ids[] { "repair.toolBox", "repair.toolLasso", "repair.toolBrush", "repair.toolSpotHeal" };
    commands.action (QString::fromLatin1 (ids[(int) tool]))->setChecked (true);

    const QString hints[] {
        tr ("Box: drag to select a time range; drag less than the full height to pick frequencies too."),
        tr ("Lasso: draw around a sound to select it. Hold Shift to add to the shape."),
        tr ("Brush: paint over a sound to select it. [ and ] change the size; Shift adds to the shape."),
        tr ("Spot Healing Brush: paint over a sound and it is healed when you let go. [ and ] change the size."),
    };
    statusBar()->showMessage (hints[(int) tool], 6000);
}

void MainWindow::addHarmonics()
{
    const auto d = data();
    if (d == nullptr || ! hasSelection() || ! hasBand())
    {
        statusBar()->showMessage (tr ("First select a sound's lowest frequency: a box, lasso or brush selection in the spectral view."), 6000);
        return;
    }

    std::shared_ptr<repair::SpectralMask> withOvertones;
    if (shape != nullptr)
        withOvertones = std::make_shared<repair::SpectralMask> (*shape);
    else
    {
        withOvertones = std::make_shared<repair::SpectralMask> (d->sampleRate());
        withOvertones->fillPolygon ({ { selStart, bandLow }, { selEnd, bandLow }, { selEnd, bandHigh }, { selStart, bandHigh } });
    }
    constexpr int overtones = 8;
    withOvertones->addHarmonics (overtones);
    setShape (std::move (withOvertones), false);
    statusBar()->showMessage (tr ("Added the overtones, up to %1 times the frequency.").arg (overtones), 4000);
}

void MainWindow::findSimilar()
{
    const auto d = data();
    if (d == nullptr || ! requireSelection())
        return;
    if ((double) (selEnd - selStart) > repair::maxHealSeconds * d->sampleRate())
    {
        QMessageBox::information (this, tr ("Find Similar Sounds"), tr ("Select one short sound (up to 4 seconds) as the example."));
        return;
    }

    ValuesDialog dialog (tr ("Find Similar Sounds"),
                         tr ("Looks through the whole file for sounds like the selected one, such as the other clicks, "
                             "breaths or coughs, so you can heal them all at once or mark them to check first."),
                         { { QStringLiteral ("repair/similar/strictness"), tr ("&Strictness:"), 80.0, 50.0, 99.0, 1.0, 0, tr (" %"),
                             tr ("Higher finds only very close matches; lower finds more, with some that may not belong"), {} } },
                         this);
    if (dialog.exec() != QDialog::Accepted)
        return;

    const auto example = region (selStart, selEnd);
    const auto strictness = dialog.value (0) / 100.0;
    auto found = std::make_shared<std::vector<repair::SpectralRegion>>();
    auto finished = std::make_shared<bool> (false);
    runRepair (tr ("Find Similar Sounds"), [d, example, strictness, found, finished] (repair::Progress p)
    {
        *found = repair::findSimilar (*d, example, 0, d->length(), strictness, p);
        *finished = ! p.cancelled();
        return AudioDataPtr();
    });
    waveform->setFocus();
    if (! *finished)
        return;
    if (found->empty())
    {
        QMessageBox::information (this, tr ("Find Similar Sounds"),
                                  tr ("Found no other sounds like this one. Try a lower strictness, or select just the sound itself."));
        return;
    }

    QMessageBox ask (QMessageBox::Question, tr ("Find Similar Sounds"),
                     tr ("Found %1 more like the selected sound.").arg (found->size()), QMessageBox::Cancel, this);
    auto* healAll = ask.addButton (tr ("&Heal All %1").arg (found->size() + 1), QMessageBox::AcceptRole);
    auto* mark = ask.addButton (tr ("&Mark Them"), QMessageBox::ActionRole);
    ask.setDefaultButton (healAll);
    ask.exec();
    waveform->setFocus();

    if (ask.clickedButton() == mark)
    {
        auto markers = doc.current().markers;
        for (size_t i = 0; i < found->size(); ++i)
        {
            Marker m;
            m.id = doc.nextMarkerId();
            m.type = Marker::Type::region;
            m.start = (*found)[i].start;
            m.end = (*found)[i].end;
            m.name = tr ("Similar %1").arg (i + 1).toStdString();
            markers.push_back (m);
        }
        commitMarkers (tr ("Mark Similar Sounds"), std::move (markers));
        return;
    }
    if (ask.clickedButton() != healAll)
        return;

    // Heal the example and every match, as one step to undo.
    auto all = *found;
    all.push_back (example);
    auto skipped = std::make_shared<int> (0);
    auto result = runRepair (tr ("Heal All"), [d, all, skipped] (repair::Progress p)
    {
        auto current = d;
        for (size_t i = 0; i < all.size(); ++i)
        {
            if (p.cancelled())
                return AudioDataPtr();
            p.report ((float) i / (float) all.size());
            std::string error;
            if (auto healed = repair::autoHeal (*current, all[i], error))
                current = std::move (healed);
            else
                ++*skipped;
        }
        return current;
    });
    if (result == nullptr)
        return;
    commit (tr ("Heal Similar Sounds"), std::move (result), 0, 0, 0);
    if (*skipped > 0)
        statusBar()->showMessage (tr ("Healed %1; %2 could not be healed (no clean audio around them).")
                                      .arg ((int) all.size() - *skipped).arg (*skipped), 6000);
    else
        statusBar()->showMessage (tr ("Healed %1 sounds.").arg (all.size()), 4000);
}

void MainWindow::scanDiagnostics()
{
    const auto d = data();
    if (d == nullptr)
        return;

    const auto [s, e] = processRange();
    const auto where = hasSelection() ? tr ("in the selection") : tr ("in the whole file");
    const auto kind = diagnostics->kind();
    const auto sensitivity = diagnostics->clickSensitivity(), clip = diagnostics->clipLevel();
    const auto silenceDb = diagnostics->silenceDb(), silenceSeconds = diagnostics->silenceSeconds();
    auto found = std::make_shared<std::vector<repair::Problem>>();
    auto finished = std::make_shared<bool> (false);
    runRepair (tr ("Scan"), [=] (repair::Progress p)
    {
        using Kind = repair::Problem::Kind;
        *found = kind == Kind::click    ? repair::findClicks (*d, s, e, sensitivity, p)
               : kind == Kind::clipping ? repair::findClipping (*d, s, e, clip, p)
               : kind == Kind::dropout  ? repair::findDropouts (*d, s, e, p)
                                        : repair::findSilence (*d, s, e, silenceDb, silenceSeconds, p);
        *finished = ! p.cancelled();
        return AudioDataPtr();
    });
    if (! *finished)
        return;

    diagnosed = d.get();
    diagnostics->setProblems (std::move (*found), d->sampleRate(), d->numChannels(), where);
    diagnostics->listWidget()->setFocus();
}

void MainWindow::showProblem (Frame start, Frame end)
{
    const auto d = data();
    if (d == nullptr || d.get() != diagnosed)
        return;

    // Select it and zoom so it is easy to see and hear: a short one with some sound around it.
    const auto length = std::max ((double) (end - start) * 1.5, 0.1 * d->sampleRate());
    waveform->setView ((double) (start + end) / 2.0 - length / 2.0, length);
    selectRange (start, end);
    cursor = start;
    if (! engine.isPlaying())
        engine.setPositionFrames (start);
    waveform->setCursorFrame (start);
}

void MainWindow::repairProblems (std::vector<int> rows)
{
    const auto d = data();
    if (d == nullptr || d.get() != diagnosed || rows.empty())
        return;

    std::vector<repair::Problem> chosen;
    for (auto r : rows)
        chosen.push_back (diagnostics->problems()[(size_t) r]);
    auto report = std::make_shared<repair::Report>();
    auto result = runRepair (tr ("Repair"), [d, chosen, report] (repair::Progress p) { return repair::repairProblems (*d, chosen, *report, p); });
    if (result == nullptr)
    {
        if (report->skipped > 0)
            statusBar()->showMessage (tr ("These could not be repaired from the audio around them."), 6000);
        return;
    }

    commit (tr ("Repair from Diagnostics"), std::move (result), 0, 0, 0);
    diagnosed = data().get();
    diagnostics->remove (rows);
    diagnostics->listWidget()->setFocus();
    statusBar()->showMessage (report->skipped > 0 ? tr ("Repaired %1; %2 could not be repaired.").arg (report->fixed).arg (report->skipped)
                                                  : tr ("Repaired %1.").arg (report->fixed), 6000);
}

void MainWindow::markProblems (std::vector<int> rows)
{
    const auto d = data();
    if (d == nullptr || d.get() != diagnosed || rows.empty())
        return;

    const QString names[] { tr ("Click %1"), tr ("Clipping %1"), tr ("Dropout %1"), tr ("Silence %1") };
    auto markers = doc.current().markers;
    int count = 0;
    for (auto r : rows)
    {
        const auto& p = diagnostics->problems()[(size_t) r];
        Marker m;
        m.id = doc.nextMarkerId();
        m.type = Marker::Type::region;
        m.start = p.start;
        m.end = p.end;
        m.name = names[(int) p.kind].arg (++count).toStdString();
        markers.push_back (m);
    }
    commitMarkers (tr ("Mark Problems"), std::move (markers));
    statusBar()->showMessage (tr ("Added %1; the Markers panel (Ctrl+Shift+M) lists them.")
                                  .arg (count == 1 ? tr ("1 region") : tr ("%1 regions").arg (count)), 6000);
}

void MainWindow::deleteSilence (std::vector<int> rows)
{
    if (refuseAlbumEdit())
        return;

    const auto d = data();
    if (d == nullptr || d.get() != diagnosed || rows.empty())
        return;

    // Delete from the end backwards, so earlier positions stay valid.
    const auto list = diagnostics->problems();
    auto state = doc.current();
    for (auto it = rows.rbegin(); it != rows.rend(); ++it)
    {
        const auto& p = list[(size_t) *it];
        state.data = state.data->replaced (p.start, p.end, nullptr);
        adjustMarkersForEdit (state.markers, p.start, p.end, 0);
    }
    doc.apply (tr ("Delete Silence").toStdString(), std::move (state));
    cursor = selStart = selEnd = selAnchor = std::min (cursor, data()->length());
    refreshFromDocument (true);

    // The rest of the list moves back by what was deleted before each.
    std::vector<repair::Problem> rest;
    for (size_t i = 0; i < list.size(); ++i)
    {
        if (std::binary_search (rows.begin(), rows.end(), (int) i))
            continue;
        auto p = list[i];
        Frame removed = 0;
        for (auto r : rows)
            if (list[(size_t) r].end <= p.start)
                removed += list[(size_t) r].end - list[(size_t) r].start;
        p.start -= removed;
        p.end -= removed;
        rest.push_back (p);
    }
    diagnosed = data().get();
    diagnostics->setProblems (std::move (rest), data()->sampleRate(), data()->numChannels(), tr ("left"));
    diagnostics->listWidget()->setFocus();
    statusBar()->showMessage (rows.size() == 1 ? tr ("Deleted 1 silent stretch.") : tr ("Deleted %1 silent stretches.").arg (rows.size()), 6000);
}

void MainWindow::autoHeal()
{
    const auto d = data();
    if (d == nullptr || ! requireSelection())
        return;

    std::string error;
    QApplication::setOverrideCursor (Qt::WaitCursor);
    auto result = repair::autoHeal (*d, region (selStart, selEnd), error);
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
    if (refuseAlbumEdit())
        return nullptr;

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
                                  tr ("First teach PHI what the noise sounds like:\n\n"
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
    const auto what = shape != nullptr ? tr ("the drawn shape")
                    : hasBand() ? tr ("%1 to %2 in the selection").arg (formatHz (bandLow), formatHz (bandHigh))
                                : hasSelection() ? tr ("the selection") : tr ("the whole file");
    SpectralGainDialog dialog (spectralGainDb, what, this);
    if (dialog.exec() != QDialog::Accepted)
        return;

    spectralGainDb = std::max (-60.0, dialog.gainDb());
    QApplication::setOverrideCursor (Qt::WaitCursor);
    auto result = repair::spectralGain (*d, region (s, e), dialog.gainDb());
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
    bool limit = false;
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
            if (! dialog.limitPeaks() && newTruePeak > 0.0)
                statusBar()->showMessage (tr ("Warning: true peak will be %1 dBTP and may clip").arg (newTruePeak, 0, 'f', 1), 8000);
            limit = dialog.limitPeaks() && newTruePeak > dialog.peakCeiling();
        }
    }

    if (! problem.isEmpty())
    {
        QApplication::restoreOverrideCursor();
        QMessageBox::information (this, tr ("Normalize"), problem);
        return;
    }

    auto result = dsp::applyGain (*d, s, e, changeDb);
    if (limit)
    {
        // Limiting takes a little loudness away, so turn up by what is missing
        // and limit again from the original, a few times at most.
        for (int pass = 0; pass < 4; ++pass)
        {
            result = dsp::limitTruePeak (*result, s, e, dialog.peakCeiling());
            const auto missing = dialog.target() - dsp::measureLoudness (*result, s, e).integratedLufs;
            if (std::abs (missing) < 0.1 || pass == 3)
                break;
            changeDb += missing;
            result = dsp::applyGain (*d, s, e, changeDb);
        }
        const auto reached = dsp::measureLoudness (*result, s, e).integratedLufs;
        if (dialog.target() - reached > 0.5)
            statusBar()->showMessage (tr ("Limited to %1 dBTP: reached %2 LUFS, as loud as it goes without more limiting")
                                          .arg (dialog.peakCeiling(), 0, 'f', 1).arg (reached, 0, 'f', 1), 8000);
    }
    QApplication::restoreOverrideCursor();

    commit (tr ("Normalize to %1").arg (dialog.targetName()), std::move (result), s, e, e - s);
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
    if (refuseAlbumEdit())
        return;

    auto state = doc.current();
    state.markers = std::move (markers);
    doc.apply (description.toStdString(), std::move (state));
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
AudioDataPtr MainWindow::renderWithProgress (Frame start, Frame end, std::string& error, bool withDither)
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

    watcher.setFuture (QtConcurrent::run ([this, source, start, end, progress, cancel, withDither]
    {
        Result r;
        r.data = engine.renderThroughMaster (*source, start, end, r.error, progress.get(), cancel.get(), withDither);
        return r;
    }));

    loop.exec();
    poll.stop();
    dialog.reset();

    auto result = watcher.result();
    error = result.error;
    return result.data;
}

bool MainWindow::masterWouldChange (const QString& title)
{
    if (data() == nullptr)
        return false;
    if (engine.masterIsActive())
        return true;
    QMessageBox::information (this, title,
                              tr ("The Master Section is empty or bypassed, so rendering would not change anything.\n"
                                  "Add a plugin with Ctrl+Alt+E or change the master gain first."));
    return false;
}

void MainWindow::renderInPlace()
{
    // Bounce: the selection (or the whole file) through the Master Section, as an undoable edit.
    if (refuseAlbumEdit() || ! masterWouldChange (tr ("Render in Place")))
        return;

    const auto d = data();
    const auto [s, e] = processRange();
    std::string error;
    auto result = renderWithProgress (s, e, error);
    if (result == nullptr)
    {
        if (error != "Cancelled")
            QMessageBox::warning (this, tr ("Render in Place"), QString::fromStdString (error));
        return;
    }

    // The render covers [s, e) only; put it back into the file. A mono file that
    // the chain widened (Stereoize, a stereo reverb...) becomes stereo throughout.
    auto base = result->numChannels() == d->numChannels() ? d : dsp::toStereo (d);
    commit (tr ("Render in Place"), base->replaced (s, e, result.get()), s, e, e - s);

    // Bypassed so the effect is not heard twice.
    engine.setMasterBypassed (true);
    masterWidget->refresh();
    commands.action (QStringLiteral ("master.bypass"))->setChecked (true);
    statusBar()->showMessage (tr ("Rendered in place. The Master Section is now bypassed; Ctrl+Shift+B turns it back on."), 8000);
}

void MainWindow::masterAssistant()
{
    if (data() == nullptr || refuseAlbumEdit())
        return;
    bool anyPlugin = false;
    for (int slot = 0; slot < AudioEngine::numMasterSlots; ++slot)
        anyPlugin = anyPlugin || ! engine.masterSlot (slot).empty;
    if (! anyPlugin)
    {
        QMessageBox::information (this, tr ("Master Assistant"),
                                  tr ("The Master Assistant tunes the plugins you put in the Master Section; it does not choose them.\n"
                                      "Build a chain first (Ctrl+Alt+E adds a plugin), or load a preset chain, then open it again."));
        return;
    }

    MasterAssistant::References refs;
    for (const auto& r : references)
        refs.names << QFileInfo (r.path).fileName();
    refs.current = currentReference;
    refs.load = [this] (int row) -> AudioDataPtr
    {
        if (row < 0 || row >= (int) references.size() || ! loadReference (row))
            return nullptr;
        return references[(size_t) row].atRate;
    };

    const auto [s, e] = processRange();
    MasterAssistant dialog (engine, data(), s, e, std::move (refs), [this] { togglePlayback(); }, this);
    dialog.exec();
    masterWidget->refresh();
    waveform->setFocus();
}

void MainWindow::renderToNewTab()
{
    // The selection (or the whole file) through the Master Section, into a new
    // untitled tab; this file stays as it is and the Master Section stays on.
    if (! masterWouldChange (tr ("Render to New Tab")))
        return;

    const bool whole = ! hasSelection();
    const auto [s, e] = whole ? std::pair (Frame (0), data()->length()) : std::pair (selStart, selEnd);
    std::string error;
    auto result = renderWithProgress (s, e, error);
    if (result == nullptr)
    {
        if (error != "Cancelled")
            QMessageBox::warning (this, tr ("Render to New Tab"), QString::fromStdString (error));
        return;
    }

    std::vector<Marker> markers;
    for (auto mk : doc.current().markers)
    {
        if (mk.start < s || mk.start >= e)
            continue;
        mk.start -= s;
        mk.end = std::min (mk.end, e) - s;
        markers.push_back (mk);
    }
    auto tags = doc.current().album != nullptr ? Tags() : doc.current().tags;

    const auto options = saveOptions;
    newTab();
    doc.reset (std::move (result), std::move (markers), {}, std::move (tags));
    doc.markUnsaved();
    saveOptions = options;
    refreshFromDocument (false);
    waveform->setFocus();
    statusBar()->showMessage (whole ? tr ("Rendered the whole file through the Master Section into a new tab")
                                    : tr ("Rendered the selection through the Master Section into a new tab"), 6000);
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
    if (shape != nullptr)
    {
        shape.reset();   // a new time selection replaces a drawn one
        waveform->setShape (nullptr);
    }
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
    selectRange (hasSelection() ? selAnchor : snapToZero (cursor), snapToZero (head));
}

Frame MainWindow::snapToZero (Frame f) const
{
    // Snap to Grid comes first: a grid line is exactly where the beat is.
    if (const auto g = waveform->snapToGrid (f); g != f || (doc.isOpen() && doc.current().grid.isSet()
                                                             && commands.action (QStringLiteral ("edit.snapGrid"))->isChecked()))
        return g;
    const auto d = data();
    if (d == nullptr || ! commands.action (QStringLiteral ("edit.snapZero"))->isChecked())
        return f;
    return dsp::nearestZeroCrossing (*d, f, (Frame) (d->sampleRate() * 0.015));   // within 15 ms
}

void MainWindow::togglePlayback()
{
    if (engine.hasLiveSource())
        trackerStop();   // the Tracker hands the sound back to the file
    if (engine.isPlaying())
    {
        engine.stop();
        return;
    }

    playStart = hasSelection() ? selStart : cursor;
    engine.setPositionFrames (playStart);
    engine.play();
}

void MainWindow::changeEvent (QEvent* e)
{
    if (e->type() == QEvent::PaletteChange)
        sendThemeToPluginFrames();
    QMainWindow::changeEvent (e);
}

void MainWindow::sendThemeToPluginFrames()
{
    const auto& t = Theme::current();
    const auto c = [&] (const char* key) { return (std::uint32_t) t.color (QString::fromLatin1 (key)).rgba(); };
    PluginFrameColours f;
    f.window = c ("window");
    f.panel = c ("button");
    f.text = c ("windowText");
    f.dimText = c ("placeholderText");
    f.accent = c ("highlight");
    f.mid = c ("mid");
    f.meterBackground = c ("meterBackground");
    f.meterLow = c ("meterLow");
    f.meterMid = c ("meterMid");
    f.meterHigh = c ("meterHigh");
    engine.setPluginFrameColours (f);
}

void MainWindow::closeEvent (QCloseEvent* e)
{
    if (loading)
    {
        e->ignore();
        return;
    }

    if (batchWindow != nullptr && batchWindow->isRunning()
        && QMessageBox::question (this, tr ("Quit"), tr ("A batch is still running. Stop it and quit?")) != QMessageBox::Yes)
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

    if (! trackerConfirmDiscard())
    {
        e->ignore();
        return;
    }

    engine.stop();
    if (mixerWin != nullptr)
        mixerWin->autosave();
    savePatchBay();
    commands.save();
    saveLayout();
    autosaveTimer.stop();
    recovery.finish();   // a clean quit: nothing to recover next time
    e->accept();
}

//==============================================================================
void MainWindow::registerNyquistCommands()
{
    nyquistEntries = nyquistui::scan();
    for (const auto& e : nyquistEntries)
    {
        if (commands.action (e.id) != nullptr)
            continue;
        const auto& h = e.header;
        const auto category = h.inToolsMenu ? Category::tools
                            : h.kind == nyquist::Kind::generate ? Category::generate
                            : h.kind == nyquist::Kind::analyze ? Category::analyze : Category::effect;
        bool hasControls = false;
        for (const auto& c : h.controls)
            hasControls = hasControls || c.type != nyquist::Control::Type::text;
        auto name = QString::fromStdString (h.name).replace (QLatin1Char ('&'), QStringLiteral ("&&"));
        if (hasControls)
            name += QStringLiteral ("...");
        auto* a = commands.add (e.id, category, name, {}, [this, id = e.id] { runNyquistCommand (id); });
        if (! h.info.empty())
            a->setStatusTip (QString::fromStdString (h.info));
        addAction (a);
    }
}

void MainWindow::fillNyquistMenus()
{
    for (const auto& [key, anchor] : nyquistAnchors.asKeyValueRange())
        if (auto* menu = qobject_cast<QMenu*> (anchor->parent()))
            for (auto* a : nyquistMenuItems)
                menu->removeAction (a);
    if (generateMenu != nullptr)
    {
        generateMenu->clear();
        // PHI's own generators first, then the Nyquist ones.
        generateMenu->addAction (commands.action (QStringLiteral ("generate.tone")));
        generateMenu->addAction (commands.action (QStringLiteral ("generate.noise")));
        generateMenu->addSeparator();
    }
    nyquistMenuItems.clear();

    auto sorted = nyquistEntries;
    std::sort (sorted.begin(), sorted.end(), [] (const auto& a, const auto& b)
    {
        return QString::fromStdString (a.header.name).localeAwareCompare (QString::fromStdString (b.header.name)) < 0;
    });

    for (const auto& e : sorted)
    {
        auto* action = commands.action (e.id);
        if (action == nullptr)
            continue;
        const auto& h = e.header;
        if (! h.inToolsMenu && h.kind == nyquist::Kind::generate && generateMenu != nullptr)
        {
            generateMenu->addAction (action);
            continue;
        }
        const auto token = h.inToolsMenu ? QStringLiteral ("@nyquist.tools")
                         : h.kind == nyquist::Kind::analyze ? QStringLiteral ("@nyquist.analyze") : QStringLiteral ("@nyquist.effect");
        if (auto* anchor = nyquistAnchors.value (token))
            if (auto* menu = qobject_cast<QMenu*> (anchor->parent()))
            {
                menu->insertAction (anchor, action);
                nyquistMenuItems << action;
            }
    }
    if (generateMenu != nullptr)
        generateMenu->setEnabled (! generateMenu->isEmpty());
}

void MainWindow::rescanNyquist()
{
    const auto before = nyquistEntries.size();
    registerNyquistCommands();
    fillNyquistMenus();
    statusBar()->showMessage (tr ("Found %n Nyquist plug-in(s)", nullptr, (int) nyquistEntries.size())
                              + (nyquistEntries.size() > before ? tr (", %1 new").arg (nyquistEntries.size() - before) : QString()), 6000);
}

void MainWindow::runNyquistCommand (const QString& id, bool repeat)
{
    const auto entry = std::find_if (nyquistEntries.begin(), nyquistEntries.end(), [&] (const auto& e) { return e.id == id; });
    if (entry == nyquistEntries.end())
        return;

    // Read again, so an edited plug-in runs as it is now.
    auto script = nyquist::load (entry->path.toStdString());
    const auto title = QString::fromStdString (script.name);
    if (! script.ok)
    {
        QMessageBox::warning (this, title, QString::fromStdString (script.error));
        return;
    }

    nyquistui::restoreValues (id, script);
    bool debug = false;
    const bool hasControls = std::any_of (script.controls.begin(), script.controls.end(),
                                          [] (const auto& c) { return c.type != nyquist::Control::Type::text; });
    if (hasControls && ! repeat)
    {
        NyquistDialog dialog (script, this);
        if (dialog.exec() != QDialog::Accepted)
            return;
        debug = dialog.debugRequested();
        nyquistui::storeValues (id, script);
    }

    if (script.kind != nyquist::Kind::tool && ! script.inToolsMenu)
        lastEffectId = id;
    const bool tool = script.inToolsMenu;
    runNyquist (std::move (script), title, debug);
    if (tool)
        rescanNyquist();   // the Plugin Installer adds files
}

void MainWindow::nyquistPrompt()
{
    NyquistPromptDialog dialog (this);
    if (dialog.exec() != QDialog::Accepted)
        return;

    auto script = nyquist::parse (dialog.code().toStdString(), true);
    if (! script.ok)
    {
        QMessageBox::information (this, tr ("Nyquist Prompt"), QString::fromStdString (script.error));
        return;
    }
    // Without a ;type line it works on the audio when a file is open, else it just runs.
    if (! script.typeGiven)
        script.kind = data() != nullptr ? nyquist::Kind::process : nyquist::Kind::tool;
    script.name = tr ("Nyquist Prompt").toStdString();

    bool debug = dialog.debugRequested();
    if (std::any_of (script.controls.begin(), script.controls.end(), [] (const auto& c) { return c.type != nyquist::Control::Type::text; }))
    {
        NyquistDialog controls (script, this);
        if (controls.exec() != QDialog::Accepted)
            return;
        debug = debug || controls.debugRequested();
    }
    runNyquist (std::move (script), tr ("Nyquist Prompt"), debug);
}

void MainWindow::runNyquist (nyquist::Script script, const QString& title, bool debug)
{
    const auto d = data();
    const bool generate = script.kind == nyquist::Kind::generate;
    const bool tool = script.kind == nyquist::Kind::tool;
    if (! generate && ! tool && d == nullptr)
    {
        statusBar()->showMessage (tr ("Open a file first"), 4000);
        return;
    }

    nyquist::Input in;
    in.audio = d.get();
    if (generate)
    {
        in.start = hasSelection() ? selStart : cursor;
        in.end = hasSelection() ? selEnd : cursor;
    }
    else if (d != nullptr)
        std::tie (in.start, in.end) = processRange();
    in.sampleRate = d != nullptr ? d->sampleRate() : 48000.0;
    if (bandHigh > bandLow)
    {
        in.lowHz = bandLow;
        in.highHz = bandHigh;
    }
    in.trackName = tabName (doc).remove (QStringLiteral (" *")).toStdString();
    in.projectName = in.trackName;
    in.debug = debug;

    // Runs in the background with a progress window, like the repair tools.
    auto progress = std::make_shared<std::atomic<float>> (0.0f);
    auto cancel = std::make_shared<std::atomic<bool>> (false);
    engine.stop();
    QProgressDialog dialog (title + QStringLiteral ("..."), tr ("Cancel"), 0, 1000, this);
    dialog.setWindowTitle (title);
    dialog.setWindowModality (Qt::WindowModal);
    dialog.setMinimumDuration (400);
    connect (&dialog, &QProgressDialog::canceled, this, [cancel] { cancel->store (true); });
    QFutureWatcher<nyquist::Result> watcher;
    QEventLoop loop;
    connect (&watcher, &QFutureWatcher<nyquist::Result>::finished, &loop, &QEventLoop::quit);
    QTimer poll;
    connect (&poll, &QTimer::timeout, this, [&] { dialog.setValue ((int) (progress->load() * 1000.0f)); });
    poll.start (50);
    watcher.setFuture (QtConcurrent::run ([script, in, d, progress, cancel]
    {
        return nyquist::run (script, in, { progress.get(), cancel.get() });
    }));
    loop.exec();
    poll.stop();
    dialog.reset();
    const auto result = watcher.result();
    const auto output = QString::fromStdString (result.output).trimmed();

    using R = nyquist::Result::Type;
    switch (result.type)
    {
        case R::cancelled:
            statusBar()->showMessage (tr ("%1 cancelled").arg (title), 4000);
            break;
        case R::error:
            nyquistui::showOutput (this, title, QString::fromStdString (result.text) + (output.isEmpty() ? QString() : QStringLiteral ("\n\n") + output));
            return;
        case R::text:
        case R::number:
            QMessageBox::information (this, title, QString::fromStdString (result.text));
            break;
        case R::labels:
        {
            if (d == nullptr)
                break;
            auto markers = doc.current().markers;
            const auto rate = d->sampleRate();
            for (const auto& l : result.labels)
            {
                if (l.start * rate > (double) d->length())
                    continue;   // past the end of the file
                Marker m;
                m.id = doc.nextMarkerId();
                m.start = std::clamp<Frame> ((Frame) std::llround (l.start * rate), 0, d->length());
                m.end = std::clamp<Frame> ((Frame) std::llround (l.end * rate), m.start, d->length());
                m.type = m.end > m.start ? Marker::Type::region : Marker::Type::generic;
                m.name = l.text;
                markers.push_back (m);
            }
            commitMarkers (title, std::move (markers));
            statusBar()->showMessage (tr ("%1 added %n marker(s)", nullptr, (int) result.labels.size()).arg (title), 6000);
            break;
        }
        case R::audio:
        {
            const auto length = result.audio->length();
            if (d == nullptr)
            {
                // Generated with nothing open: a new file.
                newTab();
                doc.reset (result.audio, {}, {});
                doc.markUnsaved();
                refreshFromDocument (false);
                break;
            }
            commit (title, d->replaced (in.start, in.end, result.audio.get()), in.start, in.end, length);
            selectRange (in.start, in.start + length);
            break;
        }
        case R::nothing:
            statusBar()->showMessage (tr ("%1 finished").arg (title), 4000);
            break;
    }

    if ((debug || script.trace) && result.type != R::error)
        nyquistui::showOutput (this, title, output);
    waveform->setFocus();
}

//==============================================================================
void MainWindow::showBatch()
{
    if (batchWindow == nullptr)
    {
        batchWindow = new BatchWindow (this);
        batchWindow->masterRender = [this] (const AudioData& audio, std::string& error, const std::atomic<bool>* cancel)
        {
            const std::lock_guard<std::mutex> lock (masterRenderLock);
            return engine.renderThroughMaster (audio, 0, audio.length(), error, nullptr, cancel, false);   // outputs dither last
        };
        batchWindow->openFiles = [this]
        {
            QStringList paths;
            for (int i = 0; i < (int) tabs.size(); ++i)
            {
                const auto& path = i == activeTab ? doc.path() : tabs[(size_t) i].doc.path();
                if (! path.empty())
                    paths << QString::fromStdString (path);
            }
            return paths;
        };
        connect (batchWindow, &BatchWindow::openRequested, this, [this] (const QString& path) { openFile (path); });
    }
    batchWindow->show();
    batchWindow->raise();
    batchWindow->activateWindow();
}

PluginAnalyzer* MainWindow::pluginAnalyzer()
{
    if (pluginAnalyzerWin == nullptr)
    {
        pluginAnalyzerWin = new PluginAnalyzer (engine, this);
        connect (pluginAnalyzerWin, &PluginAnalyzer::chainChanged, this, [this] { masterWidget->refresh(); chainRow->rebuild(); });
        connect (pluginAnalyzerWin, &PluginAnalyzer::closed, this, [this]
        {
            activateWindow();
            waveform->setFocus();
        });
    }
    return pluginAnalyzerWin;
}

void MainWindow::analyzeSlot (int slot)
{
    auto* w = pluginAnalyzer();
    if (slot >= 0)
        w->analyseSlot (slot);
    else
        w->analyseMasterSection();
    w->show();
    w->raise();
    w->activateWindow();
}

AnalysisWindow* MainWindow::analysisWindow()
{
    if (analysisWin == nullptr)
    {
        analysisWin = new AnalysisWindow (this);
        connect (analysisWin, &AnalysisWindow::playStopRequested, this, [this] { togglePlayback(); });
        connect (analysisWin, &AnalysisWindow::closed, this, [this]
        {
            activateWindow();
            waveform->setFocus();
        });
    }
    return analysisWin;
}

// The Analysis window's Ambisonics card: the file's own channels around what is heard (all
// of them, past the 16 the engine plays), or around the cursor when stopped. Read again only
// when that place or the audio changes.
void MainWindow::feedAnalysisAmbisonics()
{
    const auto d = data();
    if (! analysisWin->wantsAmbisonics())
        return;
    const auto& layout = doc.current().layout;
    const int order = d == nullptr ? 0
                                   : AmbiVisualizer::orderFor (d->numChannels(), dsp::ambisonicOrder (layout),
                                                               layout.rfind ("Ambisonics", 0) == 0, analysisWin->assumesAmbisonics());
    const auto at = engine.isPlaying() ? engine.heardPositionFrames() : cursor;
    if (order <= 0)
    {
        if (ambiFedOrder != 0)
            analysisWin->addAmbisonics ({}, 0);
        ambiFedOrder = 0;
        ambiFedData = nullptr;
        return;
    }
    if (d.get() == ambiFedData && at == ambiFedAt && order == ambiFedOrder)
        return;
    ambiFedData = d.get();
    ambiFedAt = at;
    ambiFedOrder = order;
    constexpr int frames = 2048;
    std::vector<std::vector<float>> channels ((size_t) d->numChannels(), std::vector<float> (frames));
    std::vector<float*> pointers;
    for (auto& c : channels)
        pointers.push_back (c.data());
    d->read (std::max<Frame> (0, at - frames / 2), frames, pointers.data());
    analysisWin->addAmbisonics (channels, order);
}

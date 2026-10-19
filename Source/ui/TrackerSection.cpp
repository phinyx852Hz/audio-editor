// SPDX-License-Identifier: AGPL-3.0-only
// The Tracker menu: MilkyTracker's module player (MilkyPlay), played through PHINYX and
// recorded into the editor, stereo or one stereo pair per mixer track.
#include "MainWindow.h"
#include "Settings.h"
#include "TrackerWindow.h"
#include "WaveformView.h"
#include "../tracker/TrackerEngine.h"

#include <QEventLoop>
#include <QFileDialog>
#include <QFileInfo>
#include <QFutureWatcher>
#include <QMessageBox>
#include <QProgressDialog>
#include <QStatusBar>
#include <QtConcurrent>

void MainWindow::registerTrackerCommands()
{
    using KS = QKeySequence;
    const auto category = tr ("Tracker");
    auto add = [this, &category] (const char* id, const QString& text, QList<QKeySequence> keys, std::function<void()> fn)
    {
        auto* action = commands.add (QString::fromLatin1 (id), category, text, keys, std::move (fn));
        addAction (action);
        return action;
    };

    tracker = std::make_unique<TrackerEngine>();

    add ("tracker.window", tr ("&Tracker"), { KS (Qt::CTRL | Qt::ALT | Qt::SHIFT | Qt::Key_K) }, [this]
    {
        auto* w = trackerWindow();
        w->show();
        w->raise();
        w->activateWindow();
        w->patternView()->setFocus();
    })->setStatusTip (tr ("The Tracker, from MilkyTracker: play XM, MOD, IT and S3M modules and record them into the editor"));
    add ("tracker.open", tr ("&Open Module..."), {}, [this] { openModule(); })
        ->setStatusTip (tr ("Open a tracker module (XM, MOD, IT, S3M, MTM, 669, FAR, ...) in the Tracker"));
    add ("tracker.playSong", tr ("Play &Song"), {}, [this] { trackerPlay (false); });
    add ("tracker.playPattern", tr ("Play &Pattern"), {}, [this] { trackerPlay (true); });
    add ("tracker.stop", tr ("S&top Tracker"), {}, [this] { trackerStop(); });
    add ("tracker.recordStereo", tr ("&Record to Editor (Stereo)..."), {}, [this] { trackerRecord (false); })
        ->setStatusTip (tr ("Record the whole song, as the Tracker plays it, into a new stereo tab"));
    add ("tracker.recordMulti", tr ("Record to Editor (&Multi-Channel)..."), {}, [this] { trackerRecord (true); })
        ->setStatusTip (tr ("Record the whole song into a new tab with a stereo pair per mixer track (T1 to T8 on the channels)"));
    add ("tracker.resetTracks", tr ("Spread Channels over the &Tracks"), {}, [this]
    {
        tracker->resetTracks();
        if (trackerWin != nullptr)
        {
            trackerWin->patternView()->update();
            trackerWin->refresh();
        }
        statusBar()->showMessage (tr ("The module's channels are spread evenly over tracks 1 to %1").arg (std::min (tracker->numChannels(), (int) TrackerEngine::numTracks)), 4000);
    });
    add ("tracker.about", tr ("About the Tracker..."), {}, [this]
    {
        QMessageBox::about (this, tr ("About the Tracker"),
                            tr ("<p>The Tracker is built on <b>MilkyTracker</b> (milkytracker.org, github.com/milkytracker/MilkyTracker): "
                                "its player library MilkyPlay plays the modules, and its pattern display is followed here.</p>"
                                "<p>MilkyTracker is by Peter \"pailes\" Barth and its contributors (Christopher O'Neill, Dale Whinham, "
                                "Julian Harnath and many others). MilkyPlay is under the New BSD licence and MilkyTracker under the "
                                "GNU GPL version 3 or later; PHINYX uses both with thanks, under the GNU AGPL version 3.</p>"));
    });
}

TrackerWindow* MainWindow::trackerWindow()
{
    if (trackerWin == nullptr)
    {
        trackerWin = new TrackerWindow (*tracker, this);
        connect (trackerWin, &TrackerWindow::openRequested, this, [this] { openModule(); });
        connect (trackerWin, &TrackerWindow::playSongRequested, this, [this] { trackerPlay (false); });
        connect (trackerWin, &TrackerWindow::playPatternRequested, this, [this] { trackerPlay (true); });
        connect (trackerWin, &TrackerWindow::stopRequested, this, [this] { trackerStop(); });
        connect (trackerWin, &TrackerWindow::recordRequested, this, [this] (bool multi) { trackerRecord (multi); });
        connect (trackerWin, &TrackerWindow::closed, this, [this]
        {
            trackerStop();
            activateWindow();
            waveform->setFocus();
        });
    }
    return trackerWin;
}

void MainWindow::openModule()
{
    auto* w = trackerWindow();
    const auto folder = appSettings().value (QStringLiteral ("tracker/folder"), QDir::homePath()).toString();
    const auto path = QFileDialog::getOpenFileName (w, tr ("Open Module"), folder,
                                                    tr ("Modules (*.xm *.mod *.it *.s3m *.stm *.mtm *.669 *.far *.mdl *.okt *.ptm *.ult *.amf *.dbm *.dsm *.gdm *.imf *.psm *.mxm *.umx);;All files (*)"));
    if (path.isEmpty())
        return;
    appSettings().setValue (QStringLiteral ("tracker/folder"), QFileInfo (path).absolutePath());

    trackerStop();
    const auto error = tracker->load (path.toStdString());
    if (! error.empty())
    {
        QMessageBox::warning (w, tr ("Open Module"), QString::fromStdString (error));
        return;
    }
    w->moduleChanged();
    w->show();
    w->raise();
    w->activateWindow();
    w->patternView()->setFocus();
}

void MainWindow::trackerPlay (bool patternOnly)
{
    auto* w = trackerWindow();
    if (! tracker->isLoaded())
    {
        openModule();
        if (! tracker->isLoaded())
            return;
    }
    const auto rate = engine.deviceSampleRate() > 0.0 ? engine.deviceSampleRate() : 48000.0;
    auto* view = w->patternView();
    if (patternOnly)
        tracker->playPattern (tracker->patternAt (view->order()), view->row(), rate);
    else
        tracker->playSong (view->order(), view->row(), rate);
    engine.setLiveSource ([t = tracker.get()] (float* left, float* right, int frames) { t->render (left, right, frames); });
    w->refresh();
}

void MainWindow::trackerStop()
{
    if (engine.hasLiveSource())
        engine.setLiveSource ({});
    tracker->stop();
    if (trackerWin != nullptr)
        trackerWin->refresh();
}

void MainWindow::trackerRecord (bool multiChannel)
{
    auto* w = trackerWindow();
    if (! tracker->isLoaded())
    {
        QMessageBox::information (w, tr ("Record to Editor"), tr ("Open a module first (Ctrl+O in the Tracker)."));
        return;
    }
    trackerStop();

    // The file's own rate would be 44.1 or 48 kHz anyway; use the device's so it sounds as heard.
    const auto rate = engine.deviceSampleRate() > 0.0 ? engine.deviceSampleRate() : 48000.0;
    auto progress = std::make_shared<std::atomic<float>> (0.0f);
    auto cancel = std::make_shared<std::atomic<bool>> (false);

    QProgressDialog dialog (multiChannel ? tr ("Recording the song, a stereo pair per track...") : tr ("Recording the song..."),
                            tr ("Cancel"), 0, 1000, w);
    dialog.setWindowTitle (tr ("Record to Editor"));
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
    watcher.setFuture (QtConcurrent::run ([t = tracker.get(), rate, multiChannel, progress, cancel]
    {
        Result r;
        r.data = t->record (rate, multiChannel, r.error, progress.get(), cancel.get());
        return r;
    }));
    loop.exec();
    poll.stop();
    dialog.reset();

    auto result = watcher.result();
    if (result.data == nullptr || result.data->length() == 0)
    {
        if (result.error != "Cancelled.")
            QMessageBox::warning (w, tr ("Record to Editor"), result.error.empty() ? tr ("The song made no sound.") : QString::fromStdString (result.error));
        return;
    }

    Tags tags;
    if (const auto name = tracker->title(); ! name.empty())
        tags[tag::title] = name;
    const auto options = saveOptions;
    newTab();
    doc.reset (std::move (result.data), {}, {}, std::move (tags));
    doc.markUnsaved();
    saveOptions = options;
    refreshFromDocument (false);
    activateWindow();
    raise();
    waveform->setFocus();
    const auto channels = data() != nullptr ? data()->numChannels() : 2;
    statusBar()->showMessage (multiChannel ? tr ("Recorded the song into a new tab: %1 channels, a stereo pair per track").arg (channels)
                                           : tr ("Recorded the song into a new stereo tab"), 8000);
}

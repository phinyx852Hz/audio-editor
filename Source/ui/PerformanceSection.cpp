// SPDX-License-Identifier: AGPL-3.0-only
// The Performance menu: records PHINYX's own window to an MP4 (H.265 and AAC, with the
// cameras and a track for each sound) and streams it live, like OBS but for PHINYX alone.
// The clip launcher (docs/clip-launcher.md) opens from here: it is the mixer's, in its upper area.
#include "MainWindow.h"
#include "MixerWindow.h"
#include "PerformanceDialog.h"
#include "../performance/Recorder.h"

#include <QApplication>
#include <QDesktopServices>
#include <QDialog>
#include <QDir>
#include <QDockWidget>
#include <QFileInfo>
#include <QLabel>
#include <QMessageBox>
#include <QStatusBar>
#include <QUrl>
#include <QtConcurrent>

#include <cmath>

namespace
{
    QString clock (double seconds)
    {
        const auto s = (qint64) seconds;
        return QStringLiteral ("%1:%2:%3").arg (s / 3600, 2, 10, QLatin1Char ('0')).arg ((s / 60) % 60, 2, 10, QLatin1Char ('0'))
                                          .arg (s % 60, 2, 10, QLatin1Char ('0'));
    }
}

void MainWindow::registerPerformanceCommands()
{
    using KS = QKeySequence;
    const auto category = tr ("Performance");
    auto add = [this, &category] (const char* id, const QString& text, QList<QKeySequence> keys, std::function<void()> fn)
    {
        auto* action = commands.add (QString::fromLatin1 (id), category, text, keys, std::move (fn));
        addAction (action);
        return action;
    };

    performance = std::make_unique<perf::Recorder> (engine);

    // F5 brings up the mixer with the clip launcher in front, and F5 again goes back to the editor.
    add ("performance.launcher", tr ("Clip &Launcher"), { KS (Qt::Key_F5) }, [this]
    {
        if (mixerWin != nullptr && mixerWin->isVisible() && mixerWin->launcherShown())
        {
            mixerWin->close();
            return;
        }
        showMixer();
        mixerWin->showLauncher (true);
    })->setStatusTip (tr ("A grid of clips to play live, tracks across and scenes down, with the rack on the left and the mixer below; "
                          "Enter on a sound in the Selector puts it in the next slot of its colour"));

    add ("performance.record", tr ("&Record Video"), { KS (Qt::CTRL | Qt::Key_F12) }, [this] { toggleVideoRecording(); })
        ->setStatusTip (tr ("Record PHINYX's window, the cameras and the sound to an MP4 (H.265 and AAC); again to stop"));
    commands.action (QStringLiteral ("performance.record"))->setCheckable (true);
    add ("performance.stream", tr ("&Go Live"), { KS (Qt::SHIFT | Qt::Key_F12) }, [this] { toggleStreaming(); })
        ->setStatusTip (tr ("Stream PHINYX's window, the cameras and the mix live (YouTube, Twitch, any RTMP or SRT server); again to stop"));
    commands.action (QStringLiteral ("performance.stream"))->setCheckable (true);
    // Both work from any PHINYX window, such as the full-screen Visualizer being recorded.
    for (const auto* id : { "performance.record", "performance.stream" })
        commands.action (QString::fromLatin1 (id))->setShortcutContext (Qt::ApplicationShortcut);
    add ("performance.settings", tr ("Video and Stream &Settings..."), {}, [this] { performanceSettings(); })
        ->setStatusTip (tr ("Resolution, frame rate, encoder, sound tracks, cameras and the streaming service"));
    add ("performance.folder", tr ("Show Recordings &Folder"), {}, [this]
    {
        const auto folder = QString::fromStdString (perfui::loadSettings().folder);
        QDir().mkpath (folder);
        QDesktopServices::openUrl (QUrl::fromLocalFile (folder));
    });

    performanceBadge = new QLabel (this);
    performanceBadge->setTextFormat (Qt::RichText);
    performanceBadge->hide();
    statusBar()->addPermanentWidget (performanceBadge);

    performanceTimer.setTimerType (Qt::PreciseTimer);
    connect (&performanceTimer, &QTimer::timeout, this, &MainWindow::capturePerformanceFrame);

    // Which encoders work is found by opening each; done early, off the UI thread, so
    // the first recording starts at once.
    QTimer::singleShot (8000, this, []
    {
        (void) QtConcurrent::run ([] { perf::workingEncoders (perf::Codec::hevc); perf::workingEncoders (perf::Codec::h264); });
    });
}

QWidget* MainWindow::performanceTarget (bool follow)
{
    // A full-screen PHINYX window (the Visualizer on F11) always; else the one in front
    // when following; else the main window. Never dialogs, menus or popped-out panels.
    for (auto* w : QApplication::topLevelWidgets())
        if (w != this && w->isVisible() && w->isFullScreen() && qobject_cast<QDialog*> (w) == nullptr)
            return w;
    if (follow)
    {
        auto* active = QApplication::activeWindow();
        if (active != nullptr && active->isVisible() && qobject_cast<QDialog*> (active) == nullptr
            && qobject_cast<QDockWidget*> (active) == nullptr && (active->windowFlags() & Qt::Popup) != Qt::Popup)
            performanceWindow = active;
    }
    else
        performanceWindow = nullptr;
    if (performanceWindow == nullptr || ! performanceWindow->isVisible())
        performanceWindow = this;
    return performanceWindow;
}

void MainWindow::capturePerformanceFrame()
{
    if (performance == nullptr)
        return;
    if (! performanceStatusClock.isValid() || performanceStatusClock.elapsed() >= 500)
    {
        performanceStatusClock.start();
        updatePerformanceStatus();
        if (! performance->isRunning())
            return;
    }
    if (! performance->wantsPicture())
        return;

    // PHINYX draws its window into the picture itself, at the recording's size, so
    // text stays sharp at 4K and nothing outside PHINYX can get in.
    auto* window = performanceTarget (performanceFollow);
    const auto canvas = performance->canvasSize();
    const auto logical = window->size();
    if (logical.isEmpty())
        return;
    const auto scale = std::min ((double) canvas.width() / logical.width(), (double) canvas.height() / logical.height());
    const QSize pixels ((int) std::lround (logical.width() * scale), (int) std::lround (logical.height() * scale));
    QImage picture (pixels, QImage::Format_RGB32);
    picture.fill (Qt::black);
    picture.setDevicePixelRatio (scale);
    window->render (&picture, QPoint(), QRegion(), QWidget::DrawWindowBackground | QWidget::DrawChildren);
    performance->submitWindow (std::move (picture));
}

void MainWindow::startPerformanceCapture()
{
    performanceFollow = perfui::loadSettings().followWindow;
    performanceWindow = nullptr;
    if (! performanceTimer.isActive())
        performanceTimer.start (std::max (2, 500 / std::max (1, performance->fps())));
    updatePerformanceStatus();
}

void MainWindow::stopPerformanceCaptureIfIdle()
{
    if (! performance->isRunning())
        performanceTimer.stop();
    updatePerformanceStatus();
}

void MainWindow::toggleVideoRecording()
{
    if (performance->isRecording())
    {
        QApplication::setOverrideCursor (Qt::WaitCursor);
        const auto path = QString::fromStdString (performance->stopRecording());
        QApplication::restoreOverrideCursor();
        stopPerformanceCaptureIfIdle();
        if (! path.isEmpty())
            statusBar()->showMessage (tr ("Video saved: %1").arg (QDir::toNativeSeparators (path)), 10000);
        return;
    }

    QApplication::setOverrideCursor (Qt::WaitCursor);
    const auto error = performance->startRecording (perfui::loadSettings());
    QApplication::restoreOverrideCursor();
    if (! error.empty())
    {
        updatePerformanceStatus();
        QMessageBox::warning (this, tr ("Record Video"), QString::fromStdString (error));
        return;
    }
    startPerformanceCapture();
    const auto st = performance->status();
    statusBar()->showMessage (tr ("Recording video with %1 to %2").arg (QString::fromStdString (st.recordEncoder),
                                                                       QDir::toNativeSeparators (QString::fromStdString (st.recordPath))), 6000);
}

void MainWindow::toggleStreaming()
{
    if (performance->isStreaming())
    {
        QApplication::setOverrideCursor (Qt::WaitCursor);
        performance->stopStreaming();
        QApplication::restoreOverrideCursor();
        stopPerformanceCaptureIfIdle();
        statusBar()->showMessage (tr ("The stream has ended"), 6000);
        return;
    }

    auto settings = perfui::loadSettings();
    if (settings.streamServer.empty() || settings.streamKey.empty())
    {
        // Nothing to stream to yet: the settings, on the Streaming page, first.
        performanceSettings();
        settings = perfui::loadSettings();
        if (settings.streamServer.empty())
        {
            updatePerformanceStatus();
            return;
        }
    }

    QApplication::setOverrideCursor (Qt::WaitCursor);
    const auto error = performance->startStreaming (settings);
    QApplication::restoreOverrideCursor();
    if (! error.empty())
    {
        updatePerformanceStatus();
        QMessageBox::warning (this, tr ("Go Live"), QString::fromStdString (error));
        return;
    }
    startPerformanceCapture();
    statusBar()->showMessage (tr ("Live with %1").arg (QString::fromStdString (performance->status().streamEncoder)), 6000);
}

void MainWindow::performanceSettings()
{
    PerformanceDialog dialog (perfui::loadSettings(), performance->isRunning(), this);
    if (dialog.exec() == QDialog::Accepted)
        perfui::saveSettings (dialog.settings());
}

void MainWindow::updatePerformanceStatus()
{
    if (performance == nullptr || performanceBadge == nullptr)
        return;

    // An output that stopped by itself: say why, and show it as stopped.
    if (const auto problem = performance->takeProblem(); ! problem.empty())
    {
        if (problem.rfind ("The stream", 0) == 0)
            performance->stopStreaming();
        else
            performance->stopRecording();
        if (! performance->isRunning())
            performanceTimer.stop();
        // Not modal: the other output, if any, carries on.
        auto* box = new QMessageBox (QMessageBox::Warning, tr ("Performance"), QString::fromStdString (problem), QMessageBox::Ok, this);
        box->setAttribute (Qt::WA_DeleteOnClose);
        box->show();
    }

    const auto st = performance->status();
    commands.action (QStringLiteral ("performance.record"))->setChecked (st.recording);
    commands.action (QStringLiteral ("performance.stream"))->setChecked (st.streaming);

    QStringList parts;
    if (st.recording)
        parts << tr ("<span style='color:#e5484d'>●</span> REC %1 · %2 MB").arg (clock (st.recordSeconds)).arg (st.recordBytes / 1000000);
    if (st.streaming)
        parts << tr ("<span style='color:#e5484d'>●</span> LIVE %1").arg (clock (st.streamSeconds));
    if (! parts.isEmpty() && st.droppedFrames > 0)
        parts << tr ("%1 frames dropped").arg (st.droppedFrames);
    performanceBadge->setText (parts.join (QStringLiteral ("  ")));
    performanceBadge->setVisible (! parts.isEmpty());
    performanceBadge->setToolTip (st.recording ? QDir::toNativeSeparators (QString::fromStdString (st.recordPath)) : QString());
}

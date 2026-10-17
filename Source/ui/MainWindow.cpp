// SPDX-License-Identifier: AGPL-3.0-only
#include "MainWindow.h"
#include "Dialogs.h"
#include "WaveformView.h"

#include <QApplication>
#include <QCloseEvent>
#include <QFileDialog>
#include <QFileInfo>
#include <QLabel>
#include <QMenuBar>
#include <QMessageBox>
#include <QStatusBar>
#include <QTimer>
#include <QtConcurrent/QtConcurrentRun>

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

    registerCommands();
    commands.load();
    buildMenus();

    connect (waveform, &WaveformView::cursorRequested, this, &MainWindow::setCursor);

    connect (&peakWatcher, &QFutureWatcher<std::shared_ptr<WaveformPeaks>>::finished, this, [this]
    {
        if (auto peaks = peakWatcher.result())
            waveform->setPeaks (peaks);
        else
            waveform->setLoading (false);
    });

    auto* uiTimer = new QTimer (this);
    connect (uiTimer, &QTimer::timeout, this, [this]
    {
        engine.dispatchMessages();
        waveform->setCursorSeconds (engine.position());
        updateStatus();
    });
    uiTimer->start (30);

    updateCommandStates();
    waveform->setFocus();
}

MainWindow::~MainWindow()
{
    if (cancelPeaks)
        cancelPeaks->store (true);

    peakWatcher.waitForFinished();
}

void MainWindow::registerCommands()
{
    using KS = QKeySequence;
    auto add = [this] (const char* id, const QString& cat, const QString& text,
                       QList<QKeySequence> keys, std::function<void()> fn)
    {
        addAction (commands.add (QString::fromLatin1 (id), cat, text, keys, std::move (fn)));
    };

    add ("file.open", Category::file, tr ("&Open..."), { KS::Open }, [this] { showOpenDialog(); });
    add ("file.quit", Category::file, tr ("&Quit"), { KS (Qt::CTRL | Qt::Key_Q) }, [this] { close(); });

    add ("transport.playStop", Category::transport, tr ("&Play / Stop"), { KS (Qt::Key_Space) }, [this] { togglePlayback(); });
    add ("transport.stop", Category::transport, tr ("&Stop"), { KS (Qt::Key_K) }, [this] { engine.stop(); });
    add ("transport.start", Category::transport, tr ("Go to S&tart"), { KS (Qt::Key_Home) }, [this] { setCursor (0.0); });
    add ("transport.end", Category::transport, tr ("Go to &End"), { KS (Qt::Key_End) }, [this] { setCursor (engine.length()); });
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

    add ("options.audio", Category::options, tr ("&Audio Settings..."), { KS (Qt::CTRL | Qt::Key_Comma) },
         [this] { AudioSettingsDialog (engine, this).exec(); });
    add ("options.keymap", Category::options, tr ("&Keyboard Shortcuts..."), { KS (Qt::CTRL | Qt::SHIFT | Qt::Key_K) },
         [this] { KeymapDialog (commands, this).exec(); });

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
    // Menu titles carry & mnemonics, so Alt+F, Alt+T, Alt+V, Alt+O, Alt+H open them.
    const QList<QPair<QString, QStringList>> layout {
        { tr ("&File"),      { "file.open", "-", "file.quit" } },
        { tr ("&Transport"), { "transport.playStop", "transport.stop", "-", "transport.start", "transport.end",
                               "cursor.back", "cursor.forward", "cursor.backLarge", "cursor.forwardLarge" } },
        { tr ("&View"),      { "view.zoomIn", "view.zoomOut", "view.zoomFit" } },
        { tr ("&Options"),   { "options.audio", "options.keymap" } },
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

void MainWindow::openMenuBar()
{
    if (auto first = menuBar()->actions(); ! first.isEmpty())
    {
        menuBar()->setFocus (Qt::ShortcutFocusReason);
        menuBar()->setActiveAction (first.first());
    }
}

void MainWindow::updateCommandStates()
{
    const bool hasAudio = engine.hasAudio();

    for (const auto* id : { "transport.playStop", "transport.stop", "transport.start", "transport.end",
                            "cursor.back", "cursor.forward", "cursor.backLarge", "cursor.forwardLarge",
                            "view.zoomIn", "view.zoomOut", "view.zoomFit" })
        commands.action (QString::fromLatin1 (id))->setEnabled (hasAudio);
}

void MainWindow::updateStatus()
{
    QString text;

    if (currentFile.isEmpty())
        text = tr ("No file");
    else
        text = QStringLiteral ("%1   %2 / %3   %4")
                   .arg (QFileInfo (currentFile).fileName(), formatTime (engine.position()),
                         formatTime (engine.length()), engine.isPlaying() ? tr ("Playing") : tr ("Stopped"));

    text += QStringLiteral ("   |   ") + QString::fromStdString (engine.deviceDescription());

    if (statusLabel->text() != text)
        statusLabel->setText (text);
}

void MainWindow::showOpenDialog()
{
    QStringList patterns;
    for (const auto& ext : AudioEngine::supportedExtensions())
        patterns << QString::fromStdString (ext);

    const auto path = QFileDialog::getOpenFileName (this, tr ("Open Audio File"),
                                                    currentFile.isEmpty() ? QString() : QFileInfo (currentFile).absolutePath(),
                                                    tr ("Audio files (%1);;All files (*)").arg (patterns.join (QLatin1Char (' '))));
    if (! path.isEmpty())
        openFile (path);
}

void MainWindow::openFile (const QString& path)
{
    std::string error;
    if (! engine.load (path.toStdString(), error))
    {
        QMessageBox::warning (this, tr ("Open failed"), QString::fromStdString (error));
        return;
    }

    currentFile = path;
    setWindowTitle (QFileInfo (path).fileName() + QStringLiteral (" - " APP_DISPLAY_NAME));
    updateCommandStates();

    if (cancelPeaks)
        cancelPeaks->store (true);

    cancelPeaks = std::make_shared<std::atomic<bool>> (false);
    waveform->clear();
    waveform->setLoading (true);

    peakWatcher.setFuture (QtConcurrent::run ([p = path.toStdString(), cancel = cancelPeaks]
    {
        auto peaks = std::make_shared<WaveformPeaks>();
        return AudioEngine::computePeaks (p, *peaks, *cancel) ? peaks : nullptr;
    }));

    waveform->setFocus();
}

void MainWindow::togglePlayback()
{
    if (engine.isPlaying())
        engine.stop();
    else
        engine.play();
}

void MainWindow::setCursor (double seconds)
{
    engine.setPosition (seconds);
    waveform->setCursorSeconds (engine.position());
}

void MainWindow::moveCursorBy (double seconds)
{
    setCursor (engine.position() + seconds);
}

void MainWindow::closeEvent (QCloseEvent* e)
{
    engine.stop();
    commands.save();
    e->accept();
}

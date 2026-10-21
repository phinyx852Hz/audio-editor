// SPDX-License-Identifier: AGPL-3.0-only
// The Mixer menu: the 8-track mixer (mixer/MixerEngine.h) in its own window, heard
// with the Sampler and Slicer through the engine's instrument slot, played from the
// MIDI inputs, and mixed down into a new tab of the editor.
#include "MainWindow.h"
#include "MixerWindow.h"
#include "../mixer/MixerEngine.h"
#include "../sampler/SamplerEngine.h"
#include "../sampler/SlicerEngine.h"

#include <QEventLoop>
#include <QFutureWatcher>
#include <QMessageBox>
#include <QProgressDialog>
#include <QStatusBar>
#include <QtConcurrent>

void MainWindow::registerMixerCommands()
{
    using KS = QKeySequence;
    const auto category = tr ("Mixer");
    auto add = [this, &category] (const char* id, const QString& text, QList<QKeySequence> keys, std::function<void()> fn)
    {
        auto* action = commands.add (QString::fromLatin1 (id), category, text, keys, std::move (fn));
        addAction (action);
        return action;
    };

    mixerEngine = std::make_unique<mixer::Engine>();

    add ("mixer.window", tr ("&Mixer"), { KS (Qt::Key_F3) }, [this] { showMixer(); })
        ->setStatusTip (tr ("The 8-track mixer: each track plays an audio file, a MIDI file through an instrument, or both"));
    add ("mixer.new", tr ("&New Mix"), {}, [this] { showMixer(); mixerWin->newMix(); });
    add ("mixer.open", tr ("&Open Mix..."), {}, [this] { showMixer(); mixerWin->openMix(); });
    add ("mixer.save", tr ("&Save Mix"), {}, [this] { showMixer(); mixerWin->saveMix (false); });
    add ("mixer.saveAs", tr ("Save Mix &As..."), {}, [this] { showMixer(); mixerWin->saveMix (true); });
    add ("mixer.addTrack", tr ("Add &Track"), {}, [this] { showMixer(); mixerWin->addTrack(); });
    add ("mixer.removeTrack", tr ("&Remove Last Track"), {}, [this] { showMixer(); mixerWin->removeTrack(); });
    add ("mixer.playStop", tr ("&Play or Stop the Mix"), { KS (Qt::SHIFT | Qt::Key_F3) }, [this]
    {
        mixerWindow();
        mixerWin->togglePlay();
    });
    add ("mixer.mixdown", tr ("Mix &Down to Editor"), { KS (Qt::CTRL | Qt::Key_F3) }, [this] { mixDownToEditor(); })
        ->setStatusTip (tr ("Render the whole mix, with the instruments' tails, into a new stereo tab"));
}

MixerWindow* MainWindow::mixerWindow()
{
    if (mixerWin == nullptr)
    {
        const auto rate = engine.deviceSampleRate() > 0.0 ? engine.deviceSampleRate() : 48000.0;
        mixerEngine->prepare (rate);
        MixerWindow::Hooks hooks;
        hooks.samplerSound = [this] { return std::make_pair (samplerEngine->sample(), samplerEngine->settings()); };
        hooks.slicerSound = [this] { return std::make_pair (slicerEngine->sample(), slicerEngine->kit()); };
        mixerWin = new MixerWindow (*mixerEngine, engine, std::move (hooks), this);
        connect (mixerWin, &MixerWindow::mixdownRequested, this, [this] { mixDownToEditor(); });
        connect (mixerWin, &MixerWindow::midiInputsRequested, this, [this] { midiInputsDialog(); });
        connect (mixerWin, &MixerWindow::activated, this, [this] { midiTarget = 2; });
        connect (mixerWin, &MixerWindow::playStarted, this, [this] { playInstruments(); });
        connect (mixerWin, &MixerWindow::closed, this, [this]
        {
            mixerEngine->panic();
            instrumentClosed();
            activateWindow();
            waveform->setFocus();
        });
        mixerWin->restoreAutosave();
        updateMidiStatus();
    }
    return mixerWin;
}

void MainWindow::showMixer()
{
    auto* w = mixerWindow();
    midiTarget = 2;
    playInstruments();
    w->show();
    w->raise();
    w->activateWindow();
    w->setFocus();
}

void MainWindow::mixDownToEditor()
{
    auto* w = mixerWindow();
    if (mixerEngine->length() <= 0)
    {
        QMessageBox::information (w, tr ("Mix Down to Editor"), tr ("Put an audio or MIDI file on a track first."));
        return;
    }
    mixerEngine->stop();

    auto progress = std::make_shared<std::atomic<float>> (0.0f);
    auto cancel = std::make_shared<std::atomic<bool>> (false);
    QProgressDialog dialog (tr ("Mixing down..."), tr ("Cancel"), 0, 1000, w);
    dialog.setWindowTitle (tr ("Mix Down to Editor"));
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
    watcher.setFuture (QtConcurrent::run ([m = mixerEngine.get(), progress, cancel]
    {
        Result r;
        r.data = m->mixdown (10.0, r.error, progress.get(), cancel.get());
        return r;
    }));
    loop.exec();
    poll.stop();
    dialog.reset();

    auto result = watcher.result();
    if (result.data == nullptr || result.data->length() == 0)
    {
        if (result.error != "Cancelled.")
            QMessageBox::warning (w, tr ("Mix Down to Editor"), result.error.empty() ? tr ("The mix made no sound.") : QString::fromStdString (result.error));
        return;
    }

    Tags tags;
    tags[tag::title] = w->mixName().toStdString();
    const auto options = saveOptions;
    newTab();
    doc.reset (std::move (result.data), {}, {}, std::move (tags));
    doc.markUnsaved();
    saveOptions = options;
    refreshFromDocument (false);
    activateWindow();
    raise();
    waveform->setFocus();
    statusBar()->showMessage (tr ("Mixed down %1 into a new tab").arg (w->mixName()), 8000);
}

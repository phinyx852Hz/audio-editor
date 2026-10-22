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
#include <QScreen>
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
        connect (mixerWin, &MixerWindow::editClipRequested, this, [this] (int track, quint32 clip) { editMixerClip (track, clip); });
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
    returnMixerEdits();
    midiTarget = 2;
    playInstruments();
    // Over the editor, between the panels on either side of it; tall enough for the strips
    // even when the editor is short, but on the screen.
    if (auto* centre = centralWidget(); centre != nullptr && isVisible())
    {
        const auto topLeft = centre->mapToGlobal (QPoint (0, 0));
        QRect area (topLeft, centre->size());
        area.adjust (4, 4, -4, -4);
        const auto screenArea = screen() != nullptr ? screen()->availableGeometry() : area;
        area.setHeight (std::min (std::max (area.height(), 700), screenArea.height()));
        if (area.bottom() > screenArea.bottom())
            area.moveBottom (screenArea.bottom());
        w->setGeometry (area);
    }
    w->show();
    w->raise();
    w->activateWindow();
    w->setFocus();
}

// A clip from the mixer in a tab of its own; the mixer steps aside but keeps going.
void MainWindow::editMixerClip (int track, quint32 clip)
{
    QString name;
    auto data = mixerWin->clipAudio (track, clip, name);
    if (data == nullptr)
        return;
    mixerWin->hide();
    const auto options = saveOptions;
    newTab();
    Tags tags;
    tags[tag::title] = name.toStdString();
    doc.reset (std::move (data), {}, {}, std::move (tags));
    saveOptions = options;
    tabs[(size_t) activeTab].mixerTrack = track;
    tabs[(size_t) activeTab].mixerClip = clip;
    tabs[(size_t) activeTab].mixerName = name;
    refreshFromDocument (false);
    activateWindow();
    raise();
    waveform->setFocus();
    statusBar()->showMessage (tr ("Editing %1 from the mixer: F3 puts it back on the track and brings the mixer back").arg (name), 10000);
}

// The tabs opened from the mixer go back to it: an edited sound replaces its clip, and the tab closes.
void MainWindow::returnMixerEdits()
{
    for (;;)
    {
        int index = -1;
        for (int i = 0; i < (int) tabs.size() && index < 0; ++i)
            if (tabs[(size_t) i].mixerClip != 0)
                index = i;
        if (index < 0)
            return;
        switchToTab (index);
        const auto track = tabs[(size_t) index].mixerTrack;
        const auto clip = tabs[(size_t) index].mixerClip;
        const auto name = tabs[(size_t) index].mixerName;
        tabs[(size_t) index].mixerClip = 0;
        if (doc.isOpen() && doc.isModified() && doc.current().data != nullptr)
        {
            QString error;
            if (! mixerWin->replaceClipAudio (track, clip, *doc.current().data, name, error))
            {
                QMessageBox::warning (this, tr ("Mixer"), error + tr ("\nThe tab stays open so the edit is not lost."));
                continue;
            }
        }
        doc.markSaved();   // it lives on in the mixer now
        closeTab();
    }
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

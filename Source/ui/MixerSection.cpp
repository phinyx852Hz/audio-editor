// SPDX-License-Identifier: AGPL-3.0-only
// The Mixer menu: the 8-track mixer (mixer/MixerEngine.h) in its own window, heard
// with the Sampler and Slicer through the engine's instrument slot, played from the
// MIDI inputs, and mixed down into a new tab of the editor.
#include "MainWindow.h"
#include "MixAssistant.h"
#include "FindPanel.h"
#include "MixerWindow.h"
#include "Panels.h"
#include "Settings.h"
#include "../mixer/MixerEngine.h"
#include "../sampler/SamplerEngine.h"
#include "../sampler/SlicerEngine.h"

#include <QApplication>
#include <QDockWidget>
#include <QEventLoop>
#include <QFileInfo>
#include <QFutureWatcher>
#include <QLayout>
#include <QMessageBox>
#include <QProgressDialog>
#include <QScreen>
#include <QStackedWidget>
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

    // F3 opens the mixer in the editor's place, and F3 again (or Ctrl+W) closes it; the mix
    // stays as it was left, so the next F3 comes back to it.
    add ("mixer.window", tr ("&Mixer"), { KS (Qt::Key_F3) }, [this]
    {
        const bool inFront = mixerWin != nullptr && mixerWin->isVisible() && (! mixerDocked() || centralStack->currentWidget() == mixerWin);
        if (inFront)
            mixerWin->close();
        else
            showMixer();
    })->setStatusTip (tr ("The 8-track mixer: each track plays an audio file, a MIDI file through an instrument, or both"));
    add ("mixer.dock", tr ("&Undock / Dock Mixer"), { KS (Qt::CTRL | Qt::SHIFT | Qt::Key_F3) }, [this] { placeMixer (! mixerDocked()); })
        ->setStatusTip (tr ("The mixer in the editor's place, or in a window of its own (also Ctrl+Alt+P from the mixer)"));
    add ("mixer.new", tr ("&New Project"), {}, [this] { showMixer(); mixerWin->newMix(); });
    add ("mixer.newFromSet", tr ("New Project from &Selector Set"), {}, [this]
    {
        showMixer();
        QStringList names;
        QList<QColor> colours;
        const auto set = findWidget->currentSet();
        for (int c = 0; c < FindPanel::numColours; ++c)
        {
            names.push_back (findWidget->markName (set, c));
            colours.push_back (findWidget->markColour (set, c));
        }
        mixerWin->newMixFromSet (findWidget->setName (set), names, colours);
    })->setStatusTip (tr ("A new project of 8 tracks with the names and colours of the set chosen in the Find panel's Selector, from the bottom up"));
    add ("mixer.open", tr ("&Open Project..."), {}, [this] { showMixer(); mixerWin->openMix(); })
        ->setStatusTip (tr ("Open a PHI project (.phi): the mix, the patchbay and the view as they were saved"));
    add ("mixer.save", tr ("&Save Project"), {}, [this] { mixerWindow()->saveMix (false); })
        ->setStatusTip (tr ("Save the mix, the patchbay and the view in a PHI project (.phi)"));
    add ("mixer.saveAs", tr ("Save Project &As..."), {}, [this] { mixerWindow()->saveMix (true); });
    add ("mixer.addTrack", tr ("Add &Track"), {}, [this] { showMixer(); mixerWin->addTrack(); });
    add ("mixer.removeTrack", tr ("&Remove Last Track"), {}, [this] { showMixer(); mixerWin->removeTrack(); });
    add ("mixer.playStop", tr ("&Play or Stop the Mix"), { KS (Qt::SHIFT | Qt::Key_F3) }, [this]
    {
        mixerWindow();
        mixerWin->togglePlay();
    });
    add ("mixer.mixdown", tr ("Mix &Down to Editor"), { KS (Qt::CTRL | Qt::Key_F3) }, [this] { mixDownToEditor(); })
        ->setStatusTip (tr ("Render the whole mix, with the instruments' tails, into a new stereo tab"));
    add ("mixer.sendToLauncher", tr ("Send to Clip &Launcher"), { KS (Qt::ALT | Qt::Key_F3) }, [this] { sendToLauncher(); })
        ->setStatusTip (tr ("The sound in the editor (or its selection) into the mixer's clip launcher, under its cursor or in the selected track's first empty slot"));
    add ("mixer.sendToTimeline", tr ("Send to &Timeline"), { KS (Qt::ALT | Qt::SHIFT | Qt::Key_F3) }, [this] { sendToTimeline(); })
        ->setStatusTip (tr ("The sound in the editor (or its selection) onto the editor's track at the playhead"));
    add ("mixer.editorTrack", tr ("Editor &Track"), { KS (Qt::SHIFT | Qt::Key_F4) }, [this]
    {
        showEditorTrack (commands.action (QStringLiteral ("mixer.editorTrack"))->isChecked());
    })->setStatusTip (tr ("One track of the project under the editor's waveform: Audio only, MIDI only or Audio + MIDI, "
                          "its clip launcher slots, its clips across time, recording and the MIDI tools"));
    commands.action (QStringLiteral ("mixer.editorTrack"))->setCheckable (true);
    commands.action (QStringLiteral ("mixer.editorTrack"))->setChecked (appSettings().value (QStringLiteral ("view/editorTrack"), true).toBool());
    if (commands.action (QStringLiteral ("mixer.editorTrack"))->isChecked())
        QTimer::singleShot (0, this, [this] { showEditorTrack (true); });   // once the window is built
    add ("mixer.trackToEditor", tr ("&Render the Editor's Track to a Tab"), {}, [this]
    {
        mixerWindow();
        mixDownToEditor (mixerWin->editorTrack());
    })->setStatusTip (tr ("The editor's track as it sounds, its MIDI through its instrument and its audio, as one sound in a new tab"));
    add ("mixer.assistant", tr ("Mix &Assistant..."), { KS (Qt::ALT | Qt::SHIFT | Qt::Key_X) }, [this] { mixAssistant(); })
        ->setStatusTip (tr ("Sets the faders and pans of the mix by what each track is, with headroom for mastering"));
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
        hooks.projectState = [this] { return projectState(); };
        hooks.setProjectState = [this] (const QJsonObject& project) { setProjectState (project); };
        hooks.resetProject = [this] { resetPatchBay(); };
        hooks.selectorSet = [this] (QStringList& names, QList<QColor>& colours)
        {
            const auto set = findWidget->currentSet();
            for (int c = 0; c < FindPanel::numColours; ++c)
            {
                names.push_back (findWidget->markName (set, c));
                colours.push_back (findWidget->markColour (set, c));
            }
        };
        mixerWin = new MixerWindow (*mixerEngine, engine, std::move (hooks), this);
        connect (mixerWin, &MixerWindow::mixdownRequested, this, [this] { mixDownToEditor(); });
        connect (mixerWin, &MixerWindow::midiInputsRequested, this, [this] { midiInputsDialog(); });
        connect (mixerWin, &MixerWindow::activated, this, [this] { midiTarget = 2; });
        connect (mixerWin, &MixerWindow::playStarted, this, [this] { playInstruments(); });
        connect (mixerWin, &MixerWindow::editClipRequested, this, [this] (int track, quint32 clip) { editMixerClip (track, clip); });
        connect (mixerWin, &MixerWindow::devicePanelChanged, this, [this] { updateChainDock(); });
        connect (centralStack, &QStackedWidget::currentChanged, this, [this] { updateChainDock(); updateFindDock(); });
        connect (mixerWin, &MixerWindow::finderWanted, this, [this] (bool show) { mixerFinder (show); });
        // The Find panel closed by hand while it was the Finder: the Inspector takes its place.
        connect (findPanel->dock(), &QDockWidget::visibilityChanged, mixerWin, [this] (bool visible)
        {
            if (! visible && ! hidingFindForMixer && mixerDocked() && centralStack->currentWidget() == mixerWin
                && mixerWin->sideFinderShown() && ! findPanel->isShown())
                mixerWin->showSideFinder (false);
        });
        focusFollowsMouse->addAreas (mixerWin->focusAreas());
        connect (mixerWin, &MixerWindow::closed, this, [this]
        {
            mixerEngine->panic();
            instrumentClosed();
            if (mixerWin->isFullScreen())
            {
                mixerWin->showNormal();
                if (mixerFullFromDock)
                    placeMixer (true);
            }
            mixerFullFromDock = false;
            showEditorPage();
            activateWindow();
            waveform->setFocus();
        });
        // In the editor's place, the mixer has the keyboard only while it has the focus.
        connect (qApp, &QApplication::focusChanged, mixerWin, [this] (QWidget* old, QWidget* now)
        {
            const auto in = [this] (QWidget* w) { return w != nullptr && (w == mixerWin || mixerWin->isAncestorOf (w)); };
            if (mixerDocked() && in (now) && ! in (old))
                midiTarget = 2;
        });
        mixerWin->restoreAutosave();
        // The tracks follow the Selector set, bottom category first, as long as they keep its names.
        mixerWin->applySelectorSet();
        connect (findWidget, &FindPanel::setChanged, mixerWin, [this] { mixerWin->applySelectorSet(); });
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
    if (! appSettings().value (QStringLiteral ("mixer/undocked"), false).toBool())
    {
        placeMixer (true);
        return;
    }
    if (mixerDocked())
    {
        placeMixer (false);
        return;
    }
    // Over the editor, between the panels on either side of it; tall enough for the strips
    // even when the editor is short, but on the screen.
    if (isVisible())
    {
        const auto topLeft = centralStack->mapToGlobal (QPoint (0, 0));
        QRect area (topLeft, centralStack->size());
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
    showEditorPage();
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

void MainWindow::updateChainDock()
{
    // The mixer has its own along its bottom; the editor's would show twice.
    auto* dock = chainPanel->dock();
    const bool mixerOwns = mixerDocked() && centralStack->currentWidget() == mixerWin && mixerWin->devicePanelShown();
    if (mixerOwns && ! chainPanel->isPoppedOut() && dock->isVisible())
    {
        chainHiddenForMixer = true;
        dock->hide();
    }
    else if (! mixerOwns && chainHiddenForMixer)
    {
        chainHiddenForMixer = false;
        dock->show();
    }
}

void MainWindow::mixerFinder (bool show)
{
    if (! mixerDocked() || centralStack->currentWidget() != mixerWin)
        return;
    if (show)
    {
        findHiddenForMixer = false;
        findPanel->show (true);
        return;
    }
    if (findPanel->isShown())
    {
        findHiddenForMixer = true;
        hidingFindForMixer = true;
        findPanel->hide();
        hidingFindForMixer = false;
    }
}

void MainWindow::updateFindDock()
{
    const bool mixerInFront = mixerDocked() && centralStack->currentWidget() == mixerWin;
    if (mixerInFront)
        mixerWin->showSideFinder (mixerWin->sideFinderShown());
    else if (findHiddenForMixer)
    {
        findHiddenForMixer = false;
        findPanel->show (false);
    }
}

void MainWindow::sendToLauncher()
{
    if (! doc.isOpen() || doc.isAlbum() || doc.current().data == nullptr)
    {
        statusBar()->showMessage (tr ("Open a sound first"), 5000);
        return;
    }
    const auto& data = *doc.current().data;
    const bool part = selEnd > selStart;
    const auto sound = part ? data.slice (selStart, selEnd) : nullptr;
    auto name = QFileInfo (QString::fromStdString (doc.path())).completeBaseName();
    if (name.isEmpty())
        name = tabName (doc);
    if (part)
        name += tr (" part");
    auto* w = mixerWindow();
    showMixer();
    QString error;
    if (! w->sendAudioToLauncher (part ? *sound : data, name, error))
        QMessageBox::warning (w, tr ("Send to Clip Launcher"), error);
}

void MainWindow::sendToTimeline()
{
    if (! doc.isOpen() || doc.isAlbum() || doc.current().data == nullptr)
    {
        statusBar()->showMessage (tr ("Open a sound first"), 5000);
        return;
    }
    const auto& data = *doc.current().data;
    const bool part = selEnd > selStart;
    const auto sound = part ? data.slice (selStart, selEnd) : nullptr;
    auto name = QFileInfo (QString::fromStdString (doc.path())).completeBaseName();
    if (name.isEmpty())
        name = tabName (doc);
    if (part)
        name += tr (" part");
    auto* w = mixerWindow();
    if (! commands.action (QStringLiteral ("mixer.editorTrack"))->isChecked())
        commands.action (QStringLiteral ("mixer.editorTrack"))->trigger();   // where it went, in view
    QString error;
    if (! w->sendAudioToTimeline (part ? *sound : data, name, error))
        QMessageBox::warning (this, tr ("Send to Timeline"), error);
}

void MainWindow::showEditorTrack (bool on)
{
    appSettings().setValue (QStringLiteral ("view/editorTrack"), on);
    if (on && trackHolder->layout()->count() == 0)
    {
        auto* w = mixerWindow();
        trackHolder->layout()->addWidget (w->editorTrackView (trackHolder));
        connect (w, &MixerWindow::trackToEditorRequested, this, [this] (int track) { mixDownToEditor (track); });
        // Played or recorded on from the editor: the keyboard and the sound go to the mixer.
        connect (w, &MixerWindow::activated, this, [this] { playInstruments(); });
        playInstruments();
    }
    trackHolder->setVisible (on);
    if (auto* a = commands.action (QStringLiteral ("mixer.editorTrack")); a->isChecked() != on)
    {
        const QSignalBlocker quiet (a);
        a->setChecked (on);
    }
}

bool MainWindow::mixerDocked() const
{
    return mixerWin != nullptr && centralStack != nullptr && mixerWin->parentWidget() == centralStack;
}

void MainWindow::showEditorPage()
{
    if (centralStack->currentWidget() != editorPage)
        centralStack->setCurrentWidget (editorPage);
}

// The mixer in the editor's place (the editor steps behind it, the panels stay around it),
// or in a window of its own over the editor. Either way it shows, with the keyboard.
void MainWindow::placeMixer (bool docked)
{
    auto* w = mixerWindow();
    appSettings().setValue (QStringLiteral ("mixer/undocked"), ! docked);
    if (docked)
    {
        if (! mixerDocked())
        {
            if (w->isFullScreen())
                w->showNormal();
            centralStack->addWidget (w);   // no longer a window of its own
        }
        centralStack->setCurrentWidget (w);
        w->show();
        w->showSideFinder (w->sideFinderShown());
        activateWindow();
        w->setFocus();
        midiTarget = 2;
        return;
    }
    if (mixerDocked())
    {
        const QRect area (centralStack->mapToGlobal (QPoint (0, 0)), centralStack->size());
        centralStack->removeWidget (w);
        showEditorPage();
        w->setParent (this, Qt::Window);
        w->setGeometry (area.adjusted (4, 4, -4, -4));
        updateFindDock();
    }
    w->showSideFinder (w->sideFinderShown());
    w->show();
    w->raise();
    w->activateWindow();
    w->setFocus();
}

void MainWindow::mixDownToEditor (int track)
{
    auto* w = mixerWindow();
    const auto title = track < 0 ? tr ("Mix Down to Editor") : tr ("Render to Editor");
    // A track of two kinds is one sound only once rendered (owner, 2026-10-26: "it will be mixed
    // down through the mastering chain or it can be sent to the main editor").
    if (mixerEngine->length() <= 0 || (track >= 0 && mixerEngine->clips (track).empty()))
    {
        QMessageBox::information (this, title, track < 0 ? tr ("Put an audio or MIDI file on a track first.")
                                                         : tr ("%1 has nothing on its timeline to render. Send a launcher clip to the timeline first.").arg (w->trackName (track)));
        return;
    }
    mixerEngine->stop();

    auto progress = std::make_shared<std::atomic<float>> (0.0f);
    auto cancel = std::make_shared<std::atomic<bool>> (false);
    QProgressDialog dialog (track < 0 ? tr ("Mixing down...") : tr ("Rendering %1...").arg (w->trackName (track)), tr ("Cancel"), 0, 1000, this);
    dialog.setWindowTitle (title);
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
    watcher.setFuture (QtConcurrent::run ([m = mixerEngine.get(), progress, cancel, track]
    {
        Result r;
        r.data = m->mixdown (10.0, r.error, progress.get(), cancel.get(), track);
        return r;
    }));
    loop.exec();
    poll.stop();
    dialog.reset();

    auto result = watcher.result();
    if (result.data == nullptr || result.data->length() == 0)
    {
        if (result.error != "Cancelled.")
            QMessageBox::warning (this, title, result.error.empty() ? tr ("The mix made no sound.") : QString::fromStdString (result.error));
        return;
    }

    Tags tags;
    tags[tag::title] = (track < 0 ? w->mixName() : w->mixName() + QStringLiteral (" - ") + w->trackName (track)).toStdString();
    const auto options = saveOptions;
    newTab();
    doc.reset (std::move (result.data), {}, {}, std::move (tags));
    doc.markUnsaved();
    saveOptions = options;
    refreshFromDocument (false);
    activateWindow();
    raise();
    waveform->setFocus();
    statusBar()->showMessage (track < 0 ? tr ("Mixed down %1 into a new tab").arg (w->mixName())
                                        : tr ("Rendered %1 into a new tab").arg (w->trackName (track)), 8000);
}

void MainWindow::mixAssistant()
{
    auto* w = mixerWindow();
    showMixer();
    if (mixerEngine->length() <= 0)
    {
        QMessageBox::information (w, tr ("Mix Assistant"), tr ("Put an audio or MIDI file on a track first."));
        return;
    }
    QStringList names;
    for (int t = 0; t < mixerEngine->numTracks(); ++t)
        names << w->trackName (t);
    MixAssistant::References refs;
    for (const auto& r : references)
        refs.names << QFileInfo (r.path).fileName();
    refs.current = currentReference;
    refs.load = [this] (int row) -> AudioDataPtr
    {
        if (row < 0 || row >= (int) references.size() || ! loadReference (row))
            return nullptr;
        return references[(size_t) row].original;
    };
    MixAssistant dialog (*mixerEngine, names, std::move (refs), [w] { w->togglePlay(); }, [w] { w->stripsChanged(); }, w);
    dialog.applyEq = [w] (int track, const std::vector<std::pair<double, double>>& cuts) { return w->applyEqCuts (track, cuts); };
    dialog.exec();
}

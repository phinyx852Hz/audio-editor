// SPDX-License-Identifier: AGPL-3.0-only
// The Sample menu: the Sampler (sampler/SamplerEngine.h) and the Slicer
// (sampler/SlicerEngine.h) in their own windows, fed from the editor, and MIDI inputs
// that play them.
#include "MainWindow.h"
#include "FindPanel.h"
#include "MixerWindow.h"
#include "PatchBayWindow.h"
#include "Panels.h"
#include "SamplerWindow.h"
#include "SlicerWindow.h"
#include "Settings.h"
#include "../engine/ControlSurfaces.h"
#include "../engine/MidiInputs.h"
#include "../mixer/MixerEngine.h"
#include "../patchbay/PatchBay.h"
#include "../sampler/SamplerEngine.h"
#include "../sampler/SlicerEngine.h"

#include <QDialog>
#include <QDockWidget>
#include <QTimer>
#include <QDialogButtonBox>
#include <QApplication>
#include <QFileInfo>
#include <QLabel>
#include <QListWidget>
#include <QPushButton>
#include <QScrollArea>
#include <QMessageBox>
#include <QStatusBar>
#include <QVBoxLayout>
#include <QtConcurrent>

void MainWindow::registerSamplerCommands()
{
    using KS = QKeySequence;
    const auto category = tr ("Sample");
    auto add = [this, &category] (const char* id, const QString& text, QList<QKeySequence> keys, std::function<void()> fn)
    {
        auto* action = commands.add (QString::fromLatin1 (id), category, text, keys, std::move (fn));
        addAction (action);
        return action;
    };

    samplerEngine = std::make_unique<sampler::Engine>();
    slicerEngine = std::make_unique<slicer::Engine>();
    midiInputs = std::make_unique<MidiInputs>();

    add ("sample.send", tr ("&Send to Simplest"), { KS (Qt::Key_F7) }, [this] { sendToSampler (false); })
        ->setStatusTip (tr ("Play the selection (or the whole file) across the keyboard in Simplest; later edits follow it"));
    add ("sample.window", tr ("&Simplest"), { KS (Qt::SHIFT | Qt::Key_F7) }, [this] { showSampler(); });
    add ("sample.refresh", tr ("&Refresh from Editor"), { KS (Qt::CTRL | Qt::Key_F7) }, [this] { sendToSampler (true); })
        ->setStatusTip (tr ("Take the sound Simplest came from again, keeping its start, end and loop"));
    add ("slice.send", tr ("Send to Sli&cer"), { KS (Qt::Key_F8) }, [this] { sendToSlicer (false); })
        ->setStatusTip (tr ("Cut the selection (or the whole file) into slices on the Slicer's 128 pads; later edits follow it"));
    add ("slice.window", tr ("Slicer W&indow"), { KS (Qt::SHIFT | Qt::Key_F8) }, [this] { showSlicer(); });
    add ("slice.refresh", tr ("Re&fresh Slicer from Editor"), { KS (Qt::CTRL | Qt::Key_F8) }, [this] { sendToSlicer (true); })
        ->setStatusTip (tr ("Take the sound the Slicer came from again, keeping its slices"));
    add ("sample.midiInputs", tr ("&MIDI Inputs..."), {}, [this] { midiInputsDialog(); })
        ->setStatusTip (tr ("Choose the MIDI keyboards and controllers (Push, pads) that play Simplest"));
    add ("sample.panic", tr ("All Notes &Off"), { KS (Qt::SHIFT | Qt::Key_Escape) }, [this]
    {
        samplerEngine->panic();
        slicerEngine->panic();
        if (patchBay != nullptr)
            patchBay->panic();
        statusBar()->showMessage (tr ("All notes off"), 2000);
    });

    // MIDI plays the Sampler or the Slicer (whichever was used last) straight from the MIDI thread.
    midiInputs->setHandler ([this] (int status, int channel, int a, int b) { playMidi (status, channel, a, b); });
    QTimer::singleShot (0, this, [this] { openMidiInputs(); });

    samplerFollowTimer.setSingleShot (true);
    samplerFollowTimer.setInterval (300);
    connect (&samplerFollowTimer, &QTimer::timeout, this, [this] { sendToSampler (true); });
    slicerFollowTimer.setSingleShot (true);
    slicerFollowTimer.setInterval (300);
    connect (&slicerFollowTimer, &QTimer::timeout, this, [this] { sendToSlicer (true); });

    // Both are panels like Find and the meters: Simplest along the bottom above the device chain,
    // the Slicer on the left under Find (taking its place when it is closed); drag them anywhere,
    // or pop them out into windows of their own (View > Pop Out / Dock Panel, Ctrl+Alt+P). Each
    // scrolls inside its dock rather than pushing past the panels next to it. Further samplers
    // and synths of the Sample menu open as floating windows.
    auto scrolling = [this] (QWidget* content)
    {
        auto* area = new QScrollArea (this);
        area->setWidget (content);
        area->setWidgetResizable (true);
        area->setFrameShape (QFrame::NoFrame);
        area->setFocusProxy (content);   // showing the panel gives the instrument the playing keys
        area->viewport()->setAutoFillBackground (false);
        content->setAutoFillBackground (false);
        return area;
    };
    samplerWin = new SamplerWindow (*samplerEngine, this);
    connect (samplerWin, &SamplerWindow::refreshRequested, this, [this] { sendToSampler (true); });
    connect (samplerWin, &SamplerWindow::midiInputsRequested, this, [this] { midiInputsDialog(); });
    connect (samplerWin, &SamplerWindow::activated, this, [this] { midiTarget = 0; });
    connect (samplerWin, &SamplerWindow::closed, this, [this] { instrumentClosed(); });
    connect (samplerWin, &SamplerWindow::fileDropped, this, &MainWindow::loadSamplerFile);
    samplerPanel = new DockablePanel (*this, QStringLiteral ("samplerPanel"), tr ("Simplest"), scrolling (samplerWin), Qt::BottomDockWidgetArea);
    slicerWin = new SlicerWindow (*slicerEngine, this);
    connect (slicerWin, &SlicerWindow::refreshRequested, this, [this] { sendToSlicer (true); });
    connect (slicerWin, &SlicerWindow::midiInputsRequested, this, [this] { midiInputsDialog(); });
    connect (slicerWin, &SlicerWindow::activated, this, [this] { midiTarget = 1; });
    connect (slicerWin, &SlicerWindow::closed, this, [this] { instrumentClosed(); });
    connect (slicerWin, &SlicerWindow::fileDropped, this, &MainWindow::loadSlicerFile);
    // Follow Mixer: the step sequencer reads the Mixer's transport on the audio thread.
    slicerWin->setMixerClock ([this]
    {
        return slicer::Engine::Clock ([this] (double& beat, double& bpm)
        {
            const auto* m = mixerEngine.get();
            if (m == nullptr || ! m->isPlaying() || m->sampleRate() <= 0.0)
                return false;
            bpm = m->tempo.load();
            beat = (double) m->position() / m->sampleRate() * bpm / 60.0;
            return true;
        });
    });
    slicerPanel = new DockablePanel (*this, QStringLiteral ("slicerPanel"), tr ("Slicer"), scrolling (slicerWin), Qt::BottomDockWidgetArea);
    // The place they share with the chain takes the height of whichever is picked: compact,
    // with no room left over (they scroll past it).
    for (auto* p : { chainPanel, samplerPanel, slicerPanel })
        connect (p->dock(), &QDockWidget::visibilityChanged, this, [this, p] (bool visible)
        {
            if (! visible)
                return;
            QTimer::singleShot (0, this, [this, p]
            {
                auto* d = p->dock();
                if (d->isFloating() || ! d->isVisible() || dockWidgetArea (d) != Qt::BottomDockWidgetArea)
                    return;
                const auto wanted = std::min (p->content()->sizeHint().height() + 30, 400);
                if (std::abs (d->height() - wanted) > 8)
                    resizeDocks ({ d }, { wanted }, Qt::Vertical);
            });
        });
    placeInstrumentDocks();
    panels << samplerPanel << slicerPanel;
    updateMidiStatus();
}

// What a MIDI keyboard plays: the instrument used last (Simplest, the Slicer, the Mixer's
// armed track or a patchbay synth). Any thread: the MIDI thread, or the sonified Visualizer.
void MainWindow::playMidi (int status, int channel, int a, int b)
{
    auto* e = samplerEngine.get();
    auto* s = slicerEngine.get();

    // Controllers also turn the Prism macros that follow them (and teach one waiting to learn).
    if (status == 0xB0)
        QMetaObject::invokeMethod (this, [this, channel, a, b]
        {
            if (engine.midiController (channel, a, b))
                statusBar()->showMessage (tr ("Macro follows CC %1 on channel %2").arg (a).arg (channel), 4000);
        }, Qt::QueuedConnection);

    // No instrument window open: MIDI plays nothing.
    const auto target = midiTarget.load (std::memory_order_relaxed);
    // A patchbay synth: everything, on the channel it came on.
    if (target == 3)
    {
        if (auto* p = patchBay.get())
        {
            const auto st = (uint8_t) (status | std::clamp (channel - 1, 0, 15));
            if (status == 0xE0)
                p->liveMidi (patchSynth.load(), st, (uint8_t) (a & 0x7F), (uint8_t) ((a >> 7) & 0x7F));
            else
                p->liveMidi (patchSynth.load(), st, (uint8_t) a, (uint8_t) b);
        }
        return;
    }
    // The Mixer: everything goes to its armed track, on the channel it came on.
    if (target == 2)
    {
        if (auto* m = mixerEngine.get())
        {
            const auto st = (uint8_t) (status | std::clamp (channel - 1, 0, 15));
            if (status == 0xE0)
                m->liveMidi (st, (uint8_t) (a & 0x7F), (uint8_t) ((a >> 7) & 0x7F));
            else
                m->liveMidi (st, (uint8_t) a, (uint8_t) b);
        }
        return;
    }
    const bool slices = target == 1;
    if (target < 0 && (status == 0x90 || status == 0xE0))
        return;
    switch (status)
    {
        case 0x90:
            if (slices)
                s->noteOn (a, (float) b / 127.0f);
            else
                e->noteOn (a, (float) b / 127.0f);
            break;
        case 0x80:
            if (slices)
                s->noteOff (a);
            else
                e->noteOff (a);
            break;
        case 0xE0:
            if (! slices)
                e->pitchBend ((a - 8192) / 8192.0);
            break;
        case 0xB0:
            if (a == 64)
                e->sustainPedal (b >= 64);
            else if (a == 120)
            {
                e->panic();
                s->panic();
            }
            else if (a == 123)
                e->allNotesOff();
            break;
        default: break;
    }
}

// Everything live plays through the patchbay, which routes it to the engine, while any
// of its windows is open (or the patchbay holds anything of its own).
void MainWindow::playInstruments()
{
    const auto rate = engine.deviceSampleRate() > 0.0 ? engine.deviceSampleRate() : 48000.0;
    if (std::abs (patchBay->sampleRate() - rate) > 0.5)
        patchBay->prepare (rate);
    if (! engine.hasInstrument())
        engine.setInstrument ([this, p = patchBay.get()] (float* left, float* right, int frames)
        {
            p->render (left, right, frames);
            trackerPatchRendered();
        });
}

// The file highlighted in Find while Find has the keyboard: F7 and F8 send it.
QString MainWindow::findFile() const
{
    const auto* focus = QApplication::focusWidget();
    return findWidget != nullptr && focus != nullptr && findWidget->isAncestorOf (focus) ? findWidget->currentFile() : QString();
}

// Opening Simplest or the Slicer takes the keyboard; give it back to Find once
// the panel's own deferred focus has run, and again once a window that has just
// opened has been mapped (the window manager focuses a new window).
void MainWindow::keepFindFocus (QWidget* had)
{
    const auto giveBack = [focus = QPointer<QWidget> (had)]
    {
        if (focus == nullptr)
            return;
        focus->window()->activateWindow();
        focus->setFocus (Qt::OtherFocusReason);
    };
    QTimer::singleShot (0, this, giveBack);
    QTimer::singleShot (300, this, giveBack);
}

// Simplest and the Slicer take turns with the Device Chain in one place along the bottom, as
// tabs (owner, 2026-10-29: "toggle between the device chain and the slicer in that space");
// under the mixer they take turns with its own chain instead (updateInstrumentDocks).
void MainWindow::placeInstrumentDocks()
{
    auto* chain = chainPanel->dock();
    auto* sampler = samplerPanel->dock();
    auto* slicerDock = slicerPanel->dock();
    const bool samplerShown = sampler->isVisible(), slicerShown = slicerDock->isVisible();
    for (auto* d : { sampler, slicerDock })
    {
        d->setFloating (false);
        addDockWidget (Qt::BottomDockWidgetArea, d);
        tabifyDockWidget (chain, d);
    }
    sampler->setVisible (samplerShown);
    slicerDock->setVisible (slicerShown);
    chain->raise();
}

void MainWindow::showSampler()
{
    midiTarget = 0;
    playInstruments();
    samplerPanel->show (true);
}

// Takes the selection (or the whole file) of the active tab to the Sampler, at the
// device's rate. With keepMarkers it takes the same range of the tab it came from
// (after an edit), and the start, end and loop stay where they were.
void MainWindow::sendToSampler (bool keepMarkers)
{
    if (const auto path = findFile(); ! keepMarkers && ! path.isEmpty())
    {
        auto* focus = QApplication::focusWidget();
        loadSamplerFile (path);
        keepFindFocus (focus);   // to try the next file
        return;
    }
    if (keepMarkers)
    {
        if (samplerTab != activeTab || samplerPath != doc.path() || data() == nullptr)
        {
            if (! samplerFollowTimer.isActive())
                statusBar()->showMessage (tr ("Simplest's sound came from another tab; switch to it to refresh"), 4000);
            return;
        }
    }
    const auto d = data();
    if (d == nullptr || doc.isAlbum())
    {
        // Nothing to send: open it empty, ready for a sound dropped on it.
        showSampler();
        statusBar()->showMessage (tr ("Drop an audio file on Simplest, or open one and send it (or a selection) with F7"), 5000);
        return;
    }
    if (! keepMarkers)
    {
        const auto [s, e] = processRange();
        samplerFrom = s;
        samplerTo = e;
        samplerWhole = ! hasSelection();
        samplerTab = activeTab;
        samplerPath = doc.path();
    }
    const auto from = samplerWhole ? 0 : std::clamp<Frame> (samplerFrom, 0, d->length());
    const auto to = samplerWhole ? d->length() : std::clamp<Frame> (samplerTo, from, d->length());
    if (to - from < 2)
        return;
    samplerSource = d;

    const auto rate = engine.deviceSampleRate() > 0.0 ? engine.deviceSampleRate() : d->sampleRate();
    const auto name = (doc.path().empty() ? tr ("Untitled") : QFileInfo (QString::fromStdString (doc.path())).completeBaseName())
                      + (samplerWhole ? QString() : tr (" (selection)"));
    showSampler();
    samplerEngine->prepare (rate);
    playInstruments();

    // Resampling a long sound takes a moment, so it runs aside.
    auto result = std::make_shared<sampler::SamplePtr>();
    samplerLoad.waitForFinished();
    disconnect (&samplerLoad, nullptr, this, nullptr);
    connect (&samplerLoad, &QFutureWatcher<void>::finished, this, [this, result, rate, name, keepMarkers]
    {
        if (*result == nullptr || samplerWin == nullptr)
            return;
        samplerWin->setSample (*result, rate, name, keepMarkers);
        statusBar()->showMessage (keepMarkers ? tr ("Simplest updated") : tr ("In Simplest: play it with A to K or a MIDI keyboard"), 4000);
    });
    samplerLoad.setFuture (QtConcurrent::run ([d, from, to, rate, result]
    {
        auto part = d->slice (from, to);
        if (std::abs (part->sampleRate() - rate) > 0.5)
            part = dsp::resample (*part, rate);
        *result = sampler::makeSample (*part);
    }));
}

// A file dropped on Simplest (from Find or a file manager): the whole sound, loaded and
// resampled aside. It came from no tab, so edits in the editor do not follow it.
void MainWindow::loadSamplerFile (const QString& path)
{
    const auto rate = engine.deviceSampleRate() > 0.0 ? engine.deviceSampleRate() : 48000.0;
    samplerTab = -1;
    samplerPath.clear();
    samplerSource = nullptr;
    showSampler();
    samplerEngine->prepare (rate);
    playInstruments();
    struct Loaded { sampler::SamplePtr sample; std::string error; };
    auto result = std::make_shared<Loaded>();
    samplerLoad.waitForFinished();
    disconnect (&samplerLoad, nullptr, this, nullptr);
    connect (&samplerLoad, &QFutureWatcher<void>::finished, this, [this, result, rate, path]
    {
        if (result->sample == nullptr)
        {
            QMessageBox::warning (this, tr ("Simplest"), tr ("%1 could not be read: %2").arg (QFileInfo (path).fileName(), QString::fromStdString (result->error)));
            return;
        }
        samplerWin->setSample (result->sample, rate, QFileInfo (path).completeBaseName(), false);
        statusBar()->showMessage (tr ("In Simplest: play it with A to K or a MIDI keyboard"), 4000);
    });
    samplerLoad.setFuture (QtConcurrent::run ([path, rate, result]
    {
        std::vector<Marker> markers;
        Tags tags;
        auto loaded = AudioEngine::loadFile (path.toStdString(), markers, tags, result->error);
        if (loaded == nullptr)
            return;
        if (std::abs (loaded->sampleRate() - rate) > 0.5)
            loaded = dsp::resample (*loaded, rate);
        result->sample = sampler::makeSample (*loaded);
    }));
}

// As loadSamplerFile, for the Slicer: cut into 16 equal slices to start with.
void MainWindow::loadSlicerFile (const QString& path)
{
    const auto rate = engine.deviceSampleRate() > 0.0 ? engine.deviceSampleRate() : 48000.0;
    // Hot swap: the sound goes onto the chosen pad to try instead.
    if (slicerWin != nullptr && slicerWin->hotSwapping())
    {
        showSlicer();
        slicerWin->hotSwap (path);
        return;
    }
    slicerTab = -1;
    slicerPath.clear();
    slicerSource = nullptr;
    showSlicer();
    slicerEngine->prepare (rate);
    playInstruments();
    struct Loaded { sampler::SamplePtr sample; AudioDataPtr audio; std::string error; };
    auto result = std::make_shared<Loaded>();
    slicerLoad.waitForFinished();
    disconnect (&slicerLoad, nullptr, this, nullptr);
    connect (&slicerLoad, &QFutureWatcher<void>::finished, this, [this, result, rate, path]
    {
        if (result->sample == nullptr)
        {
            QMessageBox::warning (this, tr ("Slicer"), tr ("%1 could not be read: %2").arg (QFileInfo (path).fileName(), QString::fromStdString (result->error)));
            return;
        }
        slicerWin->setSample (result->sample, result->audio, rate, QFileInfo (path).completeBaseName(), false, BeatGrid {}, {});
        statusBar()->showMessage (tr ("In the Slicer: play the pads with Z X C V, A S D F, Q W E R, 1 2 3 4 or Push"), 4000);
    });
    slicerLoad.setFuture (QtConcurrent::run ([path, rate, result]
    {
        std::vector<Marker> markers;
        Tags tags;
        auto loaded = AudioEngine::loadFile (path.toStdString(), markers, tags, result->error);
        if (loaded == nullptr)
            return;
        if (std::abs (loaded->sampleRate() - rate) > 0.5)
            loaded = dsp::resample (*loaded, rate);
        result->sample = sampler::makeSample (*loaded);
        result->audio = loaded;
    }));
}

// After an edit in the tab the Sampler came from, it takes the sound again.
void MainWindow::samplerFollow()
{
    if (samplerWin == nullptr || ! samplerWin->followsEditor() || samplerSource == nullptr)
        return;
    if (samplerTab != activeTab || samplerPath != doc.path() || data() == nullptr || data() == samplerSource)
        return;
    samplerFollowTimer.start();
}

// A closed window stops sounding: MIDI goes to the other one if it is open, and with
// both closed the instruments leave the engine (a mono file plays mono again).
void MainWindow::instrumentClosed()
{
    const bool sampler = samplerWin != nullptr && samplerWin->isVisible();
    const bool slices = slicerWin != nullptr && slicerWin->isVisible();
    const bool mixing = mixerWin != nullptr && mixerWin->isVisible();
    const bool patching = patchBayWin != nullptr && patchBayWin->isVisible();
    midiTarget = sampler ? 0 : (slices ? 1 : (mixing ? 2 : (patching ? 3 : -1)));
    if (! sampler && ! slices && ! mixing && ! patchBayInUse())
        engine.setInstrument ({});
}

void MainWindow::showSlicer()
{
    // Sounds dropped on its pads are read at the rate it plays at.
    if (slicerEngine->sample() == nullptr && engine.deviceSampleRate() > 0.0)
        slicerEngine->prepare (engine.deviceSampleRate());
    midiTarget = 1;
    playInstruments();
    slicerPanel->show (true);
}

// As sendToSampler, for the Slicer: the sound comes with the tab's tempo grid and
// markers (measured from the sound's start) to slice by.
void MainWindow::sendToSlicer (bool keepSlices)
{
    if (const auto path = findFile(); ! keepSlices && ! path.isEmpty())
    {
        auto* focus = QApplication::focusWidget();
        loadSlicerFile (path);
        keepFindFocus (focus);
        return;
    }
    if (keepSlices && (slicerTab != activeTab || slicerPath != doc.path() || data() == nullptr))
    {
        if (! slicerFollowTimer.isActive())
            statusBar()->showMessage (tr ("The Slicer's sound came from another tab; switch to it to refresh"), 4000);
        return;
    }
    const auto d = data();
    if (d == nullptr || doc.isAlbum())
    {
        showSlicer();
        statusBar()->showMessage (tr ("Drop an audio file on the Slicer, or open one and send it (or a selection) with F8"), 5000);
        return;
    }
    if (! keepSlices)
    {
        const auto [s, e] = processRange();
        slicerFrom = s;
        slicerTo = e;
        slicerWhole = ! hasSelection();
        slicerTab = activeTab;
        slicerPath = doc.path();
    }
    const auto from = slicerWhole ? 0 : std::clamp<Frame> (slicerFrom, 0, d->length());
    const auto to = slicerWhole ? d->length() : std::clamp<Frame> (slicerTo, from, d->length());
    if (to - from < 2)
        return;
    slicerSource = d;

    const auto rate = engine.deviceSampleRate() > 0.0 ? engine.deviceSampleRate() : d->sampleRate();
    const auto fileRate = d->sampleRate();
    const auto name = (doc.path().empty() ? tr ("Untitled") : QFileInfo (QString::fromStdString (doc.path())).completeBaseName())
                      + (slicerWhole ? QString() : tr (" (selection)"));
    auto grid = doc.current().grid;
    grid.downbeat -= (double) from / fileRate;
    std::vector<Frame> cuts;
    for (const auto& m : doc.current().markers)
        for (const auto f : { m.start, m.end })
            if (f > from && f < to && (f == m.start || m.isRegion()))
                cuts.push_back ((Frame) std::llround ((double) (f - from) * rate / fileRate));

    showSlicer();
    slicerEngine->prepare (rate);
    playInstruments();

    struct Loaded { sampler::SamplePtr sample; AudioDataPtr audio; };
    auto result = std::make_shared<Loaded>();
    slicerLoad.waitForFinished();
    disconnect (&slicerLoad, nullptr, this, nullptr);
    connect (&slicerLoad, &QFutureWatcher<void>::finished, this, [this, result, rate, name, keepSlices, grid, cuts]
    {
        if (result->sample == nullptr || slicerWin == nullptr)
            return;
        slicerWin->setSample (result->sample, result->audio, rate, name, keepSlices, grid, cuts);
        statusBar()->showMessage (keepSlices ? tr ("Slicer updated") : tr ("In the Slicer: play the pads with Z X C V, A S D F, Q W E R, 1 2 3 4 or Push"), 4000);
    });
    slicerLoad.setFuture (QtConcurrent::run ([d, from, to, rate, result]
    {
        auto part = d->slice (from, to);
        if (std::abs (part->sampleRate() - rate) > 0.5)
            part = dsp::resample (*part, rate);
        result->sample = sampler::makeSample (*part);
        result->audio = part;
    }));
}

void MainWindow::slicerFollow()
{
    if (slicerWin == nullptr || ! slicerWin->followsEditor() || slicerSource == nullptr)
        return;
    if (slicerTab != activeTab || slicerPath != doc.path() || data() == nullptr || data() == slicerSource)
        return;
    slicerFollowTimer.start();
}

void MainWindow::openMidiInputs()
{
    // Every keyboard and controller plays, but those turned off in MIDI Inputs. They are known by
    // their names: ALSA numbers a device afresh each time it is plugged in.
    const auto off = appSettings().value (QStringLiteral ("midi/off")).toStringList();
    const auto devices = MidiInputs::available();
    const auto claimed = surfaces != nullptr ? surfaces->claimedInputs() : std::vector<std::string>();
    std::vector<std::string> ids;
    midiDevicesSeen.clear();
    for (const auto& d : devices)
    {
        midiDevicesSeen.push_back (d.identifier);
        const auto name = QString::fromStdString (d.name);
        if (name.contains (QLatin1String ("Midi Through"), Qt::CaseInsensitive) || off.contains (name)
            || std::find (claimed.begin(), claimed.end(), d.identifier) != claimed.end())   // a surface's alone
            continue;
        ids.push_back (d.identifier);
    }
    midiInputs->open (ids);
    updateMidiStatus();
}

void MainWindow::lookForMidiInputs()
{
    std::vector<std::string> now;
    for (const auto& d : MidiInputs::available())
        now.push_back (d.identifier);
    if (now != midiDevicesSeen)
        openMidiInputs();
}

void MainWindow::updateMidiStatus()
{
    if (samplerWin == nullptr && slicerWin == nullptr && mixerWin == nullptr && patchBayWin == nullptr)
        return;
    QStringList names;
    const auto open = midiInputs->opened();
    for (const auto& d : MidiInputs::available())
        if (std::find (open.begin(), open.end(), d.identifier) != open.end())
            names << QString::fromStdString (d.name);
    const auto text = names.isEmpty() ? tr ("MIDI: none (Sample > MIDI Inputs)") : tr ("MIDI: %1").arg (names.join (QStringLiteral (", ")));
    if (samplerWin != nullptr)
        samplerWin->setMidiStatus (text);
    if (slicerWin != nullptr)
        slicerWin->setMidiStatus (text);
    if (mixerWin != nullptr)
        mixerWin->setMidiStatus (text);
    if (patchBayWin != nullptr)
        patchBayWin->setMidiStatus (text);
}

void MainWindow::midiInputsDialog()
{
    QWidget* parent = this;
    if (samplerWin != nullptr && samplerWin->isActiveWindow())
        parent = samplerWin;
    else if (slicerWin != nullptr && slicerWin->isActiveWindow())
        parent = slicerWin;
    else if (mixerWin != nullptr && mixerWin->isActiveWindow())
        parent = mixerWin;
    QDialog dialog (parent);
    dialog.setWindowTitle (tr ("MIDI Inputs"));
    auto* layout = new QVBoxLayout (&dialog);
    auto* about = new QLabel (tr ("Ticked keyboards and controllers play the instrument you used last: Simplest, the Slicer, the mixer's armed "
                                  "track or a patchbay synth. Launchpads, Launchkeys and Push are driven as control surfaces "
                                  "(Performance > Control Surfaces); their other ports stay here. A keyboard plays as soon as it is plugged "
                                  "in; untick it to keep it out."), &dialog);
    about->setWordWrap (true);
    layout->addWidget (about);
    auto* list = new QListWidget (&dialog);
    layout->addWidget (list);
    auto fill = [this, list]
    {
        list->clear();
        const auto open = midiInputs->opened();
        const auto claimed = surfaces != nullptr ? surfaces->claimedInputs() : std::vector<std::string>();
        for (const auto& d : MidiInputs::available())
        {
            const bool surface = std::find (claimed.begin(), claimed.end(), d.identifier) != claimed.end();
            auto* item = new QListWidgetItem (surface ? tr ("%1 (a control surface)").arg (QString::fromStdString (d.name)) : QString::fromStdString (d.name), list);
            item->setData (Qt::UserRole, QString::fromStdString (d.identifier));
            item->setData (Qt::UserRole + 1, QString::fromStdString (d.name));
            if (surface)
            {
                item->setFlags (item->flags() & ~Qt::ItemIsEnabled);
                continue;
            }
            item->setFlags (item->flags() | Qt::ItemIsUserCheckable);
            item->setCheckState (std::find (open.begin(), open.end(), d.identifier) != open.end() ? Qt::Checked : Qt::Unchecked);
        }
        if (list->count() == 0)
            new QListWidgetItem (tr ("No MIDI inputs found. Plug one in and press Look Again."), list);
    };
    fill();
    auto* buttons = new QDialogButtonBox (QDialogButtonBox::Ok | QDialogButtonBox::Cancel, &dialog);
    auto* again = buttons->addButton (tr ("&Look Again"), QDialogButtonBox::ActionRole);
    connect (again, &QPushButton::clicked, &dialog, [this, fill]
    {
        lookForMidiInputs();
        fill();
    });
    connect (buttons, &QDialogButtonBox::accepted, &dialog, &QDialog::accept);
    connect (buttons, &QDialogButtonBox::rejected, &dialog, &QDialog::reject);
    layout->addWidget (buttons);
    dialog.resize (420, 300);
    if (dialog.exec() != QDialog::Accepted)
        return;
    // Those unticked are kept off by name, as are those turned off before and not plugged in now.
    auto settings = appSettings();
    auto off = settings.value (QStringLiteral ("midi/off")).toStringList();
    for (int i = 0; i < list->count(); ++i)
        if (const auto* item = list->item (i); item->flags() & Qt::ItemIsUserCheckable)
        {
            const auto name = item->data (Qt::UserRole + 1).toString();
            off.removeAll (name);
            if (item->checkState() == Qt::Unchecked)
                off << name;
        }
    settings.setValue (QStringLiteral ("midi/off"), off);
    settings.remove (QStringLiteral ("midi/inputs"));   // the choice by ALSA's numbers, before
    openMidiInputs();
}

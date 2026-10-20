// SPDX-License-Identifier: AGPL-3.0-only
// The Sample menu: the Sampler (sampler/SamplerEngine.h) and the Slicer
// (sampler/SlicerEngine.h) in their own windows, fed from the editor, and MIDI inputs
// that play them.
#include "MainWindow.h"
#include "SamplerWindow.h"
#include "SlicerWindow.h"
#include "Settings.h"
#include "../engine/MidiInputs.h"
#include "../sampler/SamplerEngine.h"
#include "../sampler/SlicerEngine.h"

#include <QDialog>
#include <QDialogButtonBox>
#include <QFileInfo>
#include <QLabel>
#include <QListWidget>
#include <QPushButton>
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

    add ("sample.send", tr ("&Send to Sampler"), { KS (Qt::Key_F7) }, [this] { sendToSampler (false); })
        ->setStatusTip (tr ("Play the selection (or the whole file) across the keyboard in the Sampler; later edits follow it"));
    add ("sample.window", tr ("Sampler &Window"), { KS (Qt::SHIFT | Qt::Key_F7) }, [this] { showSampler(); });
    add ("sample.refresh", tr ("&Refresh from Editor"), { KS (Qt::CTRL | Qt::Key_F7) }, [this] { sendToSampler (true); })
        ->setStatusTip (tr ("Take the sound the Sampler came from again, keeping its start, end and loop"));
    add ("slice.send", tr ("Send to Sli&cer"), { KS (Qt::Key_F8) }, [this] { sendToSlicer (false); })
        ->setStatusTip (tr ("Cut the selection (or the whole file) into slices on 64 pads in the Slicer; later edits follow it"));
    add ("slice.window", tr ("Slicer W&indow"), { KS (Qt::SHIFT | Qt::Key_F8) }, [this] { showSlicer(); });
    add ("slice.refresh", tr ("Re&fresh Slicer from Editor"), { KS (Qt::CTRL | Qt::Key_F8) }, [this] { sendToSlicer (true); })
        ->setStatusTip (tr ("Take the sound the Slicer came from again, keeping its slices"));
    add ("sample.midiInputs", tr ("&MIDI Inputs..."), {}, [this] { midiInputsDialog(); })
        ->setStatusTip (tr ("Choose the MIDI keyboards and controllers (Push, pads) that play the Sampler"));
    add ("sample.panic", tr ("All Notes &Off"), { KS (Qt::SHIFT | Qt::Key_Escape) }, [this]
    {
        samplerEngine->panic();
        slicerEngine->panic();
        statusBar()->showMessage (tr ("All notes off"), 2000);
    });

    // MIDI plays the Sampler or the Slicer (whichever was used last) straight from the MIDI thread.
    midiInputs->setHandler ([this, e = samplerEngine.get(), s = slicerEngine.get()] (int status, int, int a, int b)
    {
        const bool slices = midiTarget.load (std::memory_order_relaxed) == 1;
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
            case 0xE0: e->pitchBend ((a - 8192) / 8192.0); break;
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
    });
    QTimer::singleShot (0, this, [this] { openMidiInputs(); });

    samplerFollowTimer.setSingleShot (true);
    samplerFollowTimer.setInterval (300);
    connect (&samplerFollowTimer, &QTimer::timeout, this, [this] { sendToSampler (true); });
    slicerFollowTimer.setSingleShot (true);
    slicerFollowTimer.setInterval (300);
    connect (&slicerFollowTimer, &QTimer::timeout, this, [this] { sendToSlicer (true); });
}

// Both instruments play through the engine together.
void MainWindow::playInstruments()
{
    if (! engine.hasInstrument())
        engine.setInstrument ([e = samplerEngine.get(), s = slicerEngine.get()] (float* left, float* right, int frames)
        {
            e->render (left, right, frames);
            s->render (left, right, frames);
        });
}

void MainWindow::showSampler()
{
    if (samplerWin == nullptr)
    {
        samplerWin = new SamplerWindow (*samplerEngine, this);
        connect (samplerWin, &SamplerWindow::refreshRequested, this, [this] { sendToSampler (true); });
        connect (samplerWin, &SamplerWindow::midiInputsRequested, this, [this] { midiInputsDialog(); });
        connect (samplerWin, &SamplerWindow::activated, this, [this] { midiTarget = 0; });
        updateMidiStatus();
    }
    midiTarget = 0;
    samplerWin->show();
    samplerWin->raise();
    samplerWin->activateWindow();
}

// Takes the selection (or the whole file) of the active tab to the Sampler, at the
// device's rate. With keepMarkers it takes the same range of the tab it came from
// (after an edit), and the start, end and loop stay where they were.
void MainWindow::sendToSampler (bool keepMarkers)
{
    if (keepMarkers)
    {
        if (samplerTab != activeTab || samplerPath != doc.path() || data() == nullptr)
        {
            if (! samplerFollowTimer.isActive())
                statusBar()->showMessage (tr ("The Sampler's sound came from another tab; switch to it to refresh"), 4000);
            return;
        }
    }
    const auto d = data();
    if (d == nullptr || doc.isAlbum())
    {
        statusBar()->showMessage (tr ("Open a file first, then send it (or a selection) to the Sampler with F7"), 5000);
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
        statusBar()->showMessage (keepMarkers ? tr ("Sampler updated") : tr ("In the Sampler: play it with A to K, a MIDI keyboard or the piano"), 4000);
    });
    samplerLoad.setFuture (QtConcurrent::run ([d, from, to, rate, result]
    {
        auto part = d->slice (from, to);
        if (std::abs (part->sampleRate() - rate) > 0.5)
            part = dsp::resample (*part, rate);
        *result = sampler::makeSample (*part);
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

void MainWindow::showSlicer()
{
    if (slicerWin == nullptr)
    {
        slicerWin = new SlicerWindow (*slicerEngine, this);
        connect (slicerWin, &SlicerWindow::refreshRequested, this, [this] { sendToSlicer (true); });
        connect (slicerWin, &SlicerWindow::midiInputsRequested, this, [this] { midiInputsDialog(); });
        connect (slicerWin, &SlicerWindow::activated, this, [this] { midiTarget = 1; });
        updateMidiStatus();
    }
    midiTarget = 1;
    slicerWin->show();
    slicerWin->raise();
    slicerWin->activateWindow();
}

// As sendToSampler, for the Slicer: the sound comes with the tab's tempo grid and
// markers (measured from the sound's start) to slice by.
void MainWindow::sendToSlicer (bool keepSlices)
{
    if (keepSlices && (slicerTab != activeTab || slicerPath != doc.path() || data() == nullptr))
    {
        if (! slicerFollowTimer.isActive())
            statusBar()->showMessage (tr ("The Slicer's sound came from another tab; switch to it to refresh"), 4000);
        return;
    }
    const auto d = data();
    if (d == nullptr || doc.isAlbum())
    {
        statusBar()->showMessage (tr ("Open a file first, then send it (or a selection) to the Slicer with F8"), 5000);
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
    // First time: every input. Afterwards: the ones chosen.
    auto settings = appSettings();
    const auto devices = MidiInputs::available();
    std::vector<std::string> ids;
    if (! settings.contains (QStringLiteral ("midi/inputs")))
    {
        for (const auto& d : devices)
            if (QString::fromStdString (d.name).contains (QLatin1String ("Midi Through"), Qt::CaseInsensitive) == false)
                ids.push_back (d.identifier);
    }
    else
        for (const auto& id : settings.value (QStringLiteral ("midi/inputs")).toStringList())
            ids.push_back (id.toStdString());
    midiInputs->open (ids);
    updateMidiStatus();
}

void MainWindow::updateMidiStatus()
{
    if (samplerWin == nullptr && slicerWin == nullptr)
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
}

void MainWindow::midiInputsDialog()
{
    QWidget* parent = this;
    if (samplerWin != nullptr && samplerWin->isActiveWindow())
        parent = samplerWin;
    else if (slicerWin != nullptr && slicerWin->isActiveWindow())
        parent = slicerWin;
    QDialog dialog (parent);
    dialog.setWindowTitle (tr ("MIDI Inputs"));
    auto* layout = new QVBoxLayout (&dialog);
    auto* about = new QLabel (tr ("Ticked keyboards and controllers play the Sampler or the Slicer, whichever you used last. Ableton Push: use its User mode, "
                                  "or its own MIDI port when Live is not running; its 64 pads send notes 36 to 99."), &dialog);
    about->setWordWrap (true);
    layout->addWidget (about);
    auto* list = new QListWidget (&dialog);
    layout->addWidget (list);
    auto fill = [this, list]
    {
        list->clear();
        const auto open = midiInputs->opened();
        for (const auto& d : MidiInputs::available())
        {
            auto* item = new QListWidgetItem (QString::fromStdString (d.name), list);
            item->setData (Qt::UserRole, QString::fromStdString (d.identifier));
            item->setFlags (item->flags() | Qt::ItemIsUserCheckable);
            item->setCheckState (std::find (open.begin(), open.end(), d.identifier) != open.end() ? Qt::Checked : Qt::Unchecked);
        }
        if (list->count() == 0)
            new QListWidgetItem (tr ("No MIDI inputs found. Plug one in and press Look Again."), list);
    };
    fill();
    auto* buttons = new QDialogButtonBox (QDialogButtonBox::Ok | QDialogButtonBox::Cancel, &dialog);
    auto* again = buttons->addButton (tr ("&Look Again"), QDialogButtonBox::ActionRole);
    connect (again, &QPushButton::clicked, &dialog, fill);
    connect (buttons, &QDialogButtonBox::accepted, &dialog, &QDialog::accept);
    connect (buttons, &QDialogButtonBox::rejected, &dialog, &QDialog::reject);
    layout->addWidget (buttons);
    dialog.resize (420, 300);
    if (dialog.exec() != QDialog::Accepted)
        return;
    QStringList chosen;
    std::vector<std::string> ids;
    for (int i = 0; i < list->count(); ++i)
        if (const auto* item = list->item (i); item->checkState() == Qt::Checked)
        {
            chosen << item->data (Qt::UserRole).toString();
            ids.push_back (chosen.back().toStdString());
        }
    appSettings().setValue (QStringLiteral ("midi/inputs"), chosen);
    midiInputs->open (ids);
    updateMidiStatus();
}

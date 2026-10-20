// SPDX-License-Identifier: AGPL-3.0-only
// The Sample menu: the Sampler (sampler/SamplerEngine.h) in its own window, fed from
// the editor, and MIDI inputs that play it.
#include "MainWindow.h"
#include "SamplerWindow.h"
#include "Settings.h"
#include "../engine/MidiInputs.h"
#include "../sampler/SamplerEngine.h"

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
    midiInputs = std::make_unique<MidiInputs>();

    add ("sample.send", tr ("&Send to Sampler"), { KS (Qt::Key_F7) }, [this] { sendToSampler (false); })
        ->setStatusTip (tr ("Play the selection (or the whole file) across the keyboard in the Sampler; later edits follow it"));
    add ("sample.window", tr ("Sampler &Window"), { KS (Qt::SHIFT | Qt::Key_F7) }, [this] { showSampler(); });
    add ("sample.refresh", tr ("&Refresh from Editor"), { KS (Qt::CTRL | Qt::Key_F7) }, [this] { sendToSampler (true); })
        ->setStatusTip (tr ("Take the sound the Sampler came from again, keeping its start, end and loop"));
    add ("sample.midiInputs", tr ("&MIDI Inputs..."), {}, [this] { midiInputsDialog(); })
        ->setStatusTip (tr ("Choose the MIDI keyboards and controllers (Push, pads) that play the Sampler"));
    add ("sample.panic", tr ("All Notes &Off"), { KS (Qt::SHIFT | Qt::Key_Escape) }, [this]
    {
        samplerEngine->panic();
        statusBar()->showMessage (tr ("All notes off"), 2000);
    });

    // MIDI plays the Sampler straight from the MIDI thread.
    midiInputs->setHandler ([e = samplerEngine.get()] (int status, int, int a, int b)
    {
        switch (status)
        {
            case 0x90: e->noteOn (a, (float) b / 127.0f); break;
            case 0x80: e->noteOff (a); break;
            case 0xE0: e->pitchBend ((a - 8192) / 8192.0); break;
            case 0xB0:
                if (a == 64)
                    e->sustainPedal (b >= 64);
                else if (a == 120)
                    e->panic();
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
}

void MainWindow::showSampler()
{
    if (samplerWin == nullptr)
    {
        samplerWin = new SamplerWindow (*samplerEngine, this);
        connect (samplerWin, &SamplerWindow::refreshRequested, this, [this] { sendToSampler (true); });
        connect (samplerWin, &SamplerWindow::midiInputsRequested, this, [this] { midiInputsDialog(); });
        updateMidiStatus();
    }
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
    if (! engine.hasInstrument())
        engine.setInstrument ([e = samplerEngine.get()] (float* left, float* right, int frames) { e->render (left, right, frames); });

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
    if (samplerWin == nullptr)
        return;
    QStringList names;
    const auto open = midiInputs->opened();
    for (const auto& d : MidiInputs::available())
        if (std::find (open.begin(), open.end(), d.identifier) != open.end())
            names << QString::fromStdString (d.name);
    samplerWin->setMidiStatus (names.isEmpty() ? tr ("MIDI: none (Sample > MIDI Inputs)") : tr ("MIDI: %1").arg (names.join (QStringLiteral (", "))));
}

void MainWindow::midiInputsDialog()
{
    QDialog dialog (samplerWin != nullptr && samplerWin->isActiveWindow() ? static_cast<QWidget*> (samplerWin) : this);
    dialog.setWindowTitle (tr ("MIDI Inputs"));
    auto* layout = new QVBoxLayout (&dialog);
    auto* about = new QLabel (tr ("Ticked keyboards and controllers play the Sampler. Ableton Push: use its User mode, "
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

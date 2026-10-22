// SPDX-License-Identifier: AGPL-3.0-only
// The stock instruments (engine/StockPlugins.h, cmake/Instruments.cmake): the
// samplers in the Sample menu, the synths in the Synth menu, and both under
// Instruments in Find. Each plays as one of the patchbay's synths, so the MIDI
// keyboard plays it and the patchbay routes it, and its own window floats with
// an opacity bar below (engine/InstrumentFrame.h), kept per instrument.
#include "MainWindow.h"
#include "PatchBayWindow.h"
#include "Settings.h"
#include "../engine/InstrumentFrame.h"
#include "../engine/StockPlugins.h"
#include "../patchbay/PatchBay.h"

#include <QApplication>
#include <QMessageBox>
#include <QStatusBar>

void MainWindow::registerInstrumentCommands()
{
    InstrumentFrame::loadOpacity = [] (const std::string& key)
    {
        return appSettings().value (QStringLiteral ("instruments/%1/opacity").arg (QString::fromStdString (key)), 1.0).toDouble();
    };
    InstrumentFrame::saveOpacity = [] (const std::string& key, double value)
    {
        appSettings().setValue (QStringLiteral ("instruments/%1/opacity").arg (QString::fromStdString (key)), value);
    };

    for (const auto& info : stock::all())
    {
        if (info.role == stock::Role::effect)
            continue;
        const auto key = QString::fromLatin1 (info.key);
        const auto category = info.role == stock::Role::sampler ? tr ("Sample") : tr ("Synth");
        // "Retro Sampler (Ami)": named by what it does, with the name its authors gave it.
        const auto text = QStringLiteral ("%1 (%2)").arg (tr (info.function), QString::fromUtf8 (info.original));
        auto* action = commands.add (QStringLiteral ("instrument.") + key, category, text, {}, [this, k = info.key] { openInstrument (k); });
        action->setStatusTip (tr ("Open %1 in a floating window; the MIDI keyboard plays it (%2, %3)")
                                  .arg (text, QString::fromUtf8 (info.credit), QString::fromLatin1 (info.licence)));
        addAction (action);
    }
}

// The patchbay synth playing a stock instrument: the one it already is, or with `load`
// the first free one, started here. -1 when there is none; `why` then says why, and
// `failed` is set when the instrument is there but would not start.
int MainWindow::instrumentSynth (const char* key, bool load, QString* why, bool* failed)
{
    const auto* info = stock::instrument (key);
    if (info == nullptr)
        return -1;
    const auto name = QStringLiteral ("%1 (%2)").arg (tr (info->function), QString::fromUtf8 (info->original));
    const auto id = engine.stockInstrumentId (key);
    if (id.empty())
    {
        if (why != nullptr)
            *why = tr ("%1 was not built with this copy of %2").arg (name, QStringLiteral (APP_DISPLAY_NAME));
        return -1;
    }

    int slot = -1, free = -1;
    for (int i = 0; i < patchbay::maxSynths; ++i)
    {
        const auto s = patchBay->synth (i);
        if (s == nullptr)
            free = free < 0 ? i : free;
        else if (slot < 0 && AudioEngine::instrumentPluginId (*s) == id)
            slot = i;
    }
    if (slot >= 0 || ! load)
        return slot;
    if (free < 0)
    {
        if (why != nullptr)
            *why = tr ("All %1 patchbay synths are in use: clear one in the Patchbay (F4) first").arg (patchbay::maxSynths);
        return -1;
    }
    playInstruments();
    std::string error;
    QApplication::setOverrideCursor (Qt::WaitCursor);
    auto made = engine.loadInstrumentPlugin (id, patchBay->sampleRate(), error);
    QApplication::restoreOverrideCursor();
    if (made == nullptr)
    {
        if (why != nullptr)
            *why = tr ("%1 could not be started: %2").arg (name, QString::fromStdString (error));
        if (failed != nullptr)
            *failed = true;
        return -1;
    }
    patchBay->setSynth (free, made);
    if (patchBayWin != nullptr)
        patchBayWin->refill();
    patchBaySave.start();
    return free;
}

// Plays the instrument as a patchbay synth (the one it already is, or the first free one)
// and opens its window, or brings it to the front.
void MainWindow::openInstrument (const char* key)
{
    const auto* info = stock::instrument (key);
    if (info == nullptr)
        return;
    const auto name = QStringLiteral ("%1 (%2)").arg (tr (info->function), QString::fromUtf8 (info->original));
    playInstruments();
    QString why;
    bool failed = false;
    const auto slot = instrumentSynth (key, true, &why, &failed);
    if (slot < 0)
    {
        if (failed)
            QMessageBox::warning (this, name, why);
        else
            statusBar()->showMessage (why, 6000);
        return;
    }

    patchSynth = slot;
    midiTarget = 3;
    patchBay->synth (slot)->openWindow();
    statusBar()->showMessage (tr ("%1 is Synth %2 of the patchbay; the MIDI keyboard plays it").arg (name).arg (slot + 1), 6000);
}

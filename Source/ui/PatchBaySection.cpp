// SPDX-License-Identifier: AGPL-3.0-only
// Mixer > Patchbay: the routing graph (patchbay/PatchBay.h) for everything played
// live, its pop-out window, and its patches, synths and effects kept between sessions
// in settings.ini.
#include "MainWindow.h"
#include "MixerWindow.h"
#include "PatchBayWindow.h"
#include "SamplerWindow.h"
#include "Settings.h"
#include "SlicerWindow.h"
#include "../mixer/MixerEngine.h"
#include "../patchbay/PatchBay.h"
#include "../sampler/SamplerEngine.h"
#include "../sampler/SlicerEngine.h"

#include <QFileInfo>
#include <QStatusBar>

#include <algorithm>

using namespace patchbay;

namespace
{
    // Names in settings.ini, so the numbering inside the patchbay can change.
    QString nodeKey (int n)
    {
        const auto number = QString::number (indexOf (n) + 1);
        switch (kindOf (n))
        {
            case Kind::editor:  return QStringLiteral ("editor");
            case Kind::sampler: return QStringLiteral ("sampler");
            case Kind::slicer:  return QStringLiteral ("slicer");
            case Kind::synth:   return QStringLiteral ("synth") + number;
            case Kind::track:   return QStringLiteral ("track") + number;
            case Kind::mixBus:  return QStringLiteral ("mixbus");
            case Kind::fxBus:   return QStringLiteral ("fx") + number;
            case Kind::mainOut: return QStringLiteral ("main");
        }
        return {};
    }

    int nodeFromKey (const QString& key)
    {
        for (int n = 0; n < numNodes; ++n)
            if (nodeKey (n) == key)
                return n;
        return -1;
    }

    QString fileName (const std::string& path) { return QFileInfo (QString::fromStdString (path)).fileName(); }
}

void MainWindow::registerPatchBayCommands()
{
    patchBay = std::make_unique<PatchBay> (*samplerEngine, *slicerEngine, mixerEngine.get());
    auto* action = commands.add (QStringLiteral ("mixer.patchbay"), tr ("Mixer"), tr ("&Patchbay"), { QKeySequence (Qt::Key_F4) },
                                 [this] { showPatchBay(); });
    action->setStatusTip (tr ("Patch the editor, Sampler, Slicer, mixer tracks, synths and FX buses into each other on an 8x8 grid"));
    addAction (action);

    patchBaySave.setSingleShot (true);
    patchBaySave.setInterval (500);
    connect (&patchBaySave, &QTimer::timeout, this, [this] { savePatchBay(); });
    QTimer::singleShot (0, this, [this] { restorePatchBay(); });
}

PatchBayWindow* MainWindow::patchBayWindow()
{
    if (patchBayWin != nullptr)
        return patchBayWin;

    PatchBayWindow::Hooks hooks;
    hooks.active = [this] (int n)
    {
        const auto i = indexOf (n);
        switch (kindOf (n))
        {
            case Kind::editor:  return data() != nullptr;
            case Kind::sampler: return (samplerWin != nullptr && samplerWin->isVisible()) || samplerEngine->sample() != nullptr;
            case Kind::slicer:  return (slicerWin != nullptr && slicerWin->isVisible()) || slicerEngine->sample() != nullptr;
            case Kind::synth:   return patchBay->synth (i) != nullptr;
            case Kind::track:   return mixerWin != nullptr && i < mixerEngine->numTracks();
            case Kind::mixBus:  return mixerWin != nullptr;
            case Kind::fxBus:   return patchBay->effect (i) != nullptr;
            case Kind::mainOut: return true;
        }
        return false;
    };
    hooks.detail = [this] (int n) -> QString
    {
        switch (kindOf (n))
        {
            case Kind::editor:
                if (data() == nullptr)
                    return tr ("no file");
                return doc.path().empty() ? tr ("Untitled") : fileName (doc.path());
            case Kind::sampler: return samplerEngine->sample() != nullptr ? tr ("has a sound") : tr ("no sound");
            case Kind::slicer:  return slicerEngine->sample() != nullptr ? tr ("has a sound") : tr ("no sound");
            case Kind::track:
            {
                // From the mixer's window, not its engine: the engine's lock belongs to the audio thread.
                const auto i = indexOf (n);
                if (mixerWin == nullptr || i >= mixerEngine->numTracks())
                    return tr ("not in the mix");
                const auto name = mixerWin->trackName (i);
                return name.isEmpty() || name == tr ("Track %1").arg (i + 1) ? tr ("in the mix") : name;
            }
            case Kind::mixBus:
            {
                const auto db = mixerEngine->masterDb.load();
                return db <= -96.0f ? tr ("master off") : tr ("master %1 dB").arg (db, 0, 'f', 1);
            }
            case Kind::mainOut: return tr ("speakers");
            default:            return {};
        }
    };
    hooks.open = [this] (int n)
    {
        switch (kindOf (n))
        {
            case Kind::sampler: showSampler(); break;
            case Kind::slicer:  showSlicer(); break;
            case Kind::track:
            case Kind::mixBus:  showMixer(); break;
            default: break;
        }
    };
    hooks.playSynth = [this] (int i)
    {
        patchSynth = i;
        midiTarget = 3;
        statusBar()->showMessage (tr ("The MIDI keyboard plays Synth %1 of the patchbay").arg (i + 1), 4000);
    };

    patchBayWin = new PatchBayWindow (*patchBay, engine, std::move (hooks), this);
    connect (patchBayWin, &PatchBayWindow::changed, this, [this] { patchBaySave.start(); });
    connect (patchBayWin, &PatchBayWindow::activated, this, [this]
    {
        if (patchBay->synth (patchSynth.load()) != nullptr)
            midiTarget = 3;
    });
    connect (patchBayWin, &PatchBayWindow::closed, this, [this]
    {
        savePatchBay();
        instrumentClosed();
        activateWindow();
        waveform->setFocus();
    });

    // Where the rows and columns were, unless they follow what is in use.
    auto settings = appSettings();
    settings.beginGroup (QStringLiteral ("patchbay"));
    const auto rows = settings.value (QStringLiteral ("rows")).toStringList();
    const auto columns = settings.value (QStringLiteral ("columns")).toStringList();
    const auto autoFill = settings.value (QStringLiteral ("autoFill"), true).toBool();
    if (! autoFill && rows.size() == PatchGrid::size && columns.size() == PatchGrid::size)
    {
        auto r = patchBayWin->shownRows();
        auto c = patchBayWin->shownColumns();
        for (int i = 0; i < PatchGrid::size; ++i)
        {
            if (const auto n = nodeFromKey (rows[i]); n >= 0 && std::find (r.begin(), r.end(), n) == r.end())
                r[(size_t) i] = n;
            if (const auto n = nodeFromKey (columns[i]); n >= 0 && std::find (c.begin(), c.end(), n) == c.end())
                c[(size_t) i] = n;
        }
        patchBayWin->setShown (r, c);
    }
    patchBayWin->setAutoFill (autoFill);
    updateMidiStatus();
    return patchBayWin;
}

void MainWindow::showPatchBay()
{
    auto* w = patchBayWindow();
    playInstruments();
    if (patchBay->synth (patchSynth.load()) != nullptr)
        midiTarget = 3;
    w->show();
    w->raise();
    w->activateWindow();
}

bool MainWindow::patchBayInUse() const
{
    if (patchBay == nullptr)
        return false;
    if (patchBayWin != nullptr && patchBayWin->isVisible())
        return true;
    for (int i = 0; i < maxSynths; ++i)
        if (patchBay->synth (i) != nullptr)
            return true;
    for (int i = 0; i < maxBuses; ++i)
        if (patchBay->effect (i) != nullptr)
            return true;
    return ! patchBay->isDefault();
}

void MainWindow::savePatchBay()
{
    if (patchBay == nullptr)
        return;
    auto settings = appSettings();
    settings.beginGroup (QStringLiteral ("patchbay"));
    QStringList links;
    for (const auto& k : patchBay->links())
        links << QStringLiteral ("%1>%2>%3").arg (nodeKey (k.from), nodeKey (k.to)).arg (k.gainDb, 0, 'f', 2);
    settings.setValue (QStringLiteral ("links"), links);
    for (int i = 0; i < maxSynths; ++i)
    {
        const auto key = QStringLiteral ("synth%1").arg (i + 1);
        const auto s = patchBay->synth (i);
        const auto id = s == nullptr ? std::string() : s->kind() == mixer::Instrument::Kind::synth ? std::string ("synth") : AudioEngine::instrumentPluginId (*s);
        settings.setValue (key, QString::fromStdString (id));
        settings.setValue (key + QStringLiteral ("State"), s != nullptr ? QString::fromStdString (s->state()) : QString());
    }
    for (int i = 0; i < maxBuses; ++i)
    {
        const auto key = QStringLiteral ("fx%1").arg (i + 1);
        const auto fx = patchBay->effect (i);
        settings.setValue (key, fx != nullptr ? QString::fromStdString (fx->pluginId()) : QString());
        settings.setValue (key + QStringLiteral ("State"), fx != nullptr ? QString::fromStdString (fx->state()) : QString());
    }
    if (patchBayWin != nullptr)
    {
        QStringList rows, columns;
        for (const auto n : patchBayWin->shownRows())
            rows << nodeKey (n);
        for (const auto n : patchBayWin->shownColumns())
            columns << nodeKey (n);
        settings.setValue (QStringLiteral ("rows"), rows);
        settings.setValue (QStringLiteral ("columns"), columns);
        settings.setValue (QStringLiteral ("autoFill"), patchBayWin->autoFill());
    }
}

// The synths, effects and patches of last time. With anything of its own, the
// patchbay is heard straight away, so routing made last time still holds.
void MainWindow::restorePatchBay()
{
    auto settings = appSettings();
    settings.beginGroup (QStringLiteral ("patchbay"));
    if (! settings.contains (QStringLiteral ("links")))
        return;
    const auto rate = engine.deviceSampleRate() > 0.0 ? engine.deviceSampleRate() : 48000.0;
    patchBay->prepare (rate);
    QStringList problems;
    for (int i = 0; i < maxSynths; ++i)
    {
        const auto key = QStringLiteral ("synth%1").arg (i + 1);
        const auto id = settings.value (key).toString().toStdString();
        if (id.empty())
            continue;
        mixer::InstrumentPtr made;
        std::string error;
        if (id == "synth")
            made = std::make_shared<mixer::Synth>();
        else
            made = engine.loadInstrumentPlugin (id, rate, error);
        if (made == nullptr)
        {
            problems << tr ("Synth %1: %2").arg (i + 1).arg (QString::fromStdString (error));
            continue;
        }
        made->setState (settings.value (key + QStringLiteral ("State")).toString().toStdString());
        patchBay->setSynth (i, made);
    }
    for (int i = 0; i < maxBuses; ++i)
    {
        const auto key = QStringLiteral ("fx%1").arg (i + 1);
        const auto id = settings.value (key).toString().toStdString();
        if (id.empty())
            continue;
        std::string error;
        auto made = engine.loadEffectPlugin (id, rate, error);
        if (made == nullptr)
        {
            problems << tr ("FX Bus %1: %2").arg (i + 1).arg (QString::fromStdString (error));
            continue;
        }
        made->setState (settings.value (key + QStringLiteral ("State")).toString().toStdString());
        patchBay->setEffect (i, made);
    }
    std::vector<Link> links;
    for (const auto& item : settings.value (QStringLiteral ("links")).toStringList())
    {
        const auto parts = item.split (QLatin1Char ('>'));
        if (parts.size() != 3)
            continue;
        const auto from = nodeFromKey (parts[0]), to = nodeFromKey (parts[1]);
        if (from >= 0 && to >= 0)
            links.push_back ({ from, to, parts[2].toFloat() });
    }
    patchBay->setLinks (links);
    if (! problems.isEmpty())
        statusBar()->showMessage (tr ("Patchbay: %1").arg (problems.join (QStringLiteral ("; "))), 10000);
    if (patchBayInUse())
        playInstruments();
}

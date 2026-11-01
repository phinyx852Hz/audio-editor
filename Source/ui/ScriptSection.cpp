// SPDX-License-Identifier: AGPL-3.0-only
// Tools > Scripts (docs/scripting.md): the Script Editor, a command for each
// script in the scripts folder (so each can have a shortcut, or be a macro
// step), and the phi API scripts call, run here against the open file, its
// markers and tags, PHI's commands and the mixer.
#include "MainWindow.h"
#include "MixerWindow.h"
#include "ScriptEditor.h"
#include "../mixer/MixerEngine.h"
#include "../script/ScriptRunner.h"

#include <QApplication>
#include <QDesktopServices>
#include <QEventLoop>
#include <QFile>
#include <QFileDialog>
#include <QFileInfo>
#include <QInputDialog>
#include <QMenu>
#include <QMessageBox>
#include <QPushButton>
#include <QRegularExpression>
#include <QStatusBar>
#include <QTimer>
#include <QUrl>

#include <cmath>

namespace
{
    QString commandIdFor (const QString& path)
    {
        static const QRegularExpression unsafe (QStringLiteral ("[^A-Za-z0-9._-]"));
        return QStringLiteral ("script.") + QFileInfo (path).fileName().replace (unsafe, QStringLiteral ("_"));
    }

    using script::Error;

    double number (const QVariantList& args, int i, const char* what)
    {
        if (i >= args.size() || ! args[i].canConvert<double>() || args[i].typeId() == QMetaType::QString)
            throw Error (QStringLiteral ("%1 should be a number").arg (QLatin1String (what)));
        const auto v = args[i].toDouble();
        if (! std::isfinite (v))
            throw Error (QStringLiteral ("%1 should be a number").arg (QLatin1String (what)));
        return v;
    }

    double numberOr (const QVariantList& args, int i, double fallback, const char* what)
    {
        return i < args.size() && args[i].isValid() ? number (args, i, what) : fallback;
    }

    QString text (const QVariantList& args, int i, const char* what)
    {
        if (i >= args.size() || ! args[i].isValid())
            throw Error (QStringLiteral ("%1 is missing").arg (QLatin1String (what)));
        return args[i].toString();
    }

    QString textOr (const QVariantList& args, int i, const QString& fallback)
    {
        return i < args.size() && args[i].isValid() ? args[i].toString() : fallback;
    }
}

void MainWindow::registerScriptCommands()
{
    using KS = QKeySequence;
    const auto category = tr ("Tools");
    auto add = [this, &category] (const char* id, const QString& text, QList<QKeySequence> keys, std::function<void()> fn)
    {
        auto* a = commands.add (QString::fromLatin1 (id), category, text, keys, std::move (fn));
        addAction (a);
        return a;
    };

    scriptRunner = new ScriptRunner (this);
    connect (scriptRunner, &ScriptRunner::output, this, [this] (const QString& output, bool error)
    {
        scriptOutput += output;
        if (scriptEditor != nullptr && scriptEditor->isVisible())
            scriptEditor->showOutput (output, error);
    });
    scriptStopButton = new QPushButton (tr ("Stop Script"), this);
    scriptStopButton->setToolTip (tr ("Ends the script that is running"));
    scriptStopButton->hide();
    connect (scriptStopButton, &QPushButton::clicked, scriptRunner, &ScriptRunner::stop);
    statusBar()->insertPermanentWidget (0, scriptStopButton);
    connect (scriptRunner, &ScriptRunner::runningChanged, this, [this] (bool on)
    {
        scriptStopButton->setVisible (on);
        commands.action (QStringLiteral ("tools.scriptStop"))->setEnabled (on);
        if (scriptEditor != nullptr)
            scriptEditor->setRunning (on);
    });

    add ("tools.scriptEditor", tr ("Script &Editor..."), { KS (Qt::CTRL | Qt::SHIFT | Qt::Key_F9) }, [this] { showScriptEditor(); });
    add ("tools.scriptRun", tr ("&Run Script..."), {}, [this]
    {
        const auto file = QFileDialog::getOpenFileName (this, tr ("Run Script"), scripts::folder(), tr ("Scripts (*.lua *.py *.ny)"));
        if (! file.isEmpty())
            runScriptFile (file);
    });
    add ("tools.scriptStop", tr ("&Stop Script"), {}, [this] { scriptRunner->stop(); })->setEnabled (false);
    add ("tools.scriptsFolder", tr ("Open Scripts &Folder"), {}, [this]
    {
        QDesktopServices::openUrl (QUrl::fromLocalFile (scripts::folder()));
        statusBar()->showMessage (tr ("Put .lua, .py and .ny scripts here; they appear in Tools > Scripts"), 8000);
    });

    // A command for each script, so the palette finds it and it can have a shortcut.
    for (const auto& file : scripts::all())
        registerScript (file);
}

void MainWindow::registerScript (const QString& path)
{
    const auto id = commandIdFor (path);
    if (auto* existing = commands.action (id))
    {
        existing->setVisible (true);
        return;
    }
    auto* a = commands.add (id, tr ("Scripts"), QFileInfo (path).completeBaseName(), {}, [this, path] { runScriptFile (path); });
    a->setStatusTip (tr ("Runs the script %1").arg (QFileInfo (path).fileName()));
    addAction (a);
}

void MainWindow::fillScriptsMenu (QMenu* menu)
{
    for (auto* a : scriptMenuItems)
        menu->removeAction (a);
    scriptMenuItems.clear();

    bool added = false;
    for (const auto& file : scripts::all())
    {
        if (commands.action (commandIdFor (file)) == nullptr)
        {
            registerScript (file);
            added = true;
        }
        scriptMenuItems << commands.action (commandIdFor (file));
    }
    if (added)
        commands.load();   // shortcuts given to scripts in an earlier run

    if (scriptMenuItems.isEmpty())
    {
        static QAction* none = nullptr;
        if (none == nullptr)
        {
            none = new QAction (tr ("(no scripts yet)"), this);
            none->setEnabled (false);
        }
        scriptMenuItems << none;
    }
    menu->insertActions (scriptsAnchor, scriptMenuItems);
}

void MainWindow::showScriptEditor()
{
    if (scriptEditor == nullptr)
    {
        scriptEditor = new ScriptEditor (this);
        scriptEditor->runRequested = [this] (script::Language language, const QString& code, const QString& name, const QString& folder)
        {
            if (language == script::Language::nyquist)
                runNyquistCode (code, name);
            else
                runScript (language, code, name, folder);
        };
        scriptEditor->stopRequested = [this] { scriptRunner->stop(); };
        scriptEditor->scriptsChanged = [this]
        {
            for (const auto& file : scripts::all())
                registerScript (file);
            commands.load();
        };
    }
    scriptEditor->refreshList();
    scriptEditor->setRunning (scriptRunner->isRunning());
    scriptEditor->show();
    scriptEditor->raise();
    scriptEditor->activateWindow();
}

void MainWindow::runScriptFile (const QString& path)
{
    QFile f (path);
    if (! f.open (QIODevice::ReadOnly))
    {
        QMessageBox::information (this, tr ("Run Script"), tr ("The script %1 is no longer there.").arg (QFileInfo (path).fileName()));
        if (auto* a = commands.action (commandIdFor (path)))
            a->setVisible (false);
        return;
    }
    const auto code = QString::fromUtf8 (f.readAll());
    const auto language = script::languageFor (path);
    if (language == script::Language::nyquist)
    {
        runNyquistCode (code, QFileInfo (path).completeBaseName());
        return;
    }
    if (language == script::Language::unknown)
    {
        QMessageBox::information (this, tr ("Run Script"), tr ("PHI runs scripts in Lua (.lua), Python (.py) and Nyquist (.ny)."));
        return;
    }

    const auto name = QFileInfo (path).fileName();
    const bool ok = runScript (language, code, name, QFileInfo (path).absolutePath());
    if (! ok && ! scriptRunner->wasStopped() && ! (scriptEditor != nullptr && scriptEditor->isVisible()))
        nyquistui::showOutput (this, tr ("Script: %1").arg (name), scriptOutput);
    else if (ok)
    {
        // What it printed last, for a moment.
        const auto lines = scriptOutput.trimmed().split (QLatin1Char ('\n'));
        statusBar()->showMessage (lines.isEmpty() || lines.last().isEmpty() ? tr ("Ran %1").arg (QFileInfo (path).completeBaseName()) : lines.last(), 8000);
    }
}

void MainWindow::runNyquistCode (const QString& code, const QString& name)
{
    auto script = nyquist::parse (code.toStdString(), true);
    if (! script.ok)
    {
        QMessageBox::information (this, name, QString::fromStdString (script.error));
        return;
    }
    if (! script.typeGiven)
        script.kind = data() != nullptr ? nyquist::Kind::process : nyquist::Kind::tool;
    script.name = name.toStdString();
    if (std::any_of (script.controls.begin(), script.controls.end(), [] (const auto& c) { return c.type != nyquist::Control::Type::text; }))
    {
        NyquistDialog controls (script, this);
        if (controls.exec() != QDialog::Accepted)
            return;
    }
    runNyquist (std::move (script), name, false);
}

bool MainWindow::runScript (script::Language language, const QString& code, const QString& name, const QString& folder)
{
    if (scriptRunner->isRunning())
    {
        statusBar()->showMessage (tr ("A script is running; stop it before starting another"), 6000);
        return false;
    }
    scriptOutput.clear();
    scriptUndoName.clear();
    const auto documentId = doc.id();
    const auto from = doc.historyPosition();
    statusBar()->showMessage (tr ("Running the script %1...").arg (name));

    const bool ok = scriptRunner->run (language, code, name, folder,
                                       [this] (const QString& function, const QVariantList& args) { return scriptCall (function, args); });

    // Everything the script did to the file it began on is one undo step; in a file it
    // made or opened, everything after the file as it opened.
    const auto squashFrom = doc.id() == documentId ? from : scriptMadeDocuments.contains (doc.id()) ? 0 : -1;
    scriptMadeDocuments.clear();
    if (doc.isOpen() && squashFrom >= 0 && doc.historyPosition() > squashFrom)
    {
        doc.squash (squashFrom, (scriptUndoName.isEmpty() ? tr ("Script: %1").arg (QFileInfo (name).completeBaseName()) : scriptUndoName).toStdString());
        refreshFromDocument (true);
    }
    statusBar()->clearMessage();
    updateCommandStates();
    return ok;
}

void MainWindow::settleLoading()
{
    for (int i = 0; loading && i < 6000; ++i)
    {
        QEventLoop wait;
        QTimer::singleShot (10, &wait, &QEventLoop::quit);
        wait.exec();
        scriptRunner->keepAlive();
    }
}

QVariant MainWindow::scriptCall (const QString& name, const QVariantList& args)
{
    QWidget* parent = scriptEditor != nullptr && scriptEditor->isVisible() ? static_cast<QWidget*> (scriptEditor) : this;
    const auto is = [&name] (const char* n) { return name == QLatin1String (n); };

    // Output and questions.
    if (is ("message"))
    {
        QMessageBox::information (parent, textOr (args, 1, tr ("Script")), text (args, 0, "the message"));
        return {};
    }
    if (is ("ask"))
    {
        bool ok = false;
        const auto answer = QInputDialog::getText (parent, tr ("Script"), text (args, 0, "the prompt"), QLineEdit::Normal, textOr (args, 1, {}), &ok);
        return ok ? QVariant (answer) : QVariant();
    }
    if (is ("ask_number"))
    {
        bool ok = false;
        const auto low = numberOr (args, 2, -1.0e9, "the lowest");
        const auto high = numberOr (args, 3, 1.0e9, "the highest");
        const auto decimals = (int) std::clamp (numberOr (args, 4, 3.0, "decimals"), 0.0, 10.0);
        const auto value = QInputDialog::getDouble (parent, tr ("Script"), text (args, 0, "the prompt"), std::clamp (numberOr (args, 1, 0.0, "the default"), low, high),
                                                    low, high, decimals, &ok);
        return ok ? QVariant (value) : QVariant();
    }
    if (is ("confirm"))
        return QMessageBox::question (parent, tr ("Script"), text (args, 0, "the question")) == QMessageBox::Yes;
    if (is ("choose"))
    {
        if (args.size() < 2 || (args[1].typeId() != QMetaType::QVariantList && args[1].typeId() != QMetaType::QStringList))
            throw Error (QStringLiteral ("the options should be a list"));
        QStringList options;
        for (const auto& o : args[1].toList())
            options << o.toString();
        if (options.isEmpty())
            throw Error (QStringLiteral ("there are no options"));
        bool ok = false;
        const auto choice = QInputDialog::getItem (parent, tr ("Script"), text (args, 0, "the prompt"), options,
                                                   std::clamp ((int) numberOr (args, 2, 0.0, "the default"), 0, (int) options.size() - 1), false, &ok);
        return ok ? QVariant (choice) : QVariant();
    }
    if (is ("version"))
        return QApplication::applicationVersion();

    // Tabs and files that need nothing open.
    if (is ("is_open"))
        return data() != nullptr;
    if (is ("tab_count"))
        return (int) tabs.size();
    if (is ("tab"))
        return activeTab;
    if (is ("set_tab"))
    {
        const auto index = (int) number (args, 0, "the tab");
        if (index < 0 || index >= (int) tabs.size())
            throw Error (QStringLiteral ("there is no tab %1 (there are %2, from 0)").arg (index).arg (tabs.size()));
        switchToTab (index);
        return {};
    }
    if (is ("open"))
    {
        const auto file = QFileInfo (text (args, 0, "the path")).absoluteFilePath();
        if (! QFileInfo::exists (file))
            throw Error (QStringLiteral ("there is no file %1").arg (file));
        openFile (file);
        settleLoading();
        scriptMadeDocuments.insert (doc.id());
        return QFileInfo (QString::fromStdString (doc.path())).absoluteFilePath() == file;
    }
    if (is ("new_file"))
    {
        const auto seconds = number (args, 0, "the length");
        const auto channels = (int) numberOr (args, 1, 2.0, "the channels");
        const auto rate = numberOr (args, 2, 48000.0, "the sample rate");
        if (seconds < 0.0 || seconds > 24.0 * 3600.0 || channels < 1 || channels > 64 || rate < 1000.0 || rate > 768000.0)
            throw Error (QStringLiteral ("new_file takes up to 24 hours, 1 to 64 channels and 1000 to 768000 Hz"));
        AudioData::Builder builder (channels, rate);
        std::vector<std::vector<float>> zeros ((size_t) channels, std::vector<float> ((size_t) AudioChunk::maxFrames, 0.0f));
        std::vector<const float*> pointers;
        for (const auto& z : zeros)
            pointers.push_back (z.data());
        for (auto left = (Frame) std::llround (seconds * rate); left > 0;)
        {
            const auto n = (int) std::min<Frame> (left, AudioChunk::maxFrames);
            builder.append (pointers.data(), n);
            left -= n;
        }
        newTab();
        doc.reset (builder.finish(), {}, {});
        doc.markUnsaved();
        refreshFromDocument (false);
        scriptMadeDocuments.insert (doc.id());
        return {};
    }
    if (is ("commands"))
    {
        const auto filter = textOr (args, 0, {});
        QVariantList result;
        for (const auto& c : commands.all())
            if (filter.isEmpty() || c.name().contains (filter, Qt::CaseInsensitive) || c.id.contains (filter, Qt::CaseInsensitive))
                result << QVariantMap { { QStringLiteral ("id"), c.id }, { QStringLiteral ("name"), c.name() },
                                        { QStringLiteral ("category"), c.category }, { QStringLiteral ("shortcut"), c.shortcutText() } };
        return result;
    }
    if (is ("run"))
    {
        const auto id = text (args, 0, "the command's id");
        auto* action = commands.action (id);
        if (action == nullptr)
            throw Error (QStringLiteral ("there is no command \"%1\" (phi.commands() lists them)").arg (id));
        if (id.startsWith (QLatin1String ("script.")) || id.startsWith (QLatin1String ("tools.script")))
            throw Error (QStringLiteral ("a script cannot start another script"));
        if (! action->isEnabled())
            return false;
        action->trigger();
        settleLoading();
        return true;
    }
    if (is ("run_macro"))
    {
        const auto macroName = text (args, 0, "the macro's name");
        for (const auto& m : macro::loadAll())
            if (m.name == macroName)
            {
                runMacro (m);
                settleLoading();
                return true;
            }
        return false;
    }
    if (is ("play"))
    {
        if (data() != nullptr && ! engine.isPlaying())
            togglePlayback();
        return {};
    }
    if (is ("stop"))
    {
        if (engine.isPlaying())
            engine.stop();
        return {};
    }
    if (is ("is_playing"))
        return engine.isPlaying();
    if (is ("undo"))
    {
        undo();
        return {};
    }
    if (is ("redo"))
    {
        redo();
        return {};
    }
    if (is ("set_undo_name"))
    {
        scriptUndoName = text (args, 0, "the name");
        return {};
    }

    // The mixer.
    if (is ("track_count") || is ("track") || is ("set_track"))
    {
        auto* w = mixerWindow();
        if (w == nullptr || mixerEngine == nullptr)
            throw Error (QStringLiteral ("the mixer is not available"));
        auto& mix = *mixerEngine;
        if (is ("track_count"))
            return mix.numTracks();
        const auto index = (int) number (args, 0, "the track");
        if (index < 0 || index >= mix.numTracks())
            throw Error (QStringLiteral ("there is no track %1 (there are %2, from 0)").arg (index).arg (mix.numTracks()));
        auto& strip = mix.strip (index);
        if (is ("track"))
            return QVariantMap { { QStringLiteral ("name"), w->trackName (index) }, { QStringLiteral ("volume"), (double) strip.faderDb.load() },
                                 { QStringLiteral ("pan"), (double) strip.pan.load() }, { QStringLiteral ("trim"), (double) strip.trimDb.load() },
                                 { QStringLiteral ("mute"), strip.mute.load() }, { QStringLiteral ("solo"), strip.solo.load() } };
        if (args.size() < 2 || args[1].typeId() != QMetaType::QVariantMap)
            throw Error (QStringLiteral ("set_track takes a table of settings, such as { volume = -6, pan = 0 }"));
        const auto settings = args[1].toMap();
        for (auto it = settings.begin(); it != settings.end(); ++it)
        {
            const auto& key = it.key();
            const QVariantList value { it.value() };
            if (key == QLatin1String ("volume"))
                strip.faderDb = (float) std::clamp (number (value, 0, "volume"), -100.0, 6.0);
            else if (key == QLatin1String ("pan"))
                strip.pan = (float) std::clamp (number (value, 0, "pan"), -1.0, 1.0);
            else if (key == QLatin1String ("trim"))
                strip.trimDb = (float) std::clamp (number (value, 0, "trim"), -24.0, 24.0);
            else if (key == QLatin1String ("mute"))
                strip.mute = it.value().toBool();
            else if (key == QLatin1String ("solo"))
                strip.solo = it.value().toBool();
            else
                throw Error (QStringLiteral ("a track has no setting \"%1\" (volume, pan, trim, mute, solo)").arg (key));
        }
        w->stripsChanged();
        return {};
    }

    // Everything else works on the open file.
    const auto d = data();
    if (d == nullptr)
        throw Error (QStringLiteral ("no file is open"));
    const auto rate = d->sampleRate();
    const auto toFrame = [&] (double seconds) { return std::clamp<Frame> ((Frame) std::llround (seconds * rate), 0, d->length()); };
    const auto toSeconds = [&] (Frame f) { return (double) f / rate; };
    const auto refuseAlbum = [this]
    {
        if (doc.isAlbum())
            throw Error (QStringLiteral ("an album's mixdown cannot be changed by a script; open its songs instead"));
    };

    if (is ("path"))
        return QString::fromStdString (doc.path());
    if (is ("name"))
        return tabName (doc);
    if (is ("sample_rate"))
        return rate;
    if (is ("channels"))
        return d->numChannels();
    if (is ("frames"))
        return (qlonglong) d->length();
    if (is ("duration"))
        return d->lengthSeconds();
    if (is ("to_frames"))
        return (qlonglong) std::llround (number (args, 0, "the seconds") * rate);
    if (is ("to_seconds"))
        return number (args, 0, "the frames") / rate;
    if (is ("save"))
        return ! doc.path().empty() && save();

    if (is ("cursor"))
        return toSeconds (cursor);
    if (is ("set_cursor"))
    {
        setCursor (toFrame (number (args, 0, "the time")));
        return {};
    }
    if (is ("selection"))
        return hasSelection() ? QVariantList { toSeconds (selStart), toSeconds (selEnd) } : QVariantList { toSeconds (cursor), toSeconds (cursor) };
    if (is ("has_selection"))
        return hasSelection();
    if (is ("set_selection"))
    {
        selectRange (toFrame (number (args, 0, "the start")), toFrame (number (args, 1, "the end")));
        return {};
    }

    if (is ("get_samples") || is ("set_samples"))
    {
        const auto channel = (int) number (args, 0, "the channel");
        if (channel < 0 || channel >= d->numChannels())
            throw Error (QStringLiteral ("there is no channel %1 (the file has %2, from 0)").arg (channel).arg (d->numChannels()));
        const auto start = (Frame) number (args, 1, "the start frame");
        if (start < 0 || start > d->length())
            throw Error (QStringLiteral ("frame %1 is outside the file (0 to %2)").arg (start).arg (d->length()));
        if (is ("get_samples"))
        {
            const auto count = (Frame) number (args, 2, "the count");
            if (count < 0 || count > 16 * 1024 * 1024)
                throw Error (QStringLiteral ("read up to 16777216 samples at a time"));
            const auto n = (int) std::min<Frame> (count, d->length() - start);
            std::vector<std::vector<float>> buffers ((size_t) d->numChannels(), std::vector<float> ((size_t) n));
            std::vector<float*> pointers;
            for (auto& b : buffers)
                pointers.push_back (b.data());
            d->read (start, n, pointers.data());
            QVariantList result;
            result.reserve (n);
            for (const auto v : buffers[(size_t) channel])
                result << (double) v;
            return result;
        }
        refuseAlbum();
        if (args.size() < 3 || args[2].typeId() != QMetaType::QVariantList)
            throw Error (QStringLiteral ("the samples should be a list of numbers"));
        const auto values = args[2].toList();
        if (start + values.size() > d->length())
            throw Error (QStringLiteral ("%1 samples from frame %2 go past the end of the file (%3 frames)").arg (values.size()).arg (start).arg (d->length()));
        if (values.isEmpty())
            return {};
        std::vector<float> samples ((size_t) values.size());
        for (qsizetype i = 0; i < values.size(); ++i)
        {
            bool ok = false;
            const auto v = values[i].toDouble (&ok);
            if (! ok || ! std::isfinite (v))
                throw Error (QStringLiteral ("sample %1 is not a number").arg (i + 1));
            samples[(size_t) i] = (float) v;
        }
        const auto end = start + (Frame) samples.size();
        auto result = d->processed (start, end, [&] (std::vector<std::vector<float>>& block, Frame blockStart)
        {
            auto& out = block[(size_t) channel];
            for (size_t i = 0; i < out.size(); ++i)
                out[i] = samples[(size_t) (blockStart - start) + i];
        });
        commit (tr ("Script"), std::move (result), start, end, end - start);
        return {};
    }
    if (is ("peak"))
    {
        const auto s = args.size() >= 2 ? toFrame (number (args, 0, "the start")) : Frame (0);
        const auto e = args.size() >= 2 ? toFrame (number (args, 1, "the end")) : d->length();
        return (double) d->peakAbs (s, std::max (s, e));
    }

    if (is ("markers"))
    {
        QVariantList result;
        for (const auto& m : doc.current().markers)
            result << QVariantMap { { QStringLiteral ("id"), m.id }, { QStringLiteral ("name"), QString::fromStdString (m.name) },
                                    { QStringLiteral ("start"), toSeconds (m.start) }, { QStringLiteral ("finish"), toSeconds (m.end) },
                                    { QStringLiteral ("region"), m.isRegion() } };
        return result;
    }
    if (is ("add_marker") || is ("add_region"))
    {
        refuseAlbum();
        const bool region = is ("add_region");
        auto markers = doc.current().markers;
        Marker m;
        m.id = doc.nextMarkerId();
        m.type = region ? Marker::Type::region : Marker::Type::generic;
        m.start = toFrame (number (args, 0, region ? "the start" : "the time"));
        m.end = region ? toFrame (number (args, 1, "the end")) : m.start;
        if (region && m.end < m.start)
            std::swap (m.start, m.end);
        if (region && m.end == m.start)
            throw Error (QStringLiteral ("a region needs an end after its start"));
        m.name = textOr (args, region ? 2 : 1, region ? tr ("Region %1").arg (m.id) : tr ("Marker %1").arg (m.id)).toStdString();
        markers.push_back (m);
        commitMarkers (region ? tr ("Add Region") : tr ("Add Marker"), std::move (markers));
        return m.id;
    }
    if (is ("rename_marker") || is ("move_marker") || is ("delete_marker"))
    {
        refuseAlbum();
        const auto id = (int) number (args, 0, "the marker's id");
        auto markers = doc.current().markers;
        const auto it = std::find_if (markers.begin(), markers.end(), [id] (const Marker& m) { return m.id == id; });
        if (it == markers.end())
            throw Error (QStringLiteral ("there is no marker %1").arg (id));
        if (is ("rename_marker"))
            it->name = text (args, 1, "the name").toStdString();
        else if (is ("delete_marker"))
            markers.erase (it);
        else
        {
            it->start = toFrame (number (args, 1, "the start"));
            it->end = it->isRegion() ? toFrame (numberOr (args, 2, toSeconds (it->end), "the end")) : it->start;
            if (it->end < it->start)
                std::swap (it->start, it->end);
        }
        commitMarkers (is ("delete_marker") ? tr ("Delete Marker") : tr ("Edit Marker"), std::move (markers));
        return {};
    }

    if (is ("tag"))
    {
        const auto& tags = doc.current().tags;
        const auto it = tags.find (text (args, 0, "the key").toStdString());
        return it == tags.end() ? QString() : QString::fromStdString (it->second);
    }
    if (is ("set_tag"))
    {
        refuseAlbum();
        const auto key = text (args, 0, "the key").toStdString();
        const auto value = textOr (args, 1, {}).toStdString();
        auto state = doc.current();
        if (value.empty())
            state.tags.erase (key);
        else
            state.tags[key] = value;
        doc.apply (tr ("Edit Tags").toStdString(), std::move (state));
        refreshFromDocument (true);
        return {};
    }

    throw Error (QStringLiteral ("not available yet"));
}

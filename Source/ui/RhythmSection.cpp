// SPDX-License-Identifier: AGPL-3.0-only
// The Mode menu's rhythmic modes (owner, 2026-10-26): tāls, tāḷams, breakbeats, trap, the
// medieval rhythmic modes, and the timelines and grooves of the Caribbean, South America,
// Africa and Korea (mixer/Rhythms.h, docs/rhythms.md). A rhythm chosen there opens a small
// dialog that puts it on the selected mixer track as a MIDI clip, writes it into a new
// Tracker pattern, or saves it as a MIDI file. Every name is lower case (owner). "my breaks"
// holds the owner's own drum loops, transcribed by mixer::breakRhythm and kept in breaks.json
// beside settings.ini.
#include "MainWindow.h"
#include "MixerWindow.h"
#include "Settings.h"
#include "TrackerWindow.h"
#include "../engine/AudioEngine.h"
#include "../mixer/DrumsToMidi.h"
#include "../mixer/RhythmGenerator.h"
#include "../mixer/Rhythms.h"
#include "../tracker/PatternText.h"
#include "../tracker/TrackerEngine.h"

#include <QComboBox>
#include <QDialog>
#include <QDialogButtonBox>
#include <QDir>
#include <QDoubleSpinBox>
#include <QFileDialog>
#include <QFileInfo>
#include <QFile>
#include <QFormLayout>
#include <QFutureWatcher>
#include <QEventLoop>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QProgressDialog>
#include <QtConcurrent>
#include <QLabel>
#include <QMenu>
#include <QMessageBox>
#include <QPushButton>
#include <QSpinBox>
#include <QStatusBar>
#include <QVBoxLayout>

#include <algorithm>
#include <cmath>
#include <map>

namespace
{
    QString text (const std::string& s) { return QString::fromStdString (s); }

    QString tip (const rhythm::Rhythm& r)
    {
        auto t = text (r.about);
        if (! r.ipa.empty())
            t = QStringLiteral ("/%1/\n").arg (text (r.ipa)) + t;
        if (! r.counting.empty())
            t += QStringLiteral ("\n") + text (r.counting);
        return t;
    }

    // Tracker speed (ticks a row) and tempo for `rowsPerBeat` rows a beat at `bpm`: a row lasts
    // speed × 2.5 / tempo seconds. The speed nearest FastTracker 2's 6 that gives a tempo it has.
    std::pair<int, int> trackerTiming (int rowsPerBeat, double bpm)
    {
        for (int d = 0; d < 31; ++d)
            for (int speed : { 6 - d, 6 + d })
                if (speed >= 1 && speed <= 31)
                    if (const auto tempo = (int) std::lround (speed * bpm * rowsPerBeat / 24.0); tempo >= 32 && tempo <= 255)
                        return { speed, tempo };
        return { 6, std::clamp ((int) std::lround (bpm), 32, 255) };
    }

    // The owner's breaks, each with the file it came from (its `plain` name).
    QString breaksFile()
    {
        return QStandardPaths::writableLocation (QStandardPaths::GenericConfigLocation) + QStringLiteral ("/" APP_ID "/breaks.json");
    }

    std::vector<rhythm::Rhythm> loadBreaks()
    {
        std::vector<rhythm::Rhythm> out;
        QFile f (breaksFile());
        if (! f.open (QIODevice::ReadOnly))
            return out;
        for (const auto& v : QJsonDocument::fromJson (f.readAll()).array())
        {
            const auto o = v.toObject();
            rhythm::Rhythm r;
            r.name = o.value (QStringLiteral ("name")).toString().toStdString();
            r.plain = o.value (QStringLiteral ("file")).toString().toStdString();
            r.group = "my breaks";
            r.about = o.value (QStringLiteral ("about")).toString().toStdString();
            r.bpm = o.value (QStringLiteral ("bpm")).toDouble (120.0);
            r.beats = o.value (QStringLiteral ("beats")).toInt (4);
            r.sections.assign ((size_t) std::max (1, r.beats / 4), 4);
            for (const auto& l : o.value (QStringLiteral ("lanes")).toArray())
            {
                const auto lo = l.toObject();
                r.lanes.push_back ({ lo.value (QStringLiteral ("name")).toString().toStdString(), lo.value (QStringLiteral ("note")).toInt (36),
                                     lo.value (QStringLiteral ("perBeat")).toInt (4), lo.value (QStringLiteral ("hits")).toString().toStdString(), 9 });
            }
            if (rhythm::valid (r))
                out.push_back (std::move (r));
        }
        return out;
    }

    void saveBreaks (const std::vector<rhythm::Rhythm>& breaks)
    {
        QJsonArray all;
        for (const auto& r : breaks)
        {
            QJsonArray lanes;
            for (const auto& l : r.lanes)
                lanes.append (QJsonObject { { QStringLiteral ("name"), text (l.name) }, { QStringLiteral ("note"), l.note },
                                            { QStringLiteral ("perBeat"), l.perBeat }, { QStringLiteral ("hits"), text (l.hits) } });
            all.append (QJsonObject { { QStringLiteral ("name"), text (r.name) }, { QStringLiteral ("file"), text (r.plain) },
                                      { QStringLiteral ("about"), text (r.about) }, { QStringLiteral ("bpm"), r.bpm },
                                      { QStringLiteral ("beats"), r.beats }, { QStringLiteral ("lanes"), lanes } });
        }
        QDir().mkpath (QFileInfo (breaksFile()).absolutePath());
        QFile f (breaksFile());
        if (f.open (QIODevice::WriteOnly | QIODevice::Truncate))
            f.write (QJsonDocument (all).toJson());
    }

    // A break's file transcribed; called on a worker thread.
    struct Transcribed
    {
        QString file;
        rhythm::Rhythm rhythm;
        QString error;   // empty: transcribed
    };
    Transcribed transcribe (const QString& file)
    {
        Transcribed t;
        t.file = file;
        std::vector<Marker> markers;
        Tags tags;
        std::string error;
        const auto audio = AudioEngine::loadFile (file.toStdString(), markers, tags, error);
        if (audio == nullptr)
        {
            t.error = text (error);
            return t;
        }
        const auto name = QFileInfo (file).completeBaseName().toLower().toStdString();
        if (! mixer::breakRhythm (mixer::makeAudioClip (*audio, file.toStdString()), audio->sampleRate(), name, t.rhythm, error))
            t.error = text (error);
        t.rhythm.plain = file.toStdString();
        return t;
    }
}

void MainWindow::fillRhythmMenu (QMenu* menu)
{
    menu->clear();
    qDeleteAll (menu->findChildren<QMenu*> (QString(), Qt::FindDirectChildrenOnly));
    menu->setToolTipsVisible (true);
    auto* generator = menu->addAction (tr ("rhythm &generator..."));
    generator->setToolTip (tr ("euclidean lanes against each other, with chance, rules (fills, density, rotation) and a feel"));
    connect (generator, &QAction::triggered, this, [this] { rhythmGenerator(); });
    menu->addSeparator();
    const auto& all = rhythm::rhythms();
    // Regions in melody and tuning's order, the ones with no rhythms yet left out.
    std::map<std::string, QMenu*> regions;
    const auto groups = rhythm::groups();
    for (const auto& region : rhythm::regions())
    {
        if (std::none_of (groups.begin(), groups.end(), [&] (const std::string& g) { return rhythm::region (g) == region; }))
            continue;
        auto* m = menu->addMenu (text (region));
        m->setToolTipsVisible (true);
        regions[region] = m;
    }
    for (const auto& group : groups)
    {
        const auto it = regions.find (rhythm::region (group));
        auto* sub = (it != regions.end() ? it->second : menu)->addMenu (text (group));
        sub->setToolTipsVisible (true);
        std::map<std::string, QMenu*> inner;
        for (size_t i = 0; i < all.size(); ++i)
        {
            const auto& r = all[i];
            if (r.group != group)
                continue;
            auto* into = sub;
            if (! r.subgroup.empty())
            {
                auto& m = inner[r.subgroup];
                if (m == nullptr)
                {
                    m = sub->addMenu (text (r.subgroup));
                    m->setToolTipsVisible (true);
                }
                into = m;
            }
            auto* a = into->addAction (text (r.name));
            a->setToolTip (tip (r));
            a->setStatusTip (text (r.about));
            connect (a, &QAction::triggered, this, [this, i] { useRhythm (rhythm::rhythms()[i]); });
        }
    }

    // The owner's own breaks, added as needed.
    menu->addSeparator();
    auto* mine = menu->addMenu (tr ("&my breaks"));
    mine->setToolTipsVisible (true);
    connect (mine->addAction (tr ("&add break files...")), &QAction::triggered, this, [this] { addBreaks (false); });
    connect (mine->addAction (tr ("add a &folder of breaks...")), &QAction::triggered, this, [this] { addBreaks (true); });
    const auto breaks = loadBreaks();
    if (! breaks.empty())
    {
        auto* remove = mine->addMenu (tr ("&remove"));
        mine->addSeparator();
        for (size_t i = 0; i < breaks.size(); ++i)
        {
            const auto& r = breaks[i];
            auto* a = mine->addAction (text (r.name));
            a->setToolTip (text (r.about) + QStringLiteral ("\n") + text (r.plain));
            a->setStatusTip (text (r.about));
            connect (a, &QAction::triggered, this, [this, r] { useRhythm (r); });
            connect (remove->addAction (text (r.name)), &QAction::triggered, this, [file = r.plain]
            {
                auto all = loadBreaks();
                all.erase (std::remove_if (all.begin(), all.end(), [&] (const rhythm::Rhythm& x) { return x.plain == file; }), all.end());
                saveBreaks (all);
            });
        }
        remove->addSeparator();
        connect (remove->addAction (tr ("&all of them")), &QAction::triggered, this, [] { saveBreaks ({}); });
    }
}

void MainWindow::addBreaks (bool folder)
{
    auto settings = appSettings();
    const auto from = settings.value (QStringLiteral ("rhythm/breaksFolder")).toString();
    QStringList files;
    if (folder)
    {
        const auto dir = QFileDialog::getExistingDirectory (this, tr ("folder of breaks"), from);
        if (dir.isEmpty())
            return;
        settings.setValue (QStringLiteral ("rhythm/breaksFolder"), dir);
        QStringList patterns;
        for (const auto& ext : AudioEngine::readableExtensions())
            patterns << QStringLiteral ("*.") + text (ext);
        for (const auto& name : QDir (dir).entryList (patterns, QDir::Files, QDir::Name | QDir::IgnoreCase))
            files << QDir (dir).filePath (name);
    }
    else
    {
        files = QFileDialog::getOpenFileNames (this, tr ("break files"), from, tr ("Audio files (*.wav *.aif *.aiff *.flac *.ogg *.mp3);;All files (*)"));
        if (! files.isEmpty())
            settings.setValue (QStringLiteral ("rhythm/breaksFolder"), QFileInfo (files.front()).absolutePath());
    }
    if (files.isEmpty())
        return;

    QProgressDialog progress (tr ("transcribing breaks..."), tr ("Cancel"), 0, (int) files.size(), this);
    progress.setWindowModality (Qt::WindowModal);
    progress.setMinimumDuration (300);
    auto all = loadBreaks();
    int added = 0;
    QStringList failed;
    for (int i = 0; i < files.size() && ! progress.wasCanceled(); ++i)
    {
        progress.setValue (i);
        progress.setLabelText (tr ("transcribing %1...").arg (QFileInfo (files[i]).fileName()));
        QFutureWatcher<Transcribed> watcher;
        QEventLoop loop;
        connect (&watcher, &QFutureWatcher<Transcribed>::finished, &loop, &QEventLoop::quit);
        watcher.setFuture (QtConcurrent::run (transcribe, files[i]));
        loop.exec();
        const auto t = watcher.result();
        if (! t.error.isEmpty())
        {
            failed << QStringLiteral ("%1: %2").arg (QFileInfo (t.file).fileName(), t.error);
            continue;
        }
        all.erase (std::remove_if (all.begin(), all.end(), [&] (const rhythm::Rhythm& x) { return x.plain == t.rhythm.plain; }), all.end());
        all.push_back (t.rhythm);
        ++added;
    }
    progress.setValue ((int) files.size());
    std::stable_sort (all.begin(), all.end(), [] (const rhythm::Rhythm& a, const rhythm::Rhythm& b) { return a.name < b.name; });
    saveBreaks (all);
    if (failed.isEmpty())
        statusBar()->showMessage (tr ("%n break(s) added to Mode > rhythm > my breaks", nullptr, added), 8000);
    else
        QMessageBox::information (this, tr ("my breaks"),
                                  tr ("%1 added; these were left out:\n\n%2").arg (added).arg (failed.join (QStringLiteral ("\n"))));
}

void MainWindow::useRhythm (const rhythm::Rhythm& r)
{
    QDialog dialog (this);
    dialog.setWindowTitle (text (r.name));
    auto* layout = new QVBoxLayout (&dialog);
    auto* about = new QLabel (tip (r), &dialog);
    about->setWordWrap (true);
    about->setTextInteractionFlags (Qt::TextSelectableByMouse);
    layout->addWidget (about);
    QStringList sounds;
    for (const auto& l : r.lanes)
    {
        const auto drum = rhythm::drumName (l.note);
        sounds << QStringLiteral ("%1: %2").arg (text (l.name), l.channel == 9 && ! drum.empty() ? text (drum) : tr ("note %1").arg (l.note));
    }
    auto* lanes = new QLabel (tr ("sounds (general midi drums on channel 10): %1").arg (sounds.join (QStringLiteral ("; "))), &dialog);
    lanes->setWordWrap (true);
    layout->addWidget (lanes);

    auto* form = new QFormLayout;
    auto* cycles = new QSpinBox (&dialog);
    cycles->setRange (1, 256);
    cycles->setValue (std::max (1, (int) std::lround (16.0 / r.beats)));   // about four bars of 4/4
    cycles->setToolTip (tr ("how many times the cycle repeats"));
    form->addRow (tr ("&cycles"), cycles);
    auto* bpm = new QDoubleSpinBox (&dialog);
    bpm->setRange (20.0, 400.0);
    bpm->setDecimals (1);
    bpm->setValue (r.bpm);
    bpm->setSuffix (tr (" bpm"));
    bpm->setToolTip (tr ("beats (mātrās, akṣaras, pulses of the beat) a minute"));
    form->addRow (tr ("&tempo"), bpm);
    // Layakārī: the rhythm two, three or four times as fast in the same cycle.
    auto* laya = new QComboBox (&dialog);
    laya->addItem (tr ("as written"), 1);
    laya->addItem (tr ("duguṇ (twice as fast)"), 2);
    laya->addItem (tr ("tiguṇ (three times)"), 3);
    laya->addItem (tr ("cauguṇ (four times)"), 4);
    laya->setToolTip (tr ("each sound repeated in the same cycle; the claps, waves and kriyās that keep the cycle stay as they are"));
    form->addRow (tr ("&laya (speed)"), laya);
    // A tāḷam's naḍai: the pulses under each akṣara.
    QComboBox* nadai = nullptr;
    if (rhythm::hasGati (r))
    {
        nadai = new QComboBox (&dialog);
        for (auto [label, pulses] : { std::pair { tr ("caturaśra (4)"), 4 }, { tr ("tiśra (3)"), 3 }, { tr ("khaṇḍa (5)"), 5 },
                                      { tr ("miśra (7)"), 7 }, { tr ("saṅkīrṇa (9)"), 9 } })
            nadai->addItem (label, pulses);
        nadai->setToolTip (tr ("the gati: how many pulses each akṣara is divided into"));
        form->addRow (tr ("&naḍai (gati)"), nadai);
    }
    layout->addLayout (form);

    auto* buttons = new QDialogButtonBox (&dialog);
    auto* toMixer = buttons->addButton (tr ("add to the &mixer"), QDialogButtonBox::AcceptRole);
    toMixer->setToolTip (tr ("a midi clip on the selected track, at the playhead"));
    auto* toTracker = buttons->addButton (tr ("write a t&racker pattern"), QDialogButtonBox::AcceptRole);
    toTracker->setToolTip (tr ("a new pattern at the end of the song, a channel and an instrument for each sound"));
    auto* toFile = buttons->addButton (tr ("&save as midi..."), QDialogButtonBox::AcceptRole);
    auto* toGenerator = buttons->addButton (tr ("edit in the &generator..."), QDialogButtonBox::AcceptRole);
    toGenerator->setToolTip (tr ("its sounds as lanes of the rhythm generator, to add chance, rules and a feel"));
    buttons->addButton (QDialogButtonBox::Cancel);
    layout->addWidget (buttons);
    int choice = -1;
    for (auto [b, n] : { std::pair { toMixer, 0 }, { toTracker, 1 }, { toFile, 2 }, { toGenerator, 3 } })
        connect (b, &QPushButton::clicked, &dialog, [&choice, &dialog, n] { choice = n; dialog.accept(); });
    connect (buttons, &QDialogButtonBox::rejected, &dialog, &QDialog::reject);
    if (dialog.exec() != QDialog::Accepted || choice < 0)
        return;

    auto played = r;
    if (nadai != nullptr)
        played = rhythm::gati (played, nadai->currentData().toInt());
    if (laya->currentData().toInt() > 1)
        played = rhythm::laya (played, laya->currentData().toInt());
    useRhythmAs (played, choice, cycles->value(), bpm->value());
}

void MainWindow::useRhythmAs (const rhythm::Rhythm& r, int choice, int cycles, double bpm)
{
    auto settings = appSettings();
    if (choice == 3)
    {
        auto s = rhythmgen::fromRhythm (r);
        s.bars = std::clamp (cycles * r.beats / std::max (1, s.beatsPerBar), 0, 64);
        settings.setValue (QStringLiteral ("rhythm/generatorBpm"), bpm);
        rhythmGenerator (&s, text (r.name));
        return;
    }
    const auto song = rhythm::toSong (r, cycles, bpm);
    if (choice == 0)
    {
        showMixer();
        if (! mixerWin->addRhythm (*song, text (r.name)))
            statusBar()->showMessage (tr ("Pick a track in the mixer first"), 5000);
    }
    else if (choice == 1)
        rhythmToTracker (r, cycles, bpm);
    else
    {
        const auto folder = settings.value (QStringLiteral ("rhythm/folder")).toString();
        const auto file = QFileDialog::getSaveFileName (this, tr ("save rhythm as midi"),
                                                        QDir (folder).filePath (text (r.name) + QStringLiteral (".mid")),
                                                        tr ("MIDI files (*.mid *.midi)"));
        if (file.isEmpty())
            return;
        settings.setValue (QStringLiteral ("rhythm/folder"), QFileInfo (file).absolutePath());
        std::string why;
        if (! midifile::write (*song, file.toStdString(), why))
            statusBar()->showMessage (tr ("%1 could not be saved: %2").arg (file, text (why)), 8000);
        else
            statusBar()->showMessage (tr ("%1 saved").arg (QFileInfo (file).fileName()), 5000);
    }
}

void MainWindow::rhythmToTracker (const rhythm::Rhythm& r, int cycles, double bpm)
{
    auto* w = trackerWindow();
    if (! tracker->isLoaded())
    {
        trackerStop();
        tracker->newSong (8);
    }
    if (! tracker->isEditable())
        if (const auto error = tracker->makeEditable(); ! error.empty())
        {
            statusBar()->showMessage (text (error), 8000);
            return;
        }

    // A row for the finest step any sound has; as many cycles as fit in a pattern's 256 rows.
    const auto perBeat = rhythm::rowsPerBeat (r);
    const auto cycleRows = r.beats * perBeat;
    const auto fit = std::clamp (256 / std::max (1, cycleRows), 1, std::max (1, cycles));
    const auto rows = std::min (256, cycleRows * fit);
    const auto channels = std::max (2, (int) r.lanes.size());

    // The first run of instruments with no sample of their own, one for each sound.
    int first = 1;
    for (int i = 1; i + (int) r.lanes.size() - 1 <= TrackerEngine::numInstruments; ++i)
    {
        bool free = true;
        for (int k = 0; k < (int) r.lanes.size() && free; ++k)
            free = ! tracker->hasSample (i + k);
        if (free)
        {
            first = i;
            break;
        }
    }

    TrackerEngine::Block block;
    block.rows = rows;
    block.channels = channels;
    block.cells.assign ((size_t) (rows * channels * 6), 0);
    auto cell = [&] (int row, int channel) { return block.cells.data() + (size_t) (row * channels + channel) * 6; };
    for (const auto& h : rhythm::hits (r))
    {
        const auto& l = r.lanes[(size_t) h.lane];
        const auto row0 = h.step * (perBeat / std::max (1, l.perBeat));
        for (int c = 0; c < fit; ++c)
        {
            const auto row = c * cycleRows + row0;
            if (row >= rows)
                break;
            auto* p = cell (row, h.lane);
            p[0] = 49;   // C-4: the sample at its own pitch
            p[1] = (unsigned char) (first + h.lane);
            int eff = 0, op = 0;
            PatternText::convertVolumeToEffect (0x10 + h.velocity * 64 / 127, eff, op);
            p[2] = (unsigned char) eff;
            p[3] = (unsigned char) op;
        }
    }
    // The pattern's own speed and tempo, so it plays at the rhythm's pace in any song.
    const auto [speed, tempo] = trackerTiming (perBeat, bpm);
    for (auto [channel, value] : { std::pair { 0, speed }, { 1, tempo } })
    {
        int eff = 0xF, op = value;
        PatternText::convertEffectsFromFT2 (eff, op);
        cell (0, channel)[4] = (unsigned char) eff;
        cell (0, channel)[5] = (unsigned char) op;
    }

    const auto pattern = tracker->unusedPattern();
    if (pattern < 0)
    {
        statusBar()->showMessage (tr ("The song has no free pattern left"), 8000);
        return;
    }
    if (tracker->numChannels() < channels)
        tracker->setNumChannels (channels);
    tracker->insertOrder (tracker->numOrders(), pattern);
    tracker->setPatternRows (pattern, rows);
    tracker->pasteBlock (pattern, 0, 0, block);
    for (size_t k = 0; k < r.lanes.size(); ++k)
    {
        const auto drum = rhythm::drumName (r.lanes[k].note);
        tracker->setInstrumentName (first + (int) k, drum.empty() ? r.name : drum);
    }
    w->moduleChanged();
    w->show();
    w->raise();
    w->activateWindow();
    statusBar()->showMessage (tr ("%1: pattern %2, %3 rows (%4 a beat), instruments %5 to %6; load a sample into each, or link it to a Retro Sampler slot")
                                  .arg (text (r.name)).arg (pattern).arg (rows).arg (perBeat).arg (first).arg (first + (int) r.lanes.size() - 1), 15000);
}

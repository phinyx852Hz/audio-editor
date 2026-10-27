// SPDX-License-Identifier: AGPL-3.0-only
// The rhythm generator's dialog (owner, 2026-10-27; mixer/RhythmGenerator.h, docs/rhythms.md):
// a row for each drum lane (its sound, a Euclidean rhythm or written hits, a sieve, a chance),
// the rules and the feel under it, and the groove put on the selected mixer track, written into
// a new Tracker pattern, or saved as a MIDI file. It opens from Mode > rhythmic modes, or with a
// rhythmic mode's lanes from that rhythm's dialog. The last settings are kept in settings.ini.
#include "MainWindow.h"
#include "MixerWindow.h"
#include "Settings.h"
#include "../mixer/RhythmGenerator.h"
#include "../mixer/Rhythms.h"

#include <QCheckBox>
#include <QComboBox>
#include <QDialog>
#include <QDialogButtonBox>
#include <QDir>
#include <QDoubleSpinBox>
#include <QFileDialog>
#include <QFileInfo>
#include <QFormLayout>
#include <QGridLayout>
#include <QGroupBox>
#include <QHBoxLayout>
#include <QHeaderView>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QLabel>
#include <QLineEdit>
#include <QPushButton>
#include <QRandomGenerator>
#include <QSpinBox>
#include <QStatusBar>
#include <QTableWidget>
#include <QTimer>
#include <QVBoxLayout>

#include <algorithm>
#include <cmath>

namespace
{
    QString text (const std::string& s) { return QString::fromStdString (s); }

    QJsonObject toJson (const rhythmgen::Settings& s)
    {
        QJsonArray lanes;
        for (const auto& l : s.lanes)
            lanes.append (QJsonObject { { QStringLiteral ("name"), text (l.name) },
                                        { QStringLiteral ("note"), l.note },
                                        { QStringLiteral ("steps"), l.steps },
                                        { QStringLiteral ("stepsPerBeat"), l.stepsPerBeat },
                                        { QStringLiteral ("pulses"), l.pulses },
                                        { QStringLiteral ("rotation"), l.rotation },
                                        { QStringLiteral ("pattern"), text (l.pattern) },
                                        { QStringLiteral ("sieve"), text (l.sieve) },
                                        { QStringLiteral ("chance"), l.chance },
                                        { QStringLiteral ("velocity"), l.velocity },
                                        { QStringLiteral ("muted"), l.muted },
                                        { QStringLiteral ("pushMs"), l.pushMs } });
        return { { QStringLiteral ("lanes"), lanes },
                 { QStringLiteral ("beatsPerBar"), s.beatsPerBar },
                 { QStringLiteral ("bars"), s.bars },
                 { QStringLiteral ("swing"), s.swing },
                 { QStringLiteral ("swingPerBeat"), s.swingPerBeat },
                 { QStringLiteral ("humanizeVelocity"), s.humanizeVelocity },
                 { QStringLiteral ("humanizeMs"), s.humanizeMs },
                 { QStringLiteral ("fillEvery"), s.fillEvery },
                 { QStringLiteral ("fillChance"), s.fillChance },
                 { QStringLiteral ("densityFrom"), s.densityFrom },
                 { QStringLiteral ("densityTo"), s.densityTo },
                 { QStringLiteral ("skipEvery"), s.skipEvery },
                 { QStringLiteral ("rotateEvery"), s.rotateEvery },
                 { QStringLiteral ("seed"), (double) s.seed } };
    }

    rhythmgen::Settings fromJson (const QJsonObject& o)
    {
        rhythmgen::Settings s;
        for (const auto& v : o.value (QStringLiteral ("lanes")).toArray())
        {
            const auto j = v.toObject();
            rhythmgen::Lane l;
            l.name = j.value (QStringLiteral ("name")).toString().toStdString();
            l.note = j.value (QStringLiteral ("note")).toInt (36);
            l.steps = j.value (QStringLiteral ("steps")).toInt (16);
            l.stepsPerBeat = j.value (QStringLiteral ("stepsPerBeat")).toInt (4);
            l.pulses = j.value (QStringLiteral ("pulses")).toInt (4);
            l.rotation = j.value (QStringLiteral ("rotation")).toInt();
            l.pattern = j.value (QStringLiteral ("pattern")).toString().toStdString();
            l.sieve = j.value (QStringLiteral ("sieve")).toString().toStdString();
            l.chance = j.value (QStringLiteral ("chance")).toDouble (1.0);
            l.velocity = j.value (QStringLiteral ("velocity")).toInt (100);
            l.muted = j.value (QStringLiteral ("muted")).toBool();
            l.pushMs = j.value (QStringLiteral ("pushMs")).toDouble();
            s.lanes.push_back (std::move (l));
        }
        s.beatsPerBar = o.value (QStringLiteral ("beatsPerBar")).toInt (4);
        s.bars = o.value (QStringLiteral ("bars")).toInt();
        s.swing = o.value (QStringLiteral ("swing")).toDouble();
        s.swingPerBeat = o.value (QStringLiteral ("swingPerBeat")).toInt();
        s.humanizeVelocity = o.value (QStringLiteral ("humanizeVelocity")).toDouble();
        s.humanizeMs = o.value (QStringLiteral ("humanizeMs")).toDouble();
        s.fillEvery = o.value (QStringLiteral ("fillEvery")).toInt();
        s.fillChance = o.value (QStringLiteral ("fillChance")).toDouble (0.35);
        s.densityFrom = o.value (QStringLiteral ("densityFrom")).toDouble (1.0);
        s.densityTo = o.value (QStringLiteral ("densityTo")).toDouble (1.0);
        s.skipEvery = o.value (QStringLiteral ("skipEvery")).toInt();
        s.rotateEvery = o.value (QStringLiteral ("rotateEvery")).toInt();
        s.seed = (uint32_t) o.value (QStringLiteral ("seed")).toDouble (1.0);
        return s;
    }

    enum Column { sound, steps, perBeat, pulses, rotation, pattern, sieve, chance, velocity, push, mute, columns };

    // General MIDI's drums, 35 to 81.
    QComboBox* soundBox (int note, QWidget* parent)
    {
        auto* box = new QComboBox (parent);
        for (int n = 35; n <= 81; ++n)
            box->addItem (QStringLiteral ("%1 %2").arg (n).arg (text (rhythm::drumName (n))), n);
        box->setCurrentIndex (std::clamp (note, 35, 81) - 35);
        return box;
    }
}

void MainWindow::rhythmGenerator (const rhythmgen::Settings* start, const QString& startName)
{
    auto settings = appSettings();
    rhythmgen::Settings initial;
    if (start != nullptr)
        initial = *start;
    else
    {
        const auto saved = QJsonDocument::fromJson (settings.value (QStringLiteral ("rhythm/generator")).toByteArray()).object();
        initial = saved.isEmpty() ? rhythmgen::presets().front().settings : fromJson (saved);
    }
    if (initial.lanes.empty())
        initial.lanes.push_back ({});

    QDialog dialog (this);
    dialog.setWindowTitle (tr ("rhythm generator"));
    dialog.resize (1100, 640);
    auto* layout = new QVBoxLayout (&dialog);

    // Where to start: a preset, and a feel over it.
    auto* top = new QHBoxLayout;
    auto* presets = new QComboBox (&dialog);
    presets->addItem (tr ("start from..."));
    for (const auto& p : rhythmgen::presets())
    {
        presets->addItem (text (p.name));
        presets->setItemData (presets->count() - 1, text (p.about), Qt::ToolTipRole);
    }
    presets->setToolTip (tr ("euclidean timelines from many traditions (toussaint 2005); any rhythmic mode opens here from its own dialog"));
    top->addWidget (presets);
    auto* feel = new QComboBox (&dialog);
    feel->addItem (tr ("feel..."));
    for (const auto& f : rhythmgen::feels())
    {
        feel->addItem (text (f.name));
        feel->setItemData (feel->count() - 1, text (f.about), Qt::ToolTipRole);
    }
    feel->setToolTip (tr ("swing, and the kick, snare and cymbals pushed or laid back"));
    top->addWidget (feel);
    top->addStretch();
    auto* addLane = new QPushButton (tr ("&add a lane"), &dialog);
    auto* removeLane = new QPushButton (tr ("re&move the lane"), &dialog);
    top->addWidget (addLane);
    top->addWidget (removeLane);
    layout->addLayout (top);

    auto* table = new QTableWidget (0, columns, &dialog);
    table->setHorizontalHeaderLabels ({ tr ("sound"), tr ("steps"), tr ("a beat"), tr ("pulses"), tr ("rotation"), tr ("written hits"),
                                        tr ("sieve"), tr ("chance %"), tr ("velocity"), tr ("push ms"), tr ("mute") });
    const QStringList tips { tr ("the general midi drum"), tr ("the lane's cycle; lanes of different lengths run against each other"),
                             tr ("steps to the beat: 4 16ths, 3 triplet 8ths, 2 8ths"), tr ("hits spread as evenly as they go over the steps (bjorklund)"),
                             tr ("the cycle starts this many steps later"), tr ("X accent, x hit, o soft, . rest: when written, these take the euclidean rhythm's place"),
                             tr ("a xenakis sieve, such as 4@0|4@3: only the steps it holds may play"), tr ("each hit plays with this chance"),
                             tr ("a plain hit; an accent is 120, a soft one 60"), tr ("late (+, laid back) or early (-, pushing)"), tr ("silent") };
    for (int c = 0; c < columns; ++c)
        table->horizontalHeaderItem (c)->setToolTip (tips[c]);
    table->horizontalHeader()->setSectionResizeMode (pattern, QHeaderView::Stretch);
    table->verticalHeader()->setVisible (false);
    layout->addWidget (table, 1);

    auto spin = [&dialog] (int lo, int hi, int value, const QString& suffix = {}) {
        auto* b = new QSpinBox (&dialog);
        b->setRange (lo, hi);
        b->setValue (value);
        b->setSuffix (suffix);
        return b;
    };
    auto real = [&dialog] (double lo, double hi, double value, int decimals, const QString& suffix = {}) {
        auto* b = new QDoubleSpinBox (&dialog);
        b->setRange (lo, hi);
        b->setDecimals (decimals);
        b->setValue (value);
        b->setSuffix (suffix);
        return b;
    };

    auto setRow = [&] (int row, const rhythmgen::Lane& l) {
        table->setCellWidget (row, sound, soundBox (l.note, table));
        table->setCellWidget (row, steps, spin (1, 64, l.steps));
        table->setCellWidget (row, perBeat, spin (1, 12, l.stepsPerBeat));
        table->setCellWidget (row, pulses, spin (0, 64, l.pulses));
        table->setCellWidget (row, rotation, spin (0, 63, l.rotation));
        auto* written = new QLineEdit (text (l.pattern), table);
        written->setPlaceholderText (tr ("euclidean"));
        table->setCellWidget (row, pattern, written);
        auto* sv = new QLineEdit (text (l.sieve), table);
        sv->setPlaceholderText (tr ("every step"));
        table->setCellWidget (row, sieve, sv);
        table->setCellWidget (row, chance, spin (0, 100, (int) std::lround (l.chance * 100.0), QStringLiteral ("%")));
        table->setCellWidget (row, velocity, spin (1, 127, l.velocity));
        table->setCellWidget (row, push, real (-60.0, 60.0, l.pushMs, 0));
        auto* m = new QCheckBox (table);
        m->setChecked (l.muted);
        table->setCellWidget (row, mute, m);
    };
    auto setLanes = [&] (const std::vector<rhythmgen::Lane>& lanes) {
        table->setRowCount ((int) lanes.size());
        for (int r = 0; r < (int) lanes.size(); ++r)
            setRow (r, lanes[(size_t) r]);
    };

    // The clip: its length, the rules, the feel and the seed.
    auto* below = new QHBoxLayout;
    auto* clip = new QGroupBox (tr ("clip"), &dialog);
    auto* clipForm = new QFormLayout (clip);
    auto* beatsPerBar = spin (1, 32, initial.beatsPerBar);
    clipForm->addRow (tr ("beats a &bar"), beatsPerBar);
    auto* bars = spin (0, 64, initial.bars);
    bars->setSpecialValueText (tr ("until the lanes line up"));
    clipForm->addRow (tr ("ba&rs"), bars);
    auto* bpm = real (20.0, 400.0, settings.value (QStringLiteral ("rhythm/generatorBpm"), 100.0).toDouble(), 1, tr (" bpm"));
    clipForm->addRow (tr ("&tempo"), bpm);
    auto* length = new QLabel (clip);
    clipForm->addRow (tr ("length"), length);
    below->addWidget (clip);

    auto* rules = new QGroupBox (tr ("rules"), &dialog);
    auto* rulesForm = new QFormLayout (rules);
    auto* fillEvery = spin (0, 64, initial.fillEvery);
    fillEvery->setSpecialValueText (tr ("never"));
    fillEvery->setToolTip (tr ("the last bar of every n bars fills in: rests play softly by chance"));
    rulesForm->addRow (tr ("&fill every (bars)"), fillEvery);
    auto* fillChance = spin (0, 100, (int) std::lround (initial.fillChance * 100.0), QStringLiteral ("%"));
    rulesForm->addRow (tr ("fill c&hance"), fillChance);
    auto* densityFrom = spin (0, 200, (int) std::lround (initial.densityFrom * 100.0), QStringLiteral ("%"));
    auto* densityTo = spin (0, 200, (int) std::lround (initial.densityTo * 100.0), QStringLiteral ("%"));
    auto* density = new QHBoxLayout;
    density->addWidget (densityFrom);
    density->addWidget (new QLabel (tr ("to"), rules));
    density->addWidget (densityTo);
    rulesForm->addRow (tr ("&density"), density);
    auto* skipEvery = spin (0, 64, initial.skipEvery);
    skipEvery->setSpecialValueText (tr ("none"));
    rulesForm->addRow (tr ("s&kip every nth hit"), skipEvery);
    auto* rotateEvery = spin (0, 64, initial.rotateEvery);
    rotateEvery->setSpecialValueText (tr ("never"));
    rotateEvery->setToolTip (tr ("each lane turns a step later every n bars"));
    rulesForm->addRow (tr ("r&otate every (bars)"), rotateEvery);
    below->addWidget (rules);

    auto* feelBox = new QGroupBox (tr ("feel"), &dialog);
    auto* feelForm = new QFormLayout (feelBox);
    auto* swing = real (50.0, 75.0, 50.0 + initial.swing * 50.0, 1, QStringLiteral ("%"));
    swing->setToolTip (tr ("as on an mpc: 50% straight, 66% a triplet shuffle"));
    feelForm->addRow (tr ("&swing"), swing);
    auto* swingOn = new QComboBox (feelBox);
    swingOn->addItem (tr ("each lane's own steps"), 0);
    swingOn->addItem (tr ("8ths"), 2);
    swingOn->addItem (tr ("16ths"), 4);
    swingOn->setCurrentIndex (std::max (0, swingOn->findData (initial.swingPerBeat)));
    feelForm->addRow (tr ("swing &on"), swingOn);
    auto* humanizeMs = real (0.0, 50.0, initial.humanizeMs, 1, tr (" ms"));
    feelForm->addRow (tr ("h&umanize timing"), humanizeMs);
    auto* humanizeVelocity = real (0.0, 40.0, initial.humanizeVelocity, 1);
    feelForm->addRow (tr ("humanize &loudness"), humanizeVelocity);
    auto* seed = spin (1, 999999, (int) std::clamp<uint32_t> (initial.seed, 1, 999999));
    auto* reseed = new QPushButton (tr ("&new"), feelBox);
    auto* seedRow = new QHBoxLayout;
    seedRow->addWidget (seed, 1);
    seedRow->addWidget (reseed);
    feelForm->addRow (tr ("s&eed"), seedRow);
    below->addWidget (feelBox);
    layout->addLayout (below);

    auto current = [&] {
        rhythmgen::Settings s;
        for (int r = 0; r < table->rowCount(); ++r)
        {
            rhythmgen::Lane l;
            l.note = static_cast<QComboBox*> (table->cellWidget (r, sound))->currentData().toInt();
            const auto drum = rhythm::drumName (l.note);
            l.name = drum.empty() ? "drum" : drum;
            l.steps = static_cast<QSpinBox*> (table->cellWidget (r, steps))->value();
            l.stepsPerBeat = static_cast<QSpinBox*> (table->cellWidget (r, perBeat))->value();
            l.pulses = static_cast<QSpinBox*> (table->cellWidget (r, pulses))->value();
            l.rotation = static_cast<QSpinBox*> (table->cellWidget (r, rotation))->value();
            l.pattern = static_cast<QLineEdit*> (table->cellWidget (r, pattern))->text().trimmed().toStdString();
            l.sieve = static_cast<QLineEdit*> (table->cellWidget (r, sieve))->text().trimmed().toStdString();
            l.chance = static_cast<QSpinBox*> (table->cellWidget (r, chance))->value() / 100.0;
            l.velocity = static_cast<QSpinBox*> (table->cellWidget (r, velocity))->value();
            l.pushMs = static_cast<QDoubleSpinBox*> (table->cellWidget (r, push))->value();
            l.muted = static_cast<QCheckBox*> (table->cellWidget (r, mute))->isChecked();
            s.lanes.push_back (std::move (l));
        }
        s.beatsPerBar = beatsPerBar->value();
        s.bars = bars->value();
        s.swing = swing->value() / 50.0 - 1.0;
        s.swingPerBeat = swingOn->currentData().toInt();
        s.humanizeMs = humanizeMs->value();
        s.humanizeVelocity = humanizeVelocity->value();
        s.fillEvery = fillEvery->value();
        s.fillChance = fillChance->value() / 100.0;
        s.densityFrom = densityFrom->value() / 100.0;
        s.densityTo = densityTo->value() / 100.0;
        s.skipEvery = skipEvery->value();
        s.rotateEvery = rotateEvery->value();
        s.seed = (uint32_t) seed->value();
        return s;
    };
    auto showLength = [&] {
        const auto s = current();
        QStringList cycles;
        for (const auto& l : s.lanes)
            cycles << text (rhythmgen::cycle (l));
        length->setText (tr ("%n bar(s)", nullptr, rhythmgen::lengthBars (s)));
        length->setToolTip (cycles.join (QStringLiteral ("\n")));
    };
    // Every widget in the table is made anew on a change of lanes; a timer catches their edits.
    QTimer refresh;
    connect (&refresh, &QTimer::timeout, &dialog, showLength);
    refresh.start (400);

    auto setAll = [&] (const rhythmgen::Settings& s) {
        setLanes (s.lanes);
        beatsPerBar->setValue (s.beatsPerBar);
        bars->setValue (s.bars);
        swing->setValue (50.0 + s.swing * 50.0);
        swingOn->setCurrentIndex (std::max (0, swingOn->findData (s.swingPerBeat)));
        humanizeMs->setValue (s.humanizeMs);
        humanizeVelocity->setValue (s.humanizeVelocity);
        fillEvery->setValue (s.fillEvery);
        fillChance->setValue ((int) std::lround (s.fillChance * 100.0));
        densityFrom->setValue ((int) std::lround (s.densityFrom * 100.0));
        densityTo->setValue ((int) std::lround (s.densityTo * 100.0));
        skipEvery->setValue (s.skipEvery);
        rotateEvery->setValue (s.rotateEvery);
        showLength();
    };
    setAll (initial);
    QString name = startName.isEmpty() ? tr ("generated rhythm") : startName;

    connect (presets, &QComboBox::activated, &dialog, [&] (int i) {
        if (i <= 0)
            return;
        const auto& p = rhythmgen::presets()[(size_t) i - 1];
        auto s = p.settings;
        s.seed = (uint32_t) seed->value();
        setAll (s);
        name = text (p.name);
        presets->setCurrentIndex (0);
    });
    connect (feel, &QComboBox::activated, &dialog, [&] (int i) {
        if (i <= 0)
            return;
        auto s = current();
        rhythmgen::applyFeel (s, rhythmgen::feels()[(size_t) i - 1]);
        setAll (s);
        feel->setCurrentIndex (0);
    });
    connect (addLane, &QPushButton::clicked, &dialog, [&] {
        rhythmgen::Lane l;
        l.note = 42;
        l.steps = 16;
        l.pulses = 8;
        table->insertRow (table->rowCount());
        setRow (table->rowCount() - 1, l);
    });
    connect (removeLane, &QPushButton::clicked, &dialog, [&] {
        if (table->rowCount() > 1)
            table->removeRow (table->currentRow() >= 0 ? table->currentRow() : table->rowCount() - 1);
    });
    connect (reseed, &QPushButton::clicked, &dialog, [&] { seed->setValue ((int) QRandomGenerator::global()->bounded (1, 1000000)); });

    auto* buttons = new QDialogButtonBox (&dialog);
    auto* toMixer = buttons->addButton (tr ("add to the &mixer"), QDialogButtonBox::AcceptRole);
    toMixer->setToolTip (tr ("a midi clip on the selected track, at the playhead"));
    auto* toTracker = buttons->addButton (tr ("write a t&racker pattern"), QDialogButtonBox::AcceptRole);
    toTracker->setToolTip (tr ("on the grid, without swing or humanizing; up to 256 rows"));
    auto* toFile = buttons->addButton (tr ("&save as midi..."), QDialogButtonBox::AcceptRole);
    buttons->addButton (QDialogButtonBox::Cancel);
    layout->addWidget (buttons);
    int choice = -1;
    for (auto [b, n] : { std::pair { toMixer, 0 }, { toTracker, 1 }, { toFile, 2 } })
        connect (b, &QPushButton::clicked, &dialog, [&choice, &dialog, n] { choice = n; dialog.accept(); });
    connect (buttons, &QDialogButtonBox::rejected, &dialog, &QDialog::reject);
    const auto accepted = dialog.exec() == QDialog::Accepted;
    refresh.stop();
    const auto s = current();
    settings.setValue (QStringLiteral ("rhythm/generator"), QJsonDocument (toJson (s)).toJson (QJsonDocument::Compact));
    settings.setValue (QStringLiteral ("rhythm/generatorBpm"), bpm->value());
    if (! accepted || choice < 0)
        return;

    std::string why;
    if (choice == 1)
    {
        rhythm::Rhythm r;
        if (! rhythmgen::toRhythm (s, name.toStdString(), r, why))
            statusBar()->showMessage (text (why), 8000);
        else
            rhythmToTracker (r, 1, bpm->value());
        return;
    }
    const auto song = rhythmgen::generate (s, bpm->value(), why);
    if (! why.empty())
    {
        statusBar()->showMessage (text (why), 8000);
        return;
    }
    if (choice == 0)
    {
        showMixer();
        if (! mixerWin->addRhythm (*song, name))
            statusBar()->showMessage (tr ("Pick a track in the mixer first"), 5000);
        return;
    }
    const auto folder = settings.value (QStringLiteral ("rhythm/folder")).toString();
    const auto file = QFileDialog::getSaveFileName (this, tr ("save rhythm as midi"), QDir (folder).filePath (name + QStringLiteral (".mid")),
                                                    tr ("MIDI files (*.mid *.midi)"));
    if (file.isEmpty())
        return;
    settings.setValue (QStringLiteral ("rhythm/folder"), QFileInfo (file).absolutePath());
    if (! midifile::write (*song, file.toStdString(), why))
        statusBar()->showMessage (tr ("%1 could not be saved: %2").arg (file, text (why)), 8000);
    else
        statusBar()->showMessage (tr ("%1 saved").arg (QFileInfo (file).fileName()), 5000);
}

// SPDX-License-Identifier: AGPL-3.0-only
// The Mode menu's rhythmic modes (owner, 2026-10-26): tāls, tāḷams, breakbeats, trap, the
// medieval rhythmic modes, and the timelines and grooves of the Caribbean, South America,
// Africa and Korea (mixer/Rhythms.h, docs/rhythms.md). A rhythm chosen there opens a small
// dialog that puts it on the selected mixer track as a MIDI clip, writes it into a new
// Tracker pattern, or saves it as a MIDI file. Every name is lower case (owner).
#include "MainWindow.h"
#include "MixerWindow.h"
#include "Settings.h"
#include "TrackerWindow.h"
#include "../mixer/Rhythms.h"
#include "../tracker/PatternText.h"
#include "../tracker/TrackerEngine.h"

#include <QDialog>
#include <QDialogButtonBox>
#include <QDir>
#include <QDoubleSpinBox>
#include <QFileDialog>
#include <QFileInfo>
#include <QFormLayout>
#include <QLabel>
#include <QMenu>
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
}

void MainWindow::fillRhythmMenu (QMenu* menu)
{
    menu->clear();
    qDeleteAll (menu->findChildren<QMenu*> (QString(), Qt::FindDirectChildrenOnly));
    menu->setToolTipsVisible (true);
    const auto& all = rhythm::rhythms();
    for (const auto& group : rhythm::groups())
    {
        auto* sub = menu->addMenu (text (group));
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
            connect (a, &QAction::triggered, this, [this, i] { useRhythm ((int) i); });
        }
    }
}

void MainWindow::useRhythm (int index)
{
    const auto& all = rhythm::rhythms();
    if (index < 0 || index >= (int) all.size())
        return;
    const auto& r = all[(size_t) index];
    auto settings = appSettings();

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
    layout->addLayout (form);

    auto* buttons = new QDialogButtonBox (&dialog);
    auto* toMixer = buttons->addButton (tr ("add to the &mixer"), QDialogButtonBox::AcceptRole);
    toMixer->setToolTip (tr ("a midi clip on the selected track, at the playhead"));
    auto* toTracker = buttons->addButton (tr ("write a t&racker pattern"), QDialogButtonBox::AcceptRole);
    toTracker->setToolTip (tr ("a new pattern at the end of the song, a channel and an instrument for each sound"));
    auto* toFile = buttons->addButton (tr ("&save as midi..."), QDialogButtonBox::AcceptRole);
    buttons->addButton (QDialogButtonBox::Cancel);
    layout->addWidget (buttons);
    int choice = -1;
    for (auto [b, n] : { std::pair { toMixer, 0 }, { toTracker, 1 }, { toFile, 2 } })
        connect (b, &QPushButton::clicked, &dialog, [&choice, &dialog, n] { choice = n; dialog.accept(); });
    connect (buttons, &QDialogButtonBox::rejected, &dialog, &QDialog::reject);
    if (dialog.exec() != QDialog::Accepted || choice < 0)
        return;

    const auto song = rhythm::toSong (r, cycles->value(), bpm->value());
    if (choice == 0)
    {
        showMixer();
        if (! mixerWin->addRhythm (*song, text (r.plain)))
            statusBar()->showMessage (tr ("Pick a track in the mixer first"), 5000);
    }
    else if (choice == 1)
        rhythmToTracker (index, cycles->value(), bpm->value());
    else
    {
        const auto folder = settings.value (QStringLiteral ("rhythm/folder")).toString();
        const auto file = QFileDialog::getSaveFileName (this, tr ("save rhythm as midi"),
                                                        QDir (folder).filePath (text (r.plain) + QStringLiteral (".mid")),
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

void MainWindow::rhythmToTracker (int index, int cycles, double bpm)
{
    const auto& r = rhythm::rhythms()[(size_t) index];
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
        tracker->setInstrumentName (first + (int) k, drum.empty() ? r.plain : drum);
    }
    w->moduleChanged();
    w->show();
    w->raise();
    w->activateWindow();
    statusBar()->showMessage (tr ("%1: pattern %2, %3 rows (%4 a beat), instruments %5 to %6; load a sample into each, or link it to a Retro Sampler slot")
                                  .arg (text (r.name)).arg (pattern).arg (rows).arg (perBeat).arg (first).arg (first + (int) r.lanes.size() - 1), 15000);
}

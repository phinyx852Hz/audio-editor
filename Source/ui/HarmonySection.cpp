// SPDX-License-Identifier: AGPL-3.0-only
// The Mode menu's harmony (owner, 2026-10-29: mode "in the broader sense of a method or way"):
// the ways voices sound together, from Western art music by era, jazz, the folk and classical
// traditions of the world by region (as melody and tuning are), and the traditions grown from
// meeting Western harmony (mixer/Harmonies.h, docs/harmony.md). Each opens a small dialog with
// what it is, where it is from and its sources, and puts its example, on the Mode menu's final,
// on the selected mixer track or saves it as a MIDI file. Every name is lower case (owner);
// peoples, places and persons are capitalized.
#include "MainWindow.h"
#include "MixerWindow.h"
#include "Settings.h"
#include "../mixer/Harmonies.h"
#include "../mixer/Tunings.h"

#include <QCheckBox>
#include <QComboBox>
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
#include <functional>
#include <map>

namespace
{
    QString text (const std::string& s) { return QString::fromStdString (s); }

    const char* const finals[12] { "c", "c♯", "d", "e♭", "e", "f", "f♯", "g", "a♭", "a", "b♭", "b" };

    QString origin (const harmony::Harmony& h)
    {
        return h.dates.empty() ? text (h.tradition) : QStringLiteral ("%1, %2").arg (text (h.tradition), text (h.dates));
    }

    QString tip (const harmony::Harmony& h)
    {
        return QStringLiteral ("%1\n%2\n%3").arg (origin (h), text (h.about), text (h.sonorities));
    }

    // A group's harmonies: a tradition with several gets a submenu of its own, a single one is
    // listed with its tradition beside it.
    void addGroup (QMenu* into, const std::vector<int>& members, const std::function<void (int)>& pick)
    {
        const auto& all = harmony::harmonies();
        std::map<std::string, int> count;
        for (auto i : members)
            ++count[all[(size_t) i].tradition];
        std::map<std::string, QMenu*> subs;
        into->setToolTipsVisible (true);
        for (auto i : members)
        {
            const auto& h = all[(size_t) i];
            QAction* a = nullptr;
            if (count[h.tradition] > 1 && ! h.tradition.empty())
            {
                auto& sub = subs[h.tradition];
                if (sub == nullptr)
                {
                    sub = into->addMenu (text (h.tradition));
                    sub->setToolTipsVisible (true);
                }
                a = sub->addAction (text (h.name));
            }
            else
                a = into->addAction (QStringLiteral ("%1 — %2").arg (text (h.name), text (h.tradition)));
            a->setToolTip (tip (h));
            a->setStatusTip (QString (tip (h)).replace (QLatin1Char ('\n'), QStringLiteral ("; ")));
            QObject::connect (a, &QAction::triggered, into, [pick, i] { pick (i); });
        }
    }

    void addPart (QMenu* into, harmony::Part part, const std::function<void (int)>& pick)
    {
        for (const auto& g : harmony::groups (part))
        {
            const auto members = harmony::inGroup (part, g);
            if (g.empty())
                addGroup (into, members, pick);
            else
                addGroup (into->addMenu (text (harmony::groupTitle (part, g))), members, pick);
        }
    }
}

void MainWindow::fillHarmonyMenu (QMenu* menu)
{
    menu->clear();
    qDeleteAll (menu->findChildren<QMenu*> (QString(), Qt::FindDirectChildrenOnly));
    menu->setToolTipsVisible (true);
    auto pick = [this] (int i) { useHarmony (i); };
    using harmony::Part;
    // Western art music by era, then jazz (owner: "most definitely ... a submenu for jazz"),
    // then the world's traditions by region, then those grown from meeting Western harmony.
    addPart (menu->addMenu (text (harmony::partTitle (Part::western))), Part::western, pick);
    addPart (menu->addMenu (text (harmony::partTitle (Part::jazz))), Part::jazz, pick);
    menu->addSeparator();
    addPart (menu, Part::world, pick);
    menu->addSeparator();
    addPart (menu->addMenu (text (harmony::partTitle (Part::meeting))), Part::meeting, pick);
}

void MainWindow::useHarmony (int index)
{
    const auto& h = harmony::harmonies()[(size_t) index];
    auto settings = appSettings();
    QDialog dialog (this);
    dialog.setWindowTitle (text (h.name));
    auto* layout = new QVBoxLayout (&dialog);
    auto* about = new QLabel (QStringLiteral ("<b>%1</b> · %2<p>%3</p><p><i>%4</i></p><p>%5</p>")
                                  .arg (origin (h).toHtmlEscaped(), text (h.texture).toHtmlEscaped(), text (h.about).toHtmlEscaped(),
                                        text (h.sonorities).toHtmlEscaped(), tr ("sources: %1").arg (text (h.source)).toHtmlEscaped()),
                              &dialog);
    about->setWordWrap (true);
    about->setTextInteractionFlags (Qt::TextSelectableByMouse);
    about->setMinimumWidth (460);
    layout->addWidget (about);
    QStringList voices;
    for (const auto& v : h.voices)
        voices << text (v.name);
    auto* parts = new QLabel (tr ("the example's voices, a midi channel each: %1").arg (voices.join (QStringLiteral (", "))), &dialog);
    parts->setWordWrap (true);
    layout->addWidget (parts);

    auto* form = new QFormLayout;
    auto* final = new QComboBox (&dialog);
    for (int k = 0; k < 12; ++k)
        final->addItem (QString::fromUtf8 (finals[k]), k);
    final->setCurrentIndex (std::clamp (settings.value (QStringLiteral ("mode/final"), 0).toInt(), 0, 11));
    final->setToolTip (tr ("the final, tonic or drone the example is built on (the Mode menu's)"));
    form->addRow (tr ("&final"), final);
    auto* cycles = new QSpinBox (&dialog);
    cycles->setRange (1, 64);
    cycles->setValue (2);
    form->addRow (tr ("&repeats"), cycles);
    auto* bpm = new QDoubleSpinBox (&dialog);
    bpm->setRange (20.0, 400.0);
    bpm->setDecimals (1);
    bpm->setValue (h.bpm);
    bpm->setSuffix (tr (" bpm"));
    form->addRow (tr ("&tempo"), bpm);
    // Some are heard in their own tuning (a laras, Pythagorean, an echos): the track tuned to it.
    QCheckBox* tune = nullptr;
    const auto tuning = h.tuning.empty() ? -1 : mixer::findTuning (h.tuning);
    if (tuning >= 0)
    {
        tune = new QCheckBox (tr ("tune the track to %1").arg (text (mixer::tuningSystems()[(size_t) tuning].name)), &dialog);
        tune->setChecked (true);
        tune->setToolTip (tr ("the selected track's PHI synth plays in this tuning, on the final; other instruments stay in 12-TET"));
        form->addRow (QString(), tune);
    }
    layout->addLayout (form);

    auto* buttons = new QDialogButtonBox (&dialog);
    auto* toMixer = buttons->addButton (tr ("add to the &mixer"), QDialogButtonBox::AcceptRole);
    toMixer->setToolTip (tr ("a midi clip on the selected track, at the playhead"));
    auto* toFile = buttons->addButton (tr ("&save as midi..."), QDialogButtonBox::AcceptRole);
    buttons->addButton (QDialogButtonBox::Close);
    layout->addWidget (buttons);
    int choice = -1;
    for (auto [b, n] : { std::pair { toMixer, 0 }, { toFile, 1 } })
        connect (b, &QPushButton::clicked, &dialog, [&choice, &dialog, n] { choice = n; dialog.accept(); });
    connect (buttons, &QDialogButtonBox::rejected, &dialog, &QDialog::reject);
    if (dialog.exec() != QDialog::Accepted || choice < 0)
        return;

    const auto root = 48 + final->currentData().toInt();
    const auto song = harmony::toSong (h, root, cycles->value(), bpm->value());
    if (choice == 0)
    {
        showMixer();
        if (! mixerWin->addRhythm (*song, text (h.name)))
            return statusBar()->showMessage (tr ("Pick a track in the mixer first"), 5000);
        if (tune != nullptr && tune->isChecked())
            mixerWin->tuneSelectedTrack ({ tuning, final->currentData().toInt() });
        return;
    }
    const auto folder = settings.value (QStringLiteral ("harmony/folder"), settings.value (QStringLiteral ("rhythm/folder"))).toString();
    const auto file = QFileDialog::getSaveFileName (this, tr ("save harmony as midi"), QDir (folder).filePath (text (h.name) + QStringLiteral (".mid")),
                                                    tr ("MIDI files (*.mid *.midi)"));
    if (file.isEmpty())
        return;
    settings.setValue (QStringLiteral ("harmony/folder"), QFileInfo (file).absolutePath());
    std::string why;
    if (! midifile::write (*song, file.toStdString(), why))
        statusBar()->showMessage (tr ("%1 could not be saved: %2").arg (file, text (why)), 8000);
    else
        statusBar()->showMessage (tr ("%1 saved").arg (QFileInfo (file).fileName()), 5000);
}

// SPDX-License-Identifier: AGPL-3.0-only
// The mixer's fugue (mixer/Fugue.h): the dialog that asks for one on a MIDI clip's melody, and
// its voices written as MIDI files beside the mix, each a clip on a new track where the clip is.
#include "MixerWindow.h"
#include "Settings.h"
#include "../mixer/Fugue.h"
#include "../mixer/Tunings.h"

#include <QCheckBox>
#include <QComboBox>
#include <QDialog>
#include <QDialogButtonBox>
#include <QDir>
#include <QEventLoop>
#include <QFileInfo>
#include <QFormLayout>
#include <QFutureWatcher>
#include <QHBoxLayout>
#include <QLabel>
#include <QMessageBox>
#include <QProgressDialog>
#include <QPushButton>
#include <QRegularExpression>
#include <QSpinBox>
#include <QtConcurrent>

#include <algorithm>

bool MixerWindow::writeFugue (int track, const mixer::Clip& clip, mixer::SlotPtr slot, int scene)
{
    const auto title = tr ("Fugue");
    auto song = clip.midi->song;
    if (song->ticksPerQuarter <= 0)
    {
        say (tr ("This MIDI file counts in SMPTE time, so it cannot be edited here."));
        return false;
    }
    roomForTracks (2, title);
    const auto free = engine.trackLimit() - engine.numTracks();
    if (free < 2)
    {
        QMessageBox::information (this, title, tr ("A fugue's voices go on new tracks, two at least, and the mixer has room for %n more.", "", free));
        return false;
    }
    if (clip.reversed)
        song = midifile::reversed (*song);   // the subject as it is heard
    const auto subject = fugue::subject (counterpoint::cantus (*song), song->ticksPerQuarter);
    if (subject.empty())
    {
        say (tr ("The clip needs a melody of two notes or more to make a subject of."));
        return false;
    }
    const auto tuning = slot != nullptr ? mixer::tuningInForce (slot->tuning, sceneTunings[(size_t) scene], info[(size_t) track].tuning)
                                        : info[(size_t) track].tuning;

    // What to write.
    QDialog dialog (this);
    dialog.setWindowTitle (title);
    auto* form = new QFormLayout (&dialog);
    auto* voicesBox = new QSpinBox (&dialog);
    voicesBox->setRange (2, std::min (4, free));
    voicesBox->setToolTip (tr ("The fugue's voices, each on a new track: two, three (soprano, alto, bass) or four (and tenor)"));
    auto* modeBox = new QComboBox (&dialog);
    modeBox->addItem (tr ("The subject's own (guessed)"), -1);
    {
        std::string family;
        const auto& all = mixer::tuningSystems();
        for (size_t i = 0; i < all.size(); ++i)
        {
            if (all[i].family != family)
            {
                family = all[i].family;
                modeBox->insertSeparator (modeBox->count());
            }
            modeBox->addItem (QString::fromStdString (all[i].name), (int) i);
            modeBox->setItemData (modeBox->count() - 1, QString::fromStdString (all[i].family), Qt::ToolTipRole);
        }
    }
    modeBox->setMaxVisibleItems (24);
    modeBox->setToolTip (tr ("The notes the voices use and how their intervals sound; the answer and the other entries move "
                             "by its degrees, so a raga's subject stays in the raga"));
    auto* rootChoice = new QComboBox (&dialog);
    for (const auto* name : { "C", "C♯", "D", "E♭", "E", "F", "F♯", "G", "A♭", "A", "B♭", "B" })
        rootChoice->addItem (QString::fromUtf8 (name));
    rootChoice->setToolTip (tr ("The tonic: Sa, the final, or the maqam's root"));
    auto* answerBox = new QComboBox (&dialog);
    answerBox->addItem (tr ("Tonal: tonic and dominant answer each other"));
    answerBox->addItem (tr ("Real: every note a fifth up"));
    answerBox->setToolTip (tr ("The answer, the subject at the fifth in the second voice to enter, by the mode's degrees"));
    auto* csBox = new QCheckBox (tr ("A countersubject"), &dialog);
    csBox->setToolTip (tr ("The voice that had the subject goes on against the answer in a line of its own, "
                           "which comes back against the later entries where it fits"));
    auto* speciesBox = new QComboBox (&dialog);
    speciesBox->addItem (tr ("Note against the beat"));
    speciesBox->addItem (tr ("Two notes a beat"));
    speciesBox->addItem (tr ("Four notes a beat"));
    speciesBox->addItem (tr ("Syncopated, tied over the beat"));
    speciesBox->addItem (tr ("Florid: all of them mixed"));
    speciesBox->setToolTip (tr ("The rhythm of the free voices, by the species of counterpoint"));
    auto* middleBox = new QSpinBox (&dialog);
    middleBox->setRange (0, 4);
    middleBox->setToolTip (tr ("Entries after the exposition on other degrees of the mode, each after an episode "
                               "on the subject's head in sequence"));
    auto* inversionBox = new QCheckBox (tr ("Every other middle entry upside down"), &dialog);
    auto* strettoBox = new QCheckBox (tr ("A stretto: the entries overlapping at the end"), &dialog);
    strettoBox->setToolTip (tr ("Every voice enters again before the one before has finished, as close as they sound well"));
    auto* variation = new QSpinBox (&dialog);
    variation->setRange (1, 9999);
    variation->setToolTip (tr ("Another number writes other free voices and countersubject on the same plan"));

    auto settings = appSettings();
    voicesBox->setValue (settings.value (QStringLiteral ("fugue/voices"), 3).toInt());
    answerBox->setCurrentIndex (std::clamp (settings.value (QStringLiteral ("fugue/answer"), 0).toInt(), 0, 1));
    csBox->setChecked (settings.value (QStringLiteral ("fugue/countersubject"), true).toBool());
    speciesBox->setCurrentIndex (std::clamp (settings.value (QStringLiteral ("fugue/species"), 4).toInt(), 0, 4));
    middleBox->setValue (settings.value (QStringLiteral ("fugue/middle"), 2).toInt());
    inversionBox->setChecked (settings.value (QStringLiteral ("fugue/inversion"), false).toBool());
    strettoBox->setChecked (settings.value (QStringLiteral ("fugue/stretto"), true).toBool());
    variation->setValue (settings.value (QStringLiteral ("fugue/variation"), 1).toInt());
    // The tuning the clip plays in, else the last mode chosen.
    auto system = tuning.system;
    auto root = tuning.root;
    if (system < 0)
    {
        system = mixer::findTuning (settings.value (QStringLiteral ("fugue/mode")).toString().toStdString());
        root = settings.value (QStringLiteral ("fugue/root"), subject.back().pitch % 12).toInt();
    }
    modeBox->setCurrentIndex (std::max (0, modeBox->findData (system)));
    rootChoice->setCurrentIndex (std::clamp (root, 0, 11));
    auto rootRow = [&] { rootChoice->setEnabled (modeBox->currentData().toInt() >= 0); };
    connect (modeBox, &QComboBox::currentIndexChanged, &dialog, rootRow);
    rootRow();

    auto* modeRow = new QHBoxLayout;
    modeRow->addWidget (modeBox, 1);
    modeRow->addWidget (rootChoice);
    form->addRow (tr ("Voices"), voicesBox);
    form->addRow (tr ("Mode"), modeRow);
    form->addRow (tr ("Answer"), answerBox);
    form->addRow (QString(), csBox);
    form->addRow (tr ("Free voices"), speciesBox);
    form->addRow (tr ("Middle entries"), middleBox);
    form->addRow (QString(), inversionBox);
    form->addRow (QString(), strettoBox);
    form->addRow (tr ("Variation"), variation);
    const auto beats = (double) (subject.back().end - subject.front().start) / song->ticksPerQuarter;
    auto* note = new QLabel (tr ("The subject: the clip's first %n note(s), %1 beats. Each voice goes on a new track, "
                                 "a clip beside this one.", "", (int) subject.size()).arg (beats, 0, 'g', 3), &dialog);
    note->setWordWrap (true);
    note->setForegroundRole (QPalette::PlaceholderText);
    form->addRow (note);
    auto* buttons = new QDialogButtonBox (QDialogButtonBox::Ok | QDialogButtonBox::Cancel, &dialog);
    buttons->button (QDialogButtonBox::Ok)->setText (tr ("Write"));
    form->addRow (buttons);
    connect (buttons, &QDialogButtonBox::accepted, &dialog, &QDialog::accept);
    connect (buttons, &QDialogButtonBox::rejected, &dialog, &QDialog::reject);
    if (dialog.exec() != QDialog::Accepted)
        return false;

    fugue::Settings options;
    options.voices = voicesBox->value();
    options.answer = (fugue::Answer) answerBox->currentIndex();
    options.countersubject = csBox->isChecked();
    options.species = (counterpoint::Species) (speciesBox->currentIndex() + 1);
    options.middleEntries = middleBox->value();
    options.inversion = inversionBox->isChecked();
    options.stretto = strettoBox->isChecked();
    options.seed = (uint32_t) variation->value();
    const auto chosen = modeBox->currentData().toInt();
    if (chosen >= 0)
        options.mode = counterpoint::Mode::fromTuning (mixer::tuningSystems()[(size_t) chosen], rootChoice->currentIndex());
    settings.setValue (QStringLiteral ("fugue/voices"), options.voices);
    settings.setValue (QStringLiteral ("fugue/answer"), answerBox->currentIndex());
    settings.setValue (QStringLiteral ("fugue/countersubject"), options.countersubject);
    settings.setValue (QStringLiteral ("fugue/species"), speciesBox->currentIndex());
    settings.setValue (QStringLiteral ("fugue/middle"), options.middleEntries);
    settings.setValue (QStringLiteral ("fugue/inversion"), options.inversion);
    settings.setValue (QStringLiteral ("fugue/stretto"), options.stretto);
    settings.setValue (QStringLiteral ("fugue/variation"), variation->value());
    settings.setValue (QStringLiteral ("fugue/mode"), chosen >= 0 ? QString::fromStdString (mixer::tuningSystems()[(size_t) chosen].plain) : QString());
    settings.setValue (QStringLiteral ("fugue/root"), rootChoice->currentIndex());

    // Written on a worker: four voices in many equal steps take a second or two.
    QProgressDialog progress (tr ("Writing a fugue on %1...").arg (QFileInfo (QString::fromStdString (clip.midi->path)).completeBaseName()),
                              QString(), 0, 0, this);
    progress.setWindowTitle (title);
    progress.setWindowModality (Qt::WindowModal);
    progress.setMinimumDuration (300);
    QFutureWatcher<fugue::Result> watcher;
    QEventLoop loop;
    connect (&watcher, &QFutureWatcher<fugue::Result>::finished, &loop, &QEventLoop::quit);
    const auto tpq = song->ticksPerQuarter;
    watcher.setFuture (QtConcurrent::run ([subject, tpq, options] { return fugue::generate (subject, tpq, options); }));
    loop.exec();
    progress.reset();
    const auto result = watcher.result();
    if (result.voices.empty())
        return false;

    // Each voice a MIDI file beside the mix, then a clip on a new track where this one is.
    const auto base = editsFolder();
    QDir().mkpath (base);
    auto stem = QFileInfo (QString::fromStdString (clip.midi->path)).completeBaseName();
    if (stem.isEmpty())
        stem = tr ("MIDI");
    stem.replace (QRegularExpression (QStringLiteral ("[/\\\\:*?\"<>|]")), QStringLiteral ("_"));
    int channel = 0;
    for (int c = 0; c < 16; ++c)
        if (song->channelsUsed & (1u << c))
        {
            channel = c;
            break;
        }
    const auto bpm = followTempoBox->isChecked() ? tempoBox->value() : 0.0;
    remember();
    int made = 0;
    for (size_t v = 0; v < result.voices.size() && engine.roomForTracks(); ++v)
    {
        if (result.voices[v].empty())
            continue;
        const auto voiceName = QString::fromStdString (result.names[v]);
        const auto written = counterpoint::toSong (*song, result.voices[v], channel);
        QString file;
        for (int n = 1; file.isEmpty() || QFileInfo::exists (file); ++n)
            file = QDir (base).filePath (n == 1 ? QStringLiteral ("%1 fugue %2.mid").arg (stem, voiceName)
                                                : QStringLiteral ("%1 fugue %2 %3.mid").arg (stem, voiceName).arg (n));
        std::string why;
        if (! midifile::write (*written, file.toStdString(), why))
        {
            say (tr ("The fugue's %1 could not be saved to %2: %3").arg (voiceName, file, QString::fromStdString (why)));
            break;
        }
        const auto target = engine.numTracks();
        engine.setNumTracks (target + 1);
        auto& to = engine.strip (target);
        to.trimDb = 0.0f;
        to.invertLeft = to.invertRight = false;
        to.lowCut = to.highCut = false;
        to.width = 1.0f;
        to.delayMs = 0.0f;
        to.faderDb = engine.strip (track).faderDb.load();
        to.pan = 0.0f;
        to.mute = to.solo = false;
        to.transpose = 0;
        to.channels = 0xFFFF;
        engine.setInserts (target, {});
        engine.setInstrument (target, nullptr);
        engine.setClips (target, {});
        info[(size_t) target].name = tr ("%1 fugue %2").arg (info[(size_t) track].name, voiceName);
        info[(size_t) target].colour = info[(size_t) track].colour;
        info[(size_t) target].tuning = tuning;   // the PHI synth plays the voices in the subject's tuning
        if (slot != nullptr)
        {
            auto placed = std::make_shared<mixer::Slot> (*slot);
            placed->clip.midi = launcherMidi (written, file.toStdString());
            placed->clip.reversed = false;
            placed->clip.offset = std::min (clip.offset, placed->clip.midi->length);
            placed->clip.length = placed->clip.midi->length - placed->clip.offset;   // the whole fugue
            slots[(size_t) target][(size_t) scene] = placed;
            engine.setSlot (target, scene, placed);
        }
        else
        {
            auto placed = mixer::makeClip (mixer::placeMidi (written, file.toStdString(), rate(), bpm), clip.start);
            placed.offset = std::min (clip.offset, placed.midi->length);
            placed.length = placed.midi->length - placed.offset;
            editClips (target, { placed });
        }
        giveSynth (target);
        ++made;
    }
    int middle = 0;
    for (const auto& s : result.plan)
        middle += s.what.starts_with ("middle entry") ? 1 : 0;
    const auto mode = chosen >= 0 ? tuningText ({ chosen, rootChoice->currentIndex() }) : tr ("the subject's mode");
    auto text = tr ("A fugue in %n voice(s) in %1, %2 bars", "", made).arg (mode).arg ((double) result.end / (4.0 * tpq), 0, 'f', 0);
    if (middle > 0)
        text += tr (", %n middle entry(s)", "", middle);
    if (result.strettoDelay > 0)
        text += tr (", a stretto %1 beats apart").arg ((double) result.strettoDelay / tpq, 0, 'g', 3);
    if (made < (int) result.voices.size())
        text += tr ("; the mixer had room for %1 of %2 voices").arg (made).arg (result.voices.size());
    if (result.bent > 0)
        text += tr ("; %n strict rule(s) bent where nothing else fitted", "", result.bent);
    say (text);
    return made > 0;
}

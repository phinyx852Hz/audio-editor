// SPDX-License-Identifier: AGPL-3.0-only
// Stochasticize (owner, 2026-10-26; the owner's spelling): a new line for the MIDI clip shown
// in the Device Chain by Iannis Xenakis's methods (mixer/Stochastic.h, docs/stochastic.md):
// an ST cloud or Markov screens, its pitches through a sieve, its rhythm free (a Poisson
// process) or through a sieve. A sieve can be filled from the Mode menu's mode, a rhythmic
// mode or the clip itself, and then edited. Every name in it is lower case (owner).
#include "MixerWindow.h"
#include "Settings.h"
#include "../mixer/Rhythms.h"
#include "../mixer/Stochastic.h"
#include "../mixer/Tunings.h"

#include <QComboBox>
#include <QDialog>
#include <QDialogButtonBox>
#include <QDir>
#include <QDoubleSpinBox>
#include <QFileInfo>
#include <QFormLayout>
#include <QHBoxLayout>
#include <QLabel>
#include <QLineEdit>
#include <QMessageBox>
#include <QPushButton>
#include <QRegularExpression>
#include <QSpinBox>

#include <algorithm>
#include <cmath>
#include <limits>

namespace
{
    const char* const keyNames[12] { "c", "c♯", "d", "e♭", "e", "f", "f♯", "g", "a♭", "a", "b♭", "b" };

    QString noteName (int key)
    {
        return QStringLiteral ("%1%2").arg (QString::fromUtf8 (keyNames[((key % 12) + 12) % 12])).arg (key / 12 - 1);
    }

    // Sieves Xenakis wrote out, to start from.
    struct Example
    {
        const char* name;
        const char* sieve;
    };
    const Example pitchExamples[] {
        { "xenakis: nomos alpha", "(13@3 | 13@5 | 13@7 | 13@9) & 11@2 | (11@4 & 13@5) | (13@9 & 11@8) | 13@0 | 13@1 | 13@6" },
        { "xenakis: the major scale", "(-3@2 & 4@0) | (-3@1 & 4@1) | (3@2 & 4@2) | (-3@0 & 4@3)" },
        { "messiaen: second mode (octatonic)", "3@0 | 3@1" },
        { "messiaen: third mode", "4@0 | 4@2 | 4@3" },
    };
    const Example rhythmExamples[] {
        { "xenakis: psappha", "(8@0 | 8@1 | 8@7) & (5@1 | 5@3) | (8@0 | 8@1 | 8@2) & 5@0 | 8@3 & (5@0 | 5@1 | 5@2 | 5@3 | 5@4)"
                              " | 8@4 & (5@0 | 5@1 | 5@2 | 5@3 | 5@4) | (8@5 | 8@6) & (5@2 | 5@3 | 5@4) | 8@1 & 5@2 | 8@6 & 5@1" },
        { "every third and fifth step", "3@0 | 5@0" },
        { "a tresillo: 8@0 | 8@3 | 8@6", "8@0 | 8@3 | 8@6" },
    };
}

void MixerWindow::stochasticizeShownMidi()
{
    int track = -1, scene = -1;
    mixer::Clip clip;
    mixer::SlotPtr slot;
    mixer::TuningChoice tuning;
    if (! shownMidiClip (track, scene, clip, slot, tuning))
        return;
    const auto title = tr ("stochasticize");
    auto song = clip.midi->song;
    if (song->ticksPerQuarter <= 0)
    {
        say (tr ("This MIDI file counts in SMPTE time, so it cannot be edited here."));
        return;
    }
    if (clip.reversed)
        song = midifile::reversed (*song);   // read as it is heard
    const auto tpq = song->ticksPerQuarter;
    const auto bar = (int64_t) tpq * 4;
    // The clip's range, for the new line's.
    int lowest = 128, highest = -1;
    for (const auto& e : song->events)
        if (e.kind() == 0x90 && e.data2 > 0 && e.channel() != 9)
        {
            lowest = std::min (lowest, (int) e.data1);
            highest = std::max (highest, (int) e.data1);
        }
    // The mode: the clip's tuning, else the Mode menu's (it keeps its choice for counterpoint).
    auto settings = appSettings();
    auto modeSystem = tuning.system;
    auto modeRoot = tuning.root;
    if (modeSystem < 0)
    {
        modeSystem = mixer::findTuning (settings.value (QStringLiteral ("counterpoint/mode")).toString().toStdString());
        modeRoot = settings.value (QStringLiteral ("counterpoint/root"), 0).toInt();
    }

    QDialog dialog (this);
    dialog.setWindowTitle (title);
    auto* form = new QFormLayout (&dialog);

    auto* methodBox = new QComboBox (&dialog);
    methodBox->addItem (tr ("st cloud (achorripsis, st/10)"));
    methodBox->addItem (tr ("markov screens (analogique a and b)"));
    methodBox->setToolTip (tr ("st cloud: sections of the line, each with a density drawn from the exponential law, the notes in it "
                               "as a poisson process.\nmarkov screens: each section a screen, low or high, soft or loud, sparse or "
                               "dense, the next chosen by a markov chain"));
    auto* bars = new QSpinBox (&dialog);
    bars->setRange (1, 256);
    bars->setSuffix (tr (" bars"));
    bars->setToolTip (tr ("How long the new line is, in bars of 4/4: at first the clip's length"));
    bars->setValue ((int) std::max<int64_t> (1, (midifile::span (*song) + bar - 1) / bar));
    auto* density = new QDoubleSpinBox (&dialog);
    density->setRange (0.05, 32.0);
    density->setDecimals (2);
    density->setSingleStep (0.25);
    density->setSuffix (tr (" notes a beat"));
    density->setToolTip (tr ("The mean density; each section or screen draws its own about it"));
    auto* section = new QDoubleSpinBox (&dialog);
    section->setRange (0.25, 64.0);
    section->setDecimals (2);
    section->setSuffix (tr (" beats"));
    section->setToolTip (tr ("The length of each cloud section, or of each markov screen"));
    auto* stay = new QDoubleSpinBox (&dialog);
    stay->setRange (0.0, 1.0);
    stay->setDecimals (2);
    stay->setSingleStep (0.05);
    stay->setToolTip (tr ("The chance each of a screen's three states (register, loudness, density) stays the same in the next "
                          "screen. analogique's matrices move more often than they stay: 0.2"));

    // Pitches: a sieve counted in semitones from the root.
    auto* pitchSieve = new QLineEdit (&dialog);
    pitchSieve->setPlaceholderText (tr ("every key (or a sieve: 12@0 | 12@4 | 12@7)"));
    pitchSieve->setToolTip (tr ("Xenakis's sieve, counted in semitones from the root: m@r is every key r more than a multiple of m "
                                "above it; | joins, & keeps what both have, - leaves out"));
    auto* pitchFrom = new QComboBox (&dialog);
    pitchFrom->addItem (tr ("fill from..."));
    if (modeSystem >= 0)
        pitchFrom->addItem (tr ("the mode: %1").arg (QString::fromStdString (mixer::tuningSystems()[(size_t) modeSystem].name)), QStringLiteral ("mode"));
    pitchFrom->addItem (tr ("the clip's notes"), QStringLiteral ("clip"));
    pitchFrom->addItem (tr ("every key"), QStringLiteral ("all"));
    for (const auto& e : pitchExamples)
        pitchFrom->addItem (QString::fromUtf8 (e.name), QString::fromUtf8 (e.sieve));
    auto* rootChoice = new QComboBox (&dialog);
    for (const auto* name : keyNames)
        rootChoice->addItem (QString::fromUtf8 (name));
    rootChoice->setToolTip (tr ("The root the pitch sieve counts from: 0 is this key"));
    auto* low = new QSpinBox (&dialog);
    auto* high = new QSpinBox (&dialog);
    for (auto* box : { low, high })
    {
        box->setRange (0, 127);
        box->setToolTip (tr ("MIDI key: 60 is middle c (c4)"));
    }
    auto showNames = [low, high] (QLabel* label) { label->setText (QStringLiteral ("%1 – %2").arg (noteName (low->value()), noteName (high->value()))); };

    auto* intervalLaw = new QComboBox (&dialog);
    auto* lengthLaw = new QComboBox (&dialog);
    auto* loudnessLaw = new QComboBox (&dialog);
    for (auto* box : { intervalLaw, lengthLaw, loudnessLaw })
        for (int l = 0; l < stochastic::numLaws; ++l)
            box->addItem (QString::fromUtf8 (stochastic::lawName ((stochastic::Law) l)));
    intervalLaw->setToolTip (tr ("How far each note leaps from the last. linear is xenakis's law of intervals: small leaps likeliest"));
    auto* intervalSpread = new QDoubleSpinBox (&dialog);
    intervalSpread->setRange (0.5, 64.0);
    intervalSpread->setDecimals (1);
    intervalSpread->setSuffix (tr (" semitones"));
    auto* lengthMean = new QDoubleSpinBox (&dialog);
    lengthMean->setRange (0.03, 8.0);
    lengthMean->setDecimals (2);
    lengthMean->setSingleStep (0.25);
    lengthMean->setSuffix (tr (" beats"));
    lengthLaw->setToolTip (tr ("How long each note is, about the mean"));
    auto* velocity = new QSpinBox (&dialog);
    velocity->setRange (1, 127);
    auto* velocitySpread = new QSpinBox (&dialog);
    velocitySpread->setRange (0, 64);
    velocitySpread->setPrefix (QStringLiteral ("± "));
    loudnessLaw->setToolTip (tr ("How loud each note is (velocity), about the centre"));

    // Rhythm: free, or a sieve of grid steps.
    auto* timeBox = new QComboBox (&dialog);
    timeBox->addItem (tr ("free: a poisson process"));
    timeBox->addItem (tr ("on the rhythm sieve"));
    auto* rhythmSieve = new QLineEdit (&dialog);
    rhythmSieve->setPlaceholderText (tr ("a sieve of steps: 16@0 | 16@3 | 16@6 | 16@10 | 16@12"));
    rhythmSieve->setToolTip (tr ("The steps notes may start on, counted from the clip's start; each is taken with the chance the density gives it"));
    auto* perBeat = new QSpinBox (&dialog);
    perBeat->setRange (1, 48);
    perBeat->setSuffix (tr (" steps a beat"));
    auto* rhythmFrom = new QComboBox (&dialog);
    rhythmFrom->setMaxVisibleItems (24);
    rhythmFrom->addItem (tr ("fill from..."));
    rhythmFrom->addItem (tr ("the clip's onsets"), -2);
    for (const auto& e : rhythmExamples)
        rhythmFrom->addItem (QString::fromUtf8 (e.name), QString::fromUtf8 (e.sieve));
    {
        std::string group;
        const auto& all = rhythm::rhythms();
        for (size_t i = 0; i < all.size(); ++i)
        {
            if (all[i].group != group)
            {
                group = all[i].group;
                rhythmFrom->insertSeparator (rhythmFrom->count());
            }
            rhythmFrom->addItem (QString::fromStdString (all[i].name), (int) i);
            rhythmFrom->setItemData (rhythmFrom->count() - 1, QString::fromStdString (all[i].group), Qt::ToolTipRole);
        }
    }

    auto* whereBox = new QComboBox (&dialog);
    whereBox->addItem (tr ("a new track, beside this clip"));
    whereBox->addItem (tr ("in place of this clip (ctrl+z puts it back)"));
    if (engine.numTracks() >= mixer::maxTracks)
    {
        whereBox->setCurrentIndex (1);
        whereBox->setItemData (0, 0, Qt::UserRole - 1);   // disabled: the mixer is full
    }
    auto* variation = new QSpinBox (&dialog);
    variation->setRange (1, 9999);
    variation->setToolTip (tr ("Another number draws another line from the same laws"));

    // The last choices.
    methodBox->setCurrentIndex (std::clamp (settings.value (QStringLiteral ("stochastic/method"), 0).toInt(), 0, 1));
    density->setValue (settings.value (QStringLiteral ("stochastic/density"), 2.0).toDouble());
    section->setValue (settings.value (QStringLiteral ("stochastic/section"), 4.0).toDouble());
    stay->setValue (settings.value (QStringLiteral ("stochastic/stay"), 0.2).toDouble());
    pitchSieve->setText (settings.value (QStringLiteral ("stochastic/pitches")).toString());
    rootChoice->setCurrentIndex (std::clamp (settings.value (QStringLiteral ("stochastic/root"), std::max (0, modeRoot)).toInt(), 0, 11));
    low->setValue (lowest <= highest ? std::max (0, lowest - 5) : 48);
    high->setValue (lowest <= highest ? std::min (127, highest + 5) : 84);
    intervalLaw->setCurrentIndex (std::clamp (settings.value (QStringLiteral ("stochastic/intervalLaw"), (int) stochastic::Law::linear).toInt(), 0, stochastic::numLaws - 1));
    intervalSpread->setValue (settings.value (QStringLiteral ("stochastic/intervalSpread"), 12.0).toDouble());
    lengthLaw->setCurrentIndex (std::clamp (settings.value (QStringLiteral ("stochastic/lengthLaw"), (int) stochastic::Law::exponential).toInt(), 0, stochastic::numLaws - 1));
    lengthMean->setValue (settings.value (QStringLiteral ("stochastic/length"), 0.5).toDouble());
    loudnessLaw->setCurrentIndex (std::clamp (settings.value (QStringLiteral ("stochastic/loudnessLaw"), (int) stochastic::Law::gaussian).toInt(), 0, stochastic::numLaws - 1));
    velocity->setValue (settings.value (QStringLiteral ("stochastic/velocity"), 80).toInt());
    velocitySpread->setValue (settings.value (QStringLiteral ("stochastic/velocitySpread"), 18).toInt());
    timeBox->setCurrentIndex (std::clamp (settings.value (QStringLiteral ("stochastic/time"), 0).toInt(), 0, 1));
    rhythmSieve->setText (settings.value (QStringLiteral ("stochastic/rhythm")).toString());
    perBeat->setValue (settings.value (QStringLiteral ("stochastic/perBeat"), 4).toInt());
    variation->setValue (settings.value (QStringLiteral ("stochastic/variation"), 0).toInt() + 1);
    if (whereBox->currentIndex() == 0)
        whereBox->setCurrentIndex (std::clamp (settings.value (QStringLiteral ("stochastic/where"), 0).toInt(), 0, 1));

    // Filling a sieve from somewhere: it is then the user's to edit.
    connect (pitchFrom, &QComboBox::activated, &dialog, [&, pitchSieve, pitchFrom, rootChoice] (int index)
    {
        const auto chosen = pitchFrom->itemData (index).toString();
        pitchFrom->setCurrentIndex (0);
        if (chosen == QLatin1String ("mode") && modeSystem >= 0)
        {
            rootChoice->setCurrentIndex (std::clamp (modeRoot, 0, 11));
            pitchSieve->setText (QString::fromStdString (stochastic::Sieve::fromKeys (mixer::modeKeys (mixer::tuningSystems()[(size_t) modeSystem])).text()));
        }
        else if (chosen == QLatin1String ("clip"))
            pitchSieve->setText (QString::fromStdString (stochastic::pitchSieveOf (*song, rootChoice->currentIndex()).text()));
        else if (chosen == QLatin1String ("all"))
            pitchSieve->clear();
        else if (! chosen.isEmpty())
            pitchSieve->setText (chosen);
    });
    connect (rhythmFrom, &QComboBox::activated, &dialog, [&, rhythmSieve, rhythmFrom, perBeat, timeBox, bars] (int index)
    {
        const auto chosen = rhythmFrom->itemData (index);
        rhythmFrom->setCurrentIndex (0);
        if (! chosen.isValid())
            return;
        if (chosen.typeId() == QMetaType::QString)
            rhythmSieve->setText (chosen.toString());
        else if (chosen.toInt() == -2)
        {
            const auto step = std::max (1, tpq / perBeat->value());
            const auto period = (int) std::max<int64_t> (1, (int64_t) bars->value() * bar / step);
            rhythmSieve->setText (QString::fromStdString (stochastic::rhythmSieveOf (*song, step, period).text()));
        }
        else if (chosen.toInt() >= 0)
        {
            const auto& r = rhythm::rhythms()[(size_t) chosen.toInt()];
            perBeat->setValue (rhythm::rowsPerBeat (r));
            rhythmSieve->setText (QString::fromStdString (stochastic::sieveFromRhythm (r).text()));
        }
        timeBox->setCurrentIndex (1);
    });

    auto* stayLabel = new QLabel (tr ("screens stay"), &dialog);
    auto* sectionLabel = new QLabel (&dialog);
    auto* rangeNames = new QLabel (&dialog);
    rangeNames->setForegroundRole (QPalette::PlaceholderText);
    auto* problem = new QLabel (&dialog);
    problem->setWordWrap (true);
    auto* buttons = new QDialogButtonBox (QDialogButtonBox::Ok | QDialogButtonBox::Cancel, &dialog);
    buttons->button (QDialogButtonBox::Ok)->setText (tr ("write"));
    auto check = [&, rangeNames, problem, buttons, stayLabel, sectionLabel]
    {
        const bool screens = methodBox->currentIndex() == 1;
        stay->setEnabled (screens);
        stayLabel->setEnabled (screens);
        sectionLabel->setText (screens ? tr ("each screen") : tr ("each section"));
        rhythmSieve->setEnabled (timeBox->currentIndex() == 1);
        perBeat->setEnabled (timeBox->currentIndex() == 1);
        showNames (rangeNames);
        QString text;
        std::string error;
        if (! pitchSieve->text().trimmed().isEmpty() && stochastic::Sieve::parse (pitchSieve->text().toStdString(), error).empty())
            text = tr ("pitch sieve: %1").arg (QString::fromStdString (error));
        else if (timeBox->currentIndex() == 1 && stochastic::Sieve::parse (rhythmSieve->text().toStdString(), error).empty())
            text = tr ("rhythm sieve: %1").arg (QString::fromStdString (error));
        else if (low->value() > high->value())
            text = tr ("the lowest note is above the highest");
        problem->setText (text);
        problem->setVisible (! text.isEmpty());
        buttons->button (QDialogButtonBox::Ok)->setEnabled (text.isEmpty());
    };
    connect (methodBox, &QComboBox::currentIndexChanged, &dialog, check);
    connect (timeBox, &QComboBox::currentIndexChanged, &dialog, check);
    connect (pitchSieve, &QLineEdit::textChanged, &dialog, check);
    connect (rhythmSieve, &QLineEdit::textChanged, &dialog, check);
    connect (low, &QSpinBox::valueChanged, &dialog, check);
    connect (high, &QSpinBox::valueChanged, &dialog, check);
    check();

    auto row = [] (std::initializer_list<QWidget*> widgets, int stretchAt = 0)
    {
        auto* layout = new QHBoxLayout;
        int i = 0;
        for (auto* w : widgets)
            layout->addWidget (w, i++ == stretchAt ? 1 : 0);
        return layout;
    };
    form->addRow (tr ("method"), methodBox);
    form->addRow (tr ("length"), bars);
    form->addRow (tr ("density"), row ({ density, sectionLabel, section }));
    form->addRow (stayLabel, stay);
    form->addRow (tr ("pitches"), row ({ pitchSieve, pitchFrom }));
    form->addRow (tr ("root"), row ({ rootChoice, new QLabel (tr ("range"), &dialog), low, high, rangeNames }, 4));
    form->addRow (tr ("leaps"), row ({ intervalLaw, intervalSpread }));
    form->addRow (tr ("lengths"), row ({ lengthLaw, lengthMean }));
    form->addRow (tr ("loudness"), row ({ loudnessLaw, velocity, velocitySpread }));
    form->addRow (tr ("rhythm"), row ({ timeBox, perBeat }));
    form->addRow (QString(), row ({ rhythmSieve, rhythmFrom }));
    form->addRow (tr ("where"), whereBox);
    form->addRow (tr ("variation"), variation);
    form->addRow (problem);
    form->addRow (buttons);
    connect (buttons, &QDialogButtonBox::accepted, &dialog, &QDialog::accept);
    connect (buttons, &QDialogButtonBox::rejected, &dialog, &QDialog::reject);
    dialog.resize (std::max (dialog.sizeHint().width(), 620), dialog.sizeHint().height());
    if (dialog.exec() != QDialog::Accepted)
        return;

    settings.setValue (QStringLiteral ("stochastic/method"), methodBox->currentIndex());
    settings.setValue (QStringLiteral ("stochastic/density"), density->value());
    settings.setValue (QStringLiteral ("stochastic/section"), section->value());
    settings.setValue (QStringLiteral ("stochastic/stay"), stay->value());
    settings.setValue (QStringLiteral ("stochastic/pitches"), pitchSieve->text().trimmed());
    settings.setValue (QStringLiteral ("stochastic/root"), rootChoice->currentIndex());
    settings.setValue (QStringLiteral ("stochastic/intervalLaw"), intervalLaw->currentIndex());
    settings.setValue (QStringLiteral ("stochastic/intervalSpread"), intervalSpread->value());
    settings.setValue (QStringLiteral ("stochastic/lengthLaw"), lengthLaw->currentIndex());
    settings.setValue (QStringLiteral ("stochastic/length"), lengthMean->value());
    settings.setValue (QStringLiteral ("stochastic/loudnessLaw"), loudnessLaw->currentIndex());
    settings.setValue (QStringLiteral ("stochastic/velocity"), velocity->value());
    settings.setValue (QStringLiteral ("stochastic/velocitySpread"), velocitySpread->value());
    settings.setValue (QStringLiteral ("stochastic/time"), timeBox->currentIndex());
    settings.setValue (QStringLiteral ("stochastic/rhythm"), rhythmSieve->text().trimmed());
    settings.setValue (QStringLiteral ("stochastic/perBeat"), perBeat->value());
    settings.setValue (QStringLiteral ("stochastic/variation"), variation->value() - 1);
    settings.setValue (QStringLiteral ("stochastic/where"), whereBox->currentIndex());

    stochastic::Settings options;
    std::string error;
    options.method = methodBox->currentIndex() == 1 ? stochastic::Method::screens : stochastic::Method::cloud;
    options.ticksPerQuarter = tpq;
    options.length = (int64_t) bars->value() * bar;
    options.density = density->value();
    options.sectionBeats = section->value();
    options.low = low->value();
    options.high = high->value();
    options.root = rootChoice->currentIndex();
    if (! pitchSieve->text().trimmed().isEmpty())
        options.pitches = stochastic::Sieve::parse (pitchSieve->text().toStdString(), error);
    options.intervalLaw = (stochastic::Law) intervalLaw->currentIndex();
    options.intervalSpread = intervalSpread->value();
    options.lengthLaw = (stochastic::Law) lengthLaw->currentIndex();
    options.lengthBeats = lengthMean->value();
    options.loudnessLaw = (stochastic::Law) loudnessLaw->currentIndex();
    options.velocity = velocity->value();
    options.velocitySpread = velocitySpread->value();
    if (timeBox->currentIndex() == 1)
        options.grid = stochastic::gridFromSieve (stochastic::Sieve::parse (rhythmSieve->text().toStdString(), error),
                                                  std::max (1, tpq / perBeat->value()), options.length);
    options.stay[0] = options.stay[1] = options.stay[2] = stay->value();
    options.seed = (uint32_t) variation->value();

    // Drawn at once: even thousands of notes take a few milliseconds.
    const auto result = stochastic::generate (options);
    if (! result.error.empty() || result.notes.empty())
    {
        say (result.error.empty() ? tr ("stochasticize drew no notes: raise the density") : QString::fromStdString (result.error));
        return;
    }
    int channel = 0;
    for (int c = 0; c < 16; ++c)
        if ((song->channelsUsed & (1u << c)) && c != 9)
        {
            channel = c;
            break;
        }
    auto what = options.method == stochastic::Method::screens ? tr ("%n note(s) in markov screens", "", (int) result.notes.size())
                                                              : tr ("%n note(s) in an st cloud", "", (int) result.notes.size());
    if (whereBox->currentIndex() == 1)
    {
        const auto notes = result.notes;
        editShownMidi ([notes, channel] (const midifile::Song& s) { return stochastic::toSong (s, notes, channel); },
                       tr ("stochasticized: %1").arg (what));
        return;
    }
    if (engine.numTracks() >= mixer::maxTracks)
    {
        QMessageBox::information (this, title, tr ("The new line goes on a new track, and the mixer has 8 tracks already."));
        return;
    }
    const auto written = stochastic::toSong (*song, result.notes, channel);
    const auto base = editsFolder();
    QDir().mkpath (base);
    auto stem = QFileInfo (QString::fromStdString (clip.midi->path)).completeBaseName();
    if (stem.isEmpty())
        stem = tr ("MIDI");
    stem.replace (QRegularExpression (QStringLiteral ("[/\\\\:*?\"<>|]")), QStringLiteral ("_"));
    QString file;
    for (int n = 1; file.isEmpty() || QFileInfo::exists (file); ++n)
        file = QDir (base).filePath (n == 1 ? QStringLiteral ("%1 stochastic.mid").arg (stem)
                                            : QStringLiteral ("%1 stochastic %2.mid").arg (stem).arg (n));
    std::string why;
    if (! midifile::write (*written, file.toStdString(), why))
    {
        say (tr ("The new line could not be saved to %1: %2").arg (file, QString::fromStdString (why)));
        return;
    }
    remember();
    auto at = clip;   // the whole new line, however long the clip is
    at.offset = 0;
    at.length = std::numeric_limits<decltype (at.length)>::max();
    placeGenerated (track, at, slot, scene, written, file, tr ("%1 stochastic").arg (info[(size_t) track].name), tuning, -1, -1);
    generatedPlaced();
    if (! result.screens.empty())
        what += tr (" (%1)").arg (QString::fromStdString (result.screens));
    say (what);
}

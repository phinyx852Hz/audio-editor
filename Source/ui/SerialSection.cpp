// SPDX-License-Identifier: AGPL-3.0-only
// serialize (12 tone row) (owner, 2026-10-27, the name theirs: Schoenberg's serial method among
// PHI's composing tools; mixer/Serial.h, docs/serial.md): the Mode menu's dialog for a row, from the user, a famous
// work, chance or the MIDI clip shown; its 12 x 12 matrix, whose p, i, r and ri labels build
// the plan; and a piece of one to four voices on it, which loops, put in the mixer's launcher
// as a scene (for cubify and minimalize after), on the timeline, or saved as a MIDI file.
// Counterpoint or a fugue can follow on its first voice. Every name in it is lower case (owner).
#include "MainWindow.h"
#include "MixerWindow.h"
#include "Settings.h"
#include "../mixer/Serial.h"
#include "../mixer/Tunings.h"

#include <QCheckBox>
#include <QComboBox>
#include <QDateTime>
#include <QDialog>
#include <QDialogButtonBox>
#include <QDir>
#include <QDoubleSpinBox>
#include <QFileDialog>
#include <QFileInfo>
#include <QFormLayout>
#include <QGridLayout>
#include <QHBoxLayout>
#include <QLabel>
#include <QLineEdit>
#include <QMessageBox>
#include <QPushButton>
#include <QRegularExpression>
#include <QSpinBox>
#include <QStatusBar>
#include <QToolButton>
#include <QVBoxLayout>

#include <algorithm>
#include <limits>

namespace
{
    QString text (const std::string& s) { return QString::fromStdString (s); }

    // The voices on channels 1.. of one song, to save as a single MIDI file.
    midifile::SongPtr together (const std::vector<midifile::SongPtr>& voices)
    {
        auto out = std::make_shared<midifile::Song> (*voices.front());
        out->events.clear();
        out->channelsUsed = 0;
        for (const auto& v : voices)
        {
            out->events.insert (out->events.end(), v->events.begin(), v->events.end());
            out->channelsUsed |= v->channelsUsed;
            out->lastTick = std::max (out->lastTick, v->lastTick);
        }
        std::stable_sort (out->events.begin(), out->events.end(), [] (const midifile::Event& a, const midifile::Event& b)
        {
            if (a.tick != b.tick)
                return a.tick < b.tick;
            return (a.kind() == 0x80) > (b.kind() == 0x80);   // offs first
        });
        return out;
    }
}

double MixerWindow::mixTempo() const { return tempoBox->value(); }

std::vector<int> MixerWindow::shownMidiPitches() const
{
    int track = -1, scene = -1;
    mixer::Clip clip;
    mixer::SlotPtr slot;
    mixer::TuningChoice tuning;
    std::vector<int> pitches;
    if (! shownMidiClip (track, scene, clip, slot, tuning) || clip.midi == nullptr || clip.midi->song == nullptr)
        return pitches;
    auto song = clip.midi->song;
    if (clip.reversed)
        song = midifile::reversed (*song);
    for (const auto& e : song->events)
        if (e.kind() == 0x90 && e.data2 > 0 && e.channel() != 9)
            pitches.push_back (e.data1);
    return pitches;
}

bool MixerWindow::addSerial (const std::vector<midifile::SongPtr>& voices, const QStringList& names, const QString& stem,
                             bool launcher, int then, QString& error)
{
    if (voices.empty())
        return false;
    if (selected >= mixer::masterIndex || selected >= engine.numTracks())
    {
        error = tr ("Pick a track in the mixer first");
        return false;
    }
    // The voices on the selected track and the ones after it (the Selector's tracks, with their
    // sounds), then on new tracks.
    const auto first = selected;
    const auto existing = std::min ((int) voices.size(), engine.numTracks() - first);
    const auto needed = (int) voices.size() - existing;
    if (engine.numTracks() + needed > mixer::maxTracks)
    {
        error = tr ("The voices after track %1 go on new tracks, and the mixer has room for %n more.", "", mixer::maxTracks - engine.numTracks())
                    .arg (engine.numTracks());
        return false;
    }
    // In the launcher: the cursor's scene if it is empty on those tracks, else the first that is, else a new one.
    int scene = -1;
    if (launcher)
    {
        auto empty = [&] (int sc)
        {
            for (int t = first; t < first + existing; ++t)
                if (slots[(size_t) t][(size_t) sc] != nullptr)
                    return false;
            return true;
        };
        if (empty (cursorScene))
            scene = cursorScene;
        for (int sc = 0; scene < 0 && sc < numScenes; ++sc)
            if (empty (sc))
                scene = sc;
        if (scene < 0)
        {
            if (numScenes >= mixer::maxScenes)
            {
                error = tr ("Every scene has a clip on these tracks: all %1. Empty a slot (Delete) first.").arg (mixer::maxScenes);
                return false;
            }
            scene = numScenes;
        }
    }

    // Twelve-tone: every key of the piano, as 12-TET plays it.
    const mixer::TuningChoice twelve { mixer::findTuning ("12 equal steps"), 0 };
    const auto base = editsFolder();
    QDir().mkpath (base);
    auto clean = stem;
    clean.replace (QRegularExpression (QStringLiteral ("[/\\\\:*?\"<>|]")), QStringLiteral ("_"));
    remember();
    const auto at = engine.position();
    uint32_t firstClip = 0;
    for (size_t v = 0; v < voices.size(); ++v)
    {
        const auto name = names.value ((int) v);
        QString file;
        for (int n = 1; file.isEmpty() || QFileInfo::exists (file); ++n)
            file = QDir (base).filePath (n == 1 ? QStringLiteral ("%1 %2.mid").arg (clean, name) : QStringLiteral ("%1 %2 %3.mid").arg (clean, name).arg (n));
        std::string why;
        if (! midifile::write (*voices[v], file.toStdString(), why))
        {
            error = tr ("The %1 could not be saved to %2: %3").arg (name, file, QString::fromStdString (why));
            break;
        }
        auto track = first + (int) v;
        if ((int) v >= existing)
        {
            track = engine.numTracks();
            engine.setNumTracks (track + 1);
            auto& to = engine.strip (track);
            to.trimDb = 0.0f;
            to.invertLeft = to.invertRight = false;
            to.lowCut = to.highCut = false;
            to.width = 1.0f;
            to.delayMs = 0.0f;
            to.faderDb = engine.strip (first).faderDb.load();
            to.pan = 0.0f;
            to.mute = to.solo = false;
            to.transpose = 0;
            to.channels = 0xFFFF;
            engine.setInserts (track, {});
            engine.setInstrument (track, nullptr);
            engine.setClips (track, {});
            info[(size_t) track].name = tr ("%1 %2").arg (stem, name);
            info[(size_t) track].colour = info[(size_t) first].colour;
            info[(size_t) track].tuning = twelve;
            if (launcher)
            {
                slots[(size_t) track][(size_t) scene] = nullptr;
                engine.setSlot (track, scene, nullptr);
            }
        }
        if (launcher)
        {
            mixer::Slot like;
            like.tuning = twelve;
            loadSlot (track, scene, file, like);
        }
        else
        {
            placeFiles (track, { file }, at, false);
            if (v == 0)
                for (const auto& c : engine.clips (track))
                    if (c.midi != nullptr && c.midi->path == file.toStdString())
                        firstClip = c.id;
        }
        giveSynth (track);
    }
    if (launcher && ! launcherShown())
        showLauncher (true);
    generatedPlaced();
    if (launcher)
    {
        moveCursor (first, scene);
        showSlotClip (first, scene);
    }
    else if (firstClip != 0)
        showTimelineClip (first, firstClip);
    if (! error.isEmpty())
        return true;   // said, and what was written stays
    if (then == 1)
        counterpointShown();
    else if (then == 2)
        fugueShown();
    return true;
}

void MainWindow::serialDialog()
{
    auto settings = appSettings();
    QDialog dialog (this);
    dialog.setWindowTitle (tr ("serialize (12 tone row)"));
    auto* layout = new QVBoxLayout (&dialog);
    auto* form = new QFormLayout;
    layout->addLayout (form);

    // The row.
    auto* rowEdit = new QLineEdit (&dialog);
    rowEdit->setPlaceholderText (tr ("twelve notes, each once: e f g c♯ f♯ e♭ a♭ d b c a b♭, or 4 5 7 1 6 3 8 2 11 0 9 10"));
    rowEdit->setToolTip (tr ("The row: the twelve pitch classes in the order of the piece, as note names or as numbers "
                             "(0 is c; t and e may stand for 10 and 11)"));
    auto* fill = new QComboBox (&dialog);
    fill->setMaxVisibleItems (24);
    fill->addItem (tr ("fill from..."));
    const auto clipPitches = mixerWin != nullptr ? mixerWin->shownMidiPitches() : std::vector<int>();
    if (! clipPitches.empty())
        fill->addItem (tr ("the clip's notes"), QStringLiteral ("clip"));
    fill->addItem (tr ("a row by chance"), QStringLiteral ("random"));
    fill->addItem (tr ("an all-interval row by chance"), QStringLiteral ("all-interval"));
    fill->insertSeparator (fill->count());
    for (size_t i = 0; i < serial::historical().size(); ++i)
        fill->addItem (QString::fromUtf8 (serial::historical()[i].name), (int) i);
    auto* names = new QCheckBox (tr ("note names"), &dialog);
    names->setToolTip (tr ("The matrix in note names, else in numbers (0 is c)"));
    auto* about = new QLabel (&dialog);
    about->setWordWrap (true);
    about->setForegroundRole (QPalette::PlaceholderText);

    // The matrix: its cells, and the labels round it that add a form to the plan.
    auto* matrixGrid = new QGridLayout;
    matrixGrid->setSpacing (1);
    QLabel* cells[12][12];
    QToolButton* heads[4][12];   // p on the left, r on the right, i on top, ri below
    auto* plan = new QLineEdit (&dialog);
    for (int k = 0; k < 12; ++k)
        for (int side = 0; side < 4; ++side)
        {
            auto* b = new QToolButton (&dialog);
            b->setAutoRaise (true);
            b->setToolButtonStyle (Qt::ToolButtonTextOnly);
            heads[side][k] = b;
            connect (b, &QToolButton::clicked, &dialog, [plan, b]
            {
                const auto t = plan->text().trimmed();
                plan->setText (t.isEmpty() ? b->text() : t + QStringLiteral (" ") + b->text());
            });
        }
    for (int k = 0; k < 12; ++k)
    {
        matrixGrid->addWidget (heads[2][k], 0, k + 1, Qt::AlignCenter);
        matrixGrid->addWidget (heads[0][k], k + 1, 0, Qt::AlignRight);
        matrixGrid->addWidget (heads[1][k], k + 1, 13, Qt::AlignLeft);
        matrixGrid->addWidget (heads[3][k], 13, k + 1, Qt::AlignCenter);
        for (int j = 0; j < 12; ++j)
        {
            auto* c = new QLabel (&dialog);
            c->setAlignment (Qt::AlignCenter);
            c->setMinimumWidth (28);
            cells[k][j] = c;
            matrixGrid->addWidget (c, k + 1, j + 1);
        }
    }

    // The plan.
    plan->setPlaceholderText (tr ("empty: a walk through the matrix from p0 (or click its labels: p0 ri5 i3...)"));
    plan->setToolTip (tr ("The first voice's row forms, in order, repeated as long as the piece is: p (prime), i (inversion), "
                          "r (retrograde), ri (retrograde inversion), each at a level 0 to 11, semitones up from the row"));
    auto* walkButton = new QPushButton (tr ("walk"), &dialog);
    walkButton->setToolTip (tr ("A path through the matrix: each form joined to the last by a pivot note, or by hexachords that make "
                                "all twelve notes across the join, where it can be"));
    auto* clearButton = new QPushButton (tr ("clear"), &dialog);

    // The piece.
    auto* voices = new QSpinBox (&dialog);
    voices->setRange (1, 4);
    voices->setToolTip (tr ("The voices, each a clip on a track of its own: the selected track and the ones after it, then new tracks"));
    auto* relation = new QComboBox (&dialog);
    relation->addItem (tr ("independent: each its own walk"));
    relation->addItem (tr ("canon: the same forms, entering one after another"));
    relation->addItem (tr ("mirror canon: every other voice inverted (webern)"));
    relation->addItem (tr ("combinatorial pairs: hexachords against their complements (schoenberg)"));
    relation->setToolTip (tr ("How the voices go together. combinatorial: the second voice takes the form whose first hexachord holds "
                              "the six notes the first voice's leaves out, so the two make all twelve notes at once"));
    auto* delay = new QSpinBox (&dialog);
    delay->setRange (1, 64);
    delay->setSuffix (tr (" sixteenths"));
    delay->setToolTip (tr ("How far apart a canon's voices enter"));
    auto* chord = new QComboBox (&dialog);
    chord->addItem (tr ("a line, a note at a time"), 1);
    chord->addItem (tr ("dyads, two at a time"), 2);
    chord->addItem (tr ("trichords, three at a time"), 3);
    chord->addItem (tr ("tetrachords, four at a time"), 4);
    chord->addItem (tr ("hexachords, six at a time"), 6);
    chord->setToolTip (tr ("The row cut into chords, its notes in order"));
    auto* rhythm = new QComboBox (&dialog);
    rhythm->addItem (tr ("even"));
    rhythm->addItem (tr ("serial: each note as long as its place in p0"));
    rhythm->addItem (tr ("free, with rests"));
    rhythm->setToolTip (tr ("serial: the first note of the row one unit long, the second two... the last twelve, in every form "
                            "(total serialism, after messiaen and boulez)"));
    auto* unit = new QComboBox (&dialog);
    unit->addItem (tr ("sixteenths"), 1);
    unit->addItem (tr ("eighths"), 2);
    unit->addItem (tr ("quarters"), 4);
    unit->addItem (tr ("halves"), 8);
    unit->setToolTip (tr ("Each note when the rhythm is even; the unit of the others"));
    auto* rests = new QSpinBox (&dialog);
    rests->setRange (0, 80);
    rests->setSuffix (tr ("% rests"));
    auto* registerBox = new QComboBox (&dialog);
    registerBox->addItem (tr ("close: each note near the last"));
    registerBox->addItem (tr ("wide: any octave (pointillist)"));
    registerBox->addItem (tr ("fixed: each pitch class in one octave (a frozen register)"));
    auto* low = new QSpinBox (&dialog);
    auto* high = new QSpinBox (&dialog);
    for (auto* box : { low, high })
    {
        box->setRange (0, 127);
        box->setToolTip (tr ("MIDI key: 60 is middle c (c4)"));
    }
    auto* dynamics = new QComboBox (&dialog);
    dynamics->addItem (tr ("even"));
    dynamics->addItem (tr ("terraced: each form its own"));
    dynamics->addItem (tr ("serial: each note by its place in i0"));
    auto* velocity = new QSpinBox (&dialog);
    velocity->setRange (1, 127);
    auto* bars = new QSpinBox (&dialog);
    bars->setRange (1, 256);
    bars->setSuffix (tr (" bars"));
    bars->setToolTip (tr ("As long as this in whole forms of the row, rounded up to a bar, so it loops"));
    auto* bpm = new QDoubleSpinBox (&dialog);
    bpm->setRange (20.0, 400.0);
    bpm->setDecimals (1);
    bpm->setSuffix (tr (" bpm"));
    auto* where = new QComboBox (&dialog);
    where->addItem (tr ("a scene in the launcher: the voices loop together"));
    where->addItem (tr ("the timeline, at the playhead"));
    auto* then = new QComboBox (&dialog);
    then->addItem (tr ("nothing more"));
    then->addItem (tr ("counterpoint on the first voice..."));
    then->addItem (tr ("a fugue on the first voice..."));
    then->setToolTip (tr ("Then, on the first voice: counterpoint or a fugue, in twelve-tone (every key); the fugue answers a "
                          "subject of all twelve notes at the fifth exactly"));
    auto* variation = new QSpinBox (&dialog);
    variation->setRange (1, 9999);
    variation->setToolTip (tr ("Another number walks another path and draws other registers and rhythms"));

    // The last choices.
    rowEdit->setText (settings.value (QStringLiteral ("serial/row"), QStringLiteral ("4 5 7 1 6 3 8 2 11 0 9 10")).toString());
    names->setChecked (settings.value (QStringLiteral ("serial/names"), true).toBool());
    plan->setText (settings.value (QStringLiteral ("serial/plan")).toString());
    voices->setValue (settings.value (QStringLiteral ("serial/voices"), 1).toInt());
    relation->setCurrentIndex (std::clamp (settings.value (QStringLiteral ("serial/relation"), 0).toInt(), 0, 3));
    delay->setValue (settings.value (QStringLiteral ("serial/delay"), 8).toInt());
    chord->setCurrentIndex (std::max (0, chord->findData (settings.value (QStringLiteral ("serial/chord"), 1).toInt())));
    rhythm->setCurrentIndex (std::clamp (settings.value (QStringLiteral ("serial/rhythm"), 0).toInt(), 0, 2));
    unit->setCurrentIndex (std::max (0, unit->findData (settings.value (QStringLiteral ("serial/unit"), 2).toInt())));
    rests->setValue (settings.value (QStringLiteral ("serial/rests"), 20).toInt());
    registerBox->setCurrentIndex (std::clamp (settings.value (QStringLiteral ("serial/register"), 0).toInt(), 0, 2));
    low->setValue (settings.value (QStringLiteral ("serial/low"), 48).toInt());
    high->setValue (settings.value (QStringLiteral ("serial/high"), 84).toInt());
    dynamics->setCurrentIndex (std::clamp (settings.value (QStringLiteral ("serial/dynamics"), 0).toInt(), 0, 2));
    velocity->setValue (settings.value (QStringLiteral ("serial/velocity"), 80).toInt());
    bars->setValue (settings.value (QStringLiteral ("serial/bars"), 4).toInt());
    bpm->setValue (mixerWin != nullptr ? mixerWin->mixTempo() : settings.value (QStringLiteral ("serial/bpm"), 100.0).toDouble());
    where->setCurrentIndex (std::clamp (settings.value (QStringLiteral ("serial/where"), 0).toInt(), 0, 1));
    variation->setValue (settings.value (QStringLiteral ("serial/variation"), 1).toInt());

    auto* problem = new QLabel (&dialog);
    problem->setWordWrap (true);
    auto* buttons = new QDialogButtonBox (&dialog);
    auto* toMixer = buttons->addButton (tr ("write into the &mixer"), QDialogButtonBox::AcceptRole);
    auto* toFile = buttons->addButton (tr ("&save as midi..."), QDialogButtonBox::AcceptRole);
    toFile->setToolTip (tr ("every voice in one file, each on a channel of its own"));
    buttons->addButton (QDialogButtonBox::Cancel);

    serial::Row row {};
    auto check = [&]
    {
        std::string error;
        const bool good = serial::parse (rowEdit->text().toStdString(), row, error);
        const auto shown = names->isChecked();
        for (int k = 0; k < 12; ++k)
        {
            heads[0][k]->setText (good ? QStringLiteral ("p%1").arg (serial::primeLevelOfRow (row, k)) : QString());
            heads[1][k]->setText (good ? QStringLiteral ("r%1").arg (serial::primeLevelOfRow (row, k)) : QString());
            heads[2][k]->setText (good ? QStringLiteral ("i%1").arg (serial::inversionLevelOfColumn (row, k)) : QString());
            heads[3][k]->setText (good ? QStringLiteral ("ri%1").arg (serial::inversionLevelOfColumn (row, k)) : QString());
            for (int side = 0; side < 4; ++side)
                heads[side][k]->setEnabled (good);
        }
        const auto m = serial::matrix (row);
        for (int i = 0; i < 12; ++i)
            for (int j = 0; j < 12; ++j)
            {
                const auto pc = m[(size_t) i][(size_t) j];
                cells[i][j]->setText (! good ? QString() : shown ? QString::fromUtf8 (serial::pitchName (pc)) : QString::number (pc));
            }
        about->setText (good ? text (serial::describe (row)) : QString());
        QString issue;
        std::vector<serial::Statement> forms;
        if (! good)
            issue = tr ("the row: %1").arg (text (error));
        else if (! serial::parseStatements (plan->text().toStdString(), forms, error))
            issue = tr ("the plan: %1").arg (text (error));
        else if (high->value() - low->value() < 11)
            issue = tr ("the range needs an octave at least, to hold every note");
        problem->setText (issue);
        problem->setVisible (! issue.isEmpty());
        toMixer->setEnabled (issue.isEmpty());
        toFile->setEnabled (issue.isEmpty());
        walkButton->setEnabled (good);
        delay->setEnabled (relation->currentIndex() == 1 || relation->currentIndex() == 2);
        rests->setEnabled (rhythm->currentIndex() == 2);
    };
    connect (rowEdit, &QLineEdit::textChanged, &dialog, check);
    connect (plan, &QLineEdit::textChanged, &dialog, check);
    connect (names, &QCheckBox::toggled, &dialog, check);
    connect (relation, &QComboBox::currentIndexChanged, &dialog, check);
    connect (rhythm, &QComboBox::currentIndexChanged, &dialog, check);
    connect (low, &QSpinBox::valueChanged, &dialog, check);
    connect (high, &QSpinBox::valueChanged, &dialog, check);
    connect (fill, &QComboBox::activated, &dialog, [&] (int index)
    {
        const auto chosen = fill->itemData (index);
        fill->setCurrentIndex (0);
        const auto seed = (uint32_t) variation->value() * 7u + (uint32_t) QDateTime::currentMSecsSinceEpoch();
        serial::Row next {};
        if (chosen.typeId() == QMetaType::Int)
            next = serial::historical()[(size_t) chosen.toInt()].row;
        else if (chosen.toString() == QLatin1String ("clip"))
        {
            int missing = 0;
            next = serial::rowOf (clipPitches, missing);
            if (missing > 0)
                statusBar()->showMessage (tr ("The clip plays %n pitch class(es) less than twelve: the rest are added at the end", "", missing), 6000);
        }
        else if (chosen.toString() == QLatin1String ("random"))
            next = serial::randomRow (seed);
        else if (chosen.toString() == QLatin1String ("all-interval"))
            next = serial::randomAllInterval (seed);
        else
            return;
        rowEdit->setText (text (serial::text (next, names->isChecked())));
    });
    connect (walkButton, &QPushButton::clicked, &dialog, [&]
    {
        plan->setText (text (serial::text (serial::walk (row, 8, (uint32_t) variation->value()))));
    });
    connect (clearButton, &QPushButton::clicked, plan, &QLineEdit::clear);
    int choice = -1;
    for (auto [b, n] : { std::pair { toMixer, 0 }, { toFile, 1 } })
        connect (b, &QPushButton::clicked, &dialog, [&choice, &dialog, n] { choice = n; dialog.accept(); });
    connect (buttons, &QDialogButtonBox::rejected, &dialog, &QDialog::reject);
    check();

    auto line = [] (std::initializer_list<QWidget*> widgets, int stretchAt = 0)
    {
        auto* l = new QHBoxLayout;
        int i = 0;
        for (auto* w : widgets)
            l->addWidget (w, i++ == stretchAt ? 1 : 0);
        return l;
    };
    form->addRow (tr ("row"), line ({ rowEdit, fill, names }));
    form->addRow (QString(), about);
    auto* matrixRow = new QHBoxLayout;
    matrixRow->addStretch();
    matrixRow->addLayout (matrixGrid);
    matrixRow->addStretch();
    form->addRow (tr ("matrix"), matrixRow);
    form->addRow (tr ("plan"), line ({ plan, walkButton, clearButton }));
    form->addRow (tr ("voices"), line ({ voices, relation, delay }, 1));
    form->addRow (tr ("texture"), chord);
    form->addRow (tr ("rhythm"), line ({ rhythm, unit, rests }));
    auto* rangeLabel = new QLabel (tr ("range"), &dialog);
    form->addRow (tr ("register"), line ({ registerBox, rangeLabel, low, high }));
    form->addRow (tr ("dynamics"), line ({ dynamics, velocity }));
    form->addRow (tr ("length"), line ({ bars, bpm }, 2));
    form->addRow (tr ("where"), where);
    form->addRow (tr ("then"), then);
    form->addRow (tr ("variation"), variation);
    layout->addWidget (problem);
    layout->addWidget (buttons);
    if (dialog.exec() != QDialog::Accepted || choice < 0)
        return;

    settings.setValue (QStringLiteral ("serial/row"), rowEdit->text().trimmed());
    settings.setValue (QStringLiteral ("serial/names"), names->isChecked());
    settings.setValue (QStringLiteral ("serial/plan"), plan->text().trimmed());
    settings.setValue (QStringLiteral ("serial/voices"), voices->value());
    settings.setValue (QStringLiteral ("serial/relation"), relation->currentIndex());
    settings.setValue (QStringLiteral ("serial/delay"), delay->value());
    settings.setValue (QStringLiteral ("serial/chord"), chord->currentData().toInt());
    settings.setValue (QStringLiteral ("serial/rhythm"), rhythm->currentIndex());
    settings.setValue (QStringLiteral ("serial/unit"), unit->currentData().toInt());
    settings.setValue (QStringLiteral ("serial/rests"), rests->value());
    settings.setValue (QStringLiteral ("serial/register"), registerBox->currentIndex());
    settings.setValue (QStringLiteral ("serial/low"), low->value());
    settings.setValue (QStringLiteral ("serial/high"), high->value());
    settings.setValue (QStringLiteral ("serial/dynamics"), dynamics->currentIndex());
    settings.setValue (QStringLiteral ("serial/velocity"), velocity->value());
    settings.setValue (QStringLiteral ("serial/bars"), bars->value());
    settings.setValue (QStringLiteral ("serial/bpm"), bpm->value());
    settings.setValue (QStringLiteral ("serial/where"), where->currentIndex());
    settings.setValue (QStringLiteral ("serial/variation"), variation->value());

    serial::Settings options;
    std::string error;
    serial::parse (rowEdit->text().toStdString(), options.row, error);
    serial::parseStatements (plan->text().toStdString(), options.plan, error);
    options.voices = voices->value();
    options.relation = (serial::Voices) relation->currentIndex();
    options.delay = delay->value();
    options.chord = chord->currentData().toInt();
    options.rhythm = (serial::Rhythm) rhythm->currentIndex();
    options.unit = unit->currentData().toInt();
    options.rests = rests->value() / 100.0;
    options.reg = (serial::Register) registerBox->currentIndex();
    options.low = low->value();
    options.high = high->value();
    options.dynamics = (serial::Dynamics) dynamics->currentIndex();
    options.velocity = velocity->value();
    options.bars = bars->value();
    options.seed = (uint32_t) variation->value();
    const auto result = serial::compose (options);
    if (! result.error.empty() || result.voices.empty())
    {
        statusBar()->showMessage (text (result.error), 8000);
        return;
    }
    std::vector<midifile::SongPtr> songs;
    QStringList voiceNames;
    for (size_t v = 0; v < result.voices.size(); ++v)
    {
        songs.push_back (serial::toSong (result.voices[v], result.length, options.ticksPerQuarter, bpm->value(), (int) v));
        voiceNames << text (result.voices[v].name);
    }
    const auto stem = tr ("serialize %1").arg (text (serial::label (result.voices.front().statements.front())));
    const auto what = tr ("%1: %n voice(s), %2 bars; %3", "", (int) songs.size())
                          .arg (stem).arg (result.length / (4 * options.ticksPerQuarter))
                          .arg (text (result.commentary).replace (QLatin1Char ('\n'), QStringLiteral ("; ")));
    if (choice == 1)
    {
        const auto folder = settings.value (QStringLiteral ("serial/folder")).toString();
        const auto file = QFileDialog::getSaveFileName (this, tr ("save the serialized piece as midi"), QDir (folder).filePath (stem + QStringLiteral (".mid")),
                                                        tr ("MIDI files (*.mid *.midi)"));
        if (file.isEmpty())
            return;
        settings.setValue (QStringLiteral ("serial/folder"), QFileInfo (file).absolutePath());
        std::string why;
        if (! midifile::write (*together (songs), file.toStdString(), why))
            statusBar()->showMessage (tr ("%1 could not be saved: %2").arg (file, text (why)), 8000);
        else
            statusBar()->showMessage (tr ("%1 saved (%2)").arg (QFileInfo (file).fileName(), what), 8000);
        return;
    }
    showMixer();
    QString problemText;
    if (! mixerWin->addSerial (songs, voiceNames, stem, where->currentIndex() == 0, then->currentIndex(), problemText))
    {
        statusBar()->showMessage (problemText, 8000);
        return;
    }
    statusBar()->showMessage (problemText.isEmpty() ? what : problemText, 10000);
}

// SPDX-License-Identifier: AGPL-3.0-only
// The Compose menu (owner, 2026-10-29: "Should we have a Compose menu instead? that would include
// all the compositional methods available in Phi ... non-Western compositional approaches as well
// ... in that compose menu would also be a choice to open the microtonal editor (Staff/MIDI) to
// start from a blank canvas ... limited to 8 parts"). It takes the Architectonics menu's place,
// Architectonics a submenu of it, and gathers what was spread over the Mode menu and the mixer:
// counterpoint, the fugue, the stochastic line, cubify, the twelve-tone row, minimalize and the
// rhythm generator; with the methods of mixer/Compose.h, by region and in order of age. New
// Microtonal Score opens the MIDI editor on an empty score on PHI's microtonal staff, New Score In
// on any other notation. docs/compose.md.
#include "MainWindow.h"
#include "MixerWindow.h"
#include "Settings.h"
#include "../mixer/Compose.h"
#include "../mixer/MixerEngine.h"
#include "../mixer/Notation.h"
#include "../mixer/Tunings.h"

#include <QComboBox>
#include <QDialog>
#include <QDialogButtonBox>
#include <QDir>
#include <QFileDialog>
#include <QFileInfo>
#include <QFormLayout>
#include <QLabel>
#include <QListWidget>
#include <QMenu>
#include <QPushButton>
#include <QSpinBox>
#include <QStatusBar>
#include <QVBoxLayout>

#include <algorithm>
#include <random>

namespace
{
    // The regions, as the Mode menu's atlas orders them, and how the menu titles them.
    const std::vector<std::pair<const char*, const char*>> regions {
        { "europe", "&Europe and the Americas" },
        { "south and west asia", "&South and West Asia" },
        { "east and southeast asia", "E&ast and Southeast Asia" },
        { "africa", "A&frica" },
    };


    // The pitches a method composes in: the Mode menu's mode on its final (ModeSection.cpp keeps
    // them), or a tradition's own tuning on the same final; and what the PHI synth plays it in.
    struct Pitches
    {
        counterpoint::Mode mode;    // invalid: the major scale
        mixer::TuningChoice tuning;
        QString text;
    };

    const char* const finals[12] { "c", "c♯", "d", "e♭", "e", "f", "f♯", "g", "a♭", "a", "b♭", "b" };

    int modeFinal() { return std::clamp (appSettings().value (QStringLiteral ("mode/final"), 0).toInt(), 0, 11); }

    Pitches modePitches()
    {
        auto settings = appSettings();
        Pitches p;
        const auto final = modeFinal();
        const auto& all = mixer::tuningSystems();
        const auto system = mixer::findTuning (settings.value (QStringLiteral ("mode/system")).toString().toStdString());
        const auto tuning = mixer::findTuning (settings.value (QStringLiteral ("mode/tuning")).toString().toStdString());
        const auto equal = settings.value (QStringLiteral ("mode/equal"), false).toBool();
        if (system >= 0)
        {
            p.mode = counterpoint::Mode::fromTuning (all[(size_t) system], final);
            p.text = QObject::tr ("the Mode menu's %1 on %2").arg (QString::fromStdString (all[(size_t) system].name), QString::fromUtf8 (finals[final]));
        }
        else
        {
            p.mode = counterpoint::Mode::chromatic (final);
            p.mode.scale.assign (12, false);
            for (int k : { 0, 2, 4, 5, 7, 9, 11 })
                p.mode.scale[(size_t) k] = true;
            p.text = QObject::tr ("major on %1 (no mode chosen in the Mode menu)").arg (QString::fromUtf8 (finals[final]));
        }
        if (tuning >= 0)
            p.tuning = { tuning, all[(size_t) tuning].family == "temperament" ? 0 : final };
        else if (system >= 0 && ! equal)
            p.tuning = { system, final };
        return p;
    }

    Pitches homePitches (const char* home)
    {
        Pitches p;
        const auto i = mixer::findTuning (home);
        if (i < 0)
            return modePitches();
        const auto final = modeFinal();
        const auto& t = mixer::tuningSystems()[(size_t) i];
        p.mode = counterpoint::Mode::fromTuning (t, final);
        p.tuning = { i, final };
        p.text = QObject::tr ("%1 on %2").arg (QString::fromStdString (t.name), QString::fromUtf8 (finals[final]));
        return p;
    }
}

void MainWindow::registerComposeCommands()
{
    const auto category = tr ("Compose");
    auto add = [this, &category] (const QString& id, const QString& text, std::function<void()> fn)
    {
        auto* action = commands.add (id, category, text, {}, std::move (fn));
        addAction (action);
        return action;
    };
    add (QStringLiteral ("compose.newScore"), tr ("&New Microtonal Score..."), [this] { newMicrotonalScore (QStringLiteral ("phi")); })
        ->setStatusTip (tr ("A blank score in the MIDI editor on PHI's microtonal staff, in the Mode menu's tuning; the Staff/MIDI toggle switches to the piano roll"));
    add (QStringLiteral ("compose.dialog"), tr ("&Compose..."), [this] { composeDialog(); })
        ->setStatusTip (tr ("Every method of composing PHI has, in one dialog: a score of up to eight parts for the MIDI editor"));

    // The methods PHI had before Compose, here as well as where they were. Counterpoint and the
    // fugue work on the MIDI clip shown in the mixer, else on a cantus or subject written for them.
    const auto clipOr = [this] (int method, std::function<void()> onClip)
    {
        return [this, method, onClip]
        {
            if (mixerWin != nullptr && mixerWin->showsMidiClip())
                return onClip();
            composeDialog (method);
        };
    };
    add (QStringLiteral ("compose.counterpoint"), tr ("Species Co&unterpoint..."),
         clipOr ((int) composition::Method::counterpoint, [this] { mixerWin->counterpointShown(); }))
        ->setStatusTip (tr ("Fux's species against the MIDI clip shown in the mixer, else against a cantus firmus written for them"));
    add (QStringLiteral ("compose.fugue"), tr ("&Fugue..."), clipOr ((int) composition::Method::fugue, [this] { mixerWin->fugueShown(); }))
        ->setStatusTip (tr ("A fugue on the MIDI clip shown in the mixer as its subject, else on a subject written for it"));
    add (QStringLiteral ("compose.serial"), tr ("&12-Tone Row..."), [this] { serialDialog(); })
        ->setStatusTip (tr ("Schoenberg's and Babbitt's methods: a row, its matrix and a piece in one to four voices"));
    add (QStringLiteral ("compose.stochasticize"), tr ("Stochas&ticize the Clip..."), [this]
    {
        if (mixerWin == nullptr || ! mixerWin->showsMidiClip())
            return statusBar()->showMessage (tr ("Pick a MIDI clip in the mixer first, or choose Stochastic Matrix for a score from nothing"), 6000);
        mixerWin->stochasticizeShown();
    })->setStatusTip (tr ("A new line beside the MIDI clip shown in the mixer, by Xenakis's clouds, sieves and Markov screens"));
    add (QStringLiteral ("compose.cubify"), tr ("Cu&bify..."), [this]
    {
        showMixer();
        if (mixerWin != nullptr)
            mixerWin->cubify();
    })->setStatusTip (tr ("The MIDI clip shown in the mixer, else the launcher's scene, arranged by Xenakis's cubes (Nomos Alpha) and Messiaen's modes"));
    add (QStringLiteral ("compose.minimalize"), tr ("&Minimalize a Scene..."), [this]
    {
        showMixer();
        if (mixerWin != nullptr)
            mixerWin->minimalizeCursorScene();
    })->setStatusTip (tr ("The launcher's scene at the cursor arranged by Reich's and Glass's processes (also on a scene's and the timeline's right-click menus)"));
    add (QStringLiteral ("compose.bolprocessor"), tr ("&Bol Processor..."), [this] { bolProcessorDialog(); })
        ->setStatusTip (tr ("Bernard Bel and Jim Kippen's Bol Processor (BP3) on a grammar file: a qāʿida's variations, or any grammar's items, "
                            "played as bols on the tablā or as its MIDI"));
    add (QStringLiteral ("compose.rhythm"), tr ("&Rhythm Generator..."), [this] { rhythmGenerator(); })
        ->setStatusTip (tr ("Euclidean timelines against each other (polymeter), with chance and rules"));

    // Each method of mixer/Compose.h.
    for (int i = 0; i < (int) composition::methods().size(); ++i)
    {
        const auto& m = composition::methods()[(size_t) i];
        if (m.method == composition::Method::counterpoint || m.method == composition::Method::fugue)
            continue;   // compose.counterpoint and compose.fugue above
        add (QStringLiteral ("compose.") + QString::fromLatin1 (m.id), QString::fromUtf8 (m.name) + QStringLiteral ("..."), [this, i] { composeDialog (i); })
            ->setStatusTip (QString::fromUtf8 (m.tradition) + QStringLiteral (": ") + QString::fromUtf8 (m.about));
    }
}

void MainWindow::fillComposeMenu (QMenu* menu)
{
    menu->setToolTipsVisible (true);
    menu->addAction (commands.action (QStringLiteral ("compose.newScore")));
    auto* in = menu->addMenu (tr ("New Score &In"));
    in->setToolTipsVisible (true);
    for (const auto& n : notation::systems())   // PHI's microtonal staff first, the default
    {
        auto* a = in->addAction (QString::fromStdString (n.title) + QStringLiteral ("..."));
        const auto id = QString::fromStdString (n.id);
        connect (a, &QAction::triggered, this, [this, id] { newMicrotonalScore (id); });
        if (id == QLatin1String ("phi"))
            in->addSeparator();
    }
    menu->addAction (commands.action (QStringLiteral ("compose.dialog")));
    menu->addSeparator();

    auto* arch = menu->addMenu (tr ("Architecto&nics"));
    for (const auto* id : { "architectonics.compose", "architectonics.score", "architectonics.design", "-",
                            "architectonics.play", "architectonics.deal", "architectonics.endless" })
        if (QLatin1String (id) == QLatin1String ("-"))
            arch->addSeparator();
        else
            arch->addAction (commands.action (QString::fromLatin1 (id)));
    menu->addSeparator();

    // By region, oldest first; in Europe the methods PHI had come in among them by their dates.
    for (const auto& [region, title] : regions)
    {
        auto* sub = menu->addMenu (tr (title));
        sub->setToolTipsVisible (true);
        for (const auto& m : composition::methods())
        {
            if (std::string (m.region) != region)
                continue;
            const auto id = QStringLiteral ("compose.") + QString::fromLatin1 (m.id);
            auto* a = commands.action (id);
            if (a != nullptr)
            {
                a->setToolTip (QString::fromUtf8 (m.tradition));
                sub->addAction (a);
            }
            // The older methods where they fall in time.
            if (m.method == composition::Method::fugue)
                sub->addAction (commands.action (QStringLiteral ("compose.serial")));
            if (m.method == composition::Method::stochastic)
            {
                sub->addAction (commands.action (QStringLiteral ("compose.stochasticize")));
                sub->addAction (commands.action (QStringLiteral ("compose.cubify")));
            }
            if (m.method == composition::Method::process)
                sub->addAction (commands.action (QStringLiteral ("compose.minimalize")));
            if (m.method == composition::Method::bolGrammar)
                sub->addAction (commands.action (QStringLiteral ("compose.bolprocessor")));
        }
    }
    menu->addSeparator();
    menu->addAction (commands.action (QStringLiteral ("compose.rhythm")));
    // What needs a clip shown in the mixer, or the mixer at all.
    connect (menu, &QMenu::aboutToShow, this, [this]
    {
        const auto clip = mixerWin != nullptr && mixerWin->showsMidiClip();
        commands.action (QStringLiteral ("compose.stochasticize"))->setEnabled (clip);
    });
}

void MainWindow::newMicrotonalScore (const QString& notation)
{
    showMixer();
    if (mixerWin == nullptr)
        return;
    const auto p = modePitches();
    // Four empty bars of 4/4 at the mixer's tempo.
    midifile::Song blank;
    blank.format = 0;
    blank.ticksPerQuarter = composition::ticksPerQuarter;
    blank.tempos.push_back ({ 0, 60.0e6 / std::max (1.0, mixerEngine->tempo.load()) });
    blank.lastTick = 16 * composition::ticksPerQuarter;
    const auto title = tr ("New score");
    blank.trackNames.push_back (title.toStdString());
    mixerWin->openScore (blank, p.tuning, title, notation);
}

void MainWindow::composeDialog (int methodIndex)
{
    showMixer();
    if (mixerWin == nullptr)
        return;
    auto settings = appSettings();
    const auto& all = composition::methods();
    if (methodIndex < 0 || methodIndex >= (int) all.size())
        methodIndex = std::max (0, composition::find (settings.value (QStringLiteral ("compose/method")).toString().toStdString()));

    QDialog dialog (this);
    dialog.setWindowTitle (tr ("Compose"));
    auto* layout = new QVBoxLayout (&dialog);
    auto* form = new QFormLayout;
    auto* method = new QComboBox (&dialog);
    for (const auto& [region, title] : regions)
    {
        if (method->count() > 0)
            method->insertSeparator (method->count());
        for (int i = 0; i < (int) all.size(); ++i)
            if (std::string (all[(size_t) i].region) == region)
                method->addItem (tr ("%1 — %2").arg (QString::fromUtf8 (all[(size_t) i].name), QString::fromUtf8 (all[(size_t) i].tradition)), i);
    }
    method->setCurrentIndex (std::max (0, method->findData (methodIndex)));
    form->addRow (tr ("&Method"), method);
    auto* about = new QLabel (&dialog);
    about->setWordWrap (true);
    form->addRow (QString(), about);
    auto* parts = new QSpinBox (&dialog);
    form->addRow (tr ("&Parts"), parts);
    auto* partNames = new QLabel (&dialog);
    partNames->setWordWrap (true);
    form->addRow (QString(), partNames);
    auto* bars = new QSpinBox (&dialog);
    bars->setRange (8, 256);
    bars->setSuffix (tr (" bars"));
    bars->setValue (std::clamp (settings.value (QStringLiteral ("compose/bars"), 48).toInt(), 8, 256));
    bars->setToolTip (tr ("About how long; each form rounds it to its own units (a gong cycle, a tāla, a talea)"));
    form->addRow (tr ("&Length"), bars);
    auto* tempo = new QSpinBox (&dialog);
    tempo->setRange (20, 300);
    tempo->setSuffix (tr (" BPM"));
    tempo->setValue ((int) std::lround (mixerEngine->tempo.load()));
    form->addRow (tr ("&Tempo"), tempo);
    auto* pitch = new QComboBox (&dialog);
    form->addRow (tr ("P&itches"), pitch);
    layout->addLayout (form);
    auto* summary = new QLabel (&dialog);
    summary->setWordWrap (true);
    layout->addWidget (summary);
    auto* list = new QListWidget (&dialog);
    list->setMinimumHeight (160);
    layout->addWidget (list);

    std::random_device device;
    uint32_t seed = device();
    composition::Result result;
    composition::Settings cs;
    Pitches chosen;
    auto current = [&] { return all[(size_t) std::max (0, method->currentData().toInt())]; };
    auto recompose = [&]
    {
        const auto& m = current();
        cs.method = m.method;
        cs.parts = parts->value();
        cs.bars = bars->value();
        cs.bpm = tempo->value();
        cs.seed = seed;
        chosen = pitch->currentData().toInt() == 1 ? homePitches (m.home) : modePitches();
        if (m.method == composition::Method::spectral)   // its own cents, sent as bends: played in 12-TET
            chosen.tuning = {};
        cs.mode = chosen.mode;
        result = composition::generate (cs);
        summary->setText (QString::fromStdString (result.summary) + QStringLiteral ("."));
        list->clear();
        const auto ticksPerBar = 4.0 * composition::ticksPerQuarter;
        for (const auto& x : result.sections)
            list->addItem (tr ("bar %1 · %2").arg (1 + (int) (x.start / ticksPerBar)).arg (QString::fromStdString (x.name)));
        list->addItem (tr ("end, bar %1").arg (1 + (int) std::ceil (result.end / ticksPerBar)));
    };
    auto methodChanged = [&]
    {
        const auto& m = current();
        about->setText (QStringLiteral ("%1<br><i>%2</i>").arg (QString::fromUtf8 (m.about).toHtmlEscaped(), tr ("After %1").arg (QString::fromUtf8 (m.sources).toHtmlEscaped())));
        const QSignalBlocker block (parts), blockPitch (pitch);
        parts->setRange (1, std::min (m.mostParts, composition::maxParts));
        parts->setValue (std::clamp (settings.value (QStringLiteral ("compose/parts/") + QString::fromLatin1 (m.id), m.defaultParts).toInt(), 1, m.mostParts));
        QStringList names;
        int k = 0;
        for (const auto& name : QString::fromUtf8 (m.parts).split (QLatin1Char ('|')))
            names << (k++ < parts->value() ? name : QStringLiteral ("<span style='color:gray'>%1</span>").arg (name));
        partNames->setText (names.join (QStringLiteral (", ")));
        pitch->clear();
        pitch->addItem (modePitches().text, 0);
        if (*m.home != 0)
        {
            pitch->addItem (tr ("the tradition's own: %1").arg (homePitches (m.home).text), 1);
            // The tradition's own unless the Mode menu's mode is already of its family.
            const auto system = mixer::findTuning (settings.value (QStringLiteral ("mode/system")).toString().toStdString());
            const auto home = mixer::findTuning (m.home);
            const auto same = system >= 0 && home >= 0 && mixer::tuningSystems()[(size_t) system].family == mixer::tuningSystems()[(size_t) home].family;
            pitch->setCurrentIndex (same ? 0 : 1);
        }
        pitch->setEnabled (m.method != composition::Method::spectral);
        if (m.method == composition::Method::spectral)
            pitch->setItemText (0, tr ("the harmonic series on the Mode menu's final, in exact cents"));
        recompose();
    };
    connect (method, qOverload<int> (&QComboBox::currentIndexChanged), &dialog, methodChanged);
    connect (parts, qOverload<int> (&QSpinBox::valueChanged), &dialog, [&]
    {
        settings.setValue (QStringLiteral ("compose/parts/") + QString::fromLatin1 (current().id), parts->value());
        methodChanged();
    });
    for (auto* box : { bars, tempo })
        connect (box, qOverload<int> (&QSpinBox::valueChanged), &dialog, recompose);
    connect (pitch, qOverload<int> (&QComboBox::currentIndexChanged), &dialog, recompose);

    auto* buttons = new QDialogButtonBox (&dialog);
    auto* again = buttons->addButton (tr ("Deal &Again"), QDialogButtonBox::ActionRole);
    again->setToolTip (tr ("A new seed: the same method and form, other notes"));
    auto* open = buttons->addButton (tr ("&Open the Score"), QDialogButtonBox::AcceptRole);
    open->setToolTip (tr ("In the MIDI editor, a part a channel, the sections marked; kept, it goes on a track"));
    auto* save = buttons->addButton (tr ("&Save as MIDI..."), QDialogButtonBox::AcceptRole);
    buttons->addButton (QDialogButtonBox::Close);
    layout->addWidget (buttons);
    bool saving = false;
    connect (again, &QPushButton::clicked, &dialog, [&] { seed = device(); recompose(); });
    connect (open, &QPushButton::clicked, &dialog, [&] { saving = false; dialog.accept(); });
    connect (save, &QPushButton::clicked, &dialog, [&] { saving = true; dialog.accept(); });
    connect (buttons, &QDialogButtonBox::rejected, &dialog, &QDialog::reject);
    methodChanged();
    dialog.resize (680, 640);
    const auto accepted = dialog.exec() == QDialog::Accepted;
    settings.setValue (QStringLiteral ("compose/method"), QString::fromLatin1 (current().id));
    settings.setValue (QStringLiteral ("compose/bars"), bars->value());
    if (! accepted)
        return;

    const auto title = QString::fromUtf8 (current().name);
    const auto song = composition::toSong (result, title.toStdString());
    if (! saving)
        return mixerWin->openScore (*song, chosen.tuning, title, QStringLiteral ("phi"));
    const auto folder = settings.value (QStringLiteral ("compose/folder"), QDir::homePath()).toString();
    const auto file = QFileDialog::getSaveFileName (this, tr ("Save the score as MIDI"), QDir (folder).filePath (title + QStringLiteral (".mid")),
                                                    tr ("MIDI files (*.mid *.midi)"));
    if (file.isEmpty())
        return;
    settings.setValue (QStringLiteral ("compose/folder"), QFileInfo (file).absolutePath());
    std::string why;
    if (! midifile::write (*song, file.toStdString(), why))
        return statusBar()->showMessage (tr ("%1 could not be saved: %2").arg (file, QString::fromStdString (why)), 8000);
    statusBar()->showMessage (tr ("%1 saved").arg (QFileInfo (file).fileName()), 5000);
}

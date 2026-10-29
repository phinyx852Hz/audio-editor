// SPDX-License-Identifier: AGPL-3.0-only
// The Architectonics menu (owner, 2026-10-29: "for designing a set in the clip launcher based on
// Phi (the golden ratio) that is generative"; its own menu, "I tested it on the 13" screen").
// Design a Set plans a set from the launcher's scenes (mixer/Architectonics.h): its length cut
// by φ again and again into Fibonacci-length sections, in five parts (emergence, expansion,
// disruption, climax, dissolution), the climax at the golden section, and the scenes ordered
// by the energy arch, the Fibonacci numbers or a walk of steps of 1/φ. Play the Set launches
// each section's scene on its first bar, on the exact frame of the launch clock, and stops
// the tracks on the last; endless, it deals a new set there and carries on.
// Compose a Song writes the set itself: a scene a section, its sounds dealt from the Selector a
// part at a time and its MIDI written from the Mode menu's mode, the rhythm library, harmony
// by formal function and the mode's tuning. docs/architectonics.md.
#include "MainWindow.h"
#include "FindPanel.h"
#include "MixerWindow.h"
#include "Settings.h"
#include "../mixer/Architectonics.h"
#include "../mixer/Rhythms.h"
#include "../mixer/Tunings.h"

#include <QCheckBox>
#include <QComboBox>
#include <QDialog>
#include <QDialogButtonBox>
#include <QDir>
#include <QFileDialog>
#include <QFileInfo>
#include <QFormLayout>
#include <QGridLayout>
#include <QLabel>
#include <QListWidget>
#include <QPainter>
#include <QPushButton>
#include <QSpinBox>
#include <QStatusBar>
#include <QTimer>
#include <QVBoxLayout>

#include <algorithm>
#include <cmath>
#include <functional>
#include <random>

namespace arch = architectonics;

namespace
{
    // The song Compose a Song wrote last, its scenes where they went: what Play the Set plays
    // until a set is designed or dealt from the scenes instead.
    arch::Plan composed;

    // What the menu and the dialog keep in settings.ini (architectonics/...), with the launcher's
    // clips filled in from the mixer.
    arch::Settings loadSettings (const MixerWindow* mixer)
    {
        auto settings = appSettings();
        arch::Settings s;
        s.bars = std::clamp (settings.value (QStringLiteral ("architectonics/bars"), 144).toInt(), 2, 10000);
        s.longest = std::clamp (settings.value (QStringLiteral ("architectonics/longest"), 13).toInt(), 1, 1000);
        s.order = (arch::Order) std::clamp (settings.value (QStringLiteral ("architectonics/order"), 0).toInt(), 0, 2);
        s.earlyClimax = settings.value (QStringLiteral ("architectonics/early"), false).toBool();
        s.climaxScene = settings.value (QStringLiteral ("architectonics/climax"), -1).toInt();
        s.variety = std::clamp (settings.value (QStringLiteral ("architectonics/variety"), 38).toInt(), 0, 100) / 100.0;
        if (! settings.contains (QStringLiteral ("architectonics/seed")))   // the first set starts somewhere new
        {
            std::random_device device;
            settings.setValue (QStringLiteral ("architectonics/seed"), std::uniform_real_distribution<double> (0.0, 1.0) (device));
        }
        s.seed = settings.value (QStringLiteral ("architectonics/seed")).toDouble();
        if (mixer != nullptr)
            s.clips = mixer->sceneClips();
        return s;
    }

    void saveSettings (const arch::Settings& s)
    {
        auto settings = appSettings();
        settings.setValue (QStringLiteral ("architectonics/bars"), s.bars);
        settings.setValue (QStringLiteral ("architectonics/longest"), s.longest);
        settings.setValue (QStringLiteral ("architectonics/order"), (int) s.order);
        settings.setValue (QStringLiteral ("architectonics/early"), s.earlyClimax);
        settings.setValue (QStringLiteral ("architectonics/climax"), s.climaxScene);
        settings.setValue (QStringLiteral ("architectonics/variety"), (int) std::lround (s.variety * 100.0));
        settings.setValue (QStringLiteral ("architectonics/seed"), s.seed);
    }

    bool endless() { return appSettings().value (QStringLiteral ("architectonics/endless"), false).toBool(); }

    QString minutes (double seconds)
    {
        const auto s = (int) std::lround (seconds);
        return QStringLiteral ("%1:%2").arg (s / 60).arg (s % 60, 2, 10, QLatin1Char ('0'));
    }

    double secondsPerBar (const mixer::Engine& e) { return e.beatsPerBar.load() * 60.0 / std::max (1.0, e.tempo.load()); }

    // The plan drawn: each section a block as wide as its bars and as tall as its energy, the
    // climax in gold, the five parts named along the bottom and the golden section marked.
    class PlanView : public QWidget
    {
    public:
        explicit PlanView (QWidget* parent) : QWidget (parent) { setMinimumSize (520, 150); }
        arch::Plan plan;

    protected:
        void paintEvent (QPaintEvent*) override
        {
            QPainter g (this);
            g.setRenderHint (QPainter::Antialiasing);
            g.fillRect (rect(), palette().base());
            if (plan.sections.empty())
            {
                g.setPen (palette().color (QPalette::PlaceholderText));
                g.drawText (rect(), Qt::AlignCenter, tr ("Put clips in the launcher's scenes to design a set from them"));
                return;
            }
            const auto bottom = height() - 22.0, top = 8.0, w = width() - 2.0;
            auto x = [&] (double bar) { return 1.0 + w * bar / plan.bars; };
            const auto gold = QColor (212, 175, 55);
            const auto accent = palette().color (QPalette::Highlight);
            for (const auto& s : plan.sections)
            {
                QRectF r (x (s.start), bottom - (bottom - top) * s.energy, x (s.start + s.bars) - x (s.start) - 1.0, (bottom - top) * s.energy);
                auto c = s.function == arch::Function::climax ? gold : accent;
                c.setAlphaF (0.35 + 0.65 * s.energy);
                g.fillRect (r, c);
                if (r.width() > 16.0)
                {
                    g.setPen (palette().color (QPalette::HighlightedText));
                    g.drawText (r.adjusted (0, 2, 0, 0), Qt::AlignHCenter | Qt::AlignTop, QString::number (s.scene + 1));
                }
            }
            // The five parts along the bottom.
            g.setPen (palette().color (QPalette::Text));
            for (size_t i = 0; i < plan.sections.size(); )
            {
                const auto f = plan.sections[i].function;
                auto j = i;
                while (j < plan.sections.size() && plan.sections[j].function == f)
                    ++j;
                const auto& last = plan.sections[j - 1];
                QRectF r (x (plan.sections[i].start), bottom + 2.0, x (last.start + last.bars) - x (plan.sections[i].start), 18.0);
                g.drawText (r, Qt::AlignCenter, g.fontMetrics().elidedText (QString::fromLatin1 (arch::functionName (f)), Qt::ElideRight, (int) r.width()));
                i = j;
            }
            // The golden section.
            QPen pen (gold, 1.5, Qt::DashLine);
            g.setPen (pen);
            g.drawLine (QPointF (x (plan.climaxBar), top - 6.0), QPointF (x (plan.climaxBar), bottom));
        }
    };
}

// Launches the plan's scenes on their bars while the transport plays: each a little ahead, on
// the exact frame of the launch clock (Engine::launchAt), so a tempo change takes effect from
// the next section on.
class ArchitectonicsConductor : public QObject
{
public:
    ArchitectonicsConductor (mixer::Engine& e, QObject* parent) : QObject (parent), engine (e)
    {
        timer.setInterval (25);
        connect (&timer, &QTimer::timeout, this, [this] { tick(); });
    }

    std::function<void (const QString&)> report;
    std::function<void()> finished;
    std::function<arch::Plan()> deal;   // the next set, when endless

    bool running() const { return timer.isActive(); }

    void start (arch::Plan p)
    {
        plan = std::move (p);
        sets = 1;
        shown = -1;
        frames.assign (plan.sections.size(), -1);
        const auto fpb = framesPerBar();
        if (! engine.isPlaying())
        {
            // From the top: the first scene at once; the clock starts again at 0 with the transport.
            launch (plan.sections.front().scene, -1);
            waitForReset = true;
            anchorFrame = 0;
            anchorBar = 0;
            frames[0] = 0;
            next = 1;
        }
        else
        {
            // On the next bar line that is not too close.
            const auto clock = (double) engine.launchClock();
            auto at = std::ceil (clock / fpb) * fpb;
            if (at - clock < lookahead())
                at += fpb;
            waitForReset = false;
            anchorFrame = (Frame) at;
            anchorBar = 0;
            next = 0;
        }
        ending = false;
        timer.start();
    }

    void stop()
    {
        timer.stop();
        plan = {};
    }

    // A new plan in place of this one, from the next bar line.
    void replace (arch::Plan p)
    {
        if (running())
            start (std::move (p));
    }

private:
    mixer::Engine& engine;
    QTimer timer;
    arch::Plan plan;
    std::vector<Frame> frames;   // where each section was launched, -1 not yet
    Frame anchorFrame = 0;       // a frame of the launch clock and the plan's bar on it
    int anchorBar = 0;
    size_t next = 0;                    // the next section to launch
    int shown = -1;                     // the section last reported
    int sets = 1;
    bool waitForReset = false, ending = false;
    Frame endFrame = 0;

    double framesPerBar() const { return std::max (1.0, secondsPerBar (engine) * engine.sampleRate()); }
    double lookahead() const { return 0.3 * engine.sampleRate(); }
    Frame frameOf (int bar) const { return anchorFrame + (Frame) std::llround ((bar - anchorBar) * framesPerBar()); }

    // Every track to the scene's slot (an empty one stops the track), on `at`; -1 at once.
    void launch (int scene, Frame at)
    {
        for (int t = 0; t < engine.numTracks(); ++t)
            if (at < 0)
                engine.launch (t, scene);
            else
                engine.launchAt (t, scene, at);
    }

    void tick()
    {
        if (! engine.isPlaying())
        {
            stop();
            if (report)
                report (tr ("Architectonics: the set stopped with the transport"));
            if (finished)
                finished();
            return;
        }
        const auto clock = engine.launchClock();
        if (waitForReset)
        {
            // Until the audio thread has started the clock again at 0.
            if (clock > framesPerBar() * 0.5)
                return;
            waitForReset = false;
        }
        while (next < plan.sections.size() && frameOf (plan.sections[next].start) - clock < lookahead())
        {
            const auto at = frameOf (plan.sections[next].start);
            launch (plan.sections[next].scene, at);
            frames[next] = at;
            anchorFrame = at;
            anchorBar = plan.sections[next].start;
            ++next;
        }
        if (next == plan.sections.size() && ! ending && frameOf (plan.bars) - clock < lookahead())
        {
            const auto end = frameOf (plan.bars);
            if (deal)
            {
                // Endless: the next set from the last bar of this one.
                auto p = deal();
                if (! p.sections.empty())
                {
                    plan = std::move (p);
                    ++sets;
                    frames.assign (plan.sections.size(), -1);
                    anchorFrame = end;
                    anchorBar = 0;
                    next = 0;
                    shown = -1;
                    return;
                }
            }
            launch (-1, end);
            ending = true;
            endFrame = end;
        }
        // What plays now, for the status bar.
        int now = -1;
        for (size_t i = 0; i < frames.size(); ++i)
            if (frames[i] >= 0 && frames[i] <= clock)
                now = (int) i;
        if (now != shown && now >= 0 && report)
        {
            shown = now;
            const auto& s = plan.sections[(size_t) now];
            report (tr ("Architectonics: set %1, section %2 of %3, %4: scene %5 for %6 bars (bar %7 of %8; the climax at %9)")
                        .arg (sets).arg (now + 1).arg (plan.sections.size()).arg (QString::fromLatin1 (arch::functionName (s.function)))
                        .arg (s.scene + 1).arg (s.bars).arg (s.start + 1).arg (plan.bars).arg (plan.climaxBar + 1));
        }
        if (ending && clock >= endFrame)
        {
            stop();
            if (report)
                report (tr ("Architectonics: the set has ended"));
            if (finished)
                finished();
        }
    }
};

void MainWindow::registerArchitectonicsCommands()
{
    const auto category = tr ("Architectonics");
    auto add = [this, &category] (const char* id, const QString& text, std::function<void()> fn)
    {
        auto* action = commands.add (QString::fromLatin1 (id), category, text, {}, std::move (fn));
        addAction (action);
        return action;
    };
    add ("architectonics.compose", tr ("&Compose a Song..."), [this] { composeSongDialog(); })
        ->setStatusTip (tr ("A song in the clip launcher, a scene a section: sounds dealt from the Selector, MIDI from rhythm, "
                            "melody, harmony and tuning, all by the golden ratio"));
    add ("architectonics.score", tr ("Compose a &Score..."), [this] { composeSongDialog (true); })
        ->setStatusTip (tr ("The same composition written out from the first bar to the last, a part a colour of the Selector, "
                            "opened in the MIDI editor to read as a score, then kept on a track of its own"));
    add ("architectonics.design", tr ("&Design a Set..."), [this] { architectonicsDialog(); })
        ->setStatusTip (tr ("A set from the clip launcher's scenes by the golden ratio: Fibonacci-length sections, the climax at the golden section"));
    auto* play = add ("architectonics.play", tr ("&Play the Set"), [this]
    {
        playArchitectonics (commands.action (QStringLiteral ("architectonics.play"))->isChecked());
    });
    play->setCheckable (true);
    play->setStatusTip (tr ("Launch each section's scene on its first bar, and stop on the last; again to stop following the set (the clips play on)"));
    add ("architectonics.deal", tr ("Deal a &New Set"), [this]
    {
        auto s = loadSettings (mixerWin);
        s.seed = arch::nextSeed (s.seed);
        saveSettings (s);
        const auto p = arch::plan (s);
        if (p.sections.empty())
            return statusBar()->showMessage (tr ("Put clips in the clip launcher's scenes first"), 5000);
        composed = {};   // a set from the scenes now, not the song
        if (conductor != nullptr && conductor->running())
            conductor->replace (p);
        statusBar()->showMessage (tr ("Architectonics: a new set of %1 sections, the climax at bar %2 on scene %3")
                                      .arg (p.sections.size()).arg (p.climaxBar + 1).arg (p.climaxScene + 1), 6000);
    })->setStatusTip (tr ("The same design with a new seed: other cuts, another walk through the scenes"));
    auto* endlessBox = add ("architectonics.endless", tr ("&Endless: a New Set at the End"), [this]
    {
        appSettings().setValue (QStringLiteral ("architectonics/endless"), commands.action (QStringLiteral ("architectonics.endless"))->isChecked());
    });
    endlessBox->setCheckable (true);
    endlessBox->setChecked (endless());
    endlessBox->setStatusTip (tr ("When the set ends, deal a new one from its last bar, and so on until stopped"));
}

void MainWindow::playArchitectonics (bool on)
{
    auto* action = commands.action (QStringLiteral ("architectonics.play"));
    if (! on)
    {
        if (conductor != nullptr)
            conductor->stop();
        action->setChecked (false);
        statusBar()->showMessage (tr ("Architectonics: no longer following the set"), 5000);
        return;
    }
    showMixer();
    mixerWin->showLauncher (true);
    const auto p = ! composed.sections.empty() ? composed : arch::plan (loadSettings (mixerWin));
    if (p.sections.empty())
    {
        action->setChecked (false);
        return statusBar()->showMessage (tr ("Put clips in the clip launcher's scenes first"), 5000);
    }
    if (conductor == nullptr)
    {
        conductor = new ArchitectonicsConductor (*mixerEngine, this);
        conductor->report = [this] (const QString& text) { statusBar()->showMessage (text, 0); };
        conductor->finished = [this] { commands.action (QStringLiteral ("architectonics.play"))->setChecked (false); };
    }
    conductor->deal = [this]() -> arch::Plan
    {
        if (! endless())
            return {};
        auto s = loadSettings (mixerWin);
        s.seed = arch::nextSeed (s.seed);
        saveSettings (s);
        return arch::plan (s);
    };
    action->setChecked (true);
    conductor->start (p);
}

void MainWindow::architectonicsDialog()
{
    showMixer();
    auto s = loadSettings (mixerWin);
    QDialog dialog (this);
    dialog.setWindowTitle (tr ("Design a Set"));
    auto* layout = new QVBoxLayout (&dialog);
    auto* intro = new QLabel (tr ("The set is cut by the golden ratio again and again, so every section is a Fibonacci number of bars, "
                                  "with the climax where the first cut falls. The launcher's scenes with clips fill the sections."), &dialog);
    intro->setWordWrap (true);
    layout->addWidget (intro);

    auto* view = new PlanView (&dialog);
    layout->addWidget (view, 1);
    auto* summary = new QLabel (&dialog);
    summary->setWordWrap (true);
    layout->addWidget (summary);

    auto* form = new QFormLayout;
    const auto barSeconds = secondsPerBar (*mixerEngine);
    auto* length = new QComboBox (&dialog);
    for (auto n : arch::fibonacci (2584))
        if (n >= 13)
            length->addItem (tr ("%1 bars (%2)").arg (n).arg (minutes (n * barSeconds)), n);
    if (const auto i = length->findData (s.bars); i >= 0)
        length->setCurrentIndex (i);
    else
    {
        length->addItem (tr ("%1 bars (%2)").arg (s.bars).arg (minutes (s.bars * barSeconds)), s.bars);
        length->setCurrentIndex (length->count() - 1);
    }
    length->setToolTip (tr ("A Fibonacci number of bars keeps every section one; at the mixer's tempo"));
    form->addRow (tr ("&Length"), length);
    auto* longest = new QComboBox (&dialog);
    for (auto n : arch::fibonacci (89))
        if (n >= 2)
            longest->addItem (tr ("%1 bars").arg (n), n);
    longest->setCurrentIndex (std::max (0, longest->findData (s.longest)));
    form->addRow (tr ("Longest &section"), longest);
    auto* order = new QComboBox (&dialog);
    order->addItem (tr ("The arch: the emptier scenes first, the fuller toward the climax, then down"));
    order->addItem (tr ("Fibonacci: section k takes the scene F(k) along"));
    order->addItem (tr ("The golden walk: steps of 1/φ along the scenes"));
    order->setCurrentIndex ((int) s.order);
    form->addRow (tr ("Scene &order"), order);
    auto* climax = new QComboBox (&dialog);
    climax->addItem (tr ("The fullest scene"), -1);
    for (size_t i = 0; i < s.clips.size(); ++i)
        if (s.clips[i] > 0)
            climax->addItem (tr ("%1 (%n clip(s))", nullptr, s.clips[i]).arg (mixerWin->sceneName ((int) i)), (int) i);
    climax->setCurrentIndex (std::max (0, climax->findData (s.climaxScene)));
    form->addRow (tr ("&Climax"), climax);
    auto* early = new QCheckBox (tr ("Early, at 0.382 of the way (the shorter part first)"), &dialog);
    early->setChecked (s.earlyClimax);
    form->addRow (QString(), early);
    auto* variety = new QSpinBox (&dialog);
    variety->setRange (0, 100);
    variety->setSuffix (QStringLiteral (" %"));
    variety->setValue ((int) std::lround (s.variety * 100.0));
    variety->setToolTip (tr ("How many sections are cut once more by φ"));
    form->addRow (tr ("&Variety"), variety);
    auto* endlessBox = new QCheckBox (tr ("Endless: deal a new set when this one ends"), &dialog);
    endlessBox->setChecked (endless());
    form->addRow (QString(), endlessBox);
    layout->addLayout (form);

    auto* list = new QListWidget (&dialog);
    list->setMinimumHeight (140);
    layout->addWidget (list);

    auto replan = [&]
    {
        s.bars = length->currentData().toInt();
        s.longest = longest->currentData().toInt();
        s.order = (arch::Order) order->currentIndex();
        s.climaxScene = climax->currentData().toInt();
        s.earlyClimax = early->isChecked();
        s.variety = variety->value() / 100.0;
        view->plan = arch::plan (s);
        view->update();
        list->clear();
        const auto& p = view->plan;
        if (p.sections.empty())
        {
            summary->setText (tr ("No scene has a clip yet: fill some in the clip launcher (the Selector, or the Φ button) and come back."));
            return;
        }
        summary->setText (tr ("%1 sections over %2 bars (%3); the climax at bar %4 on %5, the dissolution from bar %6.")
                              .arg (p.sections.size()).arg (p.bars).arg (minutes (p.bars * barSeconds)).arg (p.climaxBar + 1)
                              .arg (mixerWin->sceneName (p.climaxScene)).arg (p.dissolutionBar + 1));
        for (size_t i = 0; i < p.sections.size(); ++i)
        {
            const auto& x = p.sections[i];
            list->addItem (tr ("%1. bars %2–%3 · %4 · %5 (%6 bars)%7")
                               .arg (i + 1).arg (x.start + 1).arg (x.start + x.bars).arg (QString::fromLatin1 (arch::functionName (x.function)))
                               .arg (mixerWin->sceneName (x.scene)).arg (x.bars).arg (x.climax ? tr (" · the climax") : QString()));
        }
    };
    for (auto* box : { length, longest, order, climax })
        connect (box, qOverload<int> (&QComboBox::currentIndexChanged), &dialog, replan);
    connect (early, &QCheckBox::toggled, &dialog, replan);
    connect (variety, qOverload<int> (&QSpinBox::valueChanged), &dialog, replan);

    auto* buttons = new QDialogButtonBox (&dialog);
    auto* again = buttons->addButton (tr ("Deal &Again"), QDialogButtonBox::ActionRole);
    again->setToolTip (tr ("A new seed: other cuts, another walk through the scenes"));
    auto* play = buttons->addButton (tr ("&Play the Set"), QDialogButtonBox::AcceptRole);
    buttons->addButton (QDialogButtonBox::Close);
    layout->addWidget (buttons);
    connect (again, &QPushButton::clicked, &dialog, [&] { s.seed = arch::nextSeed (s.seed); replan(); });
    connect (play, &QPushButton::clicked, &dialog, &QDialog::accept);
    connect (buttons, &QDialogButtonBox::rejected, &dialog, &QDialog::reject);
    replan();
    dialog.resize (640, 620);
    const auto result = dialog.exec();
    saveSettings (s);
    appSettings().setValue (QStringLiteral ("architectonics/endless"), endlessBox->isChecked());
    commands.action (QStringLiteral ("architectonics.endless"))->setChecked (endlessBox->isChecked());
    if (result == QDialog::Accepted)
    {
        composed = {};   // the set designed here, not the song
        playArchitectonics (true);
    }
}

namespace
{
    // The Mode menu's mode as Compose a Song uses it: its keys above the final, the final, and
    // what the PHI synth plays it in (ModeSection.cpp keeps them; the hypo range is the
    // melody's business there, not the song's).
    struct ModeNow
    {
        uint16_t keys = 0xAB5;   // major, until a mode is chosen
        int final = 0;
        mixer::TuningChoice tuning;
        QString text;
    };

    ModeNow modeNow()
    {
        const char* const finals[12] { "c", "c♯", "d", "e♭", "e", "f", "f♯", "g", "a♭", "a", "b♭", "b" };
        auto settings = appSettings();
        ModeNow m;
        m.final = std::clamp (settings.value (QStringLiteral ("mode/final"), 0).toInt(), 0, 11);
        const auto system = mixer::findTuning (settings.value (QStringLiteral ("mode/system")).toString().toStdString());
        const auto tuning = mixer::findTuning (settings.value (QStringLiteral ("mode/tuning")).toString().toStdString());
        const auto equal = settings.value (QStringLiteral ("mode/equal"), false).toBool();
        const auto& all = mixer::tuningSystems();
        if (system >= 0)
        {
            m.keys = mixer::modeKeys (all[(size_t) system]);
            m.text = QObject::tr ("%1 on %2").arg (QString::fromStdString (all[(size_t) system].name), QString::fromUtf8 (finals[m.final]));
        }
        else
            m.text = QObject::tr ("major on %1 (no mode chosen)").arg (QString::fromUtf8 (finals[m.final]));
        if (tuning >= 0)
        {
            m.tuning = { tuning, all[(size_t) tuning].family == "temperament" ? 0 : m.final };
            m.text += QObject::tr (", in %1").arg (QString::fromStdString (all[(size_t) tuning].name));
        }
        else if (system >= 0 && ! equal)
            m.tuning = { system, m.final };
        else
            m.text += QObject::tr (", in 12-TET");
        return m;
    }

    QString roman (int degree)
    {
        static const char* const numerals[] { "I", "II", "III", "IV", "V", "VI", "VII", "VIII", "IX", "X", "XI", "XII" };
        return QString::fromLatin1 (numerals[(size_t) std::clamp (degree, 0, 11)]);
    }
}

void MainWindow::composeSongDialog (bool asScore, bool quick)
{
    showMixer();
    if (findWidget == nullptr)
        return;
    auto settings = appSettings();
    arch::SongSettings ss;
    ss.form = loadSettings (nullptr);
    ss.form.longest = std::max (ss.form.longest, 5);
    const auto mode = modeNow();
    ss.keys = mode.keys;
    ss.final = mode.final;
    ss.bpm = mixerEngine->tempo.load();
    ss.beatsPerBar = mixerEngine->beatsPerBar.load();
    ss.loopBars = std::clamp (settings.value (QStringLiteral ("architectonics/loop"), 4).toInt(), 1, 16);
    const auto savedRoles = settings.value (QStringLiteral ("architectonics/roles")).toString().split (QLatin1Char (','));
    for (int c = 0; c < arch::colours && c < savedRoles.size(); ++c)
        if (bool ok = false; savedRoles[c].toInt (&ok) >= 0 && ok)
            ss.roles[(size_t) c] = (arch::Role) std::clamp (savedRoles[c].toInt(), 0, (int) arch::Role::melody);

    // The Selector's picks, a deal for each part of the song: the same sounds whenever the part
    // comes back. The first deal also says which colours have sounds, and their instruments.
    std::map<arch::Function, FindPanel::DealPick> picks;
    for (auto f : { arch::Function::emergence, arch::Function::expansion, arch::Function::disruption, arch::Function::climax, arch::Function::dissolution })
        picks[f] = findWidget->pickDeal();
    const auto& first = picks[arch::Function::emergence];
    std::array<bool, arch::colours> hasSound {};
    auto scanSounds = [&]
    {
        hasSound = {};
        for (const auto& [f, p] : picks)
            for (int c = 0; c < arch::colours && c < p.files.size(); ++c)
                hasSound[(size_t) c] = hasSound[(size_t) c] || ! p.files[c].isEmpty();
    };
    scanSounds();

    QDialog dialog (this);
    dialog.setWindowTitle (asScore ? tr ("Compose a Score") : tr ("Compose a Song"));
    auto* layout = new QVBoxLayout (&dialog);
    auto* intro = new QLabel (tr ("A song in the clip launcher, a scene for every section of a form cut by the golden ratio. "
                                  "Each colour of the Selector plays a part: a sound dealt from its favourites, or MIDI written in "
                                  "the Mode menu's mode. The energy decides who plays, all of them at the climax."), &dialog);
    intro->setWordWrap (true);
    layout->addWidget (intro);
    auto* view = new PlanView (&dialog);
    layout->addWidget (view, 1);
    auto* summary = new QLabel (&dialog);
    summary->setWordWrap (true);
    layout->addWidget (summary);

    auto* columns = new QHBoxLayout;
    auto* form = new QFormLayout;
    const auto barSeconds = secondsPerBar (*mixerEngine);
    auto* length = new QComboBox (&dialog);
    for (auto n : arch::fibonacci (610))
        if (n >= 21)
            length->addItem (tr ("%1 bars (%2)").arg (n).arg (minutes (n * barSeconds)), n);
    length->setCurrentIndex (std::max (0, length->findData (ss.form.bars)));
    form->addRow (tr ("&Length"), length);
    auto* longest = new QComboBox (&dialog);
    for (auto n : arch::fibonacci (89))
        if (n >= 5)
            longest->addItem (tr ("%1 bars").arg (n), n);
    longest->setCurrentIndex (std::max (0, longest->findData (ss.form.longest)));
    longest->setToolTip (tr ("Longer sections make fewer scenes: the launcher has %1").arg (mixer::maxScenes));
    form->addRow (tr ("Longest &section"), longest);
    auto* early = new QCheckBox (tr ("Climax early, at 0.382"), &dialog);
    early->setChecked (ss.form.earlyClimax);
    form->addRow (QString(), early);
    auto* variety = new QSpinBox (&dialog);
    variety->setRange (0, 100);
    variety->setSuffix (QStringLiteral (" %"));
    variety->setValue ((int) std::lround (ss.form.variety * 100.0));
    form->addRow (tr ("&Variety"), variety);
    auto* loop = new QComboBox (&dialog);
    for (int n : { 1, 2, 4, 8 })
        loop->addItem (tr ("%n bar(s)", nullptr, n), n);
    loop->setCurrentIndex (std::max (0, loop->findData (ss.loopBars)));
    loop->setToolTip (tr ("How long each scene's MIDI is before it goes round"));
    form->addRow (tr ("MIDI &loops"), loop);
    auto* rhythms = new QComboBox (&dialog);
    rhythms->addItem (tr ("every rhythm in whole bars"), QString());
    rhythms->addItem (tr ("four on the floor (PHI's own)"), QStringLiteral ("-"));
    for (const auto& g : rhythm::groups())
        rhythms->addItem (QString::fromStdString (g), QString::fromStdString (g));
    rhythms->setCurrentIndex (std::max (0, rhythms->findData (settings.value (QStringLiteral ("architectonics/rhythms")).toString())));
    rhythms->setToolTip (tr ("Where the rhythm role takes its rhythms from, a rhythm a part, as dense as the part's energy"));
    form->addRow (tr ("&Rhythms from"), rhythms);
    auto* modeLabel = new QLabel (tr ("%1 (choose it in the Mode menu)").arg (mode.text), &dialog);
    modeLabel->setWordWrap (true);
    form->addRow (tr ("Mode"), modeLabel);
    columns->addLayout (form, 3);

    // A role for each colour of the Selector, named as the set shown names them.
    auto* roles = new QGridLayout;
    std::array<QComboBox*, arch::colours> roleBoxes {};
    for (int c = 0; c < arch::colours; ++c)
    {
        auto* swatch = new QLabel (&dialog);
        swatch->setFixedSize (12, 12);
        swatch->setStyleSheet (QStringLiteral ("background: %1; border-radius: 2px;").arg (FindPanel::colour (c).name()));
        roles->addWidget (swatch, c, 0);
        roles->addWidget (new QLabel (c < first.names.size() ? first.names[c] : FindPanel::colourName (c), &dialog), c, 1);
        auto* box = new QComboBox (&dialog);
        for (int r = 0; r <= (int) arch::Role::melody; ++r)
            box->addItem (QString::fromLatin1 (arch::roleName ((arch::Role) r)));
        box->setCurrentIndex ((int) ss.roles[(size_t) c]);
        roles->addWidget (box, c, 2);
        roleBoxes[(size_t) c] = box;
    }
    columns->addLayout (roles, 2);
    layout->addLayout (columns);

    auto* list = new QListWidget (&dialog);
    list->setMinimumHeight (130);
    layout->addWidget (list);

    arch::Song song;
    auto pool = [&]
    {
        std::vector<const rhythm::Rhythm*> out;
        const auto group = rhythms->currentData().toString();
        if (group == QLatin1String ("-"))
            return out;
        for (const auto& r : rhythm::rhythms())
            if (group.isEmpty() || QString::fromStdString (r.group) == group)
                out.push_back (&r);
        return out;
    };
    auto recompose = [&]
    {
        ss.form.bars = length->currentData().toInt();
        ss.form.longest = longest->currentData().toInt();
        ss.form.earlyClimax = early->isChecked();
        ss.form.variety = variety->value() / 100.0;
        ss.loopBars = loop->currentData().toInt();
        ss.rhythms = pool();
        for (int c = 0; c < arch::colours; ++c)
        {
            ss.roles[(size_t) c] = (arch::Role) roleBoxes[(size_t) c]->currentIndex();
            ss.present[(size_t) c] = ss.roles[(size_t) c] != arch::Role::sound || hasSound[(size_t) c];
        }
        song = arch::compose (ss);
        view->plan = song.plan;
        view->update();
        list->clear();
        if (song.plan.sections.empty())
            return summary->setText (tr ("Nothing to compose."));
        const auto scenes = (int) song.plan.sections.size();
        summary->setText (tr ("%1 sections over %2 bars (%3), a scene each%4; the climax at bar %5.")
                              .arg (scenes).arg (song.plan.bars).arg (minutes (song.plan.bars * barSeconds))
                              .arg (scenes > mixer::maxScenes ? tr (", more than the launcher's %1: choose longer sections").arg (mixer::maxScenes) : QString())
                              .arg (song.plan.climaxBar + 1));
        for (const auto& sc : song.scenes)
        {
            const auto& x = song.plan.sections[(size_t) sc.section];
            QStringList who, chords;
            for (int c = 0; c < arch::colours; ++c)
                if (sc.plays[(size_t) c])
                    who << (c < first.names.size() ? first.names[c] : FindPanel::colourName (c));
            for (auto d : sc.chords)
                chords << roman (d);
            list->addItem (tr ("%1. bars %2–%3 · %4 · %5 · %6%7")
                               .arg (sc.section + 1).arg (x.start + 1).arg (x.start + x.bars).arg (QString::fromLatin1 (arch::functionName (sc.function)))
                               .arg (chords.join (QStringLiteral (" "))).arg (who.join (QStringLiteral (", ")))
                               .arg (sc.rhythm.empty() ? QString() : tr (" · %1").arg (QString::fromStdString (sc.rhythm))));
        }
    };
    for (auto* box : { length, longest, loop, rhythms })
        connect (box, qOverload<int> (&QComboBox::currentIndexChanged), &dialog, recompose);
    for (auto* box : roleBoxes)
        connect (box, qOverload<int> (&QComboBox::currentIndexChanged), &dialog, recompose);
    connect (early, &QCheckBox::toggled, &dialog, recompose);
    connect (variety, qOverload<int> (&QSpinBox::valueChanged), &dialog, recompose);

    auto* buttons = new QDialogButtonBox (&dialog);
    auto* again = buttons->addButton (tr ("Deal &Again"), QDialogButtonBox::ActionRole);
    again->setToolTip (tr ("A new seed: another form, other progressions and motifs, other sounds from the Selector"));
    auto* write = buttons->addButton (asScore ? tr ("&Open the Score") : tr ("C&ompose"), QDialogButtonBox::AcceptRole);
    write->setToolTip (asScore ? tr ("The whole composition in the MIDI editor, a part a colour, the sections marked; kept, it goes on a track")
                               : tr ("Into the first empty scenes of the clip launcher"));
    auto* writePlay = buttons->addButton (asScore ? tr ("&Save as MIDI...") : tr ("Compose and &Play"), QDialogButtonBox::AcceptRole);
    buttons->addButton (QDialogButtonBox::Close);
    layout->addWidget (buttons);
    bool play = false;
    connect (again, &QPushButton::clicked, &dialog, [&]
    {
        ss.form.seed = arch::nextSeed (ss.form.seed);
        for (auto& [f, p] : picks)
            p = findWidget->pickDeal();
        scanSounds();
        recompose();
    });
    connect (write, &QPushButton::clicked, &dialog, [&] { play = false; dialog.accept(); });
    connect (writePlay, &QPushButton::clicked, &dialog, [&] { play = true; dialog.accept(); });
    connect (buttons, &QDialogButtonBox::rejected, &dialog, &QDialog::reject);
    if (quick)
        ss.form.seed = arch::nextSeed (ss.form.seed);   // a new song each time
    recompose();
    dialog.resize (760, 760);
    const auto result = quick ? (int) QDialog::Accepted : dialog.exec();

    auto kept = loadSettings (nullptr);
    kept.bars = ss.form.bars;
    kept.longest = ss.form.longest;
    kept.earlyClimax = ss.form.earlyClimax;
    kept.variety = ss.form.variety;
    kept.seed = ss.form.seed;
    saveSettings (kept);
    QStringList roleList;
    for (auto r : ss.roles)
        roleList << QString::number ((int) r);
    settings.setValue (QStringLiteral ("architectonics/roles"), roleList.join (QLatin1Char (',')));
    settings.setValue (QStringLiteral ("architectonics/loop"), ss.loopBars);
    settings.setValue (QStringLiteral ("architectonics/rhythms"), rhythms->currentData().toString());
    if (result != QDialog::Accepted || song.plan.sections.empty())
        return;

    if (asScore)
    {
        // The parts written out in full, the sections marked; the Selector's sounds have no notes to show.
        const auto title = tr ("Architectonics score");
        const auto full = arch::score (song, ss, title.toStdString());
        if (full->noteCount() == 0)
            return statusBar()->showMessage (tr ("Give some colours a role that writes notes (rhythm, bass, chords, arpeggio or melody) first"), 8000);
        if (! play)
            return mixerWin->openScore (*full, mode.tuning, title);
        const auto folder = settings.value (QStringLiteral ("architectonics/folder"), QDir::homePath()).toString();
        const auto file = QFileDialog::getSaveFileName (this, tr ("Save the score as MIDI"), QDir (folder).filePath (title + QStringLiteral (".mid")),
                                                        tr ("MIDI files (*.mid *.midi)"));
        if (file.isEmpty())
            return;
        settings.setValue (QStringLiteral ("architectonics/folder"), QFileInfo (file).absolutePath());
        std::string why;
        if (! midifile::write (*full, file.toStdString(), why))
            return statusBar()->showMessage (tr ("%1 could not be saved: %2").arg (file, QString::fromStdString (why)), 8000);
        return statusBar()->showMessage (tr ("%1 saved").arg (QFileInfo (file).fileName()), 5000);
    }

    const auto count = (int) song.scenes.size();
    const auto start = mixerWin->emptyScenes (count);
    if (start < 0)
        return statusBar()->showMessage (tr ("The song needs %1 empty scenes in a row and the launcher has not got them: empty some, "
                                             "or choose longer sections").arg (count), 8000);
    mixerWin->showLauncher (true);
    std::vector<bool> wanted;
    for (auto p : ss.present)
        wanted.push_back (p);
    const auto tracks = mixerWin->colourTracks (first.names, first.colours, wanted, first.instruments);
    for (const auto& sc : song.scenes)
    {
        QStringList files;
        std::vector<midifile::SongPtr> midi;
        for (int c = 0; c < arch::colours; ++c)
        {
            const auto& pick = picks[sc.function];
            const auto sound = sc.plays[(size_t) c] && ss.roles[(size_t) c] == arch::Role::sound && c < pick.files.size();
            files << (sound ? pick.files[c] : QString());
            midi.push_back (sc.midi[(size_t) c]);
        }
        mixerWin->composeScene (start + sc.section, tracks, files, midi, mode.tuning,
                                tr ("%1 %2").arg (sc.section + 1).arg (QString::fromLatin1 (arch::functionName (sc.function))));
    }
    composed = song.plan;
    for (auto& x : composed.sections)
        x.scene += start;
    composed.climaxScene += start;
    statusBar()->showMessage (tr ("Architectonics: a song of %1 sections in scenes %2 to %3; Play the Set plays it")
                                  .arg (count).arg (start + 1).arg (start + count), 8000);
    if (play)
        playArchitectonics (true);
}

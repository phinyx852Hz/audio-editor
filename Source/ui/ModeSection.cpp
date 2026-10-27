// SPDX-License-Identifier: AGPL-3.0-only
// The Mode menu (owner, 2026-10-26: "I love mode being its own thing"): every mode and raga
// PHI has, grouped by tradition, chosen once with its final and its range (authentic or
// hypo), and then used everywhere a mode is asked for: the counterpoint and fugue dialogs
// open on it, the selected mixer track can be tuned to it, and the MIDI clip shown in the
// Device Chain moved into it. Every name in it is lower case (owner), the modes as
// mixer/Tunings.cpp transliterates them.
#include "MainWindow.h"
#include "MixerWindow.h"
#include "Settings.h"
#include "../mixer/Tunings.h"

#include <QActionGroup>
#include <QMenu>
#include <QStatusBar>

#include <algorithm>
#include <map>

namespace
{
    const char* const finals[12] { "c", "c♯", "d", "e♭", "e", "f", "f♯", "g", "a♭", "a", "b♭", "b" };

    // The traditions, in the order the menu lists them, and the tuning families each takes in.
    // A family not named here gets a submenu of its own; the equal temperaments are tunings,
    // not modes, and a family's equal-tempered twins are the "equal tempered" switch.
    struct Tradition
    {
        const char* title;
        std::vector<std::string> families;
    };
    const std::vector<Tradition>& traditions()
    {
        static const std::vector<Tradition> all {
            { "&church", { "church mode", "hypo-mode" } },
            { "&jazz", { "jazz mode" } },
            { "me&ssiaen", { "messiaen mode" } },
            { "&rāga", { "raga" } },
            { "&maqām", { "maqam" } },
            { "&dastgāh", { "dastgah" } },
            { "&east asian", { "chinese mode", "chinese regional mode", "chinese tuning", "gagaku mode",
                               "japanese mode", "korean mode", "korean folk mode" } },
            { "&gamelan", { "javanese gamelan", "balinese gamelan", "sundanese gamelan" } },
            { "m&bira", { "mbira dzavadzimu", "nyunga nyunga", "matepe" } },
        };
        return all;
    }

    bool listed (const mixer::TuningSystem& t)
    {
        const std::string twin = ", equal tempered";
        return t.family != "equal"
            && ! (t.family.size() > twin.size() && t.family.compare (t.family.size() - twin.size(), twin.size(), twin) == 0);
    }

    // The mode chosen: `system` an authentic one (a hypo-mode is its church mode with the hypo
    // range from the fifth), `final` 0 (c) to 11 (b), and whether the synth plays it in 12-TET.
    struct Mode
    {
        int system = -1;
        int final = 0;
        mixer::Hypo hypo = mixer::Hypo::none;
        bool equal = false;
    };

    const mixer::TuningSystem& systemAt (int i) { return mixer::tuningSystems()[(size_t) i]; }
    bool church (int system) { return system >= 0 && systemAt (system).family == "church mode"; }

    int hypoMode (int system)   // dorian's hypodorian
    {
        return church (system) ? mixer::findTuning ("hypo" + systemAt (system).plain) : -1;
    }

    bool usable (const Mode& m, mixer::Hypo h)
    {
        if (m.system < 0)
            return h == mixer::Hypo::none;
        if (church (m.system))
            return h == mixer::Hypo::none || (h == mixer::Hypo::fifth && hypoMode (m.system) >= 0);
        return mixer::hasHypo (systemAt (m.system), h);
    }

    Mode loadMode()
    {
        auto settings = appSettings();
        Mode m;
        m.system = mixer::findTuning (settings.value (QStringLiteral ("mode/system")).toString().toStdString());
        m.final = std::clamp (settings.value (QStringLiteral ("mode/final"), 0).toInt(), 0, 11);
        m.hypo = (mixer::Hypo) std::clamp (settings.value (QStringLiteral ("mode/hypo"), 0).toInt(), 0, 2);
        m.equal = settings.value (QStringLiteral ("mode/equal"), false).toBool();
        if (m.system >= 0 && ! listed (systemAt (m.system)))
            m.system = -1;
        if (! usable (m, m.hypo))
            m.hypo = mixer::Hypo::none;
        return m;
    }

    // What the tuning boxes, counterpoint and the fugue take: a church mode's hypo range is its
    // hypo-mode, on the bottom of that range (hypodorian on a for a final on d).
    mixer::TuningChoice tuningOf (const Mode& m)
    {
        if (m.system < 0)
            return {};
        auto system = m.system;
        auto root = m.final;
        if (church (system) && m.hypo == mixer::Hypo::fifth)
        {
            system = hypoMode (system);
            root = (m.final + 7) % 12;
        }
        if (m.equal)
            if (const auto twin = mixer::findTuning (systemAt (system).plain + " (equal tempered)"); twin >= 0)
                system = twin;
        return { system, root };
    }

    QString describe (const Mode& m)
    {
        if (m.system < 0)
            return QObject::tr ("no mode chosen");
        const auto t = tuningOf (m);
        auto text = QObject::tr ("%1 on %2").arg (QString::fromStdString (systemAt (t.system).name), QString::fromUtf8 (finals[t.root]));
        if (church (m.system) && m.hypo == mixer::Hypo::fifth)
            text += QObject::tr (", final %1").arg (QString::fromUtf8 (finals[m.final]));
        else if (m.hypo == mixer::Hypo::fifth)
            text += QObject::tr (", hypo from the fifth");
        else if (m.hypo == mixer::Hypo::fourth)
            text += QObject::tr (", hypo from the fourth");
        return text;
    }

    // Kept, and handed on to the dialogs that ask for a mode: they open on it when the clip
    // has no tuning of its own.
    void saveMode (const Mode& m)
    {
        auto settings = appSettings();
        const auto t = tuningOf (m);
        const auto plain = m.system >= 0 ? QString::fromStdString (systemAt (m.system).plain) : QString();
        const auto tuned = t.system >= 0 ? QString::fromStdString (systemAt (t.system).plain) : QString();
        settings.setValue (QStringLiteral ("mode/system"), plain);
        settings.setValue (QStringLiteral ("mode/final"), m.final);
        settings.setValue (QStringLiteral ("mode/hypo"), (int) m.hypo);
        settings.setValue (QStringLiteral ("mode/equal"), m.equal);
        settings.setValue (QStringLiteral ("counterpoint/mode"), tuned);
        settings.setValue (QStringLiteral ("counterpoint/root"), t.root);
        settings.setValue (QStringLiteral ("counterpoint/hypo"), church (m.system) ? 0 : (int) m.hypo);
        settings.setValue (QStringLiteral ("fugue/mode"), tuned);
        settings.setValue (QStringLiteral ("fugue/root"), t.root);
    }
}

void MainWindow::registerModeCommands()
{
    const auto category = tr ("Mode");
    auto add = [this, &category] (const char* id, const QString& text, std::function<void()> fn)
    {
        auto* action = commands.add (QString::fromLatin1 (id), category, text, {}, std::move (fn));
        addAction (action);
        return action;
    };
    add ("mode.tuneTrack", tr ("&tune the track"), [this]
    {
        const auto m = loadMode();
        if (m.system < 0)
            return statusBar()->showMessage (tr ("Choose a mode first"), 5000);
        showMixer();
        if (! mixerWin->tuneSelectedTrack (tuningOf (m)))
            statusBar()->showMessage (tr ("Pick a track in the mixer first"), 5000);
    })->setStatusTip (tr ("The selected mixer track's PHI synth plays in the mode"));
    add ("mode.toClip", tr ("&notes into the mode"), [this]
    {
        const auto m = loadMode();
        if (m.system < 0)
            return statusBar()->showMessage (tr ("Choose a mode first"), 5000);
        const auto& system = systemAt (m.system);
        const auto below = church (m.system) && m.hypo == mixer::Hypo::fifth ? 5 : mixer::hypoBelow (m.hypo);
        if (mixerWin == nullptr || ! mixerWin->modeShownMidi (m.final, mixer::modeKeys (system), below, tr ("Into %1").arg (describe (m))))
            statusBar()->showMessage (tr ("Pick a MIDI clip in the mixer first"), 5000);
    })->setStatusTip (tr ("Every note of the MIDI clip shown in the mixer to the nearest of the mode, in its range (drums stay)"));
    add ("mode.counterpoint", tr ("co&unterpoint..."), [this]
    {
        if (mixerWin == nullptr || ! mixerWin->showsMidiClip())
            return statusBar()->showMessage (tr ("Pick a MIDI clip in the mixer first"), 5000);
        mixerWin->counterpointShown();
    })->setStatusTip (tr ("New voices against the MIDI clip shown in the mixer, in the mode"));
    add ("mode.fugue", tr ("fugue..."), [this]
    {
        if (mixerWin == nullptr || ! mixerWin->showsMidiClip())
            return statusBar()->showMessage (tr ("Pick a MIDI clip in the mixer first"), 5000);
        mixerWin->fugueShown();
    })->setStatusTip (tr ("A fugue on the MIDI clip shown in the mixer as its subject, in the mode"));
    add ("mode.stochasticize", tr ("&stochasticize..."), [this]
    {
        if (mixerWin == nullptr || ! mixerWin->showsMidiClip())
            return statusBar()->showMessage (tr ("Pick a MIDI clip in the mixer first"), 5000);
        mixerWin->stochasticizeShown();
    })->setStatusTip (tr ("A new line beside the MIDI clip shown in the mixer, by Xenakis's methods, its pitches sieved from the mode"));
    add ("mode.serial", tr ("&12 tone row..."), [this] { serialDialog(); })
        ->setStatusTip (tr ("Schoenberg's method: a row, its matrix and a looping piece on it in one to four voices, for the launcher, "
                            "counterpoint and the fugue"));
    add ("mode.cubify", tr ("cubif&y..."), [this]
    {
        if (mixerWin == nullptr)
            return statusBar()->showMessage (tr ("Open the mixer first"), 5000);
        mixerWin->cubify();
    })->setStatusTip (tr ("The MIDI clip shown in the mixer, else the launcher's scene, arranged by Xenakis's cubes; a messiaen mode chosen here is its pitches"));
}

void MainWindow::fillModeMenu (QMenu* menu)
{
    menu->clear();   // and what the last fill made for it: the submenus, the groups
    qDeleteAll (menu->findChildren<QMenu*> (QString(), Qt::FindDirectChildrenOnly));
    qDeleteAll (menu->findChildren<QActionGroup*> (QString(), Qt::FindDirectChildrenOnly));
    const auto m = loadMode();
    // Ticked: the mode, or its hypo-mode when the range is that.
    const auto shown = church (m.system) && m.hypo == mixer::Hypo::fifth ? hypoMode (m.system) : m.system;
    const auto& all = mixer::tuningSystems();

    auto* now = menu->addAction (describe (m));
    now->setEnabled (false);
    menu->addSeparator();

    auto choose = [this] (Mode next) { saveMode (next); statusBar()->showMessage (tr ("Mode: %1").arg (describe (next)), 5000); };
    auto* modes = new QActionGroup (menu);
    auto addMode = [&] (QMenu* into, int i)
    {
        auto* a = into->addAction (QString::fromStdString (all[(size_t) i].name));
        a->setCheckable (true);
        a->setChecked (i == shown);
        a->setToolTip (QString::fromStdString (all[(size_t) i].family));
        modes->addAction (a);
        connect (a, &QAction::triggered, this, [m, i, choose]
        {
            auto next = m;
            next.system = i;
            if (systemAt (i).family == "hypo-mode")   // hypodorian: dorian from the fifth, its final kept
            {
                next.system = mixer::findTuning (systemAt (i).plain.substr (4));
                next.hypo = mixer::Hypo::fifth;
            }
            else if (! usable (next, next.hypo))
                next.hypo = mixer::Hypo::none;
            choose (next);
        });
    };
    // A long list (the rāgas) in alphabetical parts, so it fits on a small screen.
    auto addFamily = [&] (QMenu* into, const std::vector<int>& members)
    {
        constexpr size_t part = 30;
        if (members.size() <= part + 10)
        {
            for (auto i : members)
                addMode (into, i);
            return;
        }
        auto sorted = members;
        std::stable_sort (sorted.begin(), sorted.end(), [&] (int a, int b) { return all[(size_t) a].plain < all[(size_t) b].plain; });
        for (size_t from = 0; from < sorted.size(); from += part)
        {
            const auto to = std::min (sorted.size(), from + part) - 1;
            auto* sub = into->addMenu (QStringLiteral ("%1 – %2").arg (QChar::fromLatin1 (all[(size_t) sorted[from]].plain.front()),
                                                                       QChar::fromLatin1 (all[(size_t) sorted[to]].plain.front())));
            for (auto k = from; k <= to; ++k)
                addMode (sub, sorted[k]);
        }
    };

    std::map<std::string, std::vector<int>> byFamily;
    std::vector<std::string> order;
    for (size_t i = 0; i < all.size(); ++i)
        if (listed (all[i]))
        {
            if (byFamily.find (all[i].family) == byFamily.end())
                order.push_back (all[i].family);
            byFamily[all[i].family].push_back ((int) i);
        }
    for (const auto& tradition : traditions())
    {
        std::vector<const std::vector<int>*> present;
        for (const auto& family : tradition.families)
            if (auto it = byFamily.find (family); it != byFamily.end())
                present.push_back (&it->second);
        if (present.empty())
            continue;
        auto* sub = menu->addMenu (QString::fromUtf8 (tradition.title));
        for (size_t f = 0; f < present.size(); ++f)
        {
            if (f > 0)
                sub->addSeparator();
            addFamily (sub, *present[f]);
        }
        for (const auto& family : tradition.families)
            byFamily.erase (family);
    }
    for (const auto& family : order)   // a tradition the table above does not know yet
        if (auto it = byFamily.find (family); it != byFamily.end())
            addFamily (menu->addMenu (QString::fromStdString (family)), it->second);
    fillRhythmMenu (menu->addMenu (tr ("r&hythmic modes")));

    menu->addSeparator();
    auto* finalMenu = menu->addMenu (tr ("&final"));
    finalMenu->setToolTip (tr ("The final, sa or tonic"));
    auto* finalGroup = new QActionGroup (finalMenu);
    for (int k = 0; k < 12; ++k)
    {
        auto* a = finalMenu->addAction (QString::fromUtf8 (finals[k]));
        a->setCheckable (true);
        a->setChecked (k == m.final);
        finalGroup->addAction (a);
        connect (a, &QAction::triggered, this, [m, k, choose] { auto next = m; next.final = k; choose (next); });
    }
    auto* rangeMenu = menu->addMenu (tr ("r&ange"));
    auto* rangeGroup = new QActionGroup (rangeMenu);
    for (auto [h, text] : { std::pair { mixer::Hypo::none, tr ("&authentic") },
                            { mixer::Hypo::fifth, church (m.system) ? tr ("hypo, from the &fifth (the hypo-mode)") : tr ("hypo, from the &fifth") },
                            { mixer::Hypo::fourth, tr ("hypo, from the f&ourth") } })
    {
        auto* a = rangeMenu->addAction (text);
        a->setCheckable (true);
        a->setChecked (h == m.hypo);
        a->setEnabled (usable (m, h));
        rangeGroup->addAction (a);
        connect (a, &QAction::triggered, this, [m, h, choose] { auto next = m; next.hypo = h; choose (next); });
    }
    auto* equal = menu->addAction (tr ("e&qual tempered"));
    equal->setCheckable (true);
    equal->setChecked (m.equal);
    equal->setStatusTip (tr ("The PHI synth plays the mode on the piano's keys (12-TET) instead of in its own tuning"));
    connect (equal, &QAction::toggled, this, [m, choose] (bool on) { auto next = m; next.equal = on; choose (next); });

    menu->addSeparator();
    const auto clip = mixerWin != nullptr && mixerWin->showsMidiClip();
    for (const auto* id : { "mode.tuneTrack", "mode.toClip", "mode.counterpoint", "mode.fugue", "mode.stochasticize" })
    {
        const auto dialog = QLatin1String (id) == QLatin1String ("mode.counterpoint") || QLatin1String (id) == QLatin1String ("mode.fugue")
                            || QLatin1String (id) == QLatin1String ("mode.stochasticize");
        auto* a = commands.action (QString::fromLatin1 (id));
        a->setEnabled (m.system >= 0 || dialog);
        if (QLatin1String (id) != QLatin1String ("mode.tuneTrack"))
            a->setEnabled (a->isEnabled() && clip);
        menu->addAction (a);
    }
    menu->addAction (commands.action (QStringLiteral ("mode.serial")));   // needs no clip: the row is its own
    auto* cube = commands.action (QStringLiteral ("mode.cubify"));
    cube->setEnabled (mixerWin != nullptr);
    menu->addAction (cube);
}

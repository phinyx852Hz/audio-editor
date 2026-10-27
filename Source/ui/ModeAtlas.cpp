// SPDX-License-Identifier: AGPL-3.0-only
#include "ModeAtlas.h"
#include "../mixer/Tunings.h"

#include <QActionGroup>
#include <QDialog>
#include <QDialogButtonBox>
#include <QKeyEvent>
#include <QLabel>
#include <QLineEdit>
#include <QListWidget>
#include <QMenu>
#include <QVBoxLayout>

#include <algorithm>
#include <memory>

namespace
{
    QString text (const std::string& s) { return QString::fromStdString (s); }

    QString originTitle (const mixer::Origin& o)
    {
        // The names of people, nations and empires capitalized; the modes stay lower case (owner, 2026-10-27).
        const auto& name = o.shown.empty() ? o.name : o.shown;
        return o.dates.empty() ? text (name) : QStringLiteral ("%1 (%2)").arg (text (name), text (o.dates));
    }

    QString tipOf (const mixer::TuningSystem& t)
    {
        auto tip = text (t.family);
        if (! t.note.empty())
            tip += QStringLiteral ("\n") + text (t.note);
        if (std::abs (t.period - 1200.0) > 1.0e-6)
            tip += QObject::tr ("\nrepeats every %1 cents").arg (t.period, 0, 'f', 1);
        return tip;
    }

    void bold (QAction* a)
    {
        auto f = a->font();
        f.setBold (true);
        a->setFont (f);
    }

    bool from (const mixer::TuningSystem& t, const std::string& origin)
    {
        return std::find (t.from.begin(), t.from.end(), origin) != t.from.end();
    }

    // The systems, a family at a time with a line between, and a long list (the rāgas, the
    // melakartas) in alphabetical parts so it fits on a small screen.
    void addSystems (QMenu* into, const std::vector<int>& members, int checked, const std::function<void (int)>& pick)
    {
        const auto& all = mixer::tuningSystems();
        auto* group = new QActionGroup (into);
        auto addOne = [&] (QMenu* menu, int i)
        {
            auto* a = menu->addAction (text (all[(size_t) i].name));
            a->setCheckable (true);
            a->setChecked (i == checked);
            a->setToolTip (tipOf (all[(size_t) i]));
            a->setStatusTip (QString (tipOf (all[(size_t) i])).replace (QLatin1Char ('\n'), QStringLiteral ("; ")));
            group->addAction (a);
            QObject::connect (a, &QAction::triggered, menu, [pick, i] { pick (i); });
        };
        constexpr size_t part = 30;
        std::vector<std::vector<int>> families;
        for (auto i : members)
            if (families.empty() || all[(size_t) families.back().front()].family != all[(size_t) i].family)
                families.push_back ({ i });
            else
                families.back().push_back (i);
        into->setToolTipsVisible (true);
        for (size_t f = 0; f < families.size(); ++f)
        {
            if (f > 0)
                into->addSeparator();
            const auto& list = families[f];
            if (list.size() <= part + 10)
            {
                for (auto i : list)
                    addOne (into, i);
                continue;
            }
            auto sorted = list;
            std::stable_sort (sorted.begin(), sorted.end(), [&] (int a, int b) { return all[(size_t) a].plain < all[(size_t) b].plain; });
            for (size_t at = 0; at < sorted.size(); at += part)
            {
                const auto to = std::min (sorted.size(), at + part) - 1;
                auto* sub = into->addMenu (QStringLiteral ("%1 – %2").arg (QChar::fromLatin1 (all[(size_t) sorted[at]].plain.front()),
                                                                           QChar::fromLatin1 (all[(size_t) sorted[to]].plain.front())));
                sub->setToolTipsVisible (true);
                bool here = false;
                for (auto k = at; k <= to; ++k)
                {
                    addOne (sub, sorted[k]);
                    here = here || sorted[k] == checked;
                }
                if (here)
                    bold (sub->menuAction());
            }
        }
    }

    // Filled the first time it opens, so the Mode menu opens at once.
    void whenShown (QMenu* menu, std::function<void (QMenu*)> fillIt)
    {
        auto done = std::make_shared<bool> (false);
        QObject::connect (menu, &QMenu::aboutToShow, menu, [menu, done, fillIt]
        {
            if (*done)
                return;
            *done = true;
            fillIt (menu);
        });
    }
}

namespace modeatlas
{
    void fill (QMenu* menu, unsigned kind, int checked, std::function<void (int)> pick)
    {
        const auto& all = mixer::tuningSystems();
        const auto* chosen = checked >= 0 && checked < (int) all.size() ? &all[(size_t) checked] : nullptr;
        menu->setToolTipsVisible (true);
        for (const auto& region : mixer::regions())
        {
            std::vector<const mixer::Origin*> here;
            for (const auto& o : mixer::origins())
                if (o.region == region && ! mixer::systemsFrom (o.name, kind).empty())
                    here.push_back (&o);
            if (here.empty())
                continue;
            auto* sub = menu->addMenu (text (mixer::regionTitle (region)));
            bool marked = false;
            for (const auto* o : here)
                marked = marked || (chosen != nullptr && from (*chosen, o->name));
            if (marked)
                bold (sub->menuAction());
            whenShown (sub, [here, kind, checked, pick, chosen] (QMenu* m)
            {
                m->setToolTipsVisible (true);
                bool people = false;
                for (const auto* o : here)
                {
                    if (o->person && ! people && o != here.front())
                        m->addSeparator();
                    people = people || o->person;
                    auto* om = m->addMenu (originTitle (*o));
                    if (chosen != nullptr && from (*chosen, o->name))
                        bold (om->menuAction());
                    const auto name = o->name;
                    whenShown (om, [name, kind, checked, pick] (QMenu* x) { addSystems (x, mixer::systemsFrom (name, kind), checked, pick); });
                }
            });
        }
        if ((kind & mixer::tuning) != 0)
        {
            std::vector<int> equal;
            for (size_t i = 0; i < all.size(); ++i)
                if (all[i].family == "equal")
                    equal.push_back ((int) i);
            menu->addSeparator();
            auto* sub = menu->addMenu (QObject::tr ("equal divisions"));
            if (chosen != nullptr && chosen->family == "equal")
                bold (sub->menuAction());
            whenShown (sub, [equal, checked, pick] (QMenu* m) { addSystems (m, equal, checked, pick); });
        }
    }

    QString where (int system)
    {
        const auto& t = mixer::tuningSystems()[(size_t) system];
        QStringList places;
        for (const auto& name : t.from)
            if (const auto* o = mixer::findOrigin (name))
                places << originTitle (*o);
        return places.isEmpty() ? text (t.name) : QStringLiteral ("%1 — %2").arg (text (t.name), places.join (QStringLiteral ("; ")));
    }

    int find (QWidget* parent, unsigned kind, const QString& title)
    {
        const auto& all = mixer::tuningSystems();
        QDialog dialog (parent);
        dialog.setWindowTitle (title);
        dialog.resize (620, 520);
        auto* layout = new QVBoxLayout (&dialog);
        auto* search = new QLineEdit (&dialog);
        search->setPlaceholderText (QObject::tr ("a name, a people, a composer, a region, a family: \"pelog\", \"partch\", \"ottoman\", \"west asia\", \"genus\""));
        search->setClearButtonEnabled (true);
        auto* list = new QListWidget (&dialog);
        auto* count = new QLabel (&dialog);
        auto* buttons = new QDialogButtonBox (QDialogButtonBox::Ok | QDialogButtonBox::Cancel, &dialog);
        layout->addWidget (search);
        layout->addWidget (list, 1);
        layout->addWidget (count);
        layout->addWidget (buttons);

        // What each system is found by: its names, family, note, and its people, cultures and regions.
        std::vector<std::pair<int, QString>> haystack;
        for (size_t i = 0; i < all.size(); ++i)
        {
            const auto& t = all[i];
            if ((t.kinds & kind) == 0)
                continue;
            auto hay = text (t.name + " " + t.plain + " " + t.family + " " + t.note);
            for (const auto& name : t.from)
                if (const auto* o = mixer::findOrigin (name))
                    hay += text (" " + o->name + " " + o->region);
            haystack.push_back ({ (int) i, hay.toCaseFolded() });
        }
        auto refill = [&]
        {
            list->clear();
            const auto words = search->text().toCaseFolded().split (QLatin1Char (' '), Qt::SkipEmptyParts);
            for (const auto& [i, hay] : haystack)
                if (std::all_of (words.begin(), words.end(), [&hay] (const QString& w) { return hay.contains (w); }))
                {
                    auto* item = new QListWidgetItem (where (i), list);
                    item->setData (Qt::UserRole, i);
                    item->setToolTip (tipOf (all[(size_t) i]));
                }
            count->setText (QObject::tr ("%n found", nullptr, list->count()));
            if (list->count() > 0)
                list->setCurrentRow (0);
        };
        QObject::connect (search, &QLineEdit::textChanged, &dialog, refill);
        QObject::connect (search, &QLineEdit::returnPressed, &dialog, &QDialog::accept);
        QObject::connect (list, &QListWidget::itemActivated, &dialog, &QDialog::accept);
        QObject::connect (buttons, &QDialogButtonBox::accepted, &dialog, &QDialog::accept);
        QObject::connect (buttons, &QDialogButtonBox::rejected, &dialog, &QDialog::reject);
        refill();
        search->setFocus();
        if (dialog.exec() != QDialog::Accepted || list->currentItem() == nullptr)
            return -1;
        return list->currentItem()->data (Qt::UserRole).toInt();
    }
}

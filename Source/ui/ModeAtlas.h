// SPDX-License-Identifier: AGPL-3.0-only
#pragma once

// The atlas of modes and tunings (owner, 2026-10-27), as a menu and as a search: by region,
// then the traditions (nations, cultures, empires) and the people of that region in order of
// date, then their systems. The Mode menu's "melody" and "tuning" submenus are it, and so is the
// Inspector's tuning browser, so a mode is found the same way everywhere (mixer/Tunings.h).
#include <QString>

#include <functional>

class QMenu;
class QWidget;

namespace modeatlas
{
    // Fills `menu` with the regions holding systems of `kind` (mixer::melodic, mixer::tuning or
    // both): each region's traditions and people, filled when first opened. `checked` (an index
    // into mixer::tuningSystems(), or -1) is ticked and its region and origin shown in bold.
    // `pick` gets the index chosen. With `kind` tuning, an "equal divisions" submenu follows.
    void fill (QMenu* menu, unsigned kind, int checked, std::function<void (int)> pick);

    // A search over every system of `kind`: its name, plain name, family, people and cultures,
    // and its note. The index chosen, or -1.
    int find (QWidget* parent, unsigned kind, const QString& title);

    // "partch 43-tone just intonation — Harry Partch (1901–1974)": the name and where it is from.
    QString where (int system);
}

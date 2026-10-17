// SPDX-License-Identifier: AGPL-3.0-only
#pragma once

#include <QKeySequence>
#include <QMap>
#include <QString>

// Key combinations the desktop keeps for itself. Under Hyprland (Omarchy) these
// come from `hyprctl binds -j`; a key bound there never reaches PHINYX.
namespace DesktopShortcuts
{
    // Key combination -> what the desktop does with it. Empty outside Hyprland.
    // Read once and cached.
    const QMap<QKeySequence, QString>& taken();

    // "Hyprland: Close all windows", or empty when the desktop does not use it.
    QString owner (const QKeySequence&);
}

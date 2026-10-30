// SPDX-License-Identifier: AGPL-3.0-only
#pragma once

#include "../engine/X11Windows.h"

#include <QTimer>
#include <QWidget>

// An instrument's or effect's own editor in a window of its own (the Synth and rāga
// windows, the knobs of a built-in effect, the parameter and Φ windows, a popped-out
// Prism), as REAPER's FX windows are. Marked so that Ctrl+F, like Super+F, takes it to
// the full screen and back to its floating size (MainWindow::navigationKey); on Hyprland
// it floats at its own size, centred, rather than tiling. Call once its title is set.
namespace editorwindow
{
    inline void mark (QWidget* w)
    {
        w->setProperty ("phiEditor", true);
        QTimer::singleShot (400, w, [w]
        {
            if (w->isVisible() && ! w->isFullScreen())
                x11::floatWindow (w->windowTitle().toStdString(), w->width(), w->height());
        });
    }

    inline bool is (const QWidget* w) { return w != nullptr && w->property ("phiEditor").toBool(); }
}

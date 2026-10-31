// SPDX-License-Identifier: AGPL-3.0-only
#pragma once

// Interface Size (owner, 2026-10-31, as Ableton Live's Zoom): the whole of PHI larger or
// smaller, the text, the controls, the meters and every window alike. Qt scales it all by
// QT_SCALE_FACTOR, which it reads only as it starts, so a new size takes a restart. The
// smallest, 70%, is about as dense as Ableton Live at its 100%; it goes no smaller.
#include "Settings.h"

#include <QByteArray>
#include <QList>

#include <algorithm>

namespace interfacesize
{
    inline const QList<int>& steps() { static const QList<int> s { 70, 75, 80, 90, 100, 110, 125, 150, 175, 200 }; return s; }

    inline int saved()
    {
        const int p = appSettings().value (QStringLiteral ("view/interfaceSize"), 100).toInt();
        return steps().contains (p) ? p : 100;
    }

    inline void save (int percent) { appSettings().setValue (QStringLiteral ("view/interfaceSize"), percent); }

    // The step `by` steps from `percent`, kept within the list.
    inline int step (int percent, int by)
    {
        const auto& s = steps();
        const int at = std::max (0, (int) s.indexOf (percent));
        return s[std::clamp (at + by, 0, (int) s.size() - 1)];
    }

    // Before the QApplication: the saved size on top of any QT_SCALE_FACTOR the user set
    // themselves. The user's own is kept aside in PHI_BASE_SCALE_FACTOR, so a restart (which
    // inherits this environment) does not scale twice.
    inline void applyBeforeStart()
    {
        QByteArray base = qgetenv ("PHI_BASE_SCALE_FACTOR");
        if (base.isEmpty())
        {
            base = qgetenv ("QT_SCALE_FACTOR");
            if (base.isEmpty())
                base = "1";
            qputenv ("PHI_BASE_SCALE_FACTOR", base);
        }
        bool ok = false;
        double factor = base.toDouble (&ok);
        if (! ok || factor <= 0.0)
            factor = 1.0;
        factor *= saved() / 100.0;
        if (factor == 1.0)
            qunsetenv ("QT_SCALE_FACTOR");
        else
            qputenv ("QT_SCALE_FACTOR", QByteArray::number (factor, 'g', 4));
    }
}

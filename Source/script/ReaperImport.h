// SPDX-License-Identifier: AGPL-3.0-only
#pragma once

#include <QList>
#include <QString>
#include <QStringList>

// Tools > Scripts > Import Reaper Script (docs/scripting.md): a ReaScript in
// Lua or Python rewritten for PHI. Each reaper.* call (RPR_* in Python) that
// PHI can match becomes phi calls, either in place or through a short helper
// written at the top of the script; one PHI cannot match becomes a call that
// stops the script with "not supported in PHI: reaper.X (what it needs)".
// A script PHI cannot run at all (EEL, a script that draws its own window,
// or one whose every call needs what PHI does not have) is refused with the
// reasons. Written from Reaper's public API documentation; no code of Reaper's.
namespace reaperimport
{
    struct Note
    {
        int line = 0;          // 1-based, in the original script
        QString function;      // as the script wrote it, e.g. "reaper.GetCursorPosition"
        QString what;          // what it became, or what PHI lacks for it
    };

    struct Result
    {
        bool ok = false;           // false: refused, see `reasons`
        QStringList reasons;       // why it cannot be converted
        QString code;              // the converted script
        QString suffix;            // "lua" or "py"
        QList<Note> converted;
        QList<Note> unsupported;

        QString report() const;    // the notes as text, for the editor's output
    };

    Result convert (const QString& code, const QString& fileName);
}

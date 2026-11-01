// SPDX-License-Identifier: AGPL-3.0-only
#pragma once

#include <QString>
#include <QVariant>

#include <functional>
#include <stdexcept>
#include <vector>

// Tools > Scripts (docs/scripting.md): scripts in Lua (.lua, built in) and
// Python (.py, the computer's own Python 3) that work PHI through one API,
// the `phi` module. The ideas come from Reaper's ReaScript (scripts as
// actions, an API for the file, the mixer and every command); the API and
// its names are PHI's own.
//
// Every function is described here once; the Lua bindings, the Python module
// and the editor's API reference are all made from this list, and every call
// lands in one Dispatch, so a function added here works in both languages.
namespace script
{
    struct Function
    {
        const char* name;        // phi.<name>
        const char* signature;   // the arguments as shown in the reference, e.g. "channel, start, count"
        const char* help;        // one or two sentences
        bool multiple = false;   // a list result comes back as several values in Lua (a tuple in Python)
        const char* group = "";  // the reference's heading
    };

    // Every function of the phi module, in the reference's order.
    const std::vector<Function>& api();
    const Function* find (const QString& name);

    // What a script sees when a call fails: the message, raised in the script as an error.
    struct Error : std::runtime_error
    {
        explicit Error (const QString& message) : std::runtime_error (message.toStdString()) {}
    };

    // Runs one function of the API. Arguments and results are null, bool, numbers,
    // strings, lists (QVariantList) and tables (QVariantMap). Throws Error.
    using Dispatch = std::function<QVariant (const QString& name, const QVariantList& args)>;

    enum class Language { lua, python, nyquist, unknown };
    Language languageFor (const QString& path);   // by the file's extension
    QString extensionFor (Language);              // "lua", "py", "ny"
    QString languageName (Language);              // "Lua", "Python"...

    // The Python module (phi.py) PHI hands to Python scripts: one function per entry of api().
    QString pythonModule();
}

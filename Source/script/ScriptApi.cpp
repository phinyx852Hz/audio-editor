// SPDX-License-Identifier: AGPL-3.0-only
#include "ScriptApi.h"

#include <QFileInfo>
#include <QStringList>

namespace script
{
    const std::vector<Function>& api()
    {
        // Times are in seconds, sample positions in frames; channels, tracks and
        // marker lists count from 0, as PHI's own windows number them from 1.
        static const std::vector<Function> all {
            { "print", "...", "Writes its arguments to the script's output, separated by spaces (print does the same).", false, "Output and questions" },
            { "message", "text [, title]", "Shows a message and waits for OK.", false, "Output and questions" },
            { "ask", "prompt [, default]", "Asks for a line of text; nil (None) when cancelled.", false, "Output and questions" },
            { "ask_number", "prompt [, default, min, max, decimals]", "Asks for a number; nil (None) when cancelled.", false, "Output and questions" },
            { "confirm", "question", "Asks a Yes or No question; true for Yes.", false, "Output and questions" },
            { "choose", "prompt, options [, default]", "Offers a list of options; the one chosen, or nil (None) when cancelled.", false, "Output and questions" },
            { "wait", "seconds", "Waits, with PHI working on (playback, meters, the Stop button).", false, "Output and questions" },
            { "version", "", "PHI's version, such as \"0.5.0\".", false, "Output and questions" },

            { "is_open", "", "Whether a file is open in the editor.", false, "The file" },
            { "path", "", "The open file's full path; empty for an untitled one.", false, "The file" },
            { "name", "", "The open file's name, as on its tab.", false, "The file" },
            { "sample_rate", "", "Frames per second.", false, "The file" },
            { "channels", "", "How many channels the file has.", false, "The file" },
            { "frames", "", "The file's length in frames.", false, "The file" },
            { "duration", "", "The file's length in seconds.", false, "The file" },
            { "to_frames", "seconds", "Seconds to a frame position in the open file (rounded).", false, "The file" },
            { "to_seconds", "frames", "A frame position to seconds.", false, "The file" },
            { "open", "path", "Opens a file in a new tab (or this one when it is empty) and waits for it; true when it opened.", false, "The file" },
            { "save", "", "Saves the open file where it is; false for an untitled file or when it fails.", false, "The file" },
            { "new_file", "seconds [, channels, sample_rate]", "A new untitled tab of silence (2 channels and 48000 Hz unless given).", false, "The file" },
            { "tab_count", "", "How many editor tabs there are.", false, "The file" },
            { "tab", "", "Which tab is in front (0 is the first).", false, "The file" },
            { "set_tab", "index", "Brings a tab to the front.", false, "The file" },

            { "cursor", "", "Where the cursor is, in seconds.", false, "Cursor and selection" },
            { "set_cursor", "seconds", "Moves the cursor (and clears the selection).", false, "Cursor and selection" },
            { "selection", "", "The selection's start and end in seconds; both equal the cursor when nothing is selected.", true, "Cursor and selection" },
            { "has_selection", "", "Whether anything is selected.", false, "Cursor and selection" },
            { "set_selection", "start, end", "Selects from start to end (seconds).", false, "Cursor and selection" },

            { "get_samples", "channel, start, count", "count samples of one channel from frame start, as a list of numbers (-1 to 1 is full scale).", false, "Samples" },
            { "set_samples", "channel, start, values", "Writes a list of samples into one channel from frame start, replacing what is there.", false, "Samples" },
            { "peak", "[start, end]", "The highest absolute sample over a range in seconds (the whole file when not given), 1.0 being full scale.", false, "Samples" },

            { "markers", "", "Every marker and region: a list of tables with id, name, start, finish (seconds) and region.", false, "Markers" },
            { "add_marker", "seconds [, name]", "Adds a marker; its id.", false, "Markers" },
            { "add_region", "start, end [, name]", "Adds a region; its id.", false, "Markers" },
            { "rename_marker", "id, name", "Renames a marker or region.", false, "Markers" },
            { "move_marker", "id, start [, end]", "Moves a marker, or a region's start and end.", false, "Markers" },
            { "delete_marker", "id", "Deletes a marker or region.", false, "Markers" },

            { "tag", "key", "A metadata tag (title, artist, album, genre, date, comment, bpm, key...); empty when not set.", false, "Tags" },
            { "set_tag", "key, value", "Sets a metadata tag; an empty value removes it.", false, "Tags" },

            { "commands", "[filter]", "Every command PHI has (the ones in its menus): tables with id, name, category and shortcut, those whose name or id contains filter when given.", false, "Commands" },
            { "run", "id", "Runs a command by its id, as if chosen from its menu, and waits for it; false when it cannot run now. Windows it opens appear as usual.", false, "Commands" },
            { "run_macro", "name", "Runs a saved macro (Tools > Macros); false when there is none of that name.", false, "Commands" },

            { "undo", "", "Undoes the last change.", false, "Undo" },
            { "redo", "", "Redoes the change last undone.", false, "Undo" },
            { "set_undo_name", "text", "What Edit > Undo calls this script's changes (they are one undo step).", false, "Undo" },

            { "play", "", "Plays the file from the cursor (or the selection).", false, "Playback" },
            { "stop", "", "Stops playback.", false, "Playback" },
            { "is_playing", "", "Whether the file is playing.", false, "Playback" },

            { "track_count", "", "How many tracks the mixer has.", false, "Mixer" },
            { "track", "index", "One mixer track: a table with name, volume (dB), pan (-1 to 1), trim (dB), mute and solo.", false, "Mixer" },
            { "set_track", "index, settings", "Sets a mixer track's volume, pan, trim, mute and solo from a table holding any of them.", false, "Mixer" },
        };
        return all;
    }

    const Function* find (const QString& name)
    {
        for (const auto& f : api())
            if (name == QLatin1String (f.name))
                return &f;
        return nullptr;
    }

    Language languageFor (const QString& path)
    {
        const auto suffix = QFileInfo (path).suffix().toLower();
        if (suffix == QLatin1String ("lua"))
            return Language::lua;
        if (suffix == QLatin1String ("py"))
            return Language::python;
        if (suffix == QLatin1String ("ny"))
            return Language::nyquist;
        return Language::unknown;
    }

    QString extensionFor (Language l)
    {
        switch (l)
        {
            case Language::lua:     return QStringLiteral ("lua");
            case Language::python:  return QStringLiteral ("py");
            case Language::nyquist: return QStringLiteral ("ny");
            case Language::unknown: break;
        }
        return {};
    }

    QString languageName (Language l)
    {
        switch (l)
        {
            case Language::lua:     return QStringLiteral ("Lua");
            case Language::python:  return QStringLiteral ("Python");
            case Language::nyquist: return QStringLiteral ("Nyquist");
            case Language::unknown: break;
        }
        return {};
    }

    QString pythonModule()
    {
        // The script's stdout carries its calls to PHI and PHI's replies come back on
        // its stdin, a JSON object a line; print() and sys.stdout are sent as output
        // messages on the same line, so nothing the script prints is taken for a call.
        QString py = QStringLiteral (R"PY(# phi.py: PHI's scripting API for Python (Tools > Scripts, docs/scripting.md).
# PHI writes this file again before each run, so changes to it are lost.
import json as _json
import io as _io
import sys as _sys

class PhiError(Exception):
    """A call PHI could not do: the message says why."""

_calls = _sys.stdout
_replies = _sys.stdin

def _send(message):
    _calls.write(_json.dumps(message) + "\n")
    _calls.flush()

class _Output(_io.TextIOBase):
    def writable(self):
        return True
    def write(self, text):
        if text:
            _send({"print": text})
        return len(text)

_sys.stdout = _Output()
_sys.stdin = _io.StringIO("")

def _call(name, *args):
    _send({"call": name, "args": list(args)})
    line = _replies.readline()
    if not line:
        raise SystemExit(1)
    reply = _json.loads(line)
    if "error" in reply:
        raise PhiError(reply["error"])
    return reply.get("result")

)PY");
        for (const auto& f : api())
        {
            const QString name = QString::fromLatin1 (f.name);
            const QString help = QString::fromUtf8 (f.help).replace (QStringLiteral ("nil (None)"), QStringLiteral ("None"))
                                                         .replace (QLatin1Char ('\\'), QStringLiteral ("\\\\")).replace (QLatin1Char ('"'), QStringLiteral ("\\\""));
            if (name == QLatin1String ("print"))
            {
                py += QStringLiteral ("def print(*args):\n    \"\"\"%1\"\"\"\n    _call(\"print\", *[str(a) for a in args])\n\n").arg (help);
                continue;
            }
            py += QStringLiteral ("def %1(*args):\n    \"\"\"%1(%2): %3\"\"\"\n").arg (name, QString::fromLatin1 (f.signature), help);
            py += f.multiple ? QStringLiteral ("    return tuple(_call(\"%1\", *args))\n\n").arg (name)
                             : QStringLiteral ("    return _call(\"%1\", *args)\n\n").arg (name);
        }
        return py;
    }
}

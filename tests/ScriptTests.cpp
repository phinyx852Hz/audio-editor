// SPDX-License-Identifier: AGPL-3.0-only
// Tools > Scripts: the Lua bridge and the Python bridge, against a dispatch of
// their own (no editor). Python's part is skipped when there is no python3.
#include "../Source/script/ReaperImport.h"
#include "../Source/script/ScriptRunner.h"

#include <QCoreApplication>
#include <QTimer>

#include <cstdio>
#include <cstdlib>

namespace
{
    int failures = 0;

    void check (bool ok, const char* what)
    {
        std::printf ("%s %s\n", ok ? "ok  " : "FAIL", what);
        if (! ok)
            ++failures;
    }

    struct Fake
    {
        QVariantList samples { 0.0, 0.25, -0.5, 1.0 };
        QVariantMap lastTrack;
        QStringList calls;
        QList<QVariantList> added;
        QString undoName;

        QVariant operator() (const QString& name, const QVariantList& args)
        {
            calls << name;
            if (name == QLatin1String ("frames"))
                return (qlonglong) samples.size();
            if (name == QLatin1String ("get_samples"))
                return samples.mid (args.value (1).toInt(), args.value (2).toInt());
            if (name == QLatin1String ("set_samples"))
            {
                const auto values = args.value (2).toList();
                for (qsizetype i = 0; i < values.size(); ++i)
                    samples[args.value (1).toInt() + i] = values[i];
                return {};
            }
            if (name == QLatin1String ("selection"))
                return QVariantList { 1.5, 2.5 };
            if (name == QLatin1String ("markers"))
                return QVariantList { QVariantMap { { "id", 1 }, { "name", "Intro" }, { "start", 0.0 }, { "finish", 0.0 }, { "region", false } } };
            if (name == QLatin1String ("set_track"))
            {
                lastTrack = args.value (1).toMap();
                return {};
            }
            if (name == QLatin1String ("ask"))
                return {};   // cancelled
            if (name == QLatin1String ("cursor"))
                return 12.5;
            if (name == QLatin1String ("add_marker") || name == QLatin1String ("add_region"))
            {
                added << args;
                return 7;
            }
            if (name == QLatin1String ("set_undo_name"))
            {
                undoName = args.value (0).toString();
                return {};
            }
            if (name == QLatin1String ("track"))
                return QVariantMap { { "name", "Drums" }, { "volume", 0.0 }, { "pan", 0.0 }, { "trim", 0.0 }, { "mute", false }, { "solo", false } };
            if (name == QLatin1String ("run"))
                throw script::Error (QStringLiteral ("no command \"%1\"").arg (args.value (0).toString()));
            return {};
        }
    };

    struct Run
    {
        bool ok = false;
        QString out, err;
    };

    Run run (ScriptRunner& runner, script::Language language, const QString& code, Fake& fake)
    {
        Run r;
        auto c = QObject::connect (&runner, &ScriptRunner::output, [&r] (const QString& text, bool error) { (error ? r.err : r.out) += text; });
        r.ok = runner.run (language, code, QStringLiteral ("test"), {}, [&fake] (const QString& n, const QVariantList& a) { return fake (n, a); });
        QObject::disconnect (c);
        return r;
    }
}

int main (int argc, char** argv)
{
    QCoreApplication app (argc, argv);
    ScriptRunner runner;

    // Lua: values both ways, lists and tables, several results, errors.
    {
        Fake fake;
        const auto r = run (runner, script::Language::lua, QStringLiteral (R"(
local n = phi.frames()
local s = phi.get_samples(0, 0, n)
for i = 1, #s do s[i] = s[i] * 0.5 end
phi.set_samples(0, 0, s)
local a, b = phi.selection()
print(math.type(n), #s, a, b)
local m = phi.markers()[1]
print(m.name, m.region)
phi.set_track(2, { volume = -6.5, mute = true })
print(phi.ask("name?") == nil)
)"), fake);
        check (r.ok, "lua: runs");
        check (fake.samples.value (1).toDouble() == 0.125 && fake.samples.value (3).toDouble() == 0.5, "lua: samples read, changed and written back");
        check (r.out.contains (QStringLiteral ("integer\t4\t1.5\t2.5")), "lua: integers, list length and two results");
        check (r.out.contains (QStringLiteral ("Intro\tfalse")), "lua: a list of tables");
        check (fake.lastTrack.value (QStringLiteral ("volume")).toDouble() == -6.5 && fake.lastTrack.value (QStringLiteral ("mute")).toBool(), "lua: a table to the dispatch");
        check (r.out.contains (QStringLiteral ("true")), "lua: nil back for a cancelled question");
    }
    {
        Fake fake;
        const auto r = run (runner, script::Language::lua, QStringLiteral ("phi.run('nothing.here')"), fake);
        check (! r.ok && r.err.contains (QStringLiteral ("no command \"nothing.here\"")) && r.err.contains (QStringLiteral ("test:1")), "lua: a failed call is an error with its line");
    }
    {
        Fake fake;
        const auto r = run (runner, script::Language::lua, QStringLiteral ("local ok, e = pcall(phi.run, 'x') print(ok, e ~= nil)"), fake);
        check (r.ok && r.out.contains (QStringLiteral ("false\ttrue")), "lua: pcall catches a failed call");
    }
    {
        Fake fake;
        const auto r = run (runner, script::Language::lua, QStringLiteral ("this is not lua"), fake);
        check (! r.ok && ! r.err.isEmpty(), "lua: a syntax error is reported");
    }
    {
        // A runaway loop ends with Stop.
        Fake fake;
        QTimer::singleShot (200, &runner, [&runner] { runner.stop(); });
        const auto r = run (runner, script::Language::lua, QStringLiteral ("while true do end"), fake);
        check (! r.ok && runner.wasStopped() && r.err.contains (QStringLiteral ("Stopped")), "lua: Stop ends a runaway loop");
    }
    {
        Fake fake;
        const auto r = run (runner, script::Language::lua, QStringLiteral ("phi.no_such_function()"), fake);
        check (! r.ok, "lua: an unknown function is an error");
    }

    // Python, when there is one.
    if (ScriptRunner::pythonExecutable().isEmpty())
        std::printf ("skip python: no python3\n");
    else
    {
        {
            Fake fake;
            const auto r = run (runner, script::Language::python, QStringLiteral (R"(
n = phi.frames()
s = phi.get_samples(0, 0, n)
phi.set_samples(0, 0, [x * 0.5 for x in s])
a, b = phi.selection()
print(type(n).__name__, len(s), a, b)
print(phi.markers()[0]["name"])
phi.set_track(2, {"volume": -6.5, "mute": True})
print(phi.ask("name?") is None)
)"), fake);
            check (r.ok, "python: runs");
            if (! r.ok)
                std::printf ("%s\n", qPrintable (r.err));
            check (fake.samples.value (1).toDouble() == 0.125 && fake.samples.value (3).toDouble() == 0.5, "python: samples read, changed and written back");
            check (r.out.contains (QStringLiteral ("int 4 1.5 2.5")), "python: numbers, lists and a tuple");
            check (r.out.contains (QStringLiteral ("Intro")), "python: a list of dicts");
            check (fake.lastTrack.value (QStringLiteral ("volume")).toDouble() == -6.5 && fake.lastTrack.value (QStringLiteral ("mute")).toBool(), "python: a dict to the dispatch");
            check (r.out.contains (QStringLiteral ("True")), "python: None back for a cancelled question");
        }
        {
            Fake fake;
            const auto r = run (runner, script::Language::python, QStringLiteral ("try:\n    phi.run('x')\nexcept phi.PhiError as e:\n    print('caught', e)\nphi.run('y')\n"), fake);
            check (! r.ok && r.out.contains (QStringLiteral ("caught no command \"x\"")) && r.err.contains (QStringLiteral ("PhiError")), "python: a failed call raises PhiError");
        }
        {
            Fake fake;
            QTimer::singleShot (500, &runner, [&runner] { runner.stop(); });
            const auto r = run (runner, script::Language::python, QStringLiteral ("while True:\n    pass\n"), fake);
            check (! r.ok && runner.wasStopped(), "python: Stop ends a runaway loop");
        }
    }


    // Import Reaper Script: a ReaScript converted, then run against the fake.
    {
        const auto lua = QStringLiteral (R"(-- a Reaper script
local pos = reaper.GetCursorPosition()
reaper.Undo_BeginBlock()
local s, e = reaper.GetSet_LoopTimeRange(false, false, 0, 0, false)
reaper.AddProjectMarker(0, false, pos, 0, "Here", -1)
reaper.AddProjectMarker2(0, true, s, e, "Verse", -1, 0)
reaper.ShowConsoleMsg("cursor " .. pos .. " sel " .. s .. "-" .. e .. "\n")
local tr = reaper.GetTrack(0, 0)
local ok, name = reaper.GetTrackName(tr)
reaper.SetMediaTrackInfo_Value(tr, "D_VOL", 0.5)
reaper.ShowConsoleMsg(name .. "\n")
if reaper.CountSelectedMediaItems(0) > 99 then reaper.ShowConsoleMsg("never") end
reaper.Undo_EndBlock("Mark it", -1)
reaper.UpdateArrange()
)");
        const auto r = reaperimport::convert (lua, QStringLiteral ("Mark.lua"));
        check (r.ok && r.suffix == QLatin1String ("lua"), "reaper: a Lua script converts");
        check (r.unsupported.size() == 1 && r.unsupported[0].line == 12 && r.unsupported[0].function == QLatin1String ("reaper.CountSelectedMediaItems"),
               "reaper: the call PHI lacks is named with its line");
        check (r.code.contains (QStringLiteral ("local pos = phi.cursor()")) && ! r.code.contains (QStringLiteral ("reaper.GetCursorPosition()\n")),
               "reaper: a simple call becomes a phi call in place");
        Fake fake;
        const auto run1 = run (runner, script::Language::lua, r.code, fake);
        check (! run1.ok && run1.err.contains (QStringLiteral ("not supported in PHI: reaper.CountSelectedMediaItems")),
               "reaper: the converted script runs until the call PHI lacks, and says so");
        check (run1.out.contains (QStringLiteral ("cursor 12.5 sel 1.5-2.5")) && run1.out.contains (QStringLiteral ("Drums")),
               "reaper: cursor, selection, console and track name work");
        check (fake.added.size() == 2 && fake.added[0].value (1).toString() == QLatin1String ("Here") && fake.added[1].value (2).toString() == QLatin1String ("Verse"),
               "reaper: a marker and a region added");
        check (fake.lastTrack.value (QStringLiteral ("volume")).toDouble() < -6.0 && fake.lastTrack.value (QStringLiteral ("volume")).toDouble() > -6.1,
               "reaper: D_VOL 0.5 becomes -6 dB");
    }
    {
        const auto lua = QStringLiteral ("reaper.Undo_BeginBlock()\nreaper.Main_OnCommand(40157, 0)\nreaper.Undo_EndBlock('Add one', -1)\n");
        const auto r = reaperimport::convert (lua, QStringLiteral ("one.lua"));
        Fake fake;
        const auto out = run (runner, script::Language::lua, r.code, fake);
        check (r.ok && out.ok && fake.added.size() == 1 && fake.undoName == QLatin1String ("Add one"), "reaper: a known action and the undo name");
    }
    check (! reaperimport::convert (QStringLiteral ("a = 1;"), QStringLiteral ("x.eel")).ok, "reaper: EEL is refused");
    {
        const auto r = reaperimport::convert (QStringLiteral ("gfx.init('x', 200, 100)\nlocal p = reaper.GetCursorPosition()\n"), QStringLiteral ("w.lua"));
        check (! r.ok && r.report().contains (QStringLiteral ("window")), "reaper: a script with its own window is refused");
    }
    {
        const auto r = reaperimport::convert (QStringLiteral ("local it = reaper.GetSelectedMediaItem(0, 0)\nreaper.SetMediaItemInfo_Value(it, 'D_VOL', 2)\n"), QStringLiteral ("items.lua"));
        check (! r.ok && r.report().contains (QStringLiteral ("media items")), "reaper: a script whose every call PHI lacks is refused, saying what it needs");
    }
    {
        const auto r = reaperimport::convert (QStringLiteral ("-- reaper.GetCursorPosition() in a comment\nlocal s = \"reaper.Foo()\"\nprint(reaper.GetCursorPosition())\n"), QStringLiteral ("c.lua"));
        check (r.ok && r.converted.size() == 1 && r.unsupported.isEmpty() && r.code.contains (QStringLiteral ("\"reaper.Foo()\"")), "reaper: comments and strings are left alone");
    }
    if (! ScriptRunner::pythonExecutable().isEmpty())
    {
        const auto py = QStringLiteral ("from reaper_python import *\n"
                                        "pos = RPR_GetCursorPosition()\n"
                                        "r = RPR_GetSet_LoopTimeRange(False, False, 0, 0, False)\n"
                                        "RPR_AddProjectMarker(0, False, pos, 0, \"Py\", -1)\n"
                                        "RPR_ShowConsoleMsg(\"py %s %s %s\\n\" % (pos, r[2], r[3]))\n"
                                        "RPR_Undo_EndBlock(\"Py mark\", -1)\n");
        const auto r = reaperimport::convert (py, QStringLiteral ("mark.py"));
        Fake fake;
        const auto out = run (runner, script::Language::python, r.code, fake);
        if (! out.ok)
            std::printf ("%s\n%s\n", qPrintable (r.code), qPrintable (out.err));
        check (r.ok && out.ok && out.out.contains (QStringLiteral ("py 12.5 1.5 2.5")) && fake.added.size() == 1 && fake.undoName == QLatin1String ("Py mark"),
               "reaper: a Python script converts and runs");
    }

    // The Python module has every function.
    const auto module = script::pythonModule();
    bool all = true;
    for (const auto& f : script::api())
        all = all && module.contains (QStringLiteral ("def %1(").arg (QString::fromLatin1 (f.name)));
    check (all, "python module: a def for every function");

    std::printf (failures == 0 ? "all script tests passed\n" : "%d script test(s) failed\n", failures);
    return failures == 0 ? EXIT_SUCCESS : EXIT_FAILURE;
}

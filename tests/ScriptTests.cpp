// SPDX-License-Identifier: AGPL-3.0-only
// Tools > Scripts: the Lua bridge and the Python bridge, against a dispatch of
// their own (no editor). Python's part is skipped when there is no python3.
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

    // The Python module has every function.
    const auto module = script::pythonModule();
    bool all = true;
    for (const auto& f : script::api())
        all = all && module.contains (QStringLiteral ("def %1(").arg (QString::fromLatin1 (f.name)));
    check (all, "python module: a def for every function");

    std::printf (failures == 0 ? "all script tests passed\n" : "%d script test(s) failed\n", failures);
    return failures == 0 ? EXIT_SUCCESS : EXIT_FAILURE;
}

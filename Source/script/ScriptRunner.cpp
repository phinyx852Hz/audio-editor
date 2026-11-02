// SPDX-License-Identifier: AGPL-3.0-only
#include "ScriptRunner.h"

#include <QCoreApplication>
#include <QDir>
#include <QEventLoop>
#include <QFile>
#include <QJsonDocument>
#include <QJsonObject>
#include <QJsonArray>
#include <QProcess>
#include <QProcessEnvironment>
#include <QStandardPaths>
#include <QTemporaryDir>
#include <QTimer>

// Lua is built as C++ (cmake/Lua.cmake), so its headers are included without
// extern "C" and its errors unwind as exceptions.
#include <lauxlib.h>
#include <lua.h>
#include <lualib.h>

#include <cmath>

namespace
{
    constexpr int maxDepth = 32;   // nested tables and lists, either way
}

// The Lua side: the phi table's functions and conversions between Lua values and QVariant.
struct LuaGlue
{
    static ScriptRunner* runner (lua_State* L)
    {
        return static_cast<ScriptRunner*> (lua_touserdata (L, lua_upvalueindex (1)));
    }

    static QVariant toVariant (lua_State* L, int index, int depth = 0)
    {
        index = lua_absindex (L, index);
        switch (lua_type (L, index))
        {
            case LUA_TBOOLEAN: return QVariant (lua_toboolean (L, index) != 0);
            case LUA_TNUMBER:
                return lua_isinteger (L, index) ? QVariant ((qlonglong) lua_tointeger (L, index)) : QVariant ((double) lua_tonumber (L, index));
            case LUA_TSTRING:
            {
                size_t length = 0;
                const auto* s = lua_tolstring (L, index, &length);
                return QString::fromUtf8 (s, (qsizetype) length);
            }
            case LUA_TTABLE:
            {
                if (depth >= maxDepth)
                    throw script::Error (QStringLiteral ("tables nested too deeply"));
                // A list when its keys are 1..n (or it is empty), else a table of named values.
                const auto n = (lua_Integer) lua_rawlen (L, index);
                lua_Integer keys = 0;
                lua_pushnil (L);
                while (lua_next (L, index) != 0)
                {
                    ++keys;
                    lua_pop (L, 1);
                }
                if (keys == n)
                {
                    QVariantList list;
                    list.reserve ((qsizetype) n);
                    for (lua_Integer i = 1; i <= n; ++i)
                    {
                        lua_rawgeti (L, index, i);
                        list << toVariant (L, -1, depth + 1);
                        lua_pop (L, 1);
                    }
                    return list;
                }
                QVariantMap map;
                lua_pushnil (L);
                while (lua_next (L, index) != 0)
                {
                    lua_pushvalue (L, -2);   // a copy of the key, as lua_tostring would change a number key in place
                    const QString key = QString::fromUtf8 (lua_tostring (L, -1));
                    lua_pop (L, 1);
                    map.insert (key, toVariant (L, -1, depth + 1));
                    lua_pop (L, 1);
                }
                return map;
            }
            default: return {};
        }
    }

    static void push (lua_State* L, const QVariant& v, int depth = 0)
    {
        if (depth >= maxDepth)
            throw script::Error (QStringLiteral ("values nested too deeply"));
        luaL_checkstack (L, 4, nullptr);
        switch (v.typeId())
        {
            case QMetaType::UnknownType:
            case QMetaType::Nullptr:
                lua_pushnil (L);
                return;
            case QMetaType::Bool:
                lua_pushboolean (L, v.toBool() ? 1 : 0);
                return;
            case QMetaType::Int:
            case QMetaType::UInt:
            case QMetaType::Long:
            case QMetaType::LongLong:
            case QMetaType::ULong:
            case QMetaType::ULongLong:
                lua_pushinteger (L, (lua_Integer) v.toLongLong());
                return;
            case QMetaType::Double:
            case QMetaType::Float:
                lua_pushnumber (L, v.toDouble());
                return;
            case QMetaType::QVariantList:
            case QMetaType::QStringList:
            {
                const auto list = v.toList();
                lua_createtable (L, (int) list.size(), 0);
                for (qsizetype i = 0; i < list.size(); ++i)
                {
                    push (L, list[i], depth + 1);
                    lua_rawseti (L, -2, (lua_Integer) i + 1);
                }
                return;
            }
            case QMetaType::QVariantMap:
            {
                const auto map = v.toMap();
                lua_createtable (L, 0, (int) map.size());
                for (auto it = map.begin(); it != map.end(); ++it)
                {
                    push (L, it.value(), depth + 1);
                    lua_setfield (L, -2, it.key().toUtf8().constData());
                }
                return;
            }
            default:
            {
                const auto bytes = v.toString().toUtf8();
                lua_pushlstring (L, bytes.constData(), (size_t) bytes.size());
                return;
            }
        }
    }

    // phi.<name>(...): upvalue 1 the runner, upvalue 2 the function's name.
    static int call (lua_State* L)
    {
        auto* r = runner (L);
        const QString name = QString::fromUtf8 (lua_tostring (L, lua_upvalueindex (2)));
        std::string error;
        int results = 0;
        try
        {
            QVariantList args;
            const auto n = lua_gettop (L);
            for (int i = 1; i <= n; ++i)
                args << toVariant (L, i);
            const auto result = r->call (name, args);
            const auto* f = script::find (name);
            if (f != nullptr && f->multiple && result.typeId() == QMetaType::QVariantList)
            {
                const auto list = result.toList();
                for (const auto& item : list)
                    push (L, item);
                results = (int) list.size();
            }
            else if (result.isValid())
            {
                push (L, result);
                results = 1;
            }
        }
        catch (const std::exception& e)
        {
            error = e.what();
        }
        if (! error.empty())
            return luaL_error (L, "phi.%s: %s", name.toUtf8().constData(), error.c_str());
        return results;
    }

    // print(...), as Lua's own but into the output.
    static int print (lua_State* L)
    {
        auto* r = runner (L);
        QStringList parts;
        const auto n = lua_gettop (L);
        for (int i = 1; i <= n; ++i)
            parts << QString::fromUtf8 (luaL_tolstring (L, i, nullptr));
        Q_EMIT r->output (parts.join (QLatin1Char ('\t')) + QLatin1Char ('\n'), false);
        return 0;
    }

    // Every so many instructions: the event loop turns, and a Stop ends the script.
    static void hook (lua_State* L, lua_Debug*)
    {
        lua_getfield (L, LUA_REGISTRYINDEX, "phi.runner");
        auto* r = static_cast<ScriptRunner*> (lua_touserdata (L, -1));
        lua_pop (L, 1);
        if (r == nullptr)
            return;
        std::string error;
        try
        {
            r->keepAlive();
        }
        catch (const std::exception& e)
        {
            error = e.what();
        }
        if (! error.empty())
            luaL_error (L, "%s", error.c_str());
    }

    static int traceback (lua_State* L)
    {
        const char* message = lua_tostring (L, 1);
        luaL_traceback (L, L, message != nullptr ? message : "(an error that is not text)", 1);
        return 1;
    }
};

ScriptRunner::ScriptRunner (QObject* parent) : QObject (parent) {}

ScriptRunner::~ScriptRunner()
{
    if (process != nullptr)
        process->kill();
}

QString ScriptRunner::pythonExecutable()
{
    auto found = QStandardPaths::findExecutable (QStringLiteral ("python3"));
    if (found.isEmpty())
        found = QStandardPaths::findExecutable (QStringLiteral ("python"));
    return found;
}

void ScriptRunner::stop()
{
    if (! running)
        return;
    stopRequested = true;
    if (process != nullptr)
        process->kill();
}

void ScriptRunner::keepAlive()
{
    if (sinceEvents.elapsed() >= 40)
    {
        QCoreApplication::processEvents (QEventLoop::AllEvents, 20);
        sinceEvents.restart();
    }
    if (stopRequested)
        throw script::Error (QStringLiteral ("stopped"));
}

QVariant ScriptRunner::call (const QString& name, const QVariantList& args)
{
    if (name == QLatin1String ("print"))
    {
        QStringList parts;
        for (const auto& a : args)
            parts << a.toString();
        Q_EMIT output (parts.join (QLatin1Char (' ')) + QLatin1Char ('\n'), false);
        return {};
    }
    if (name == QLatin1String ("wait"))
    {
        const auto seconds = args.isEmpty() ? 0.0 : args[0].toDouble();
        QElapsedTimer t;
        t.start();
        while (t.elapsed() < (qint64) (seconds * 1000.0))
        {
            if (stopRequested)
                throw script::Error (QStringLiteral ("stopped"));
            QEventLoop loop;
            QTimer::singleShot (std::clamp<qint64> ((qint64) (seconds * 1000.0) - t.elapsed(), 1, 20), &loop, &QEventLoop::quit);
            loop.exec();
        }
        if (stopRequested)
            throw script::Error (QStringLiteral ("stopped"));
        return {};
    }
    if (script::find (name) == nullptr)
        throw script::Error (QStringLiteral ("there is no phi.%1").arg (name));
    if (! dispatch)
        throw script::Error (QStringLiteral ("not available here"));
    return dispatch (name, args);
}

bool ScriptRunner::run (script::Language language, const QString& code, const QString& name, const QString& folder, const script::Dispatch& d)
{
    if (running)
    {
        Q_EMIT output (tr ("A script is already running; it has to end before another starts.\n"), true);
        return false;
    }
    dispatch = d;
    running = true;
    stopRequested = false;
    sinceEvents.start();
    Q_EMIT runningChanged (true);

    bool ok = false;
    if (language == script::Language::lua)
        ok = runLua (code, name, folder);
    else if (language == script::Language::python)
        ok = runPython (code, name, folder);
    else
        Q_EMIT output (tr ("PHI cannot run this kind of script here.\n"), true);

    running = false;
    dispatch = nullptr;
    Q_EMIT runningChanged (false);
    return ok;
}

bool ScriptRunner::runLua (const QString& code, const QString& name, const QString& folder)
{
    auto* L = luaL_newstate();
    if (L == nullptr)
    {
        Q_EMIT output (tr ("Lua could not start (out of memory).\n"), true);
        return false;
    }
    luaL_openlibs (L);

    lua_pushlightuserdata (L, this);
    lua_setfield (L, LUA_REGISTRYINDEX, "phi.runner");

    lua_pushlightuserdata (L, this);
    lua_pushcclosure (L, &LuaGlue::print, 1);
    lua_setglobal (L, "print");

    lua_createtable (L, 0, (int) script::api().size());
    for (const auto& f : script::api())
    {
        lua_pushlightuserdata (L, this);
        lua_pushstring (L, f.name);
        lua_pushcclosure (L, &LuaGlue::call, 2);
        lua_setfield (L, -2, f.name);
    }
    lua_setglobal (L, "phi");

    // require finds modules beside the script first.
    if (! folder.isEmpty())
    {
        lua_getglobal (L, "package");
        lua_getfield (L, -1, "path");
        const auto path = QDir (folder).filePath (QStringLiteral ("?.lua")).toUtf8() + ";" + lua_tostring (L, -1);
        lua_pop (L, 1);
        lua_pushstring (L, path.constData());
        lua_setfield (L, -2, "path");
        lua_pop (L, 1);
    }

    lua_sethook (L, &LuaGlue::hook, LUA_MASKCOUNT, 10000);

    bool ok = false;
    lua_pushcfunction (L, &LuaGlue::traceback);
    const auto bytes = code.toUtf8();
    const auto chunk = QByteArray ("@") + name.toUtf8();
    auto status = luaL_loadbufferx (L, bytes.constData(), (size_t) bytes.size(), chunk.constData(), "t");
    if (status == LUA_OK)
        status = lua_pcall (L, 0, 0, -2);
    if (status == LUA_OK)
        ok = true;
    else
    {
        const char* message = lua_tostring (L, -1);
        QString text = QString::fromUtf8 (message != nullptr ? message : "an error");
        if (stopRequested)
            text = tr ("Stopped.");
        Q_EMIT output (text + QLatin1Char ('\n'), true);
    }
    lua_close (L);
    return ok;
}

bool ScriptRunner::runPython (const QString& code, const QString& name, const QString& folder)
{
    const auto python = pythonExecutable();
    if (python.isEmpty())
    {
        Q_EMIT output (tr ("Python scripts need Python 3, and none was found. Install it from your distribution (the python or python3 package).\n"), true);
        return false;
    }

    // phi.py and the script go in a folder of their own; the script runs from its folder.
    QTemporaryDir temp;
    if (! temp.isValid())
    {
        Q_EMIT output (tr ("Could not make a temporary folder for the script.\n"), true);
        return false;
    }
    const auto write = [] (const QString& path, const QString& text)
    {
        QFile f (path);
        return f.open (QIODevice::WriteOnly | QIODevice::Truncate) && f.write (text.toUtf8()) >= 0;
    };
    const auto scriptPath = temp.filePath (name.endsWith (QLatin1String (".py")) ? name : name + QStringLiteral (".py"));
    if (! write (temp.filePath (QStringLiteral ("phi.py")), script::pythonModule()) || ! write (scriptPath, code))
    {
        Q_EMIT output (tr ("Could not write the script for Python.\n"), true);
        return false;
    }

    QProcess p;
    process = &p;
    auto env = QProcessEnvironment::systemEnvironment();
    const auto existing = env.value (QStringLiteral ("PYTHONPATH"));
    env.insert (QStringLiteral ("PYTHONPATH"), existing.isEmpty() ? temp.path() : temp.path() + QLatin1Char (':') + existing);
    env.insert (QStringLiteral ("PYTHONIOENCODING"), QStringLiteral ("utf-8"));
    env.insert (QStringLiteral ("PYTHONUNBUFFERED"), QStringLiteral ("1"));
    p.setProcessEnvironment (env);
    if (! folder.isEmpty() && QDir (folder).exists())
        p.setWorkingDirectory (folder);

    // The script runs as __main__ with phi already imported, as it is in Lua.
    const auto boot = QStringLiteral ("import phi, runpy, sys; sys.argv = [sys.argv[1]]; "
                                      "runpy.run_path(sys.argv[0], init_globals={'phi': phi}, run_name='__main__')");

    QByteArray pending;
    bool busy = false;
    QString protocolError;
    const auto handleLines = [&]
    {
        if (busy)
            return;   // a call is being answered; its loop reads on afterwards
        busy = true;
        pending += p.readAllStandardOutput();
        for (qsizetype newline; (newline = pending.indexOf ('\n')) >= 0;)
        {
            const auto line = pending.left (newline);
            pending.remove (0, newline + 1);
            const auto message = QJsonDocument::fromJson (line).object();
            if (message.contains (QLatin1String ("print")))
            {
                Q_EMIT output (message.value (QLatin1String ("print")).toString(), false);
                continue;
            }
            if (! message.contains (QLatin1String ("call")))
            {
                Q_EMIT output (QString::fromUtf8 (line) + QLatin1Char ('\n'), false);   // something that went round phi.py
                continue;
            }
            QJsonObject reply;
            try
            {
                const auto result = call (message.value (QLatin1String ("call")).toString(), message.value (QLatin1String ("args")).toArray().toVariantList());
                reply.insert (QStringLiteral ("result"), QJsonValue::fromVariant (result));
            }
            catch (const std::exception& e)
            {
                reply.insert (QStringLiteral ("error"), QString::fromUtf8 (e.what()));
            }
            if (p.state() == QProcess::Running)
                p.write (QJsonDocument (reply).toJson (QJsonDocument::Compact) + "\n");
            pending += p.readAllStandardOutput();
        }
        busy = false;
    };

    QEventLoop loop;
    connect (&p, &QProcess::readyReadStandardOutput, &loop, handleLines);
    connect (&p, &QProcess::readyReadStandardError, &loop, [&]
    {
        Q_EMIT output (QString::fromUtf8 (p.readAllStandardError()), true);
    });
    connect (&p, &QProcess::finished, &loop, &QEventLoop::quit);
    connect (&p, &QProcess::errorOccurred, &loop, [&] (QProcess::ProcessError e)
    {
        if (e == QProcess::FailedToStart)
        {
            protocolError = tr ("Python did not start: %1").arg (p.errorString());
            loop.quit();
        }
    });

    p.start (python, { QStringLiteral ("-u"), QStringLiteral ("-c"), boot, scriptPath });
    if (p.state() != QProcess::NotRunning || p.error() != QProcess::FailedToStart)
        loop.exec();

    // What is left once it has ended.
    handleLines();
    const auto rest = p.readAllStandardError();
    if (! rest.isEmpty())
        Q_EMIT output (QString::fromUtf8 (rest), true);
    process = nullptr;

    if (! protocolError.isEmpty())
    {
        Q_EMIT output (protocolError + QLatin1Char ('\n'), true);
        return false;
    }
    if (stopRequested)
    {
        Q_EMIT output (tr ("Stopped.\n"), true);
        return false;
    }
    return p.exitStatus() == QProcess::NormalExit && p.exitCode() == 0;
}

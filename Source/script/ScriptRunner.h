// SPDX-License-Identifier: AGPL-3.0-only
#pragma once

#include "ScriptApi.h"

#include <QElapsedTimer>
#include <QObject>
#include <QPointer>
#include <QString>

class QProcess;

// Runs one script at a time, in Lua (inside PHI) or Python (python3 as a child
// process talking JSON lines), with the phi functions going to a Dispatch.
// run() returns when the script ends, but the event loop keeps turning while
// it runs, so PHI stays responsive and stop() can end a runaway script.
class ScriptRunner final : public QObject
{
    Q_OBJECT

public:
    explicit ScriptRunner (QObject* parent = nullptr);
    ~ScriptRunner() override;

    // `name` is shown in error messages (the file's name); `folder` is where a
    // Python script runs and where Lua's require looks first. True when the
    // script ran to its end without an error.
    bool run (script::Language, const QString& code, const QString& name, const QString& folder, const script::Dispatch&);
    void stop();
    bool isRunning() const { return running; }
    bool wasStopped() const { return stopRequested; }

    // python3 (or python) on the PATH; empty when there is none.
    static QString pythonExecutable();

    // Lets the event loop turn now and then while a script works; throws
    // script::Error once stop() has been asked for. For long calls in the dispatch.
    void keepAlive();

Q_SIGNALS:
    void output (const QString& text, bool error);
    void runningChanged (bool running);

private:
    bool runLua (const QString& code, const QString& name, const QString& folder);
    bool runPython (const QString& code, const QString& name, const QString& folder);
    QVariant call (const QString& name, const QVariantList& args);   // the runner's own (print, wait), else the dispatch

    script::Dispatch dispatch;
    bool running = false, stopRequested = false;
    QElapsedTimer sinceEvents;
    QPointer<QProcess> process;

    friend struct LuaGlue;
};

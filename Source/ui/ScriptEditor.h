// SPDX-License-Identifier: AGPL-3.0-only
#pragma once

#include "../script/ScriptApi.h"

#include <QWidget>

#include <functional>

class QAction;
class QLabel;
class QListWidget;
class QPlainTextEdit;
class QSplitter;
class ScriptHighlighter;

// Tools > Scripts > Script Editor: the scripts folder on the left, the script
// in the middle (Lua, Python or Nyquist, coloured by its language) and what it
// prints below. F5 runs it on the open file; Stop ends it. Like Reaper's
// ReaScript editor, but PHI's own window.
class ScriptEditor final : public QWidget
{
    Q_OBJECT

public:
    explicit ScriptEditor (QWidget* parent);

    // Asked to run the script as it is in the editor (saved or not).
    std::function<void (script::Language, const QString& code, const QString& name, const QString& folder)> runRequested;
    std::function<void()> stopRequested;
    std::function<void()> scriptsChanged;   // a script saved, made or deleted: the menu and commands follow

    void openScript (const QString& path);
    void newScript (script::Language);
    void showOutput (const QString& text, bool error);
    void setRunning (bool);
    void refreshList();

protected:
    void closeEvent (QCloseEvent*) override;
    bool eventFilter (QObject*, QEvent*) override;

private:
    bool save (bool as);
    bool askToSave();
    void run();
    void updateTitle();
    void showReference();
    void deleteSelected();
    void setLanguage (script::Language);

    QString path;            // empty: not saved yet
    script::Language language = script::Language::lua;
    QListWidget* list;
    QPlainTextEdit* code;
    QPlainTextEdit* console;
    QLabel* position;
    QAction* runAction;
    QAction* stopAction;
    ScriptHighlighter* highlighter;
};

namespace scripts
{
    // $XDG_CONFIG_HOME/phi/scripts, made with a few examples the first time.
    QString folder();
    // Every script in it (.lua, .py, .ny), by name.
    QStringList all();
    // A new script's first lines, by language.
    QString templateFor (script::Language);
}

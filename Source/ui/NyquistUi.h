// SPDX-License-Identifier: AGPL-3.0-only
#pragma once

#include "../core/Nyquist.h"

#include <QDialog>
#include <QString>

#include <functional>
#include <vector>

class QPlainTextEdit;

// The Qt side of Nyquist effects: finding the plug-ins, their settings
// dialogs, the Nyquist Prompt and the output window.
namespace nyquistui
{
    struct Entry
    {
        QString id;          // command id, "nyquist.<file name>"
        QString path;
        nyquist::Script header;
    };

    // Points libnyquist at its runtime and returns the plug-in folders, in
    // order of preference: the user's, PHINYX's bundled ones, then any
    // installed Audacity's.
    QStringList setUpFolders();
    QString userFolder();

    // Every .ny file in those folders (a name found twice: the first wins).
    std::vector<Entry> scan();

    // Remembered control values, per plug-in.
    void restoreValues (const QString& key, nyquist::Script&);
    void storeValues (const QString& key, const nyquist::Script&);

    // Shows what Nyquist printed (errors, traces). Read-only, copyable.
    void showOutput (QWidget* parent, const QString& title, const QString& text);
}

// A plug-in's controls, laid out as Audacity does: sliders with a number box,
// menus for choices, text and file fields. Debug runs it and shows the output.
class NyquistDialog final : public QDialog
{
    Q_OBJECT

public:
    NyquistDialog (nyquist::Script&, QWidget* parent);
    bool debugRequested() const { return debug; }

private:
    void collect();

    nyquist::Script& script;
    std::vector<std::function<void()>> readers;   // copy each widget's value into the script
    bool debug = false;
};

// Tools > Nyquist Prompt: type Nyquist code and run it on the selection (or
// the whole file, or nothing at all for code that only computes or prints).
class NyquistPromptDialog final : public QDialog
{
    Q_OBJECT

public:
    explicit NyquistPromptDialog (QWidget* parent);
    QString code() const;
    bool debugRequested() const { return debug; }

private:
    void load();
    void save();

    QPlainTextEdit* editor;
    bool debug = false;
};

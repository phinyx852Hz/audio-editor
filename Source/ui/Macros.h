// SPDX-License-Identifier: AGPL-3.0-only
#pragma once

#include <QList>
#include <QObject>
#include <QPointer>
#include <QString>
#include <QVariantMap>

class QDialog;

// Macros: a named list of commands, recorded as the user works (Tools >
// Macros > Record Macro) and run again on another file or selection. Each
// step keeps the settings its windows were OK'd with (every spin box, list,
// tick box, slider and text field, in order), so a step runs with exactly
// those settings and its windows do not appear, unless the step is set to
// ask. Stored one per file as JSON in $XDG_CONFIG_HOME/phinyx/macros.
namespace macro
{
    struct Step
    {
        QString command;               // a command id, e.g. "process.normalize"
        bool ask = false;              // show the step's windows (filled in) instead of OK'ing them
        QList<QVariantMap> dialogs;    // each window's settings, in the order they opened
    };

    struct Macro
    {
        QString name;
        QList<Step> steps;
    };

    QString folder();
    QList<Macro> loadAll();                                   // sorted by name
    bool save (const Macro&, QString& error);                 // replaces one of the same name
    bool remove (const QString& name);
    bool rename (const QString& from, const QString& to, QString& error);

    // Whether a command makes sense in a macro: not view, help, options, playback or the macro commands.
    bool recordable (const QString& commandId);

    // A window's settings, and putting them back.
    QVariantMap capture (QDialog*);
    void restore (QDialog*, const QVariantMap&);
}

// Watches commands and windows to record a macro, and fills in and OKs
// windows while one runs.
class MacroRecorder final : public QObject
{
    Q_OBJECT

public:
    explicit MacroRecorder (QObject* parent = nullptr);

    // Recording.
    void startRecording();
    macro::Macro stopRecording();     // the steps recorded (unnamed)
    bool isRecording() const { return recording; }
    int recordedSteps() const { return (int) current.steps.size(); }

    // From CommandRegistry's observer.
    void commandStarted (const QString& id);
    void commandFinished (const QString& id);

    // Running: wrap each step's command in begin/end.
    void beginStep (const macro::Step&);
    void endStep();
    bool isRunning() const { return running != nullptr; }

Q_SIGNALS:
    void recordingChanged();

protected:
    bool eventFilter (QObject*, QEvent*) override;

private:
    bool recording = false;
    macro::Macro current;
    int activeStep = -1;                    // while recording: the step whose command is running
    const macro::Step* running = nullptr;   // while running a macro
    int nextDialog = 0;
};

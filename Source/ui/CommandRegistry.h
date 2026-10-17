// SPDX-License-Identifier: AGPL-3.0-only
#pragma once

#include <QAction>
#include <QKeySequence>
#include <QList>
#include <QObject>
#include <QString>
#include <functional>

// Every menu item and process is registered here as a command. Menus, the
// command palette, shortcuts and (later) MIDI mappings all trigger these same
// actions, so nothing is reachable only by mouse.
struct Command
{
    QString id;
    QString category;
    QAction* action = nullptr;
    QList<QKeySequence> defaultShortcuts;

    QString name() const;     // menu text without the & mnemonic marker
    QString shortcutText() const;
};

class CommandRegistry final : public QObject
{
    Q_OBJECT

public:
    explicit CommandRegistry (QObject* parent = nullptr);

    QAction* add (const QString& id, const QString& category, const QString& text,
                  const QList<QKeySequence>& defaults, std::function<void()> handler);

    const QList<Command>& all() const { return commands; }
    QAction* action (const QString& id) const;

    // Returns the command already using this key sequence, or nullptr.
    const Command* findConflict (const QKeySequence&, const QString& ignoreId) const;

    void setShortcuts (const QString& id, const QList<QKeySequence>&);
    void resetToDefaults();

    // Persisted under $XDG_CONFIG_HOME/<app>/keymap.ini; only changes from defaults are stored.
    void load();
    void save() const;
    static QString keymapPath();

private:
    QList<Command> commands;
};

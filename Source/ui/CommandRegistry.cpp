// SPDX-License-Identifier: AGPL-3.0-only
#include "CommandRegistry.h"

#include <QDir>
#include <QSettings>
#include <QStandardPaths>

QString Command::name() const
{
    auto text = action->text();
    return text.remove (QLatin1Char ('&')).remove (QStringLiteral ("..."));
}

QString Command::shortcutText() const
{
    QStringList keys;
    for (const auto& seq : action->shortcuts())
        keys << seq.toString (QKeySequence::NativeText);

    return keys.join (QStringLiteral (", "));
}

CommandRegistry::CommandRegistry (QObject* parent) : QObject (parent) {}

QAction* CommandRegistry::add (const QString& id, const QString& category, const QString& text,
                               const QList<QKeySequence>& defaults, std::function<void()> handler)
{
    auto* action = new QAction (text, this);
    action->setObjectName (id);
    action->setShortcuts (defaults);
    action->setShortcutContext (Qt::WindowShortcut);
    connect (action, &QAction::triggered, this, [handler = std::move (handler)] { handler(); });

    commands.push_back ({ id, category, action, defaults });
    return action;
}

QAction* CommandRegistry::action (const QString& id) const
{
    for (const auto& c : commands)
        if (c.id == id)
            return c.action;

    return nullptr;
}

const Command* CommandRegistry::findConflict (const QKeySequence& seq, const QString& ignoreId) const
{
    if (seq.isEmpty())
        return nullptr;

    for (const auto& c : commands)
        if (c.id != ignoreId && c.action->shortcuts().contains (seq))
            return &c;

    return nullptr;
}

void CommandRegistry::setShortcuts (const QString& id, const QList<QKeySequence>& keys)
{
    if (auto* a = action (id))
        a->setShortcuts (keys);
}

void CommandRegistry::resetToDefaults()
{
    for (auto& c : commands)
        c.action->setShortcuts (c.defaultShortcuts);
}

QString CommandRegistry::keymapPath()
{
    // Resolves $XDG_CONFIG_HOME or ~/.config.
    return QStandardPaths::writableLocation (QStandardPaths::GenericConfigLocation)
           + QStringLiteral ("/" APP_ID "/keymap.ini");
}

void CommandRegistry::load()
{
    QSettings settings (keymapPath(), QSettings::IniFormat);
    settings.beginGroup (QStringLiteral ("shortcuts"));

    for (auto& c : commands)
    {
        if (! settings.contains (c.id))
            continue;

        QList<QKeySequence> keys;
        for (const auto& s : settings.value (c.id).toStringList())
            if (! s.isEmpty())
                keys << QKeySequence (s, QKeySequence::PortableText);

        c.action->setShortcuts (keys);
    }
}

void CommandRegistry::save() const
{
    QDir().mkpath (QFileInfo (keymapPath()).absolutePath());
    QSettings settings (keymapPath(), QSettings::IniFormat);
    settings.remove (QStringLiteral ("shortcuts"));
    settings.beginGroup (QStringLiteral ("shortcuts"));

    for (const auto& c : commands)
    {
        if (c.action->shortcuts() == c.defaultShortcuts)
            continue;

        QStringList keys;
        for (const auto& seq : c.action->shortcuts())
            keys << seq.toString (QKeySequence::PortableText);

        // An empty list is stored as one empty entry so "no shortcut" survives a reload.
        settings.setValue (c.id, keys.isEmpty() ? QStringList { QString() } : keys);
    }
}

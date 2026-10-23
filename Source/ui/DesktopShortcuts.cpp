// SPDX-License-Identifier: AGPL-3.0-only
#include "DesktopShortcuts.h"

#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QProcess>
#include <QStringList>

namespace
{
    // xkb key names as hyprctl prints them -> Qt key names.
    QString qtKeyName (const QString& xkb)
    {
        static const QMap<QString, QString> names {
            { QStringLiteral ("return"), QStringLiteral ("Return") },   { QStringLiteral ("enter"), QStringLiteral ("Return") },
            { QStringLiteral ("kp_enter"), QStringLiteral ("Enter") },  { QStringLiteral ("tab"), QStringLiteral ("Tab") },
            { QStringLiteral ("space"), QStringLiteral ("Space") },     { QStringLiteral ("delete"), QStringLiteral ("Del") },
            { QStringLiteral ("backspace"), QStringLiteral ("Backspace") }, { QStringLiteral ("escape"), QStringLiteral ("Esc") },
            { QStringLiteral ("print"), QStringLiteral ("Print") },     { QStringLiteral ("insert"), QStringLiteral ("Ins") },
            { QStringLiteral ("home"), QStringLiteral ("Home") },       { QStringLiteral ("end"), QStringLiteral ("End") },
            { QStringLiteral ("prior"), QStringLiteral ("PgUp") },      { QStringLiteral ("page_up"), QStringLiteral ("PgUp") },
            { QStringLiteral ("next"), QStringLiteral ("PgDown") },     { QStringLiteral ("page_down"), QStringLiteral ("PgDown") },
            { QStringLiteral ("left"), QStringLiteral ("Left") },       { QStringLiteral ("right"), QStringLiteral ("Right") },
            { QStringLiteral ("up"), QStringLiteral ("Up") },           { QStringLiteral ("down"), QStringLiteral ("Down") },
            { QStringLiteral ("minus"), QStringLiteral ("-") },         { QStringLiteral ("equal"), QStringLiteral ("=") },
            { QStringLiteral ("plus"), QStringLiteral ("+") },          { QStringLiteral ("comma"), QStringLiteral (",") },
            { QStringLiteral ("period"), QStringLiteral (".") },        { QStringLiteral ("slash"), QStringLiteral ("/") },
            { QStringLiteral ("backslash"), QStringLiteral ("\\") },    { QStringLiteral ("semicolon"), QStringLiteral (";") },
            { QStringLiteral ("apostrophe"), QStringLiteral ("'") },    { QStringLiteral ("grave"), QStringLiteral ("`") },
            { QStringLiteral ("bracketleft"), QStringLiteral ("[") },   { QStringLiteral ("bracketright"), QStringLiteral ("]") },
        };

        const auto lower = xkb.toLower();
        if (names.contains (lower))
            return names.value (lower);
        if (lower.size() == 1 && lower[0].isLetterOrNumber())
            return lower.toUpper();
        if (lower.size() <= 3 && lower.startsWith (QLatin1Char ('f')) && lower.mid (1).toInt() > 0)
            return lower.toUpper();  // F1..F24
        return {};  // media keys, keycodes and the like never clash with PHI
    }

    QMap<QKeySequence, QString> readHyprland()
    {
        QMap<QKeySequence, QString> result;
        if (qEnvironmentVariableIsEmpty ("HYPRLAND_INSTANCE_SIGNATURE"))
            return result;

        QProcess hyprctl;
        hyprctl.start (QStringLiteral ("hyprctl"), { QStringLiteral ("binds"), QStringLiteral ("-j") });
        if (! hyprctl.waitForFinished (1500) || hyprctl.exitCode() != 0)
            return result;

        for (const auto& value : QJsonDocument::fromJson (hyprctl.readAllStandardOutput()).array())
        {
            const auto bind = value.toObject();
            if (bind.value (QStringLiteral ("mouse")).toBool() || ! bind.value (QStringLiteral ("submap")).toString().isEmpty())
                continue;  // mouse binds, and modes like the screenshot picker, only apply at special times

            const auto key = qtKeyName (bind.value (QStringLiteral ("key")).toString());
            if (key.isEmpty())
                continue;

            const auto mods = bind.value (QStringLiteral ("modmask")).toInt();
            QStringList parts;
            if (mods & 4)  parts << QStringLiteral ("Ctrl");
            if (mods & 8)  parts << QStringLiteral ("Alt");
            if (mods & 1)  parts << QStringLiteral ("Shift");
            if (mods & 64) parts << QStringLiteral ("Meta");
            parts << key;

            const auto seq = QKeySequence::fromString (parts.join (QLatin1Char ('+')), QKeySequence::PortableText);
            if (seq.isEmpty())
                continue;

            auto what = bind.value (QStringLiteral ("description")).toString();
            if (what.isEmpty())
                what = (bind.value (QStringLiteral ("dispatcher")).toString() + QLatin1Char (' ')
                        + bind.value (QStringLiteral ("arg")).toString()).trimmed();
            result.insert (seq, what);
        }

        return result;
    }
}

const QMap<QKeySequence, QString>& DesktopShortcuts::taken()
{
    static const auto keys = readHyprland();
    return keys;
}

QString DesktopShortcuts::owner (const QKeySequence& seq)
{
    const auto& keys = taken();
    if (! keys.contains (seq))
        return {};

    const auto what = keys.value (seq);
    return what.isEmpty() ? QStringLiteral ("Hyprland") : QStringLiteral ("Hyprland: %1").arg (what);
}

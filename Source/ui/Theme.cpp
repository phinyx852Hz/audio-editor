// SPDX-License-Identifier: AGPL-3.0-only
#include "Theme.h"

#include <QApplication>
#include <QDir>
#include <QFile>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QPalette>
#include <QSettings>
#include <QStandardPaths>
#include <QStyle>
#include <QStyleFactory>

namespace
{
    // Accepts #RGB, #RRGGBB, #RRGGBBAA (CSS order, alpha last) and colour names.
    QColor parseColor (const QString& text)
    {
        const auto t = text.trimmed();
        if (t.size() == 9 && t.startsWith (QLatin1Char ('#')))
        {
            bool ok = false;
            const auto value = t.mid (1).toUInt (&ok, 16);
            if (! ok)
                return {};
            return QColor ((int) (value >> 24) & 0xff, (int) (value >> 16) & 0xff, (int) (value >> 8) & 0xff, (int) value & 0xff);
        }

        return QColor::fromString (t);
    }

    Theme& currentTheme()
    {
        static Theme theme = Theme::builtIns().first();
        return theme;
    }

    Theme makeTheme (const QString& name, bool dark, std::initializer_list<std::pair<const char*, const char*>> colors,
                     std::initializer_list<const char*> spectrogram)
    {
        Theme t;
        t.name = name;
        t.source = QStringLiteral ("built-in");
        t.dark = dark;
        for (const auto& [key, value] : colors)
            t.colors.insert (QString::fromLatin1 (key), parseColor (QString::fromLatin1 (value)));
        for (const auto* c : spectrogram)
            t.spectrogram.push_back (parseColor (QString::fromLatin1 (c)));
        return t;
    }
}

QStringList Theme::colorKeys()
{
    return { QStringLiteral ("window"), QStringLiteral ("windowText"), QStringLiteral ("base"), QStringLiteral ("alternateBase"),
             QStringLiteral ("text"), QStringLiteral ("placeholderText"), QStringLiteral ("button"), QStringLiteral ("buttonText"),
             QStringLiteral ("highlight"), QStringLiteral ("highlightedText"), QStringLiteral ("accent"), QStringLiteral ("mid"),
             QStringLiteral ("disabledText"),
             QStringLiteral ("waveform"), QStringLiteral ("waveformBackground"), QStringLiteral ("centerLine"),
             QStringLiteral ("selection"), QStringLiteral ("cursor"), QStringLiteral ("marker"), QStringLiteral ("region"),
             QStringLiteral ("meterBackground"), QStringLiteral ("meterLow"), QStringLiteral ("meterMid"), QStringLiteral ("meterHigh"),
             QStringLiteral ("meterPeakHold") };
}

QVector<Theme> Theme::builtIns()
{
    return {
        makeTheme (QStringLiteral ("PHINYX Dark"), true, {
            { "window", "#1e2128" }, { "windowText", "#dddddd" }, { "base", "#15171c" }, { "alternateBase", "#23262e" },
            { "text", "#dddddd" }, { "placeholderText", "#808080" }, { "button", "#2a2e37" }, { "buttonText", "#dddddd" },
            { "highlight", "#4fc3f7" }, { "highlightedText", "#000000" }, { "accent", "#ffb74d" }, { "mid", "#3a3f4a" },
            { "disabledText", "#707070" },
            { "waveform", "#4fc3f7" }, { "waveformBackground", "#15171c" }, { "centerLine", "#3a3f4a" },
            { "selection", "#4fc3f740" }, { "cursor", "#ffffff" }, { "marker", "#ffb74d" }, { "region", "#ba68c8" },
            { "meterBackground", "#101216" }, { "meterLow", "#66bb6a" }, { "meterMid", "#ffca28" }, { "meterHigh", "#ef5350" },
            { "meterPeakHold", "#ffffff" } },
            { "#000000", "#140a3c", "#3b0f70", "#8c2981", "#de4968", "#fe9f6d", "#fcfdbf" }),

        makeTheme (QStringLiteral ("PHINYX Light"), false, {
            { "window", "#eceff4" }, { "windowText", "#1f2329" }, { "base", "#ffffff" }, { "alternateBase", "#f3f5f8" },
            { "text", "#1f2329" }, { "placeholderText", "#7a7f88" }, { "button", "#e2e6ed" }, { "buttonText", "#1f2329" },
            { "highlight", "#1e88e5" }, { "highlightedText", "#ffffff" }, { "accent", "#e65100" }, { "mid", "#c5cad3" },
            { "disabledText", "#9aa0a8" },
            { "waveform", "#1565c0" }, { "waveformBackground", "#ffffff" }, { "centerLine", "#c5cad3" },
            { "selection", "#1e88e533" }, { "cursor", "#000000" }, { "marker", "#e65100" }, { "region", "#8e24aa" },
            { "meterBackground", "#dfe3ea" }, { "meterLow", "#2e7d32" }, { "meterMid", "#f9a825" }, { "meterHigh", "#c62828" },
            { "meterPeakHold", "#000000" } },
            { "#ffffff", "#d6e4f0", "#7fa7d9", "#3a5bbf", "#5b1f9a", "#b0125b", "#3d0013" }),
    };
}

QColor Theme::color (const QString& key) const
{
    return colors.value (key, QColor (Qt::magenta));
}

QString Theme::userThemeFolder()
{
    return QStandardPaths::writableLocation (QStandardPaths::GenericDataLocation) + QStringLiteral ("/" APP_ID "/themes");
}

bool Theme::loadFile (const QString& path, Theme& out, QString& error)
{
    QFile file (path);
    if (! file.open (QIODevice::ReadOnly))
    {
        error = QObject::tr ("%1: cannot be read").arg (path);
        return false;
    }

    QJsonParseError parseError;
    const auto json = QJsonDocument::fromJson (file.readAll(), &parseError);
    if (! json.isObject())
    {
        error = QObject::tr ("%1: %2").arg (path, parseError.errorString());
        return false;
    }

    const auto root = json.object();
    const auto name = root.value (QStringLiteral ("name")).toString().trimmed();
    if (name.isEmpty())
    {
        error = QObject::tr ("%1: missing \"name\"").arg (path);
        return false;
    }

    // Start from the named built-in so a theme only lists what it changes.
    const auto baseName = root.value (QStringLiteral ("base")).toString (QStringLiteral ("PHINYX Dark"));
    const auto bases = builtIns();
    auto base = bases.first();
    for (const auto& b : bases)
        if (b.name.compare (baseName, Qt::CaseInsensitive) == 0
            || (baseName.compare (QStringLiteral ("light"), Qt::CaseInsensitive) == 0 && ! b.dark))
            base = b;

    out = base;
    out.name = name;
    out.source = path;

    const auto colors = root.value (QStringLiteral ("colors")).toObject();
    const auto keys = colorKeys();
    for (auto it = colors.begin(); it != colors.end(); ++it)
    {
        const auto c = parseColor (it.value().toString());
        if (! keys.contains (it.key()) || ! c.isValid())
        {
            error = QObject::tr ("%1: unknown colour \"%2\" or invalid value").arg (path, it.key());
            return false;
        }
        out.colors[it.key()] = c;
    }

    if (root.contains (QStringLiteral ("dark")))
        out.dark = root.value (QStringLiteral ("dark")).toBool();

    const auto gradient = root.value (QStringLiteral ("spectrogram")).toArray();
    if (! gradient.isEmpty())
    {
        out.spectrogram.clear();
        for (const auto& v : gradient)
        {
            const auto c = parseColor (v.toString());
            if (! c.isValid())
            {
                error = QObject::tr ("%1: invalid spectrogram colour").arg (path);
                return false;
            }
            out.spectrogram.push_back (c);
        }

        if (out.spectrogram.size() < 2)
        {
            error = QObject::tr ("%1: the spectrogram gradient needs at least two colours").arg (path);
            return false;
        }
    }

    return true;
}

QVector<Theme> Theme::available (QStringList* errors)
{
    auto result = builtIns();
    QDir folder (userThemeFolder());

    for (const auto& info : folder.entryInfoList ({ QStringLiteral ("*.json") }, QDir::Files, QDir::Name))
    {
        Theme t;
        QString error;
        if (loadFile (info.absoluteFilePath(), t, error))
            result.push_back (t);
        else if (errors != nullptr)
            errors->push_back (error);
    }

    return result;
}

const Theme& Theme::current() { return currentTheme(); }

void Theme::apply (const Theme& theme)
{
    currentTheme() = theme;

    if (qApp->style()->name().compare (QStringLiteral ("fusion"), Qt::CaseInsensitive) != 0)
        QApplication::setStyle (QStyleFactory::create (QStringLiteral ("Fusion")));

    const auto c = [&] (const char* key) { return theme.color (QString::fromLatin1 (key)); };

    QPalette p;
    p.setColor (QPalette::Window,          c ("window"));
    p.setColor (QPalette::WindowText,      c ("windowText"));
    p.setColor (QPalette::Base,            c ("base"));
    p.setColor (QPalette::AlternateBase,   c ("alternateBase"));
    p.setColor (QPalette::Text,            c ("text"));
    p.setColor (QPalette::PlaceholderText, c ("placeholderText"));
    p.setColor (QPalette::Button,          c ("button"));
    p.setColor (QPalette::ButtonText,      c ("buttonText"));
    p.setColor (QPalette::BrightText,      c ("cursor"));
    p.setColor (QPalette::Highlight,       c ("highlight"));
    p.setColor (QPalette::HighlightedText, c ("highlightedText"));
    p.setColor (QPalette::Link,            c ("accent"));
    p.setColor (QPalette::Mid,             c ("mid"));
    p.setColor (QPalette::ToolTipBase,     c ("button"));
    p.setColor (QPalette::ToolTipText,     c ("buttonText"));
    p.setColor (QPalette::Disabled, QPalette::Text,       c ("disabledText"));
    p.setColor (QPalette::Disabled, QPalette::ButtonText, c ("disabledText"));
    p.setColor (QPalette::Disabled, QPalette::WindowText, c ("disabledText"));
    QApplication::setPalette (p);
}

QString Theme::savedName()
{
    QSettings settings (QStandardPaths::writableLocation (QStandardPaths::GenericConfigLocation)
                            + QStringLiteral ("/" APP_ID "/settings.ini"), QSettings::IniFormat);
    return settings.value (QStringLiteral ("appearance/theme")).toString();
}

void Theme::saveName (const QString& name)
{
    QSettings settings (QStandardPaths::writableLocation (QStandardPaths::GenericConfigLocation)
                            + QStringLiteral ("/" APP_ID "/settings.ini"), QSettings::IniFormat);
    settings.setValue (QStringLiteral ("appearance/theme"), name);
}

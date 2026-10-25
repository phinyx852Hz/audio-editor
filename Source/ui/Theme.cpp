// SPDX-License-Identifier: AGPL-3.0-only
#include "Theme.h"

#include <QApplication>
#include <QDir>
#include <QFile>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QPaintEvent>
#include <QPainter>
#include <QPalette>
#include <QSettings>
#include <QStandardPaths>
#include <QStyle>
#include <QStyleFactory>
#include <QWidget>
#include <algorithm>
#include <cmath>

namespace
{
    // The built-in themes were "PHINYX Dark" and "PHINYX Light" before the rename
    // to PHI; a remembered choice or a theme's base by those names still finds them.
    QString renamed (const QString& themeName)
    {
        if (themeName.startsWith (QLatin1String ("PHINYX "), Qt::CaseInsensitive))
            return QStringLiteral ("PHI ") + themeName.mid (7);
        return themeName;
    }

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

    Theme& baseTheme()
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
             QStringLiteral ("meterPeakHold"), QStringLiteral ("scope"),
             QStringLiteral ("levelQuiet"), QStringLiteral ("levelSoft"), QStringLiteral ("levelMedium"),
             QStringLiteral ("levelLoud"), QStringLiteral ("levelHot"), QStringLiteral ("levelPeak") };
}

QVector<Theme> Theme::builtIns()
{
    // The first theme is the default.
    return {
        // Gold on near-black, from the PHI manual's cover and figures. The default for new installs.
        makeTheme (QStringLiteral ("PHI Gold"), true, {
            { "window", "#121214" }, { "windowText", "#e8e4da" }, { "base", "#0b0b0c" }, { "alternateBase", "#17171a" },
            { "text", "#e8e4da" }, { "placeholderText", "#8a7f63" }, { "button", "#1f1c15" }, { "buttonText", "#e8e4da" },
            { "highlight", "#c8a24a" }, { "highlightedText", "#0b0b0c" }, { "accent", "#e9dcb6" }, { "mid", "#2c2b28" },
            { "disabledText", "#6b665a" },
            { "waveform", "#c8a24a" }, { "waveformBackground", "#0b0b0c" }, { "centerLine", "#2c2b28" },
            { "selection", "#c8a24a40" }, { "cursor", "#f4ecd8" }, { "marker", "#e9dcb6" }, { "region", "#c0704a" },
            { "meterBackground", "#080809" }, { "meterLow", "#9c7a2c" }, { "meterMid", "#d6b25c" }, { "meterHigh", "#c4553a" },
            { "meterPeakHold", "#f4ecd8" }, { "scope", "#c8a24a" },
            { "levelQuiet", "#6e5a2e" }, { "levelSoft", "#8a7f63" }, { "levelMedium", "#a89464" },
            { "levelLoud", "#c8a24a" }, { "levelHot", "#e2b84e" }, { "levelPeak", "#c4553a" } },
            { "#000000", "#1f1c15", "#4a3a17", "#7d6d48", "#9c7a2c", "#c8a24a", "#e9dcb6", "#fbfaf7" }),

        makeTheme (QStringLiteral ("PHI Dark"), true, {
            { "window", "#1e2128" }, { "windowText", "#dddddd" }, { "base", "#15171c" }, { "alternateBase", "#23262e" },
            { "text", "#dddddd" }, { "placeholderText", "#808080" }, { "button", "#2a2e37" }, { "buttonText", "#dddddd" },
            { "highlight", "#4fc3f7" }, { "highlightedText", "#000000" }, { "accent", "#ffb74d" }, { "mid", "#3a3f4a" },
            { "disabledText", "#707070" },
            { "waveform", "#4fc3f7" }, { "waveformBackground", "#15171c" }, { "centerLine", "#3a3f4a" },
            { "selection", "#4fc3f740" }, { "cursor", "#ffffff" }, { "marker", "#ffb74d" }, { "region", "#ba68c8" },
            { "meterBackground", "#101216" }, { "meterLow", "#66bb6a" }, { "meterMid", "#ffca28" }, { "meterHigh", "#ef5350" },
            { "meterPeakHold", "#ffffff" }, { "scope", "#4fc3f7" },
            { "levelQuiet", "#3b82f6" }, { "levelSoft", "#06b6d4" }, { "levelMedium", "#22c55e" },
            { "levelLoud", "#eab308" }, { "levelHot", "#f97316" }, { "levelPeak", "#ef4444" } },
            { "#000000", "#140a3c", "#3b0f70", "#8c2981", "#de4968", "#fe9f6d", "#fcfdbf" }),

        makeTheme (QStringLiteral ("PHI Light"), false, {
            { "window", "#eceff4" }, { "windowText", "#1f2329" }, { "base", "#ffffff" }, { "alternateBase", "#f3f5f8" },
            { "text", "#1f2329" }, { "placeholderText", "#7a7f88" }, { "button", "#e2e6ed" }, { "buttonText", "#1f2329" },
            { "highlight", "#1e88e5" }, { "highlightedText", "#ffffff" }, { "accent", "#e65100" }, { "mid", "#c5cad3" },
            { "disabledText", "#9aa0a8" },
            { "waveform", "#1565c0" }, { "waveformBackground", "#ffffff" }, { "centerLine", "#c5cad3" },
            { "selection", "#1e88e533" }, { "cursor", "#000000" }, { "marker", "#e65100" }, { "region", "#8e24aa" },
            { "meterBackground", "#dfe3ea" }, { "meterLow", "#2e7d32" }, { "meterMid", "#f9a825" }, { "meterHigh", "#c62828" },
            { "meterPeakHold", "#000000" }, { "scope", "#1565c0" },
            { "levelQuiet", "#1d4ed8" }, { "levelSoft", "#0e7490" }, { "levelMedium", "#15803d" },
            { "levelLoud", "#a16207" }, { "levelHot", "#c2410c" }, { "levelPeak", "#b91c1c" } },
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
    // "dark", "light" and "gold" are short for the built-in of that name; anything unknown starts from PHI Dark.
    const auto baseName = renamed (root.value (QStringLiteral ("base")).toString (QStringLiteral ("PHI Dark")));
    const auto bases = builtIns();
    auto base = bases.first();
    for (const auto& b : bases)
        if (b.name.compare (QStringLiteral ("PHI Dark")) == 0)
            base = b;
    for (const auto& b : bases)
        if (b.name.compare (baseName, Qt::CaseInsensitive) == 0
            || b.name.compare (QStringLiteral ("PHI ") + baseName, Qt::CaseInsensitive) == 0)
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

    // Themes made before the imaging display had its own colour draw it in their waveform colour.
    if (! colors.contains (QStringLiteral ("scope")))
        out.colors[QStringLiteral ("scope")] = out.colors.value (QStringLiteral ("waveform"));

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

namespace
{
    QString settingsPath()
    {
        return QStandardPaths::writableLocation (QStandardPaths::GenericConfigLocation)
             + QStringLiteral ("/" APP_ID "/settings.ini");
    }

    double& opacityValue()
    {
        static double value = std::clamp (QSettings (settingsPath(), QSettings::IniFormat)
                                              .value (QStringLiteral ("appearance/opacity"), 1.0).toDouble(),
                                          Theme::minOpacity, 1.0);
        return value;
    }

    // The application palette with see-through backgrounds, for everything inside
    // a translucent window (the window's own background is drawn by BackgroundPainter).
    QPalette translucentPalette()
    {
        auto p = QApplication::palette();
        for (auto group : { QPalette::Active, QPalette::Inactive, QPalette::Disabled })
            for (auto role : { QPalette::Window, QPalette::Base, QPalette::AlternateBase, QPalette::Button })
                p.setColor (group, role, Theme::withOpacity (QApplication::palette().color (group, role)));
        return p;
    }

    void updateTranslucentWindows()
    {
        const auto p = translucentPalette();
        for (auto* w : QApplication::topLevelWidgets())
        {
            if (w->testAttribute (Qt::WA_TranslucentBackground))
            {
                w->setPalette (p);
                w->update();
            }
        }
    }
}

void Theme::apply (const Theme& chosen)
{
    baseTheme() = chosen;
    auto theme = chosen;
    const auto overrides = colorOverrides();
    for (auto it = overrides.begin(); it != overrides.end(); ++it)
        theme.colors.insert (it.key(), it.value());
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
    updateTranslucentWindows();

    for (auto* w : QApplication::topLevelWidgets())
        w->update();
}

QString Theme::savedName()
{
    QSettings settings (settingsPath(), QSettings::IniFormat);
    return renamed (settings.value (QStringLiteral ("appearance/theme")).toString());
}

void Theme::saveName (const QString& name)
{
    QSettings settings (settingsPath(), QSettings::IniFormat);
    settings.setValue (QStringLiteral ("appearance/theme"), name);
}

double Theme::opacity() { return opacityValue(); }

double Theme::surfaceOpacity()
{
    const auto a = opacityValue();
    return a * a * a;
}

QColor Theme::withOpacity (QColor c)
{
    c.setAlphaF (c.alphaF() * (float) surfaceOpacity());
    return c;
}

void Theme::setOpacity (double value)
{
    opacityValue() = std::round (std::clamp (value, minOpacity, 1.0) * 100.0) / 100.0;
    QSettings (settingsPath(), QSettings::IniFormat).setValue (QStringLiteral ("appearance/opacity"), opacityValue());
    updateTranslucentWindows();
}

namespace
{
    // Paints a translucent window's background (with WA_TranslucentBackground Qt leaves it empty).
    class BackgroundPainter final : public QObject
    {
    public:
        using QObject::QObject;

        bool eventFilter (QObject* watched, QEvent* e) override
        {
            if (e->type() == QEvent::Paint)
            {
                auto* w = static_cast<QWidget*> (watched);
                QPainter g (w);
                g.setCompositionMode (QPainter::CompositionMode_Source);
                auto back = QApplication::palette().color (w->isActiveWindow() ? QPalette::Active : QPalette::Inactive, QPalette::Window);
                back.setAlphaF (back.alphaF() * (float) Theme::opacity());
                g.fillRect (static_cast<QPaintEvent*> (e)->rect(), back);
            }
            return false;
        }
    };
}

void Theme::makeTranslucent (QWidget* window)
{
    window->setAttribute (Qt::WA_TranslucentBackground);
    window->setPalette (translucentPalette());
    window->installEventFilter (new BackgroundPainter (window));
}

const Theme& Theme::base() { return baseTheme(); }

QString Theme::formatColor (const QColor& c)
{
    auto text = c.name (QColor::HexRgb);
    if (c.alpha() != 255)
        text += QStringLiteral ("%1").arg (c.alpha(), 2, 16, QLatin1Char ('0'));
    return text.toUpper();
}

QMap<QString, QColor> Theme::colorOverrides()
{
    QSettings settings (settingsPath(), QSettings::IniFormat);
    settings.beginGroup (QStringLiteral ("colors"));
    QMap<QString, QColor> result;
    const auto keys = colorKeys();
    for (const auto& key : settings.childKeys())
    {
        const auto c = parseColor (settings.value (key).toString());
        if (keys.contains (key) && c.isValid())
            result.insert (key, c);
    }
    return result;
}

void Theme::setColorOverrides (const QMap<QString, QColor>& overrides)
{
    {
        QSettings settings (settingsPath(), QSettings::IniFormat);
        settings.remove (QStringLiteral ("colors"));
        settings.beginGroup (QStringLiteral ("colors"));
        for (auto it = overrides.begin(); it != overrides.end(); ++it)
            if (it.value().isValid())
                settings.setValue (it.key(), formatColor (it.value()));
    }
    apply (Theme (baseTheme()));
}

void Theme::setColorOverride (const QString& key, const QColor& colour)
{
    auto overrides = colorOverrides();
    if (colour.isValid())
        overrides.insert (key, colour);
    else
        overrides.remove (key);
    setColorOverrides (overrides);
}

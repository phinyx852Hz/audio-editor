// SPDX-License-Identifier: AGPL-3.0-only
#pragma once

#include <QColor>
#include <QMap>
#include <QString>
#include <QStringList>
#include <QVector>

class QWidget;

// Colour themes. Built-in themes ship with the app; anyone can add more as
// JSON files in ~/.local/share/phinyx/themes (see docs/themes.md). A theme
// file names a built-in "base" and overrides any of its colours.
struct Theme
{
    QString name;
    QString source;                   // "built-in" or the file path
    bool dark = true;
    QMap<QString, QColor> colors;     // keys listed by Theme::colorKeys()
    QVector<QColor> spectrogram;      // gradient from quiet to loud

    QColor color (const QString& key) const;

    static QStringList colorKeys();
    static QVector<Theme> builtIns();
    static QString userThemeFolder();

    // Built-ins followed by valid user themes. Problems with user files are appended to `errors`.
    static QVector<Theme> available (QStringList* errors = nullptr);
    static bool loadFile (const QString& path, Theme& out, QString& error);

    static const Theme& current();
    static void apply (const Theme&);  // sets the application palette and the custom colours

    // Remembered choice, stored in ~/.config/phinyx/settings.ini.
    static QString savedName();
    static void saveName (const QString&);

    // Personal colour choices (View > Waveform Colors) that apply on top of any theme.
    // Stored in settings.ini; an invalid colour removes the override.
    static const Theme& base();  // the chosen theme without personal colours
    static QMap<QString, QColor> colorOverrides();
    static void setColorOverride (const QString& key, const QColor&);
    static void setColorOverrides (const QMap<QString, QColor>&);
    static QString formatColor (const QColor&);  // #RRGGBBAA, or #RRGGBB when opaque

    // Window opacity, from minOpacity to 1. Backgrounds of translucent windows
    // are drawn with this alpha; text and waveforms stay solid. The compositor's
    // own opacity (for example a Hyprland window rule) still applies on top.
    static constexpr double minOpacity = 0.2;
    static double opacity();
    static void setOpacity (double);   // updates every translucent window and remembers the value
    static void makeTranslucent (QWidget* window);  // call before the window is first shown
    static QColor withOpacity (QColor);
};

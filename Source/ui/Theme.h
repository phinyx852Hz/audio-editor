// SPDX-License-Identifier: AGPL-3.0-only
#pragma once

#include <QColor>
#include <QMap>
#include <QString>
#include <QStringList>
#include <QVector>

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
};

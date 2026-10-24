// SPDX-License-Identifier: AGPL-3.0-only
#pragma once

#include <QSettings>
#include <QStandardPaths>

// Every setting PHI keeps, in one file: $XDG_CONFIG_HOME/phi/settings.ini.
inline QSettings appSettings()
{
    return QSettings (QStandardPaths::writableLocation (QStandardPaths::GenericConfigLocation)
                          + QStringLiteral ("/" APP_ID "/settings.ini"), QSettings::IniFormat);
}

// Moves what earlier versions kept elsewhere, once: the folders named phinyx
// (from before the rename to PHI) to their phi names, and the settings in Qt's
// default place (~/.config/Unknown Organization/phinyx.conf) into settings.ini.
void moveOldSettings();

// SPDX-License-Identifier: AGPL-3.0-only
#pragma once

#include <QSettings>
#include <QStandardPaths>

// Every setting PHINYX keeps, in one file: $XDG_CONFIG_HOME/phinyx/settings.ini.
inline QSettings appSettings()
{
    return QSettings (QStandardPaths::writableLocation (QStandardPaths::GenericConfigLocation)
                          + QStringLiteral ("/" APP_ID "/settings.ini"), QSettings::IniFormat);
}

// Earlier versions kept some settings in Qt's default place as well
// (~/.config/Unknown Organization/phinyx.conf). Moves them into settings.ini, once.
void moveOldSettings();

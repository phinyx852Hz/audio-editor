// SPDX-License-Identifier: AGPL-3.0-only
#include "engine/AudioEngine.h"
#include "ui/MainWindow.h"
#include "ui/Theme.h"

#include <QApplication>
#include <QCommandLineParser>
#include <QDir>
#include <QStandardPaths>

int main (int argc, char* argv[])
{
    QApplication app (argc, argv);
    QApplication::setApplicationName (QStringLiteral (APP_ID));
    QApplication::setApplicationDisplayName (QStringLiteral (APP_DISPLAY_NAME));
    QApplication::setApplicationVersion (QStringLiteral (APP_VERSION));
    // Wayland app-id, used by Hyprland window rules (class:^(phinyx)$).
    QGuiApplication::setDesktopFileName (QStringLiteral (APP_ID));

    // Use the remembered theme, falling back to the default when it has gone away.
    const auto themes = Theme::available();
    auto theme = themes.first();
    for (const auto& t : themes)
        if (t.name == Theme::savedName())
            theme = t;
    Theme::apply (theme);

    QCommandLineParser parser;
    parser.setApplicationDescription (QStringLiteral (APP_DISPLAY_NAME " audio editor and mastering suite"));
    parser.addHelpOption();
    parser.addVersionOption();
    parser.addPositionalArgument (QStringLiteral ("file"), QStringLiteral ("Audio file to open."));
    parser.process (app);

    AudioEngine engine;
    engine.setConfigFolder ((QStandardPaths::writableLocation (QStandardPaths::GenericConfigLocation)
                             + QStringLiteral ("/" APP_ID)).toStdString());
    const auto deviceError = engine.openDefaultDevice();

    int result = 0;
    {
        MainWindow window (engine);
        window.show();

        if (! parser.positionalArguments().isEmpty())
            window.openFile (QDir::current().absoluteFilePath (parser.positionalArguments().first()));

        result = app.exec();
    }

    return result;
}

// SPDX-License-Identifier: AGPL-3.0-only
#include "engine/AudioEngine.h"
#include "ui/MainWindow.h"

#include <QApplication>
#include <QCommandLineParser>
#include <QDir>
#include <QPalette>
#include <QStyleFactory>

namespace
{
    // Placeholder dark theme until the theme engine (plan 2.8b) loads theme files.
    void applyDarkTheme (QApplication& app)
    {
        app.setStyle (QStyleFactory::create (QStringLiteral ("Fusion")));

        QPalette p;
        p.setColor (QPalette::Window,          QColor (0x1e, 0x21, 0x28));
        p.setColor (QPalette::WindowText,      QColor (0xdd, 0xdd, 0xdd));
        p.setColor (QPalette::Base,            QColor (0x15, 0x17, 0x1c));
        p.setColor (QPalette::AlternateBase,   QColor (0x23, 0x26, 0x2e));
        p.setColor (QPalette::Text,            QColor (0xdd, 0xdd, 0xdd));
        p.setColor (QPalette::PlaceholderText, QColor (0x80, 0x80, 0x80));
        p.setColor (QPalette::Button,          QColor (0x2a, 0x2e, 0x37));
        p.setColor (QPalette::ButtonText,      QColor (0xdd, 0xdd, 0xdd));
        p.setColor (QPalette::BrightText,      Qt::white);
        p.setColor (QPalette::Highlight,       QColor (0x4f, 0xc3, 0xf7));
        p.setColor (QPalette::HighlightedText, Qt::black);
        p.setColor (QPalette::Link,            QColor (0xff, 0xb7, 0x4d));
        p.setColor (QPalette::Mid,             QColor (0x3a, 0x3f, 0x4a));
        p.setColor (QPalette::ToolTipBase,     QColor (0x2a, 0x2e, 0x37));
        p.setColor (QPalette::ToolTipText,     QColor (0xdd, 0xdd, 0xdd));
        p.setColor (QPalette::Disabled, QPalette::Text,       QColor (0x70, 0x70, 0x70));
        p.setColor (QPalette::Disabled, QPalette::ButtonText, QColor (0x70, 0x70, 0x70));
        p.setColor (QPalette::Disabled, QPalette::WindowText, QColor (0x70, 0x70, 0x70));
        app.setPalette (p);
    }
}

int main (int argc, char* argv[])
{
    QApplication app (argc, argv);
    QApplication::setApplicationName (QStringLiteral (APP_ID));
    QApplication::setApplicationDisplayName (QStringLiteral (APP_DISPLAY_NAME));
    QApplication::setApplicationVersion (QStringLiteral (APP_VERSION));
    // Wayland app-id, used by Hyprland window rules (class:^(phinyx)$).
    QGuiApplication::setDesktopFileName (QStringLiteral (APP_ID));
    applyDarkTheme (app);

    QCommandLineParser parser;
    parser.setApplicationDescription (QStringLiteral (APP_DISPLAY_NAME " audio editor and mastering suite"));
    parser.addHelpOption();
    parser.addVersionOption();
    parser.addPositionalArgument (QStringLiteral ("file"), QStringLiteral ("Audio file to open."));
    parser.process (app);

    AudioEngine engine;
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

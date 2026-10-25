// SPDX-License-Identifier: AGPL-3.0-only
#include "core/PhiVoice.h"
#include "engine/AudioEngine.h"
#include "engine/MasterChain.h"
#include "ui/Brand.h"
#include "ui/MainWindow.h"
#include "ui/PhiMark.h"
#include "ui/Settings.h"
#include "ui/Theme.h"

#include <QApplication>
#include <QCommandLineParser>
#include <QDir>
#include <QIcon>
#include <QPointer>
#include <QProcess>
#include <QStandardPaths>

#include <cstdio>
#include <cstdlib>
#include <exception>

namespace
{
    // A plugin or its bridge (yabridge, for example) can throw from any call
    // PHI makes into it. The Master Section catches those itself; this is the
    // last net for the rest, so a stray exception in an event is logged instead
    // of ending the program with unsaved work.
    class Application final : public QApplication
    {
    public:
        using QApplication::QApplication;

        bool notify (QObject* receiver, QEvent* event) override
        {
            try
            {
                return QApplication::notify (receiver, event);
            }
            catch (const std::exception& e)
            {
                std::fprintf (stderr, APP_DISPLAY_NAME ": an event threw and was stopped: %s\n", e.what());
            }
            catch (...)
            {
                std::fprintf (stderr, APP_DISPLAY_NAME ": an event threw and was stopped\n");
            }
            return false;
        }
    };

    // The splash's sound: the next patch of the golden sequence, so each start sounds
    // different. Every install walks the same sequence from the same start, so the first
    // start is always the same chosen sound; the count is kept in the settings.
    void playStartupSound (AudioEngine& engine)
    {
        constexpr double seed = 0.7;   // the sequence whose first sound was picked for PHI
        auto settings = appSettings();
        const auto launch = settings.value (QStringLiteral ("splash/soundLaunch"), 0).toLongLong();
        settings.setValue (QStringLiteral ("splash/soundLaunch"), launch + 1);

        auto sound = std::make_shared<phi::StartupSound> (launch, seed);
        sound->prepare (engine.deviceSampleRate());
        engine.playOneShot ([sound] (float* left, float* right, int frames) { return sound->render (left, right, frames); });
    }

    // The editor itself. Everything it made, the window, the engine with its plugin
    // instances and the application, is gone by the time this returns.
    int runEditor (int& argc, char* argv[])
    {
#if PHI_WEB_BACKDROP
        QCoreApplication::setAttribute (Qt::AA_ShareOpenGLContexts);   // Qt WebEngine asks for it before the application starts
#endif
        Application app (argc, argv);
        QApplication::setApplicationName (QStringLiteral (APP_ID));
        moveOldSettings();
        QApplication::setApplicationDisplayName (QStringLiteral (APP_DISPLAY_NAME));
        QApplication::setWindowIcon (QIcon (QStringLiteral (APP_ICON)));
        QApplication::setApplicationVersion (QStringLiteral (APP_VERSION));
        // Wayland app-id, used by Hyprland window rules (class:^(phi)$).
        QGuiApplication::setDesktopFileName (QStringLiteral (APP_ID));

        // Use the remembered theme, falling back to the default when it has gone away.
        const auto themes = Theme::available();
        auto theme = themes.first();
        for (const auto& t : themes)
            if (t.name == Theme::savedName())
                theme = t;
        Theme::apply (theme);

        QCommandLineParser parser;
        parser.setApplicationDescription (QStringLiteral (APP_BRAND_NAME " audio editor and mastering suite"));
        parser.addHelpOption();
        parser.addVersionOption();
        parser.addPositionalArgument (QStringLiteral ("file"), QStringLiteral ("Audio file to open."));
        const QCommandLineOption noSplash (QStringLiteral ("no-splash"), QStringLiteral ("Start without the Φ splash window."));
        parser.addOption (noSplash);
        parser.process (app);

        // The audio device opens first, so the splash's sound starts as it spirals open.
        AudioEngine engine;
        engine.setConfigFolder ((QStandardPaths::writableLocation (QStandardPaths::GenericConfigLocation)
                                 + QStringLiteral ("/" APP_ID)).toStdString());
        const auto deviceError = engine.openDefaultDevice();
        PhiSplash::startupSound() = [&engine, ok = deviceError.empty()]
        {
            if (ok)
                playStartupSound (engine);
        };

        // The Φ splash while the window is built.
        QPointer<PhiSplash> splash;
        if (! parser.isSet (noSplash))
        {
            splash = new PhiSplash;
            splash->play();
        }

        int result = 0;
        {
            MainWindow window (engine);
            window.show();
            if (splash != nullptr)
                splash->finish (&window);

            if (! parser.positionalArguments().isEmpty())
                window.openFile (QDir::current().absoluteFilePath (parser.positionalArguments().first()));

            result = app.exec();
        }
        PhiSplash::startupSound() = nullptr;

        // Tools > Reset Configuration: clear the settings now that nothing will write them again, and start afresh.
        if (MainWindow::finishReset())
            QProcess::startDetached (QCoreApplication::applicationFilePath(), {});

        return result;
    }
}

int main (int argc, char* argv[])
{
    // A copy started to look inside one plugin file, away from the editor (see MasterChain).
    // It leaves without running static destructors: some plugins (Cardinal) crash in theirs
    // at exit, which would read as a failed scan although the list was written.
    if (argc == 5 && std::string (argv[1]) == MasterChain::scanOption)
        std::_Exit (MasterChain::scanInThisProcess (argv[2], argv[3], juce::File (argv[4])));

    // The editor leaves the same way, for the same reason. Plugin libraries stay loaded
    // until PHI quits (see ClapFormat), so their static destructors and exit hooks would
    // run inside exit(), after the window, the GL contexts and the audio threads they
    // point at are gone; Cardinal crashes there. PHI's own cleanup has already run.
    const auto result = runEditor (argc, argv);
    std::fflush (nullptr);
    std::_Exit (result);
}

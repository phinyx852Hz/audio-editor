// SPDX-License-Identifier: AGPL-3.0-only
#include "MainComponent.h"

namespace
{
    juce::File getKeymapFile()
    {
        // Resolves $XDG_CONFIG_HOME or ~/.config on Linux.
        return juce::File::getSpecialLocation (juce::File::userApplicationDataDirectory)
                   .getChildFile ("audio-editor")
                   .getChildFile ("keymap.xml");
    }
}

class AudioEditorApplication final : public juce::JUCEApplication,
                                     private juce::ChangeListener
{
public:
    const juce::String getApplicationName() override    { return JUCE_APPLICATION_NAME_STRING; }
    const juce::String getApplicationVersion() override { return JUCE_APPLICATION_VERSION_STRING; }
    bool moreThanOneInstanceAllowed() override          { return true; }

    void initialise (const juce::String& commandLine) override
    {
        mainWindow = std::make_unique<MainWindow> (getApplicationName(), commandManager);

        commandManager.registerAllCommandsForTarget (this);
        commandManager.registerAllCommandsForTarget (&mainWindow->getMainComponent());
        commandManager.setFirstCommandTarget (&mainWindow->getMainComponent());

        loadKeymap();
        commandManager.getKeyMappings()->addChangeListener (this);

        auto args = juce::StringArray::fromTokens (commandLine, true);
        args.removeEmptyStrings();
        if (! args.isEmpty())
            mainWindow->getMainComponent().loadFile (juce::File::getCurrentWorkingDirectory()
                                                         .getChildFile (args[0].unquoted()));

        mainWindow->getMainComponent().focusEditor();
    }

    void shutdown() override
    {
        commandManager.getKeyMappings()->removeChangeListener (this);
        saveKeymap();
        mainWindow = nullptr;
    }

    void systemRequestedQuit() override { quit(); }
    void anotherInstanceStarted (const juce::String&) override {}

private:
    class MainWindow final : public juce::DocumentWindow
    {
    public:
        MainWindow (const juce::String& name, juce::ApplicationCommandManager& commands)
            : DocumentWindow (name,
                              juce::Desktop::getInstance().getDefaultLookAndFeel()
                                  .findColour (juce::ResizableWindow::backgroundColourId),
                              DocumentWindow::allButtons)
        {
            setUsingNativeTitleBar (true);
            setContentOwned (new MainComponent (commands), true);
            setResizable (true, true);
            centreWithSize (getWidth(), getHeight());

            // Route every key press that the focused component doesn't use to the keymap.
            addKeyListener (commands.getKeyMappings());
            setVisible (true);
        }

        MainComponent& getMainComponent() { return *static_cast<MainComponent*> (getContentComponent()); }

        void closeButtonPressed() override { JUCEApplication::getInstance()->systemRequestedQuit(); }
    };

    void loadKeymap()
    {
        if (auto xml = juce::parseXML (getKeymapFile()))
            commandManager.getKeyMappings()->restoreFromXml (*xml);
    }

    void saveKeymap()
    {
        auto file = getKeymapFile();
        file.getParentDirectory().createDirectory();

        if (auto xml = commandManager.getKeyMappings()->createXml (true))
            xml->writeTo (file);
    }

    void changeListenerCallback (juce::ChangeBroadcaster*) override { saveKeymap(); }

    juce::ApplicationCommandManager commandManager;
    std::unique_ptr<MainWindow> mainWindow;
};

START_JUCE_APPLICATION (AudioEditorApplication)

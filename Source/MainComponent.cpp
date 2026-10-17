// SPDX-License-Identifier: AGPL-3.0-only
#include "MainComponent.h"
#include "CommandIDs.h"
#include "CommandPalette.h"

namespace
{
    constexpr double smallStepSeconds = 0.1;
    constexpr double largeStepSeconds = 1.0;

    juce::String formatTime (double seconds)
    {
        const auto ms = juce::roundToInt (seconds * 1000.0);
        return juce::String::formatted ("%02d:%02d.%03d", ms / 60000, (ms / 1000) % 60, ms % 1000);
    }

    void launchDialog (juce::Component* content, const juce::String& title, juce::Component* parent)
    {
        juce::DialogWindow::LaunchOptions options;
        options.content.setOwned (content);
        options.dialogTitle = title;
        options.componentToCentreAround = parent;
        options.escapeKeyTriggersCloseButton = true;
        options.useNativeTitleBar = true;
        options.resizable = true;
        options.launchAsync();
    }
}

MainComponent::MainComponent (juce::ApplicationCommandManager& cm) : commands (cm)
{
    setFocusContainerType (FocusContainerType::keyboardFocusContainer);

    formatManager.registerBasicFormats();
    readAheadThread.startThread();

    deviceError = deviceManager.initialiseWithDefaultDevices (0, 2);
    player.setSource (&transport);
    deviceManager.addAudioCallback (&player);

    menuBar.setTitle ("Menu bar");
    addAndMakeVisible (menuBar);
    addAndMakeVisible (waveform);

    statusBar.setTitle ("Status");
    statusBar.setColour (juce::Label::textColourId, juce::Colours::lightgrey);
    addAndMakeVisible (statusBar);

    setApplicationCommandManagerToWatch (&commands);
    setSize (1100, 620);
    startTimerHz (15);
}

MainComponent::~MainComponent()
{
    setApplicationCommandManagerToWatch (nullptr);
    deviceManager.removeAudioCallback (&player);
    transport.setSource (nullptr);
    player.setSource (nullptr);
}

void MainComponent::paint (juce::Graphics& g)
{
    g.fillAll (juce::Colour (0xff1e2128));
}

void MainComponent::resized()
{
    auto area = getLocalBounds();
    menuBar.setBounds (area.removeFromTop (juce::LookAndFeel::getDefaultLookAndFeel().getDefaultMenuBarHeight()));
    statusBar.setBounds (area.removeFromBottom (24).reduced (6, 0));
    waveform.setBounds (area.reduced (6));
}

void MainComponent::timerCallback()
{
    juce::String text;

    if (currentFile == juce::File())
        text << "No file";
    else
        text << currentFile.getFileName() << "   " << formatTime (transport.getCurrentPosition())
             << " / " << formatTime (transport.getLengthInSeconds())
             << (transport.isPlaying() ? "   Playing" : "   Stopped");

    if (auto* device = deviceManager.getCurrentAudioDevice())
        text << "   |   " << device->getTypeName() << ": " << device->getName()
             << " @ " << juce::String (device->getCurrentSampleRate(), 0) << " Hz";
    else
        text << "   |   No audio device" << (deviceError.isNotEmpty() ? " (" + deviceError + ")" : juce::String());

    statusBar.setText (text, juce::dontSendNotification);
}

void MainComponent::loadFile (const juce::File& file)
{
    std::unique_ptr<juce::AudioFormatReader> reader (formatManager.createReaderFor (file));
    if (reader == nullptr)
    {
        juce::AlertWindow::showMessageBoxAsync (juce::MessageBoxIconType::WarningIcon, "Open failed",
                                                "Could not read " + file.getFullPathName());
        return;
    }

    transport.stop();
    transport.setSource (nullptr);

    const auto sampleRate = reader->sampleRate;
    readerSource = std::make_unique<juce::AudioFormatReaderSource> (reader.release(), true);
    transport.setSource (readerSource.get(), 32768, &readAheadThread, sampleRate);

    currentFile = file;
    waveform.setFile (file);
    focusEditor();
}

void MainComponent::openFileDialog()
{
    chooser = std::make_unique<juce::FileChooser> ("Open audio file", currentFile.getParentDirectory(),
                                                   formatManager.getWildcardForAllFormats());

    chooser->launchAsync (juce::FileBrowserComponent::openMode | juce::FileBrowserComponent::canSelectFiles,
                          [this] (const juce::FileChooser& fc)
                          {
                              if (fc.getResult().existsAsFile())
                                  loadFile (fc.getResult());
                          });
}

void MainComponent::togglePlayback()
{
    if (transport.isPlaying())
        transport.stop();
    else if (readerSource != nullptr)
    {
        if (transport.getCurrentPosition() >= transport.getLengthInSeconds())
            transport.setPosition (0.0);
        transport.start();
    }
}

void MainComponent::showAudioSettings()
{
    auto* selector = new juce::AudioDeviceSelectorComponent (deviceManager, 0, 0, 1, 2, false, false, true, false);
    selector->setSize (520, 420);
    launchDialog (selector, "Audio Settings", this);
}

void MainComponent::showKeymapEditor()
{
    auto* editor = new juce::KeyMappingEditorComponent (*commands.getKeyMappings(), true);
    editor->setSize (560, 640);
    launchDialog (editor, "Keyboard Shortcuts", this);
}

void MainComponent::showAbout()
{
    juce::AlertWindow::showMessageBoxAsync (juce::MessageBoxIconType::InfoIcon,
        juce::String (JUCE_APPLICATION_NAME_STRING) + " " + JUCE_APPLICATION_VERSION_STRING,
        "Audio editor and mastering suite for Linux.\n\n"
        "Free software under the GNU Affero General Public License v3.\n"
        "Press Ctrl+Shift+P or F1 to search every command and shortcut.");
}

//==============================================================================
juce::ApplicationCommandTarget* MainComponent::getNextCommandTarget()
{
    return juce::JUCEApplication::getInstance();
}

void MainComponent::getAllCommands (juce::Array<juce::CommandID>& ids)
{
    using namespace CommandIDs;
    ids.addArray ({ fileOpen,
                    transportPlayStop, transportStop, transportGoToStart, transportGoToEnd,
                    cursorBackSmall, cursorForwardSmall, cursorBackLarge, cursorForwardLarge,
                    viewZoomIn, viewZoomOut, viewZoomToFit,
                    optionsAudioSettings, optionsKeymapEditor,
                    helpCommandPalette, helpAbout,
                    menuBarFocus, menuFile, menuTransport, menuView, menuOptions, menuHelp });
}

void MainComponent::getCommandInfo (juce::CommandID id, juce::ApplicationCommandInfo& info)
{
    using namespace CommandIDs;
    using KP = juce::KeyPress;
    constexpr auto cmd   = juce::ModifierKeys::commandModifier;
    constexpr auto shift = juce::ModifierKeys::shiftModifier;
    constexpr auto alt   = juce::ModifierKeys::altModifier;
    const bool hasAudio  = readerSource != nullptr;

    switch (id)
    {
        case fileOpen:
            info.setInfo ("Open...", "Open an audio file", CommandCategories::file, 0);
            info.addDefaultKeypress ('o', cmd);
            break;

        case transportPlayStop:
            info.setInfo ("Play / Stop", "Toggle playback from the cursor", CommandCategories::transport, 0);
            info.addDefaultKeypress (KP::spaceKey, 0);
            info.setActive (hasAudio);
            break;
        case transportStop:
            info.setInfo ("Stop", "Stop playback", CommandCategories::transport, 0);
            info.addDefaultKeypress ('k', 0);
            info.setActive (hasAudio);
            break;
        case transportGoToStart:
            info.setInfo ("Go to Start", "Move the cursor to the start", CommandCategories::transport, 0);
            info.addDefaultKeypress (KP::homeKey, 0);
            info.setActive (hasAudio);
            break;
        case transportGoToEnd:
            info.setInfo ("Go to End", "Move the cursor to the end", CommandCategories::transport, 0);
            info.addDefaultKeypress (KP::endKey, 0);
            info.setActive (hasAudio);
            break;
        case cursorBackSmall:
            info.setInfo ("Cursor Back", "Move the cursor back 100 ms", CommandCategories::transport, 0);
            info.addDefaultKeypress (KP::leftKey, 0);
            info.setActive (hasAudio);
            break;
        case cursorForwardSmall:
            info.setInfo ("Cursor Forward", "Move the cursor forward 100 ms", CommandCategories::transport, 0);
            info.addDefaultKeypress (KP::rightKey, 0);
            info.setActive (hasAudio);
            break;
        case cursorBackLarge:
            info.setInfo ("Cursor Back 1 s", "Move the cursor back one second", CommandCategories::transport, 0);
            info.addDefaultKeypress (KP::leftKey, cmd);
            info.addDefaultKeypress ('j', 0);
            info.setActive (hasAudio);
            break;
        case cursorForwardLarge:
            info.setInfo ("Cursor Forward 1 s", "Move the cursor forward one second", CommandCategories::transport, 0);
            info.addDefaultKeypress (KP::rightKey, cmd);
            info.addDefaultKeypress ('l', 0);
            info.setActive (hasAudio);
            break;

        case viewZoomIn:
            info.setInfo ("Zoom In", "Zoom in around the cursor", CommandCategories::view, 0);
            info.addDefaultKeypress ('=', cmd);
            info.addDefaultKeypress ('+', 0);
            info.addDefaultKeypress (KP::numberPadAdd, 0);
            info.setActive (hasAudio);
            break;
        case viewZoomOut:
            info.setInfo ("Zoom Out", "Zoom out around the cursor", CommandCategories::view, 0);
            info.addDefaultKeypress ('-', cmd);
            info.addDefaultKeypress ('-', 0);
            info.addDefaultKeypress (KP::numberPadSubtract, 0);
            info.setActive (hasAudio);
            break;
        case viewZoomToFit:
            info.setInfo ("Zoom to Fit", "Show the whole file", CommandCategories::view, 0);
            info.addDefaultKeypress ('0', cmd);
            info.setActive (hasAudio);
            break;

        case optionsAudioSettings:
            info.setInfo ("Audio Settings...", "Choose audio device, sample rate and buffer size",
                          CommandCategories::options, 0);
            info.addDefaultKeypress (',', cmd);
            break;
        case optionsKeymapEditor:
            info.setInfo ("Keyboard Shortcuts...", "Edit, reset and review every key binding",
                          CommandCategories::options, 0);
            info.addDefaultKeypress ('k', cmd | shift);
            break;

        case helpCommandPalette:
            info.setInfo ("Command Palette...", "Search and run any command; lists every shortcut",
                          CommandCategories::help, 0);
            info.addDefaultKeypress ('p', cmd | shift);
            info.addDefaultKeypress (KP::F1Key, 0);
            break;
        case helpAbout:
            info.setInfo ("About", "About this application", CommandCategories::help, 0);
            break;

        case menuBarFocus:
            info.setInfo ("Open Menu Bar", "Open the first menu (then use arrow keys)", CommandCategories::menus, 0);
            info.addDefaultKeypress (KP::F10Key, 0);
            break;
        case menuFile:
            info.setInfo ("File Menu", "Open the File menu", CommandCategories::menus, 0);
            info.addDefaultKeypress ('f', alt);
            break;
        case menuTransport:
            info.setInfo ("Transport Menu", "Open the Transport menu", CommandCategories::menus, 0);
            info.addDefaultKeypress ('t', alt);
            break;
        case menuView:
            info.setInfo ("View Menu", "Open the View menu", CommandCategories::menus, 0);
            info.addDefaultKeypress ('v', alt);
            break;
        case menuOptions:
            info.setInfo ("Options Menu", "Open the Options menu", CommandCategories::menus, 0);
            info.addDefaultKeypress ('o', alt);
            break;
        case menuHelp:
            info.setInfo ("Help Menu", "Open the Help menu", CommandCategories::menus, 0);
            info.addDefaultKeypress ('h', alt);
            break;

        default:
            break;
    }
}

bool MainComponent::perform (const InvocationInfo& invocation)
{
    using namespace CommandIDs;

    switch (invocation.commandID)
    {
        case fileOpen:             openFileDialog(); break;
        case transportPlayStop:    togglePlayback(); break;
        case transportStop:        transport.stop(); break;
        case transportGoToStart:   waveform.setCursorSeconds (0.0); break;
        case transportGoToEnd:     waveform.setCursorSeconds (waveform.getLengthSeconds()); break;
        case cursorBackSmall:      waveform.moveCursorBy (-smallStepSeconds); break;
        case cursorForwardSmall:   waveform.moveCursorBy (smallStepSeconds); break;
        case cursorBackLarge:      waveform.moveCursorBy (-largeStepSeconds); break;
        case cursorForwardLarge:   waveform.moveCursorBy (largeStepSeconds); break;
        case viewZoomIn:           waveform.zoomBy (0.5); break;
        case viewZoomOut:          waveform.zoomBy (2.0); break;
        case viewZoomToFit:        waveform.zoomToFit(); break;
        case optionsAudioSettings: showAudioSettings(); break;
        case optionsKeymapEditor:  showKeymapEditor(); break;
        case helpCommandPalette:   CommandPalette::show (commands, this); break;
        case helpAbout:            showAbout(); break;
        case menuBarFocus:
        case menuFile:             menuBar.showMenu (0); break;
        case menuTransport:        menuBar.showMenu (1); break;
        case menuView:             menuBar.showMenu (2); break;
        case menuOptions:          menuBar.showMenu (3); break;
        case menuHelp:             menuBar.showMenu (4); break;
        default:                   return false;
    }

    return true;
}

//==============================================================================
juce::StringArray MainComponent::getMenuBarNames()
{
    return { "File", "Transport", "View", "Options", "Help" };
}

juce::PopupMenu MainComponent::getMenuForIndex (int index, const juce::String&)
{
    using namespace CommandIDs;
    juce::PopupMenu menu;

    auto add = [&] (std::initializer_list<juce::CommandID> ids)
    {
        for (auto id : ids)
            menu.addCommandItem (&commands, id);
    };

    switch (index)
    {
        case 0:
            add ({ fileOpen });
            menu.addSeparator();
            add ({ juce::StandardApplicationCommandIDs::quit });
            break;
        case 1:
            add ({ transportPlayStop, transportStop });
            menu.addSeparator();
            add ({ transportGoToStart, transportGoToEnd, cursorBackSmall, cursorForwardSmall,
                   cursorBackLarge, cursorForwardLarge });
            break;
        case 2:
            add ({ viewZoomIn, viewZoomOut, viewZoomToFit });
            break;
        case 3:
            add ({ optionsAudioSettings, optionsKeymapEditor });
            break;
        case 4:
            add ({ helpCommandPalette });
            menu.addSeparator();
            add ({ helpAbout });
            break;
        default:
            break;
    }

    return menu;
}

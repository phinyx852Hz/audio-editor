// SPDX-License-Identifier: AGPL-3.0-only
#pragma once

#include "WaveformView.h"

class MainComponent final : public juce::Component,
                            public juce::ApplicationCommandTarget,
                            public juce::MenuBarModel,
                            private juce::Timer
{
public:
    explicit MainComponent (juce::ApplicationCommandManager&);
    ~MainComponent() override;

    void loadFile (const juce::File&);
    void focusEditor() { waveform.grabKeyboardFocus(); }

    void resized() override;
    void paint (juce::Graphics&) override;

    // ApplicationCommandTarget
    juce::ApplicationCommandTarget* getNextCommandTarget() override;
    void getAllCommands (juce::Array<juce::CommandID>&) override;
    void getCommandInfo (juce::CommandID, juce::ApplicationCommandInfo&) override;
    bool perform (const InvocationInfo&) override;

    // MenuBarModel
    juce::StringArray getMenuBarNames() override;
    juce::PopupMenu getMenuForIndex (int index, const juce::String& name) override;
    void menuItemSelected (int, int) override {}

private:
    void timerCallback() override;
    void openFileDialog();
    void showAudioSettings();
    void showKeymapEditor();
    void showAbout();
    void togglePlayback();

    juce::ApplicationCommandManager& commands;

    juce::AudioDeviceManager deviceManager;
    juce::AudioFormatManager formatManager;
    juce::AudioSourcePlayer player;
    juce::AudioTransportSource transport;
    std::unique_ptr<juce::AudioFormatReaderSource> readerSource;
    juce::TimeSliceThread readAheadThread { "audio read-ahead" };

    juce::MenuBarComponent menuBar { this };
    WaveformView waveform { formatManager, transport };
    juce::Label statusBar;
    juce::File currentFile;
    juce::String deviceError;
    std::unique_ptr<juce::FileChooser> chooser;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (MainComponent)
};

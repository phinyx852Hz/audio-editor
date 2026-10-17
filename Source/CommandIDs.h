// SPDX-License-Identifier: AGPL-3.0-only
#pragma once

#include <juce_gui_basics/juce_gui_basics.h>

// Every menu item and process is a command. Menus, shortcuts and the command
// palette all invoke these IDs, so nothing is reachable only by mouse.
namespace CommandIDs
{
    enum : juce::CommandID
    {
        fileOpen = 0x1000,

        transportPlayStop = 0x2000,
        transportStop,
        transportGoToStart,
        transportGoToEnd,
        cursorBackSmall,
        cursorForwardSmall,
        cursorBackLarge,
        cursorForwardLarge,

        viewZoomIn = 0x3000,
        viewZoomOut,
        viewZoomToFit,

        optionsAudioSettings = 0x4000,
        optionsKeymapEditor,

        helpCommandPalette = 0x5000,
        helpAbout,

        menuBarFocus = 0x6000,
        menuFile,
        menuTransport,
        menuView,
        menuOptions,
        menuHelp
    };
}

namespace CommandCategories
{
    inline const juce::String file      { "File" };
    inline const juce::String transport { "Transport" };
    inline const juce::String view      { "View" };
    inline const juce::String options   { "Options" };
    inline const juce::String help      { "Help" };
    inline const juce::String menus     { "Menus" };
}

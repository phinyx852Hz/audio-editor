// SPDX-License-Identifier: AGPL-3.0-only
#pragma once

#include <functional>

// Plugin windows are JUCE windows on X11 (XWayland on Wayland). These keep
// PHINYX itself alive and recognisable there.
namespace x11
{
    // Gives a plugin window the class "phinyx", so window manager rules
    // (Hyprland's class:^(phinyx)$) apply to it like the main window.
    void setWindowClass (void* nativeWindowHandle);

    // If the window manager kills a plugin window's X connection (a forced
    // close), Xlib would end the whole program. After this, only plugin
    // windows stop working; the editor keeps running.
    void surviveLostConnection (std::function<void()> whenLost);
    bool connectionLost();

    // Puts PHINYX's own X error handler back. Plugins may install theirs while
    // creating or showing their windows; Xlib keeps one handler for the whole
    // process, so call this after handing control to plugin GUI code.
    void useHostErrorHandler();
}

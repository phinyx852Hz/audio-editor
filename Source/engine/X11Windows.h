// SPDX-License-Identifier: AGPL-3.0-only
#pragma once

#include <functional>
#include <string>

// Plugin windows are JUCE windows on X11 (XWayland on Wayland). These keep
// PHI itself alive and recognisable there.
namespace x11
{
    // Gives a plugin window the class "phi", so window manager rules
    // (Hyprland's class:^(phi)$) apply to it like the main window.
    void setWindowClass (void* nativeWindowHandle);

    // Sets a window's opacity, 0 to 1 (_NET_WM_WINDOW_OPACITY), for compositors
    // that honour it; JUCE's own setAlpha does nothing for a window on Linux.
    // Hyprland ignores that property, so there the window (found by its title)
    // is given the opacity through Hyprland's own socket as well.
    void setOpacity (void* nativeWindowHandle, double opacity, const std::string& title);

    // On Hyprland, floats the window with this title at this size, centred, instead of
    // tiling it. Elsewhere windows float anyway, at the size they were given.
    void floatWindow (const std::string& title, int width, int height);

    // Marks a plugin window as a dialog (_NET_WM_WINDOW_TYPE_DIALOG), before it is shown,
    // so tiling window managers float it at its own size from the start: Hyprland (and so
    // Omarchy), sway and i3 float dialogs rather than tile them.
    void setDialogType (void* nativeWindowHandle);

    // Makes Ctrl+W, Ctrl+F, Super+W and Super+F reach this plugin window even while the plugin's
    // own view (an X window of its own, often on its own X connection) has the keyboard,
    // so they close the window or take it full screen whatever has the focus inside it.
    // A desktop that binds one of them itself (Omarchy's Super+W and Super+F) still gets it
    // first, and does the same.
    void grabWindowKeys (void* nativeWindowHandle);

    // Super (Mod4) is held now. JUCE does not report it with a key, so a grabbed Super+F
    // reaches the window as a plain F; this tells the two apart.
    bool superHeld();

    // Takes the window full screen, or back (_NET_WM_STATE_FULLSCREEN, which Hyprland,
    // KDE, GNOME and most X window managers honour). JUCE's own setFullScreen only
    // maximises a window with a title bar.
    void toggleFullScreen (void* nativeWindowHandle);

    // If the window manager kills a plugin window's X connection (a forced
    // close), Xlib would end the whole program. After this, only plugin
    // windows stop working; the editor keeps running.
    void surviveLostConnection (std::function<void()> whenLost);
    bool connectionLost();

    // Puts PHI's own X error handler back. Plugins may install theirs while
    // creating or showing their windows; Xlib keeps one handler for the whole
    // process, so call this after handing control to plugin GUI code.
    void useHostErrorHandler();
}

// SPDX-License-Identifier: AGPL-3.0-only
#pragma once

#include "EmbeddedView.h"
#include "X11Windows.h"

#include <juce_audio_processors/juce_audio_processors.h>
#include <juce_gui_basics/juce_gui_basics.h>
#include <juce_gui_extra/juce_gui_extra.h>

#include <algorithm>
#include <cstdlib>
#include <functional>
#include <vector>

// The plugin's own editor in a JUCE window (X11, so XWayland on Wayland).
// Always resizable, so it can be dragged to any size, made full screen, or tiled by
// a tiling window manager like any other window. An editor of fixed size is asked to
// scale itself to fit (most VST3 and CLAP plugins can) and sits centred in what is left.
// It floats (a dialog to the window manager, so Hyprland and Omarchy float it rather than
// tile it) at about three fifths of the screen. With the window focused (on Hyprland,
// under the mouse): Esc, Ctrl+W or Super+W closes it (only the window: the plugin or
// instrument keeps playing), Ctrl+F or Super+F takes it full screen and back to its
// floating size, Ctrl+Tab and Ctrl+Shift+Tab go to the next and previous plugin window.
// Ctrl+W, Ctrl+F, Super+W and Super+F work even while the plugin's own view has the keyboard.
// On Hyprland and sway, which draw no title bars, PHI draws one with a close button.
// An editor that cannot scale and is bigger than that opens whole: the window grows to it.
class PluginWindow final : public juce::DocumentWindow
{
public:
    // `content` is the plugin's editor, or a StockFrame holding it.
    PluginWindow (juce::Component* content, bool contentResizes, const juce::String& title,
                  std::function<void()> onClose, std::function<void (int)> onCycle)
        : DocumentWindow (title, juce::Colours::black, DocumentWindow::closeButton),
          closed (std::move (onClose)), cycle (std::move (onCycle))
    {
        // Where the window manager draws no title bar (Hyprland, sway), PHI draws its own, with
        // a close button: otherwise only keys close the window, and a plugin drawn with
        // baseview (CA-72, OctaSine...) keeps every key its view is given, Esc and Ctrl+W too,
        // which left Super+W (the window manager's own close) as the only way out.
        setUsingNativeTitleBar (! drawsOwnTitleBar());
        if (! isUsingNativeTitleBar())
        {
            setTitleBarHeight (24);
            setTitleBarButtonsRequired (DocumentWindow::closeButton, false);
        }
        if (contentResizes)
            setContentOwned (content, true);
        else
            setContentOwned (new Centred (content), true);
        setResizable (true, false);
        setResizeLimits (160, 120, 16384, 16384);
        const auto size = standardSize();
        centreWithSize (size.x, size.y);
        showWholeEditor();
        setWantsKeyboardFocus (true);
        // Before it is shown: the class for window rules.
        if (auto* peer = getPeer())
        {
            x11::setWindowClass (peer->getNativeHandle());
            x11::setDialogType (peer->getNativeHandle());
            x11::grabWindowKeys (peer->getNativeHandle());
        }
        setVisible (true);
        toFront (true);
        // The window follows PHINYX's opacity, unless its content keeps its own (InstrumentFrame).
        followsOpacity = ! content->getProperties().getWithDefault ("ownOpacity", false);
        windows().push_back (this);
        applyOpacity();
        // Again once the window manager has the window (Hyprland finds it by its title),
        // and on Hyprland it floats at that size rather than tiling.
        juce::Timer::callAfterDelay (400, [w = juce::Component::SafePointer<PluginWindow> (this)]
        {
            if (w == nullptr)
                return;
            w->applyOpacity();
            w->showWholeEditor();   // an editor that took its scale late
            x11::floatWindow (w->getName().toStdString(), w->getWidth(), w->getHeight());
        });
    }

    ~PluginWindow() override
    {
        auto& all = windows();
        all.erase (std::remove (all.begin(), all.end(), this), all.end());
    }

    // PHINYX's window opacity (the Opacity slider), for every plugin window open now and later.
    static void setOpacityForAll (double value)
    {
        opacity() = juce::jlimit (0.05, 1.0, value);
        for (auto* w : windows())
            w->applyOpacity();
    }

    void closeButtonPressed() override
    {
        if (closed)
            closed();
    }

    // Keys the plugin's own view did not use.
    bool keyPressed (const juce::KeyPress& key) override
    {
        const auto mods = key.getModifiers();
        const auto code = key.getKeyCode();
        // JUCE does not see Super: a grabbed Super+W or Super+F arrives as a plain W or F.
        const bool super = ! mods.isAnyModifierKeyDown() && (code == 'W' || code == 'w' || code == 'F' || code == 'f') && x11::superHeld();
        if ((code == juce::KeyPress::escapeKey && ! mods.isAnyModifierKeyDown())
            || (mods.isCtrlDown() && ! mods.isAltDown() && (code == 'W' || code == 'w'))
            || (super && (code == 'W' || code == 'w')))
        {
            closeButtonPressed();
            return true;
        }
        if (super || (mods.isCtrlDown() && ! mods.isAltDown() && ! mods.isShiftDown() && (code == 'F' || code == 'f')))
        {
            if (auto* peer = getPeer())
                x11::toggleFullScreen (peer->getNativeHandle());
            return true;
        }
        if (mods.isCtrlDown() && key.getKeyCode() == juce::KeyPress::tabKey)
        {
            if (cycle)
                cycle (mods.isShiftDown() ? -1 : 1);
            return true;
        }
        return DocumentWindow::keyPressed (key);
    }

private:
    // The window managers that give a window no title bar of its own.
    static bool drawsOwnTitleBar()
    {
        for (const auto* name : { "HYPRLAND_INSTANCE_SIGNATURE", "SWAYSOCK" })
            if (const auto* value = std::getenv (name); value != nullptr && *value != 0)
                return true;
        return false;
    }

    static juce::AudioProcessorEditor* findEditor (juce::Component* c)
    {
        if (c == nullptr)
            return nullptr;
        if (auto* e = dynamic_cast<juce::AudioProcessorEditor*> (c))
            return e;
        for (auto* child : c->getChildren())
            if (auto* e = findEditor (child))
                return e;
        return nullptr;
    }

    // An editor of fixed size that does not scale itself (CA-72 opens at four fifths of the
    // screen and takes no scale) would be cut off in a window of the standard size, showing
    // only its middle: the window grows until the whole editor shows, up to the screen.
    void showWholeEditor()
    {
        auto* editor = findEditor (getContentComponent());
        if (editor == nullptr || editor->isResizable())
            return;
        auto* holder = editor->getParentComponent();
        if (holder == nullptr)
            return;
        const auto dx = std::max (0, editor->getWidth() - holder->getWidth());
        const auto dy = std::max (0, editor->getHeight() - holder->getHeight());
        if (dx == 0 && dy == 0)
            return;
        auto w = getWidth() + dx, h = getHeight() + dy;
        if (const auto* display = juce::Desktop::getInstance().getDisplays().getDisplayForRect (getScreenBounds()))
        {
            w = std::min (w, display->userArea.getWidth());
            h = std::min (h, display->userArea.getHeight());
        }
        centreWithSize (w, h);
    }

    // The size a plugin window opens at: its content scaled up (or down) to about three
    // fifths of the screen, keeping its proportions; smaller than the screen, never small
    // (owner, 2026-10-30). Ctrl+F or Super+F takes it full screen and back.
    juce::Point<int> standardSize() const
    {
        const auto w = std::max (1, getWidth()), h = std::max (1, getHeight());
        const auto* display = juce::Desktop::getInstance().getDisplays().getPrimaryDisplay();
        if (display == nullptr)
            return { w, h };
        const auto area = display->userArea;
        const auto scale = juce::jlimit (0.5, 4.0, std::min (area.getWidth() * 0.6 / w, area.getHeight() * 0.6 / h));
        return { juce::roundToInt (w * scale), juce::roundToInt (h * scale) };
    }

    static std::vector<PluginWindow*>& windows() { static std::vector<PluginWindow*> all; return all; }
    static double& opacity() { static double value = 1.0; return value; }

    void applyOpacity()
    {
        if (auto* peer = getPeer(); peer != nullptr && followsOpacity)
            x11::setOpacity (peer->getNativeHandle(), opacity(), getName().toStdString());
    }

    bool followsOpacity = true;

    // Holds an editor that cannot resize, so the window still can: the editor is asked to
    // scale itself to the room there is (from half to four times its own size), and sits
    // centred in what is left. A plugin that ignores the scale just stays centred.
    class Centred final : public juce::Component
    {
    public:
        explicit Centred (juce::Component* e) : editor (e), scaler (*e)
        {
            addAndMakeVisible (editor.get());
            setSize (editor->getWidth(), editor->getHeight());
        }

        void paint (juce::Graphics& g) override { g.fillAll (juce::Colours::black); }

        void resized() override
        {
            scaler.fit (getWidth(), getHeight());
            centre();
        }

        void childBoundsChanged (juce::Component*) override
        {
            if (! scaler.isScaling())
                centre();
        }

    private:
        void centre()
        {
            editor->setCentrePosition (getLocalBounds().getCentre());
            embedded::follow (*editor);
        }

        std::unique_ptr<juce::Component> editor;
        embedded::ScaleToFit scaler;
    };

    std::function<void()> closed;
    std::function<void (int)> cycle;
};

// SPDX-License-Identifier: AGPL-3.0-only
#pragma once

#include "X11Windows.h"

#include <juce_gui_basics/juce_gui_basics.h>

#include <functional>

// The plugin's own editor in a JUCE window (X11, so XWayland on Wayland).
// Always resizable, so it can be made any size (or tiled with a window rule);
// an editor of fixed size sits centred in it.
// It floats (a dialog to the window manager). With the window focused (on Hyprland,
// under the mouse): Esc or Ctrl+W closes it,
// Ctrl+Tab and Ctrl+Shift+Tab go to the next and previous plugin window.
class PluginWindow final : public juce::DocumentWindow
{
public:
    // `content` is the plugin's editor, or a StockFrame holding it.
    PluginWindow (juce::Component* content, bool contentResizes, const juce::String& title,
                  std::function<void()> onClose, std::function<void (int)> onCycle)
        : DocumentWindow (title, juce::Colours::black, DocumentWindow::closeButton),
          closed (std::move (onClose)), cycle (std::move (onCycle))
    {
        setUsingNativeTitleBar (true);
        if (contentResizes)
            setContentOwned (content, true);
        else
            setContentOwned (new Centred (content), true);
        setResizable (true, false);
        setResizeLimits (160, 120, 16384, 16384);
        centreWithSize (getWidth(), getHeight());
        setWantsKeyboardFocus (true);
        // Before it is shown: the class for window rules, and floating like a traditional plugin window.
        if (auto* peer = getPeer())
        {
            x11::setWindowClass (peer->getNativeHandle());
            x11::setFloating (peer->getNativeHandle());
        }
        setVisible (true);
        toFront (true);
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
        if ((key.getKeyCode() == juce::KeyPress::escapeKey && ! mods.isAnyModifierKeyDown())
            || (mods.isCtrlDown() && ! mods.isAltDown() && (key.getKeyCode() == 'W' || key.getKeyCode() == 'w')))
        {
            closeButtonPressed();
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
    // Holds an editor that cannot resize, centred, so the window still can.
    class Centred final : public juce::Component
    {
    public:
        explicit Centred (juce::Component* e) : editor (e)
        {
            addAndMakeVisible (editor.get());
            setSize (editor->getWidth(), editor->getHeight());
        }

        void paint (juce::Graphics& g) override { g.fillAll (juce::Colours::black); }
        void resized() override { editor->setCentrePosition (getLocalBounds().getCentre()); }
        void childBoundsChanged (juce::Component*) override { resized(); }

    private:
        std::unique_ptr<juce::Component> editor;
    };

    std::function<void()> closed;
    std::function<void (int)> cycle;
};

// SPDX-License-Identifier: AGPL-3.0-only
#pragma once

#include "EmbeddedView.h"
#include "X11Windows.h"

#include <juce_audio_processors/juce_audio_processors.h>
#include <juce_gui_basics/juce_gui_basics.h>
#include <juce_gui_extra/juce_gui_extra.h>

#include <algorithm>
#include <functional>
#include <vector>

// The plugin's own editor in a JUCE window (X11, so XWayland on Wayland).
// Always resizable, so it can be dragged to any size, made full screen, or tiled by
// a tiling window manager like any other window. An editor of fixed size is asked to
// scale itself to fit (most VST3 and CLAP plugins can) and sits centred in what is left.
// With the window focused (on Hyprland, under the mouse): Esc, Ctrl+W or Super+W closes
// it (only the window: the plugin or instrument keeps playing), Super+F takes it full
// screen and back, Ctrl+Tab and Ctrl+Shift+Tab go to the next and previous plugin window.
// Ctrl+W, Super+W and Super+F work even while the plugin's own view has the keyboard.
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
        const auto size = standardSize();
        centreWithSize (size.x, size.y);
        setWantsKeyboardFocus (true);
        // Before it is shown: the class for window rules.
        if (auto* peer = getPeer())
        {
            x11::setWindowClass (peer->getNativeHandle());
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
        juce::Timer::callAfterDelay (400, [w = juce::Component::SafePointer<PluginWindow> (this), size]
        {
            if (w == nullptr)
                return;
            w->applyOpacity();
            x11::floatWindow (w->getName().toStdString(), size.x, size.y);
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
        if (super)
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
    // The size a plugin window opens at: its content scaled up (or down) to about two
    // thirds of the screen, keeping its proportions. Super+F still takes it full screen.
    juce::Point<int> standardSize() const
    {
        const auto w = std::max (1, getWidth()), h = std::max (1, getHeight());
        const auto* display = juce::Desktop::getInstance().getDisplays().getPrimaryDisplay();
        if (display == nullptr)
            return { w, h };
        const auto area = display->userArea;
        const auto scale = juce::jlimit (0.5, 4.0, std::min (area.getWidth() * 2.0 / 3.0 / w, area.getHeight() * 2.0 / 3.0 / h));
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

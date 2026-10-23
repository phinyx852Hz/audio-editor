// SPDX-License-Identifier: AGPL-3.0-only
#pragma once

#include "X11Windows.h"

#include <juce_audio_processors/juce_audio_processors.h>
#include <juce_gui_basics/juce_gui_basics.h>
#include <juce_gui_extra/juce_gui_extra.h>

#include <functional>

// The plugin's own editor in a JUCE window (X11, so XWayland on Wayland).
// Always resizable, so it can be dragged to any size, made full screen, or tiled by
// a tiling window manager like any other window. An editor of fixed size is asked to
// scale itself to fit (most VST3 and CLAP plugins can) and sits centred in what is left.
// With the window focused (on Hyprland, under the mouse): Esc or Ctrl+W closes it,
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
        // Before it is shown: the class for window rules.
        if (auto* peer = getPeer())
            x11::setWindowClass (peer->getNativeHandle());
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
    // Holds an editor that cannot resize, so the window still can: the editor is asked to
    // scale itself to the room there is (from half to four times its own size), and sits
    // centred in what is left. A plugin that ignores the scale just stays centred.
    class Centred final : public juce::Component
    {
    public:
        explicit Centred (juce::Component* e)
            : editor (e), pluginEditor (dynamic_cast<juce::AudioProcessorEditor*> (e)),
              baseWidth (std::max (1, e->getWidth())), baseHeight (std::max (1, e->getHeight()))
        {
            addAndMakeVisible (editor.get());
            setSize (editor->getWidth(), editor->getHeight());
        }

        void paint (juce::Graphics& g) override { g.fillAll (juce::Colours::black); }

        void resized() override
        {
            if (pluginEditor != nullptr && getWidth() > 0 && getHeight() > 0)
            {
                const auto fit = juce::jlimit (0.5f, 4.0f, std::min ((float) getWidth() / (float) baseWidth,
                                                                    (float) getHeight() / (float) baseHeight));
                if (std::abs (fit - scale) > 0.01f)
                {
                    scale = fit;
                    const juce::ScopedValueSetter<bool> s (scaling, true);
                    pluginEditor->setScaleFactor (scale);
                }
            }
            centre();
        }

        void childBoundsChanged (juce::Component*) override
        {
            if (! scaling)
                centre();
        }

    private:
        void centre()
        {
            editor->setCentrePosition (getLocalBounds().getCentre());
            // The plugin's view is an X window of its own, which follows its component only
            // when that component itself moves or resizes: moving the editor around it
            // would leave the view behind in the corner.
            std::function<void (juce::Component&)> follow = [&] (juce::Component& c)
            {
                if (auto* x = dynamic_cast<juce::XEmbedComponent*> (&c))
                    x->updateEmbeddedBounds();
                for (auto* child : c.getChildren())
                    follow (*child);
            };
            follow (*editor);
        }

        std::unique_ptr<juce::Component> editor;
        juce::AudioProcessorEditor* pluginEditor;
        const int baseWidth, baseHeight;   // the editor's own size, at scale 1
        float scale = 1.0f;
        bool scaling = false;
    };

    std::function<void()> closed;
    std::function<void (int)> cycle;
};
